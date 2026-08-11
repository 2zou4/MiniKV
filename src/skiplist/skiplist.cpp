#include "skiplist.h"

#include <cstdlib>
#include <ctime>
#include <stdexcept>

#include "lookup_result.h"

namespace minikv{

// ──────────────────────────────────────────────
// 构造 / 析构
// ──────────────────────────────────────────────
    SkipList::SkipList()
        :cur_level_(1), size_(0), mem_usage_(0){
    // HEAD 节点：key/value 为空，层数固定为 MAX_LEVEL
    // HEAD 不存储数据，只是让每一层都有一个起点
        head_=new SkipListNode("","",MAX_LEVEL);
        std::srand(static_cast<unsigned>(std::time(nullptr)));
    }
    SkipList::~SkipList(){
        // 沿 L0 遍历，释放所有节点（包括 HEAD）
        SkipListNode* cur=head_;
        while(cur){
            SkipListNode* next=cur->forward[0];
            delete cur;
            cur=next;
        }
    }

// ──────────────────────────────────────────────
// RandomLevel
// ──────────────────────────────────────────────
//
// 核心逻辑：从 1 开始，每次以 1/BRANCH 的概率升一层。
// leveldb 的实现用位运算加速：
//   (std::rand() % BRANCH) == 0  →  25% 概率升层
//
// 为什么这样设计？
//   数学上可以证明，当升层概率 = 1/BRANCH 时，
//   期望层数 = log_{BRANCH}(n)，查找期望复杂度 O(log n)。
//   BRANCH=4 比 BRANCH=2（抛硬币）空间更省，性能接近。
 
