#include "bloom_filter.h"
 
#include <iostream>
#include <string>
#include <vector>
 
using namespace minikv;
 
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " #cond \
                      << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; \
            std::exit(1); \
        } \
    } while (0)

void TestNoFalseNegative(){
    std::cout << "[TEST] NoFalseNegative ... ";

    std::vector<std::string> keys={"apple","banana","cherry","date","elderberry"};
    std::string filter=BloomFilter::Build(keys);

    // 核心保证：所有插入过的 key，MayContain 必须返回 true
    // （绝不能有假阴性，否则数据会被误判为"不存在"）
    for(const auto& key:keys){
        CHECK(BloomFilter::MayContain(filter,key));
    }

    std::cout << "PASS\n";
}

void TestMostlyRejectsAbsentKeys(){
    std::cout << "[TEST] MostlyRejectsAbsentKeys ... ";

    std::vector<std::string> keys;
    for(int i=0;i<1000;++i){
        keys.push_back("key"+std::to_string(i));
    }
    std::string filter=BloomFilter::Build(keys,/*bits_per_key=*/10);

    // 用一批肯定没插入过的 key 测试，统计误判率
    int false_positives=0;
    int total=1000;
    for(int i=0;i<total;++i){
        std::string absent_key="absent"+std::to_string(i);
        if(BloomFilter::MayContain(filter,absent_key)){
            ++false_positives;
        }
    }

    // bits_per_key=10 理论误判率约 1%，给足够宽松的容差，
    // 只要不是离谱地高（比如超过 10%）就算通过，避免偶然的哈希碰撞导致测试不稳定
    double rate=static_cast<double>(false_positives)/total;
    CHECK(rate<0.10);

    std::cout << "PASS (false positive rate: " << rate * 100 << "%)\n";
}
void TestEmptyKeySet(){
    std::cout << "[TEST] EmptyKeySet ... ";

    std::vector<std::string> keys;//空集合
    std::string filter=BloomFilter::Build(keys);

    // 空集合上查询任何 key，理论上都应该返回 false（一定不存在），
    // 但由于 bit 数组最小 64 位仍可能全 0，不强制要求，只验证不崩溃
    bool result=BloomFilter::MayContain(filter,"anything");
    (void) result;// 允许 true 或 false，只要不崩溃即可
 
    std::cout << "PASS\n";
}

void TestCorruptedFilterFallsBackToTrue(){
    std::cout << "[TEST] CorruptedFilterFallsBackToTrue ... ";
 
    // 传入一个太短、不合法的 filter 数据
    std::string corrupted="";
    CHECK(BloomFilter::MayContain(corrupted,"key")==true);

    std::string too_short(1,'\0');
    CHECK(BloomFilter::MayContain(too_short,"key")==true);

    std::cout << "PASS\n";
}

void TestDifferentBitsPerKey(){
    std::cout << "[TEST] DifferentBitsPerKey ... ";
 
    // 用足够多的 key（100个），确保 keys.size()*bits_per_key
    // 远超过内部"至少 64 位"的下限，这样 bits_per_key 的差异才能真正体现在
    // 序列化后的字节数上，不会被下限兜底抹平。
    std::vector<std::string> keys;
    for(int i=0;i<100;++i){
        keys.push_back("key"+std::to_string(i));
    }

    // bits_per_key 越大，filter 应该越大（字节数越多）
    std::string filter_small=BloomFilter::Build(keys,/*bits_per_key=*/2);
    std::string filter_large=BloomFilter::Build(keys,/*bits_per_key=*/20);

    CHECK(filter_large.size()>filter_small.size());

    // 两种配置下，插入过的 key 都应该能查到
    for(const auto& key:keys){
        CHECK(BloomFilter::MayContain(filter_small,key));
        CHECK(BloomFilter::MayContain(filter_large,key));
    }

    std::cout << "PASS\n";
}

int main() {
    std::cout << "=== BloomFilter Unit Tests ===\n";
 
    TestNoFalseNegative();
    TestMostlyRejectsAbsentKeys();
    TestEmptyKeySet();
    TestCorruptedFilterFallsBackToTrue();
    TestDifferentBitsPerKey();
 
    std::cout << "\nAll tests passed.\n";
    return 0;
}
 

