#include "db.h"
 
#include "sstable.h"

#include <mutex>
 
namespace minikv {

    DB::DB(const std::string& wal_path, const std::string& sstable_dir)
        :memtable_(std::make_unique<MemTable>(wal_path)),
        levels_(sstable_dir){}

    bool DB::Put(const std::string& key, const std::string& value){
        std::unique_lock lock(mutex_);
        return memtable_->Put(key,value);
    }

    bool DB::Delete(const std::string& key){
        std::unique_lock lock(mutex_);
        return memtable_->Delete(key);
    }

// ──────────────────────────────────────────────
// Get：跨层查询的核心
// ──────────────────────────────────────────────
//
// 查找顺序：MemTable（最新）-> L0 文件（新到旧）-> L1 文件（最旧）。
//
// 每一步都用三态的 Find，而不是二态的 Get，原因见 lookup_result.h：
//   kFound    -> 直接返回，查找成功
//   kDeleted  -> 直接返回"不存在"，绝不能继续往更旧的层找
//                （否则会让已经删除的数据从旧层里"复活"）
//   kNotFound -> 这一层没有任何相关记录，继续查下一层

// 用共享锁：多个 Get 可以同时持有锁并发执行，
// 只有当有线程持有独占锁（Put/Delete/MaybeCompact 正在进行）时，
// Get 才需要等待——这正是读写锁比互斥锁更适合这里的原因，
// 读操作理应是最频繁、最需要高并发度的。

    bool DB::Get(const std::string& key, std::string* value) const{
        std::shared_lock lock(mutex_);

        // 第一步：MemTable（最新数据，内存里）
        LookupResult r=memtable_->Find(key,value);
        if(r==LookupResult::kFound) return true;
        if(r==LookupResult::kDeleted) return false;

        // 第二步：L0 文件，从新到旧
        // LevelManager 里 L0FilePaths() 是追加顺序（旧→新），要反向遍历
        const auto& l0_paths=levels_.L0FilePaths();
        for(auto it=l0_paths.rbegin();it!=l0_paths.rend();++it){
            SSTable sst(*it);
            r=sst.Find(key,value);
            if (r == LookupResult::kFound)   return true;
            if (r == LookupResult::kDeleted) return false;
            // kNotFound：继续查下一个更旧的 L0 文件
        }

        // 第三步：L1 文件（如果存在），这是最底层，没有更旧的层可查了
        std::string l1_path=levels_.L1FilePath();
        if(!l1_path.empty()){
            SSTable sst(l1_path);
            r=sst.Find(key,value);
            if (r == LookupResult::kFound) return true;
            // kDeleted 或 kNotFound 到这里都是"没找到"，没有更旧的层了
        }
        return false;
    }

    // ──────────────────────────────────────────────
// MaybeCompact
// ──────────────────────────────────────────────
//
// 用独占锁：Compaction 期间会修改 LevelManager 的文件列表、
// 删除旧的物理文件——这个过程中绝不能有任何 Get 正在读取
// 那些即将被删除的文件，也不能有其他线程同时触发 Compaction
// （否则同一批文件可能被合并两次，或者被重复删除）。
bool DB::MaybeCompact(){
    std::unique_lock lock(mutex_);
    return levels_.MaybeCompact();
}

// ──────────────────────────────────────────────
// Scan
// ──────────────────────────────────────────────
//
// 思路和 Compaction::CompactFiles 几乎一模一样，唯一的区别是
// "多一个来源"（MemTable，Compaction 不需要管它，因为它只处理
// 磁盘文件）以及"结果不落盘，直接返回"。
//
// 步骤：
//   1. 把 MemTable、每个 L0 文件、L1 文件的全部内容分别取出，
//      按"从新到旧"的顺序组成 sources（和 Get 里的查找顺序一致）
//   2. 交给 KWayMerger::Merge 合并——重复 key 自动取最新版本，
//      drop_tombstones 恒为 true，因为 Scan 面向的是"当前对外可见
//      的数据视图"，已删除的 key 不应该出现在结果里
//   3. 如果调用方指定了 [start_key, end_key) 区间，过滤一遍
//      （merged 本身已经是有序的，命中上界就可以提前结束遍历）
std::vector<KVEntry> DB::Scan(const std::string& start_key,
                            const std::string& end_key) const{
    std::shared_lock lock(mutex_);

    std::vector<std::vector<KVEntry>> sources;

    // 第一个来源：MemTable（最新）
    std::vector<KVEntry> mem_entries;
    memtable_->ForEach([&](const std::string& k, const std::string& v, bool del){
        mem_entries.push_back({k,v,del});
    });
    sources.push_back(std::move(mem_entries));

     // 接下来：L0 文件，从新到旧
     const auto& l0_paths=levels_.L0FilePaths();
     for (auto it = l0_paths.rbegin(); it != l0_paths.rend(); ++it) {
        SSTable sst(*it);
        std::vector<KVEntry> entries;
        sst.ForEach([&](const std::string& k, const std::string& v, bool del) {
            entries.push_back({k, v, del});
        });
        sources.push_back(std::move(entries));
    }

    // 最后：L1 文件（如果存在），全局最旧
    std::string l1_path = levels_.L1FilePath();
    if (!l1_path.empty()) {
        SSTable sst(l1_path);
        std::vector<KVEntry> entries;
        sst.ForEach([&](const std::string& k, const std::string& v, bool del) {
            entries.push_back({k, v, del});
        });
        sources.push_back(std::move(entries));
    }

    std::vector<KVEntry> merged = KWayMerger::Merge(sources, /*drop_tombstones=*/true);
 
    if (start_key.empty() && end_key.empty()) {
        return merged;
    }

    std::vector<KVEntry> filtered;
    filtered.reserve(merged.size());
    for (auto& e : merged) {
        if (!start_key.empty() && e.key < start_key) {
            continue;
        }
        if (!end_key.empty() && e.key >= end_key) {
            break;  // merged 按 key 升序，一旦越过上界后面不会再有匹配
        }
        filtered.push_back(std::move(e));
    }
    return filtered;
}

}