    int SkipList::RandomLevel(){
        int level=1;
        while(level<MAX_LEVEL && (std::rand() % BRANCH)==0){
            ++level;
        }
        return level;
    }

// ──────────────────────────────────────────────
// FindGreaterOrEqual
// ──────────────────────────────────────────────
//
// 跳表所有操作（Insert / Get / Delete）都依赖这个内部查找。
//
// 核心思路：
//   从最高层开始，向右走，遇到 key 比目标小的节点就继续走；
//   遇到 key >= 目标或链尾，就下沉一层。
//   同时记录每一层的「前驱节点」存入 prev[]，
//   Insert 需要它来串联新节点。
//
// prev 可以传 nullptr，此时只做查找，不记录前驱（用于 Get）。
    SkipListNode* SkipList::FindGreaterOrEqual(const std::string&key,
                                                SkipListNode** prev) const{
        SkipListNode* cur=head_;

        //从最高层向下遍历
        for(int i=cur_level_-1;i>=0;--i)
        {
            //在第i层尽可能向右走
            while(cur->forward[i]!=nullptr && cur->forward[i]->key<key)
            {
                cur=cur->forward[i];
            }
            //此时cur->forward[i]的key>=target或者为nullptr
            if(prev)
            {
                prev[i]=cur;//记录第i层前驱
            }
        }
        //返回 L0 层的候选节点（可能是目标，可能是后继，可能是 nullptr）
        return cur->forward[0];
    }

// ──────────────────────────────────────────────
// Insert
// ──────────────────────────────────────────────
//
// 步骤：
//   1. 找到每层前驱，顺便拿到候选节点
//   2. 若 key 已存在（哪怕被懒删除了），直接更新 value 和标记
//   3. 否则生成随机层数，创建新节点，逐层插入
    void SkipList::Insert(const std::string& key, const std::string& value)
    {
        SkipListNode* prev[MAX_LEVEL];
        SkipListNode* candidate=FindGreaterOrEqual(key,prev);
        //key已存在（含懒删除节点）：直接更新
        if(candidate!=nullptr && candidate->key==key)
        {
            if(candidate->is_deleted)
            {
                //复活节点：重新计为有效
                candidate->is_deleted=false;
                ++size_;
                mem_usage_ +=value.size();
                mem_usage_ -= candidate->value.size();
            }
            else
            {
                //更新value:调整内存估算
                mem_usage_ +=value.size();
                mem_usage_ -=candidate->value.size();
            }
            candidate->value=value;
            return;
        }
        //key不存在：创建新节点
        int new_level=RandomLevel();
        // 若新节点层数超过当前最高层，补全 prev[]
        // HEAD 是所有超出层的前驱
        if(new_level>cur_level_)
        {
            for(int i=cur_level_; i<new_level;++i)
            {
                prev[i]=head_;
            }
            cur_level_=new_level;
        }
        SkipListNode* new_node=new SkipListNode(key,value,new_level);
        // 在每一层中，把 new_node 插到 prev[i] 和 prev[i]->forward[i] 之间
        
        //  prev[i] --> new_node --> prev[i]->forward[i]
        
        for(int i=0;i<new_level;++i)
        {
            new_node->forward[i]=prev[i]->forward[i];
            prev[i]->forward[i]=new_node;
        }
        ++size_;
        // 内存估算：key + value + 指针数组（每个指针 8 字节）+ 节点对象本身
        mem_usage_ +=key.size()+value.size()+new_level*sizeof(SkipListNode*)+sizeof(SkipListNode);
    }

// ──────────────────────────────────────────────
// Delete（懒删除）
// ──────────────────────────────────────────────
//
// 为什么用懒删除而不直接移除节点？
//   1. 并发场景：其他线程可能持有该节点指针，
//      直接删除会造成悬垂指针。
//   2. LSM-Tree 语义：Delete 本质是写入一条「tombstone」记录，
//      懒删除的 is_deleted 标记正好对应这个语义。
//   3. 实现简单：省去了修复每层 forward 指针的复杂逻辑。
//
// 代价：内存不立即释放，刷盘后才真正回收。可接受
    // bool SkipList::Delete(const std::string& key){
    //     SkipListNode* candidate=FindGreaterOrEqual(key,nullptr);
    //     if(candidate==nullptr || candidate->key!=key || candidate->is_deleted){
    //         return false;//不存在或已被删除
    //     }
    //     candidate->is_deleted=true;
    //     --size_;
    //     return true;
    // }

// ──────────────────────────────────────────────
// Delete
// ──────────────────────────────────────────────
//
// 重要设计更正：Delete 必须始终在跳表里留下一条 tombstone 记录，
// 不管这个 key 当前在不在这个跳表里。
//
// 原因：在多层 LSM-Tree 结构里（MemTable + 若干 SSTable 文件），
// Delete 本质上是一次写入操作——它要覆盖的可能是更旧层（磁盘上的
// SSTable）里的同名 key，而不是当前这个跳表里的数据。
// 如果 key 在这个跳表里原本不存在就直接放弃，
// 那么"MemTable 里没这个 key，但磁盘上某个旧文件里有"这种情况，
// 删除操作会完全没有效果，旧数据会在跨层查询时被误判成仍然有效
// ——这个问题必须靠"始终留痕"来解决，而不能靠"找到才删"。
bool SkipList::Delete(const std::string& key){
    SkipListNode* prev[MAX_LEVEL];
    SkipListNode* candidate=FindGreaterOrEqual(key,prev);

    if(candidate!=nullptr && candidate->key==key){
        // 已存在的节点：打上删除标记（如果本来就是有效数据，size_ 要减一）
        if(!candidate->is_deleted){
            --size_;
            mem_usage_-=candidate->value.size();
        }
        candidate->is_deleted=true;
        candidate->value.clear();
        return true;
    }

    // key 在这个跳表里完全不存在：仍然要插入一个新的 tombstone 节点，
    // 逻辑和 Insert 里"key 不存在时创建新节点"那部分完全对称，
    // 只是 value 为空、is_deleted 直接置为 true。
    int new_level=RandomLevel();
    if(new_level>cur_level_){
        for(int i=cur_level_;i<new_level;++i){
            prev[i]=head_;
        }
        cur_level_=new_level;
    }
    SkipListNode* new_node=
        new SkipListNode(key,"",new_level,/*deleted=*/true);
    for(int i=0;i<new_level;++i){
        new_node->forward[i]=prev[i]->forward[i];
        prev[i]->forward[i]=new_node;
    }

    // tombstone 节点不计入 size_（size_ 统计的是"有效数据条数"），
    // 但仍然占用内存，要计入 mem_usage_
    mem_usage_ += key.size() + new_level * sizeof(SkipListNode*) +
                  sizeof(SkipListNode);
    return true;
}
 

// ──────────────────────────────────────────────
// Get
// ──────────────────────────────────────────────
    // bool SkipList::Get(const std::string&key, std::string* value) const{
    //     SkipListNode* candidate=FindGreaterOrEqual(key,nullptr);
    //     if(candidate==nullptr || candidate->key!=key || candidate->is_deleted){
    //         return false;
    //     }
    //     if(value){
    //         *value=candidate->value;
    //     }
    //     return true;
    // }
// ──────────────────────────────────────────────
// Find / Get
// ──────────────────────────────────────────────
//
// Find 是唯一真正做查找的地方，Get 只是把三态结果压缩成 bool，
// 这样"查找逻辑"只存在一份，不会出现两份实现将来悄悄跑偏的风险。
LookupResult SkipList::Find(const std::string& key, std::string* value) const{
    SkipListNode* candidate=FindGreaterOrEqual(key,nullptr);

    if(candidate==nullptr || candidate->key!=key){
        return LookupResult::kNotFound;
    }
    if (candidate == nullptr || candidate->key != key) {
        return LookupResult::kNotFound;
    }
    if (candidate->is_deleted) {
        return LookupResult::kDeleted;
    }
    if (value) {
        *value = candidate->value;
    }
    return LookupResult::kFound;
}

bool SkipList::Get(const std::string& key, std::string* value) const{
    return Find(key,value)==LookupResult::kFound;
}

// ──────────────────────────────────────────────
// ApproximateMemoryUsage
// ──────────────────────────────────────────────
 
    size_t SkipList::ApproximateMemoryUsage() const{
        return mem_usage_;
    }

// ──────────────────────────────────────────────
// ForEach
// ──────────────────────────────────────────────
//
// L0 层本身就是一条包含所有节点的完整有序链表，
// 所以只需要沿 forward[0] 一路走到底，天然按 key 升序访问。
// 不需要额外排序，这也是跳表作为 MemTable 底层结构的一个优势。

    void SkipList::ForEach(const EntryCallback& callback) const{
        SkipListNode* cur=head_->forward[0];
        while(cur!=nullptr)
        {
            callback(cur->key, cur->value, cur->is_deleted);
            cur=cur->forward[0];
        }
    }

}
