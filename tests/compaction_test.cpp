#include "compaction.h"
#include <iostream>
#include <cstdio>

#include "sstable.h"

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


// void TestSingleSource(){
//     std::cout << "[TEST] SingleSource ... ";

//     std::vector<std::vector<KVEntry>> sources={
//         {{"a","1",false}, {"b","2",false}, {"c","3",false}},
//     };

//     auto result=KWayMerger::Merge(sources, /*drop_tombstones=*/false);

//     CHECK(result.size()==3);
//     CHECK(result[0].key=="a" && result[0].value=="1");
//     CHECK(result[1].key=="b" && result[1].value=="2");
//     CHECK(result[2].key=="c" && result[2].value=="3");

//      std::cout << "PASS\n";
// }

// void TestNonOverlappingSources() {
//     std::cout << "[TEST] NonOverlappingSources ... ";

//     // 两组来源 key 完全不重叠，结果应该是简单的归并排序
//     std::vector<std::vector<KVEntry>> sources={
//         {{"c","new_c",false}, {"d","new_d",false}},
//         {{"a","old_a",false}, {"b","old_b",false}},
//     };

//     auto result=KWayMerger::Merge(sources,false);

//     CHECK(result.size()==4);
//     CHECK(result[0].key=="a");
//     CHECK(result[1].key=="b");
//     CHECK(result[2].key=="c");
//     CHECK(result[3].key=="d");

//     std::cout << "PASS\n";
// }

// ──────────────────────────────────────────────
// 第一块：纯内存多路归并算法测试
// ──────────────────────────────────────────────
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

// void TestDuplicateKeyAcrossThreeSources() {
//     std::cout << "[TEST] DuplicateKeyAcrossThreeSources ... ";
 
//     // 三个来源都有 key "x"，只有最新（source_idx=0）的版本应该保留
//     std::vector<std::vector<KVEntry>> sources={
//         {{"x","newest",false}},
//         {{"x","middle",false}},
//         {{"x","oldest",false}},
//     };

//     auto result=KWayMerger::Merge(sources,false);

//     CHECK(result.size()==1);
//     CHECK(result[0].value=="newest");

//     std::cout << "PASS\n";
// }

// void TestTombstoneKeptWhenNotDropping() {
//     std::cout << "[TEST] TombstoneKeptWhenNotDropping ... ";
 
//     std::vector<std::vector<KVEntry>> sources = {
//         {{"gone", "", true}},   // 最新版本是删除标记
//         {{"gone", "old_value", false}},  // 更旧的、已经"过期"的真实数据
//     };
 
//     // drop_tombstones=false：中间层合并，tombstone 要保留，
//     // 否则更上层查询这个 key 时会意外读到已经过期的旧值
//     auto result = KWayMerger::Merge(sources, false);
 
//     CHECK(result.size() == 1);
//     CHECK(result[0].key == "gone");
//     CHECK(result[0].is_deleted == true);  // tombstone 被保留
 
//     std::cout << "PASS\n";
// }

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

// void TestTombstoneForNonexistentKeyElsewhere() {
//     std::cout << "[TEST] TombstoneForNonexistentKeyElsewhere ... ";
 
//     // "gone" 只在一个来源里出现，且是 tombstone，其余 key 正常
//     std::vector<std::vector<KVEntry>> sources = {
//         {{"a", "1", false}, {"gone", "", true}, {"z", "26", false}},
//     };
 
//     auto result = KWayMerger::Merge(sources, /*drop_tombstones=*/true);
 
//     CHECK(result.size() == 2);  // 只剩 a 和 z，"gone" 被清除
//     CHECK(result[0].key == "a");
//     CHECK(result[1].key == "z");
 
//     std::cout << "PASS\n";
// }

// void TestEmptySources() {
//     std::cout << "[TEST] EmptySources ... ";
 
//     std::vector<std::vector<KVEntry>> sources;
//     auto result = KWayMerger::Merge(sources, false);
//     CHECK(result.empty());
 
//     // 来源列表非空，但每一组都是空的
//     std::vector<std::vector<KVEntry>> sources2 = {{}, {}};
//     auto result2 = KWayMerger::Merge(sources2, false);
//     CHECK(result2.empty());
 
//     std::cout << "PASS\n";
// }

// void TestInterleavedSources() {
//     std::cout << "[TEST] InterleavedSources (交叉但不重复的key) ... ";
 
//     // 模拟真实场景：多个 SSTable 的 key 范围交叉分布
//     std::vector<std::vector<KVEntry>> sources = {
//         {{"b", "b1", false}, {"e", "e1", false}, {"h", "h1", false}},
//         {{"a", "a1", false}, {"d", "d1", false}, {"g", "g1", false}},
//         {{"c", "c1", false}, {"f", "f1", false}, {"i", "i1", false}},
//     };
 
//     auto result = KWayMerger::Merge(sources, false);
 
//     CHECK(result.size() == 9);
//     std::string expected = "abcdefghi";
//     for (size_t i = 0; i < result.size(); ++i) {
//         CHECK(result[i].key == std::string(1, expected[i]));
//     }
 
//     std::cout << "PASS\n";
// }

// ──────────────────────────────────────────────
// 第二块：CompactFiles —— 真实文件级别的端到端测试
// ──────────────────────────────────────────────
static const char* kFileNew  = "test_compact_new.sst";
static const char* kFileOld  = "test_compact_old.sst";
static const char* kFileOut  = "test_compact_out.sst";
 
static void RemoveTestFiles() {
    std::remove(kFileNew);
    std::remove(kFileOld);
    std::remove(kFileOut);
}

