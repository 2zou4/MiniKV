#include "compaction.h"
#include <iostream>
using namespace minikv;

#define CHECK(cond)\
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " #cond \
                      << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; \
            std::exit(1); \
        } \
    } while (0)

    // ──────────────────────────────────────────────
// 测试用例
// ──────────────────────────────────────────────

void TestSingleSource(){
    std::cout << "[TEST] SingleSource ... ";

    std::vector<std::vector<KVEntry>> sources={
        {{"a","1",false}, {"b","2",false}, {"c","3",false}},
    };

    auto result=KWayMerger::Merge(sources, /*drop_tombstones=*/false);

    CHECK(result.size()==3);
    CHECK(result[0].key=="a" && result[0].value=="1");
    CHECK(result[1].key=="b" && result[1].value=="2");
    CHECK(result[2].key=="c" && result[2].value=="3");

     std::cout << "PASS\n";
}

void TestNonOverlappingSources() {
    std::cout << "[TEST] NonOverlappingSources ... ";

    // 两组来源 key 完全不重叠，结果应该是简单的归并排序
    std::vector<std::vector<KVEntry>> sources={
        {{"c","new_c",false}, {"d","new_d",false}},
        {{"a","old_a",false}, {"b","old_b",false}},
    };

    auto result=KWayMerger::Merge(sources,false);

    CHECK(result.size()==4);
    CHECK(result[0].key=="a");
    CHECK(result[1].key=="b");
    CHECK(result[2].key=="c");
    CHECK(result[3].key=="d");

    std::cout << "PASS\n";
}

void TestDuplicateKeyNewestWins() {
    std::cout << "[TEST] DuplicateKeyNewestWins ... ";

    // 核心场景：同一个 key "b" 在两个来源里都存在，
    // sources[0] 是更新的文件，它的版本应该胜出
    std::vector<std::vector<KVEntry>> sources={
        {{"a","a1",false}, {"b","b_new",false}},
        {{"b","b_old",false}, {"c","c1",false}},
    };

    auto result=KWayMerger::Merge(sources,false);

    CHECK(result.size()==3);// a, b, c —— "b" 只出现一次
    CHECK(result[0].key=="a" && result[0].value=="a1");
    CHECK(result[1].key=="b" && result[1].value=="b_new");// 新版本胜出
    CHECK(result[2].key=="c" && result[2].value=="c1");

    std::cout << "PASS\n";
}

void TestDuplicateKeyAcrossThreeSources() {
    std::cout << "[TEST] DuplicateKeyAcrossThreeSources ... ";
 
    // 三个来源都有 key "x"，只有最新（source_idx=0）的版本应该保留
    std::vector<std::vector<KVEntry>> sources={
        {{"x","newest",false}},
        {{"x","middle",false}},
        {{"x","oldest",false}},
    };

    auto result=KWayMerger::Merge(sources,false);

    CHECK(result.size()==1);
    CHECK(result[0].value=="newwst");

    std::cout << "PASS\n";
}

void TestTombstoneKeptWhenNotDropping() {
    std::cout << "[TEST] TombstoneKeptWhenNotDropping ... ";
 
    std::vector<std::vector<KVEntry>> sources = {
        {{"gone", "", true}},   // 最新版本是删除标记
        {{"gone", "old_value", false}},  // 更旧的、已经"过期"的真实数据
    };
 
    // drop_tombstones=false：中间层合并，tombstone 要保留，
    // 否则更上层查询这个 key 时会意外读到已经过期的旧值
    auto result = KWayMerger::Merge(sources, false);
 
    CHECK(result.size() == 1);
    CHECK(result[0].key == "gone");
    CHECK(result[0].is_deleted == true);  // tombstone 被保留
 
    std::cout << "PASS\n";
}

void TestTombstoneDroppedAtBottomLevel() {
    std::cout << "[TEST] TombstoneDroppedAtBottomLevel ... ";
 
    std::vector<std::vector<KVEntry>> sources = {
        {{"gone", "", true}},
        {{"gone", "old_value", false}},
    };
 
    // drop_tombstones=true：合并到最底层，确认没有更旧的层了，
    // tombstone 可以彻底清除，不再写入结果
    auto result = KWayMerger::Merge(sources, true);
 
    CHECK(result.empty());  // "gone" 完全消失，包括 tombstone 本身
 
    std::cout << "PASS\n";
}

void TestTombstoneForNonexistentKeyElsewhere() {
    std::cout << "[TEST] TombstoneForNonexistentKeyElsewhere ... ";
 
    // "gone" 只在一个来源里出现，且是 tombstone，其余 key 正常
    std::vector<std::vector<KVEntry>> sources = {
        {{"a", "1", false}, {"gone", "", true}, {"z", "26", false}},
    };
 
    auto result = KWayMerger::Merge(sources, /*drop_tombstones=*/true);
 
    CHECK(result.size() == 2);  // 只剩 a 和 z，"gone" 被清除
    CHECK(result[0].key == "a");
    CHECK(result[1].key == "z");
 
    std::cout << "PASS\n";
}

void TestEmptySources() {
    std::cout << "[TEST] EmptySources ... ";
 
    std::vector<std::vector<KVEntry>> sources;
    auto result = KWayMerger::Merge(sources, false);
    CHECK(result.empty());
 
    // 来源列表非空，但每一组都是空的
    std::vector<std::vector<KVEntry>> sources2 = {{}, {}};
    auto result2 = KWayMerger::Merge(sources2, false);
    CHECK(result2.empty());
 
    std::cout << "PASS\n";
}

void TestInterleavedSources() {
    std::cout << "[TEST] InterleavedSources (交叉但不重复的key) ... ";
 
    // 模拟真实场景：多个 SSTable 的 key 范围交叉分布
    std::vector<std::vector<KVEntry>> sources = {
        {{"b", "b1", false}, {"e", "e1", false}, {"h", "h1", false}},
        {{"a", "a1", false}, {"d", "d1", false}, {"g", "g1", false}},
        {{"c", "c1", false}, {"f", "f1", false}, {"i", "i1", false}},
    };
 
    auto result = KWayMerger::Merge(sources, false);
 
    CHECK(result.size() == 9);
    std::string expected = "abcdefghi";
    for (size_t i = 0; i < result.size(); ++i) {
        CHECK(result[i].key == std::string(1, expected[i]));
    }
 
    std::cout << "PASS\n";
}
 
// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
 
int main() {
    std::cout << "=== Compaction (KWayMerger) Unit Tests ===\n";
 
    TestSingleSource();
    TestNonOverlappingSources();
    TestDuplicateKeyNewestWins();
    TestDuplicateKeyAcrossThreeSources();
    TestTombstoneKeptWhenNotDropping();
    TestTombstoneDroppedAtBottomLevel();
    TestTombstoneForNonexistentKeyElsewhere();
    TestEmptySources();
    TestInterleavedSources();
 
    std::cout << "\nAll tests passed.\n";
    return 0;
}
 