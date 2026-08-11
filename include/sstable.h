#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "skiplist.h"

#include <functional>

namespace minikv {

// ──────────────────────────────────────────────
// SSTable 文件格式
// ──────────────────────────────────────────────
//
//   ┌───────────────┬───────────────┬──────────────┬──────────┐
//   │  Data Blocks   │  Bloom Filter │ Index Block  │  Footer  │
//   │  (多个,有序)    │   (1段)       │   (1段)      │  (定长)  │
//   └───────────────┴───────────────┴──────────────┴──────────┘
//
// Data Block 内的每条记录：
//   [key_len(4B)][value_len(4B)][is_deleted(1B)][key][value]
//   is_deleted=true 时表示这是一条 tombstone（懒删除记录也要写进 SSTable，
//   否则 Compaction 阶段无法知道某个 key 曾经被删除，可能让旧版本"复活"）
//
// Index Block 每条记录（每个 Data Block 对应一条索引项）：
//   [key_len(4B)][first_key][block_offset(8B)][block_size(8B)]
//   first_key 是该 Data Block 里的第一个（也是最小的）key
//
// Footer（固定 32 字节）：
//   [index_offset(8B)][index_size(8B)][bloom_offset(8B)][bloom_size(8B)]
//   记录 Index Block 和 Bloom Filter 在文件中的位置，
//   打开文件时先读 Footer（位置固定，在文件末尾），再据此找到其他部分。

class SSTable {
public:
    // 每个 Data Block 打包的记录数。真实数据库通常按字节大小分块（比如 4KB），
    // 这里为了实现简单、逻辑清晰，改用固定记录数分块，效果类似。
    static constexpr size_t kBlockRecordCount = 16;

    //新增内容
    struct Entry{
        std::string key;
        std::string value;
        bool is_deleted;
    };

    // 把一个 SkipList（通常是刷盘前的 MemTable）的全部数据写成一个 SSTable 文件。
    // 返回 false 表示写入失败（磁盘 IO 错误等）。
    static bool BuildFromSkipList(const SkipList& table,
                                   const std::string& file_path);

    //新增
    static bool BuildFromEntries(const std::vector<Entry>& entries,
                                    const std::string& file_path);

    // 打开一个已存在的 SSTable 文件用于查询。
    // 构造时只读取 Footer + Index + Bloom Filter（体积很小），
    // Data Block 只在真正 Get 命中候选块时才按需读取。
    explicit SSTable(const std::string& file_path);

    // 查询一个 key。
    // 返回 true 并通过 value 带出结果：key 存在且未被删除。
    // 返回 false：key 不存在，或者存在但已被标记删除（tombstone）。
    bool Get(const std::string& key, std::string* value) const;

    // 三态查询：区分"确定不存在"和"存在但已删除"，
    // 用途和 SkipList::Find 一样，供跨层查询判断要不要继续往更旧的层找。
    LookupResult Find(const std::string& key, std::string* value) const;

    //新增
    using EntryCallback=
        std::function<void(const std::string&, const std::string&, bool)>;
    bool ForEach(const EntryCallback& callback) const;

private:
    // 内存中的索引项：对应文件里 Index Block 的一条记录
    struct IndexEntry {
        std::string first_key;
        uint64_t    block_offset;
        uint64_t    block_size;
    };

    std::string              file_path_;
    std::vector<IndexEntry>  index_;
    std::string              bloom_data_;
    bool                     valid_ = false;

    // 构造时调用：读 Footer，再据此读 Index 和 Bloom Filter 到内存
    bool LoadMetadata();

    // 按需读取指定偏移和大小的 Data Block 原始字节
    bool ReadBlock(uint64_t offset, uint64_t size, std::string* out) const;

    // 在 index_ 中二分查找：key 可能落在哪个 Data Block
    // 返回 -1 表示 key 比所有 Data Block 的 first_key 都小，不可能存在
    int FindCandidateBlock(const std::string& key) const;

    //新增
    static bool WriteEntries(const std::vector<Entry>& entries,
                                const std::string& file_path);
};

}  // namespace minikv