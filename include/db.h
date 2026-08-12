#pragma once

#include <memory>
#include <string>

#include "level_manager.h"
#include "memtable.h"

#include <shared_mutex>

#include <vector>
#include "compaction.h"

namespace minikv {

// ──────────────────────────────────────────────
// DB：跨层级统一查询的入口
// ──────────────────────────────────────────────
//
// 到目前为止，数据分散在三处：
//   1. MemTable（内存，最新）
//   2. L0 SSTable 文件（磁盘，若干个，按写入顺序有新有旧）
//   3. L1 SSTable 文件（磁盘，最底层，最旧）
//
// Put/Delete 只需要写 MemTable（WAL 由 MemTable 内部处理）。
// Get 必须按"从新到旧"依次查找，且一旦某一层给出明确答案
// （找到有效值，或者找到删除标记），就要立刻停止——
// 见 lookup_result.h 里对这个正确性问题的详细说明。

// 并发安全：用一把 std::shared_mutex 保护所有共享状态
// （MemTable 内部结构 + LevelManager 的文件列表）。
//   - Get 只读，多个 Get 可以同时进行 -> 用共享锁（shared_lock）
//   - Put / Delete / MaybeCompact 会修改共享状态，必须互斥独占
//     -> 用独占锁（unique_lock）
//
// 加锁粒度选在 DB 这一层，而不是 SkipList 或 LevelManager 内部：
// 太细（比如在 SkipList::Insert 内部逐指针加锁）逻辑复杂、开销大；
// 太粗（整个进程一把全局锁）完全没有并发度。DB 层是所有读写操作
// 的汇聚点，一次 Put/Get/Compact 从头到尾持有同一把锁，
// 语义上最清晰，也最不容易漏加锁。

class DB {
public:
    // wal_path：MemTable 对应的 WAL 文件路径
    // sstable_dir：Compaction 生成的新 SSTable 文件要写到哪个目录
    DB(const std::string& wal_path, const std::string& sstable_dir);

    DB(const DB&)            = delete;
    DB& operator=(const DB&) = delete;

    bool Put(const std::string& key, const std::string& value);
    bool Delete(const std::string& key);

    // 统一查询：依次查 MemTable -> L0（新到旧）-> L1
    bool Get(const std::string& key, std::string* value) const;

    // 范围扫描：返回一份完整、有序、去重、不含已删除记录的数据视图。
    //
    // 复用 Compaction 模块的 KWayMerger——Scan 本质上和 Compaction
    // 做的是同一件事（把多个有序来源合并成一份，重复 key 取最新，
    // 清除 tombstone），区别只是这次不落盘，直接把结果返回给调用方。
    //
    // start_key 为空表示不设下界，end_key 为空表示不设上界；
    // 区间语义是 [start_key, end_key)——包含 start_key，不包含 end_key。
    std::vector<KVEntry> Scan(const std::string& start_key = "",
                               const std::string& end_key   = "") const;

    // 线程安全的 Compaction 触发入口：检查 L0 是否达到阈值，
    // 达到就执行一次合并。多线程场景下应该用这个，而不是
    // 绕过锁直接调用 Levels().MaybeCompact()。
    bool MaybeCompact();

    // 暴露内部组件，供测试直接操作
    // （比如手动构造 L0 文件模拟 MemTable 刷盘，不需要真的走完整刷盘流程）
    MemTable&      Mem()    { return *memtable_; }
    LevelManager&  Levels() { return levels_; }

private:
    std::unique_ptr<MemTable> memtable_;
    LevelManager               levels_;

    // mutable：Get 是 const 成员函数，但仍然需要加共享锁，
    // 而加锁本身要修改 mutex_ 内部状态，所以 mutex_ 必须是 mutable。
    mutable std::shared_mutex mutex_;
};

}  // namespace minikv