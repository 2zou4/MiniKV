#include "sstable.h"

#include <cstring>
#include <fstream>

#include "bloom_filter.h"

namespace minikv {

namespace {

// ──────────────────────────────────────────────
// 小工具：写入/读取定长整数字段
// ──────────────────────────────────────────────
//
// 统一走这两个函数，避免在多处手写 reinterpret_cast，减少出错概率。

void WriteU32(std::ofstream& out, uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

void WriteU64(std::ofstream& out, uint64_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

bool ReadU32(std::ifstream& in, uint32_t* v) {
    in.read(reinterpret_cast<char*>(v), sizeof(*v));
    return in.gcount() == sizeof(*v);
}

bool ReadU64(std::ifstream& in, uint64_t* v) {
    in.read(reinterpret_cast<char*>(v), sizeof(*v));
    return in.gcount() == sizeof(*v);
}

constexpr size_t kFooterSize = 32;  // 4个 uint64_t 字段

}  // namespace

// ──────────────────────────────────────────────
// BuildFromSkipList
// ──────────────────────────────────────────────
//
// 整体步骤：
//   1. 用 ForEach 把 SkipList 的全部数据（按 key 升序）取出
//   2. 每 kBlockRecordCount 条记录打包成一个 Data Block，边写边记录
//      每个 block 的 first_key / 文件偏移 / 字节数，作为 Index 的一条项
//   3. 写完所有 Data Block 后，紧接着写 Bloom Filter（对全部 key 构建）
//   4. 再写 Index Block（把第 2 步收集的索引项序列化）
//   5. 最后写 Footer，记录 Index 和 Bloom Filter 各自的位置和大小
//
// 注意：Bloom Filter 要覆盖包括 tombstone 在内的所有 key——
// 如果一个 key 被删除了，SSTable 里仍然有它的记录（is_deleted=true），
// 查询这个 key 时 Bloom Filter 应该说"可能存在"，
// 这样才能走到 Data Block 里读出 tombstone、正确返回"已删除"，
// 而不是被 Bloom Filter 提前拦截、误判成"从来没有过这个 key"。

bool SSTable::BuildFromSkipList(const SkipList& table,
                                 const std::string& file_path) {
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    // 第一步：把跳表内容按序取出到内存（对于教学项目规模完全没问题；
    // 真实数据库这里会用流式写入，避免整体载入内存）
    struct Entry {
        std::string key;
        std::string value;
        bool        is_deleted;
    };
    std::vector<Entry> entries;
    table.ForEach([&](const std::string& k, const std::string& v, bool del) {
        entries.push_back({k, v, del});
    });

    std::vector<IndexEntry> index;
    std::vector<std::string> all_keys;
    all_keys.reserve(entries.size());

    // 第二步：按 kBlockRecordCount 条一组，写 Data Block，同时记录索引
    for (size_t i = 0; i < entries.size(); i += kBlockRecordCount) {
        uint64_t block_offset = static_cast<uint64_t>(out.tellp());
        std::string first_key = entries[i].key;

        size_t end = std::min(i + kBlockRecordCount, entries.size());
        for (size_t j = i; j < end; ++j) {
            const Entry& e = entries[j];
            uint32_t key_len   = static_cast<uint32_t>(e.key.size());
            uint32_t value_len = static_cast<uint32_t>(e.value.size());
            uint8_t  del_flag  = e.is_deleted ? 1 : 0;

            WriteU32(out, key_len);
            WriteU32(out, value_len);
            out.write(reinterpret_cast<const char*>(&del_flag), sizeof(del_flag));
            out.write(e.key.data(),   e.key.size());
            out.write(e.value.data(), e.value.size());

            all_keys.push_back(e.key);
        }

        uint64_t block_size = static_cast<uint64_t>(out.tellp()) - block_offset;
        index.push_back({first_key, block_offset, block_size});
    }

    // 第三步：写 Bloom Filter
    std::string bloom_data = BloomFilter::Build(all_keys);
    uint64_t bloom_offset = static_cast<uint64_t>(out.tellp());
    out.write(bloom_data.data(), bloom_data.size());
    uint64_t bloom_size = bloom_data.size();

    // 第四步：写 Index Block
    uint64_t index_offset = static_cast<uint64_t>(out.tellp());
    for (const auto& entry : index) {
        uint32_t key_len = static_cast<uint32_t>(entry.first_key.size());
        WriteU32(out, key_len);
        out.write(entry.first_key.data(), entry.first_key.size());
        WriteU64(out, entry.block_offset);
        WriteU64(out, entry.block_size);
    }
    uint64_t index_size = static_cast<uint64_t>(out.tellp()) - index_offset;

    // 第五步：写 Footer（固定 32 字节，位于文件末尾）
    WriteU64(out, index_offset);
    WriteU64(out, index_size);
    WriteU64(out, bloom_offset);
    WriteU64(out, bloom_size);

    out.flush();
    bool ok = out.good();
    out.close();
    return ok;
}

// ──────────────────────────────────────────────
// 构造函数 / LoadMetadata
// ──────────────────────────────────────────────

SSTable::SSTable(const std::string& file_path) : file_path_(file_path) {
    valid_ = LoadMetadata();
}

bool SSTable::LoadMetadata() {
    std::ifstream in(file_path_, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    // Footer 在文件末尾，且大小固定，直接从末尾往前 seek
    in.seekg(-static_cast<int64_t>(kFooterSize), std::ios::end);
    if (!in.good()) {
        return false;  // 文件比 Footer 还小，说明不是合法的 SSTable 文件
    }

    uint64_t index_offset, index_size, bloom_offset, bloom_size;
    if (!ReadU64(in, &index_offset)) return false;
    if (!ReadU64(in, &index_size))   return false;
    if (!ReadU64(in, &bloom_offset)) return false;
    if (!ReadU64(in, &bloom_size))   return false;

    // 读取 Bloom Filter 数据
    in.seekg(static_cast<int64_t>(bloom_offset), std::ios::beg);
    bloom_data_.resize(bloom_size);
    if (bloom_size > 0) {
        in.read(&bloom_data_[0], bloom_size);
        if (static_cast<uint64_t>(in.gcount()) != bloom_size) return false;
    }

    // 读取 Index Block，解析成 index_ 数组
    in.seekg(static_cast<int64_t>(index_offset), std::ios::beg);
    uint64_t bytes_read = 0;
    while (bytes_read < index_size) {
        uint32_t key_len;
        if (!ReadU32(in, &key_len)) return false;
        bytes_read += sizeof(key_len);

        std::string first_key(key_len, '\0');
        in.read(&first_key[0], key_len);
        if (static_cast<uint32_t>(in.gcount()) != key_len) return false;
        bytes_read += key_len;

        uint64_t block_offset, block_size;
        if (!ReadU64(in, &block_offset)) return false;
        if (!ReadU64(in, &block_size))   return false;
        bytes_read += sizeof(block_offset) + sizeof(block_size);

        index_.push_back({first_key, block_offset, block_size});
    }

    return true;
}

// ──────────────────────────────────────────────
// ReadBlock
// ──────────────────────────────────────────────

bool SSTable::ReadBlock(uint64_t offset, uint64_t size, std::string* out) const {
    std::ifstream in(file_path_, std::ios::binary);
    if (!in.is_open()) return false;

    in.seekg(static_cast<int64_t>(offset), std::ios::beg);
    out->resize(size);
    if (size > 0) {
        in.read(&(*out)[0], size);
        if (static_cast<uint64_t>(in.gcount()) != size) return false;
    }
    return true;
}

// ──────────────────────────────────────────────
// FindCandidateBlock
// ──────────────────────────────────────────────
//
// 二分查找：在 index_ 中找到"最后一个 first_key <= 目标 key"的 Data Block。
// 因为 Data Block 内部有序、Block 之间也有序，
// 目标 key 如果存在，必然落在这个候选 Block 里。

int SSTable::FindCandidateBlock(const std::string& key) const {
    if (index_.empty() || key < index_[0].first_key) {
        return -1;
    }

    int lo = 0, hi = static_cast<int>(index_.size()) - 1;
    int result = -1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (index_[mid].first_key <= key) {
            result = mid;      // 候选，继续往右找更接近的
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    return result;
}

// ──────────────────────────────────────────────
// Get
// ──────────────────────────────────────────────

bool SSTable::Get(const std::string& key, std::string* value) const {
    if (!valid_) {
        return false;
    }

    // 第一步：Bloom Filter 快速过滤
    if (!BloomFilter::MayContain(bloom_data_, key)) {
        return false;  // 一定不存在，省去一次磁盘读
    }

    // 第二步：二分定位候选 Data Block
    int block_idx = FindCandidateBlock(key);
    if (block_idx < 0) {
        return false;
    }

    // 第三步：读取该 Data Block，线性扫描查找精确匹配
    const IndexEntry& entry = index_[block_idx];
    std::string block_data;
    if (!ReadBlock(entry.block_offset, entry.block_size, &block_data)) {
        return false;
    }

    size_t pos = 0;
    while (pos < block_data.size()) {
        uint32_t key_len, value_len;
        uint8_t  del_flag;

        std::memcpy(&key_len, block_data.data() + pos, sizeof(key_len));
        pos += sizeof(key_len);
        std::memcpy(&value_len, block_data.data() + pos, sizeof(value_len));
        pos += sizeof(value_len);
        std::memcpy(&del_flag, block_data.data() + pos, sizeof(del_flag));
        pos += sizeof(del_flag);

        std::string cur_key = block_data.substr(pos, key_len);
        pos += key_len;
        std::string cur_value = block_data.substr(pos, value_len);
        pos += value_len;

        if (cur_key == key) {
            if (del_flag) {
                return false;  // 命中，但是 tombstone，视为不存在
            }
            if (value) {
                *value = cur_value;
            }
            return true;
        }

        // Block 内部有序，一旦扫到比目标大的 key，后面不可能再有匹配
        if (cur_key > key) {
            break;
        }
    }

    return false;
}

}  // namespace minikv