#include "bloomfilter.h"
#include <algorithm>
#include <cmath>

namespace minikv{
    // ──────────────────────────────────────────────
// Hash：FNV-1a 32位哈希
// ──────────────────────────────────────────────
//
// FNV-1a 是一个简单、快速、分布均匀的非加密哈希算法，
// 逐字节处理，用异或+乘法混合，足够满足 Bloom Filter 对哈希质量的要求。

uint32_t BloomFilter::Hash(const std::string& key){
    const uint32_t kFnvPrime=16777619u;
    uint32_t hash=2166136261u;  // FNV offset basis
    for(char c:key){
        hash^=static_cast<uint8_t>(c);
        hash *=kFnvPrime'
    }
    return hash;
}

// ──────────────────────────────────────────────
// Build
// ──────────────────────────────────────────────
//
// 步骤：
//   1. 根据 bits_per_key 和 key 数量，计算需要多少个 bit（至少 64 位，
//      避免 key 很少时 bit 数组太小导致误判率飙升）
//   2. 计算需要几个哈希函数 k：k = bits_per_key * ln(2) ≈ bits_per_key * 0.69
//      这是让误判率最优的经典公式（k 太少漏判空间不够，k 太多把 bit 占满）
//   3. 对每个 key，用"双重哈希"技巧模拟出 k 个不同的哈希值：
//      只真正计算一次哈希 h，再用 delta 做旋转位移，避免调用 k 次独立哈希函数
//      （leveldb 采用同样的技巧，性能和效果都不错）
//   4. 把 k 存在序列化数据的最后一个字节，供 MayContain 使用
std::string BloomFilter::Build(const std::vector<std::string>& keys, int bits_per_key){
    int k=static_cast<int>(bits_per_key*0.69);
    k=std::max(1,std::min(k,30));// 限制在合理范围 [1, 30]

    size_t num_bits=std::max<size_t>(keys.size()*bits_per_key,64);
    // 向上取整到 8 的倍数，保证能用整数个字节表示
    num_bits=((num_bits+7)/8)*8;
    size_t num_bytes=num_bits/8;

    // 多申请 1 个字节，用来存 k
    std::string data(num_bytes+1,'\0');

    for(const auto& key:keys){
        uint32_t h=Hash(key);
        // delta 用于生成"第二个哈希函数"，通过旋转位移得到，
        // 避免真的写第二个哈希算法
        const uint32_t delta=(h>>17) | (h<<25);

        for(int i=0;i<k;++i){
            uint32_t bit_pos=h%num_bits;
            data[bit_pos/8] |= static_cast<char>(1u<<(bit_pos%8));
            h+=delta;
        }
    }
    data[num_bytes]=static_cast<char>(k);// 最后一个字节记录 k
    return data;
}

// ──────────────────────────────────────────────
// MayContain
// ──────────────────────────────────────────────
//
// 和 Build 用完全相同的哈希计算方式，反推出 k 个 bit 位置，
// 只要有一个位置是 0，就能 100% 确定这个 key 从未被插入过。
bool BloomFilter::MayContain(const std::string& filter_data, const std::string& key){
    // 数据太短（连 k 都存不下），说明 filter 无效，
    // 保守起见返回 true（"可能存在"），退化成一定会去磁盘查一次，
    // 不会因为 filter 损坏而误判"不存在"导致数据丢失。
    if(filter_data.size()<2){
        return true;
    }

    size_t num_bytes=filter_data.size()-1;
    size_t num_bits=num_bytes*8;
    uint8_t k=static_cast<uint8_t>(filter_data[num_bytes]);

    uint32_t h=Hash(key);
    const uint32_t delta=(h>>17) | (h<<15);

    for(uint8_t i=0;i<k;++i){
        uint32_t bit_pos=h%num_bits;
        char byte=filter_data[bit_pos/8];
        if((byte & (1<<(bit_pos%8)))==0){
            return false;// 有一位是 0，一定不存在
        }
        h+=delta;
    }
    return true;// 所有位都是 1，可能存在
}
}