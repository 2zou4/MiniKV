#pragma once

#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

#include<functional>

namespace minikv{

// ──────────────────────────────────────────────
// 跳表节点
// ──────────────────────────────────────────────
// 每个节点持有：
//   key / value / is_deleted（懒删除标记）
//   forward[]：指向同层下一节点的指针数组
//   层数由插入时随机决定，forward 数组长度 = 该节点的层数
//
// 为什么不把 value 存到节点外面？
//   因为跳表作为 MemTable，Get 需要直接拿到 value，
//   存在节点里访问最快，不需要再跳一次指针。
    struct SkipListNode{
        std:: string key;
        std:: string value;
        bool is_deleted; //懒删除标记，Delete() 只打标记不移除节点

        // forward[i] 指向第 i 层的下一个节点
    // 用 vector 而非固定数组，是因为每个节点层数不同
        std::vector<SkipListNode*> forward;

        SkipListNode(const std::string& k,
                    const std::string& v,
                    int level,
                    bool deleted=false)
                    :key(k),value(v),is_deleted(deleted),
                    forward(level,nullptr) {} //成员初始化列表
    };

// ──────────────────────────────────────────────
// 跳表
// ──────────────────────────────────────────────
// 参数说明：
//   MAX_LEVEL：最大层数，leveldb 默认 12
//   BRANCH：升层概率分母，即 1/BRANCH 概率升一层
//           leveldb 用 4（25%），本实现保持一致
    class SkipList{
        public:
        static constexpr int MAX_LEVEL=12;
        static constexpr int BRANCH=4;  //升层概率1/4

        explicit SkipList();
        ~ SkipList();

        //禁止拷贝，跳表持有裸指针，拷贝语义复杂且无必要
        SkipList(const SkipList&)=delete;
        SkipList& operator=(const SkipList&)=delete;

        // 插入或更新 key。若 key 已存在（含懒删除节点），直接更新 value
        void Insert(const std::string& key, const std::string& value);

        // 懒删除：找到节点后只打标记，不移除
        // 返回 true 表示 key 存在且标记成功，false 表示 key 不存在
        bool Delete(const std::string& key);

        // 查找 key
        // 返回 true 并通过 value 带出结果
        // 若 key 不存在，或已被懒删除，返回 false
        bool Get(const std::string& key,std::string* value) const;

        // 返回当前节点数（不含 HEAD，不含已懒删除节点）
        int Size() const {return size_;}
        bool Empty() const {return size_==0;}

        // 估算内存占用（字节），用于 MemTable 判断是否需要刷盘
        size_t ApproximateMemoryUsage() const;

    // 按 key 升序遍历所有节点（含懒删除节点），对每个节点调用 callback。
    // 用途：SSTable 构建时需要把 MemTable 的全部数据按序导出到磁盘文件，
    // 懒删除节点也要导出（写成 tombstone），否则 Compaction 时
    // 无法知道某个 key 曾经被删除过，可能导致旧数据"复活"。
    //
    // callback 签名：void(const string& key, const string& value, bool is_deleted)
        using EntryCallback=
            std::function<void(const std::string&, const std::string&, bool)>;
        void ForEach(const EntryCallback& callback) const;

    private:
        // 生成随机层数：从 1 开始，每次以 1/BRANCH 概率加一层，上限 MAX_LEVEL
        int RandomLevel();
        // 查找辅助：填充 prev[] 数组，prev[i] 是第 i 层中 key 的前驱节点
        // 返回 L0 层 prev[0]->forward[0]，即目标节点（若存在）
        SkipListNode* FindGreaterOrEqual(const std::string& key,
                                        SkipListNode** prev) const;

        SkipListNode* head_;// 哨兵头节点，key/value 为空，层数固定为 MAX_LEVEL
        int cur_level_;// 当前跳表实际最高层（动态增长，最大 MAX_LEVEL）
        int size_;// 有效节点数（懒删除节点不计）
        size_t mem_usage_;// 内存估算

    };
}