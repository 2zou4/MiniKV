#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace minikv {

// ──────────────────────────────────────────────
// Bloom Filter
// ──────────────────────────────────────────────
//
// 用途：快速判断"某个 key 一定不在这个 SSTable 里"，
// 从而跳过一次没有意义的磁盘读取。
//
// 核心特性：
//   - 可能误判"存在"（false positive）：查询返回 true，但 key 实际不存在
//   - 绝不误判"不存在"（no false negative）：如果 key 真的存在，一定返回 true
//   - 这个不对称性正是它有用的原因：MayContain() 返回 false 时，
//     调用方可以 100% 放心地跳过后续查找；返回 true 时仍需老老实实去查。
//
// 实现原理（Bit array + 多个哈希函数）：
//   - 用一个 bit 数组表示一组 key 的"存在性"
//   - 插入一个 key 时，用 k 个不同的哈希函数算出 k 个 bit 位置，全部置 1
//   - 查询一个 key 时，同样算出这 k 个位置，只要有一个是 0，就能确定不存在；
//     全部是 1 只能说"可能存在"（因为其他 key 也可能碰巧把这些位置都置了 1）
//
// 序列化格式：bit 数组原始字节 + 末尾 1 个字节记录 k（哈希函数个数），
// 这样反序列化时不需要额外传参就能知道当初用了几个哈希函数
// （思路参考 leveldb 的 BloomFilterPolicy 实现）。

class BloomFilter {
public:
    // 根据一组 key 构建 Bloom Filter，返回序列化后的字节串（可直接写入文件）。
    // bits_per_key 越大，误判率越低，但占用空间也越大。
    // leveldb 默认用 10（对应误判率约 1%），这里保持一致。
    static std::string Build(const std::vector<std::string>& keys,
                              int bits_per_key = 10);

    // 判断 key 是否"可能存在"于 filter_data 对应的 key 集合中。
    // 返回 false：key 一定不存在。
    // 返回 true：key 可能存在（也可能是误判）。
    static bool MayContain(const std::string& filter_data,
                            const std::string& key);

private:
    // 简单的字符串哈希函数（FNV-1a 变体），用于生成 bit 位置。
    // 不需要密码学强度，只需要分布均匀。
    static uint32_t Hash(const std::string& key);
};

}  // namespace minikv