void TestCompactFilesBasic() {
    RemoveTestFiles();

    // 旧文件：a, b, c
    std::vector<SSTable::Entry> old_entries = {
        {"a", "a_old", false},
        {"b", "b_old", false},
        {"c", "c_old", false},
    };
    CHECK(SSTable::BuildFromEntries(old_entries, kFileOld));

    // 新文件：更新了 b，新增了 d
    std::vector<SSTable::Entry> new_entries = {
        {"b", "b_new", false},
        {"d", "d_new", false},
    };
    CHECK(SSTable::BuildFromEntries(new_entries, kFileNew));

    // 合并：kFileNew 排在前面，代表更新
    std::vector<std::string> inputs = {kFileNew, kFileOld};
    CHECK(Compaction::CompactFiles(inputs, kFileOut, false));

    SSTable result(kFileOut);
    std::string val;
    CHECK(result.Get("a", &val) && val == "a_old");   // 只在旧文件，保留
    CHECK(result.Get("b", &val) && val == "b_new");   // 两个文件都有，新版本胜出
    CHECK(result.Get("c", &val) && val == "c_old");   // 只在旧文件，保留
    CHECK(result.Get("d", &val) && val == "d_new");   // 只在新文件，保留
}

void TestCompactFilesDropsTombstoneAtBottom() {
    std::cout << "[TEST] CompactFilesDropsTombstoneAtBottom ... ";
    RemoveTestFiles();
 
    std::vector<SSTable::Entry> old_entries = {
        {"deleted_key", "will_be_gone", false},
        {"survivor", "old_value", false},
    };
    CHECK(SSTable::BuildFromEntries(old_entries, kFileOld));
 
    std::vector<SSTable::Entry> new_entries = {
        {"deleted_key", "", true},  // 新文件里这个 key 被删除了
    };
    CHECK(SSTable::BuildFromEntries(new_entries, kFileNew));
 
    std::vector<std::string> inputs = {kFileNew, kFileOld};
 
    // 合并到最底层：tombstone 应该被彻底清除
    CHECK(Compaction::CompactFiles(inputs, kFileOut, /*drop_tombstones=*/true));
 
    SSTable result(kFileOut);
    std::string val;
    CHECK(result.Get("survivor", &val) && val == "old_value");
    CHECK(!result.Get("deleted_key", &val));  // 彻底消失
 
    // 验证真的从文件里删掉了，而不只是查询时被过滤——
    // 用 ForEach 遍历整个文件，"deleted_key" 不应该出现
    int count = 0;
    bool found_deleted_key = false;
    result.ForEach([&](const std::string& k, const std::string&, bool) {
        ++count;
        if (k == "deleted_key") found_deleted_key = true;
    });
    CHECK(count == 1);  // 只剩 survivor 一条
    CHECK(!found_deleted_key);
 
    RemoveTestFiles();
    std::cout << "PASS\n";
}

void TestCompactFilesKeepsTombstoneWhenNotBottom() {
    std::vector<SSTable::Entry> old_entries = {{"key", "old_value", false}};
    CHECK(SSTable::BuildFromEntries(old_entries, kFileOld));

    std::vector<SSTable::Entry> new_entries = {{"key", "", true}};
    CHECK(SSTable::BuildFromEntries(new_entries, kFileNew));

    std::vector<std::string> inputs = {kFileNew, kFileOld};
    CHECK(Compaction::CompactFiles(inputs, kFileOut, /*drop_tombstones=*/false));

    SSTable result(kFileOut);
    int count = 0;
    bool found_tombstone = false;
    result.ForEach([&](const std::string& k, const std::string&, bool del) {
        ++count;
        if (k == "key" && del) found_tombstone = true;
    });
    CHECK(count == 1);
    CHECK(found_tombstone);   // tombstone 必须还在
}

void TestCompactFilesMultipleInputs() {
    const char* f1 = "test_compact_f1.sst";  // 最新
    const char* f2 = "test_compact_f2.sst";  // 中间
    const char* f3 = "test_compact_f3.sst";  // 最旧

    SSTable::BuildFromEntries({{"x", "v_newest", false}}, f1);
    SSTable::BuildFromEntries({{"x", "v_middle", false}, {"y", "y1", false}}, f2);
    SSTable::BuildFromEntries({{"x", "v_oldest", false}, {"z", "z1", false}}, f3);

    std::vector<std::string> inputs = {f1, f2, f3};
    CHECK(Compaction::CompactFiles(inputs, kFileOut, false));

    SSTable result(kFileOut);
    std::string val;
    CHECK(result.Get("x", &val) && val == "v_newest");  // 三者取最新
    CHECK(result.Get("y", &val) && val == "y1");
    CHECK(result.Get("z", &val) && val == "z1");
}
 
// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
 
int main() {
    std::cout << "=== Compaction (KWayMerger) Unit Tests ===\n";
 
    // TestSingleSource();
    // TestNonOverlappingSources();
    TestDuplicateKeyNewestWins();
    // TestDuplicateKeyAcrossThreeSources();
    // TestTombstoneKeptWhenNotDropping();
    TestTombstoneDroppedAtBottomLevel();
    // TestTombstoneForNonexistentKeyElsewhere();
    // TestEmptySources();
    // TestInterleavedSources();

    TestCompactFilesBasic();                     // 新增
    TestCompactFilesDropsTombstoneAtBottom();    // 新增
    TestCompactFilesKeepsTombstoneWhenNotBottom(); // 新增
    TestCompactFilesMultipleInputs();            // 新增
 
    std::cout << "\nAll tests passed.\n";
    return 0;
}
 