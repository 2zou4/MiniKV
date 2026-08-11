#pragma once

#include <memory>
#include <string>

#include "level_manager.h"
#include "memtable.h"

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

    // 暴露内部组件，供测试直接操作
    // （比如手动构造 L0 文件模拟 MemTable 刷盘，不需要真的走完整刷盘流程）
    MemTable&      Mem()    { return *memtable_; }
    LevelManager&  Levels() { return levels_; }

private:
    std::unique_ptr<MemTable> memtable_;
    LevelManager               levels_;
};

}  // namespace minikv