#include "level_manager.h"
 
#include <cstdio>
#include <fstream>
#include <iostream>
 
#include "sstable.h"
 
using namespace minikv;
 
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " #cond << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; \
            std::exit(1); \
        } \
    } while (0)
 
static const char* kTestDir = ".";

// 生成若干测试用的 L0 文件，返回它们的路径
static std::string MakeL0File(const std::string& name,
                               const std::vector<SSTable::Entry>& entries) {
    std::string path = std::string(kTestDir) + "/" + name;
    SSTable::BuildFromEntries(entries, path);
    return path;
}

// 判断一个文件是否还存在于磁盘上
static bool FileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}
 
// 清理测试过程中可能生成的所有文件（L0_test_*.sst 和 L1_*.sst）
static void CleanupFiles(const std::vector<std::string>& paths) {
    for (const auto& p : paths) std::remove(p.c_str());
}

// ──────────────────────────────────────────────
// 测试用例
// ──────────────────────────────────────────────
 
void TestNoCompactionBelowThreshold() {
    std::cout << "[TEST] NoCompactionBelowThreshold ... ";
 
    LevelManager lm(kTestDir);
    std::vector<std::string> created;
 
    // 只加 3 个 L0 文件，低于阈值 4，不应该触发合并
    for (int i = 0; i < 3; ++i) {
        std::string path = MakeL0File(
            "test_lm_below_" + std::to_string(i) + ".sst",
            {{"k" + std::to_string(i), "v" + std::to_string(i), false}});
        created.push_back(path);
        lm.AddL0File(path);
    }
 
    CHECK(lm.L0FileCount() == 3);
    CHECK(lm.L1FileCount() == 0);
    CHECK(!lm.MaybeCompact());  // 没达到阈值，应该返回 false
    CHECK(lm.L0FileCount() == 3);  // 状态不应该变
 
    CleanupFiles(created);
    std::cout << "PASS\n";
}

void TestCompactionTriggersAtThreshold() {
    std::cout << "[TEST] CompactionTriggersAtThreshold ... ";
 
    LevelManager lm(kTestDir);
    std::vector<std::string> created;
 
    // 加满 4 个 L0 文件，key 互不重叠
    for (int i = 0; i < 4; ++i) {
        std::string path = MakeL0File(
            "test_lm_trig_" + std::to_string(i) + ".sst",
            {{"key" + std::to_string(i), "val" + std::to_string(i), false}});
        created.push_back(path);
        lm.AddL0File(path);
    }
 
    CHECK(lm.L0FileCount() == 4);
    CHECK(lm.MaybeCompact());       // 达到阈值，应该触发
    CHECK(lm.L0FileCount() == 0);   // L0 应该清空
    CHECK(lm.L1FileCount() == 1);   // L1 应该有一个新文件
 
    // 验证合并后的 L1 文件内容正确
    SSTable l1(lm.L1FilePath());
    std::string val;
    for (int i = 0; i < 4; ++i) {
        CHECK(l1.Get("key" + std::to_string(i), &val));
        CHECK(val == "val" + std::to_string(i));
    }
 
    // 验证旧的 L0 物理文件真的被删除了
    for (const auto& path : created) {
        CHECK(!FileExists(path));
    }
 
    CleanupFiles({lm.L1FilePath()});
    std::cout << "PASS\n";
}

void TestDuplicateKeysNewestWins() {
    std::cout << "[TEST] DuplicateKeysNewestWins ... ";
 
    LevelManager lm(kTestDir);
    std::vector<std::string> created;
 
    // 4 个文件都写同一个 key "shared"，模拟这个 key 被反复更新，
    // 追加顺序（AddL0File 调用顺序）越晚，逻辑上应该越新
    for (int i = 0; i < 4; ++i) {
        std::string path = MakeL0File(
            "test_lm_dup_" + std::to_string(i) + ".sst",
            {{"shared", "version" + std::to_string(i), false}});
        created.push_back(path);
        lm.AddL0File(path);
    }
 
    CHECK(lm.MaybeCompact());
 
    SSTable l1(lm.L1FilePath());
    std::string val;
    CHECK(l1.Get("shared", &val));
    // 最后一次 AddL0File 的版本（i=3）应该胜出
    CHECK(val == "version3");
 
    CleanupFiles({lm.L1FilePath()});
    std::cout << "PASS\n";
}

void TestTombstoneClearedAfterCompaction() {
    std::cout << "[TEST] TombstoneClearedAfterCompaction ... ";
 
    LevelManager lm(kTestDir);
    std::vector<std::string> created;
 
    // 第一个文件：key "gone" 有真实值
    created.push_back(MakeL0File("test_lm_ts_0.sst",
                                  {{"gone", "old_value", false}}));
    lm.AddL0File(created.back());
 
    // 后面几个占位文件，凑够触发阈值
    created.push_back(MakeL0File("test_lm_ts_1.sst",
                                  {{"a", "1", false}}));
    lm.AddL0File(created.back());
 
    created.push_back(MakeL0File("test_lm_ts_2.sst",
                                  {{"b", "2", false}}));
    lm.AddL0File(created.back());
 
    // 最后一个文件：把 "gone" 删除了（更新的 tombstone）
    created.push_back(MakeL0File("test_lm_ts_3.sst",
                                  {{"gone", "", true}}));
    lm.AddL0File(created.back());
 
    CHECK(lm.MaybeCompact());
 
    SSTable l1(lm.L1FilePath());
    std::string val;
    CHECK(!l1.Get("gone", &val));  // 应该查不到
 
    // L1 是最底层，tombstone 应该被彻底清除，而不只是查询时被过滤
    bool found_gone = false;
    int total = 0;
    l1.ForEach([&](const std::string& k, const std::string&, bool) {
        ++total;
        if (k == "gone") found_gone = true;
    });
    CHECK(!found_gone);
    CHECK(total == 2);  // 只剩 a 和 b
 
    CleanupFiles({lm.L1FilePath()});
    std::cout << "PASS\n";
}

void TestRepeatedCompactionAbsorbsIntoL1() {
    std::cout << "[TEST] RepeatedCompactionAbsorbsIntoL1 ... ";
 
    LevelManager lm(kTestDir);
    std::vector<std::string> all_created;
 
    // 第一轮：4 个文件触发一次合并
    for (int i = 0; i < 4; ++i) {
        std::string path = MakeL0File(
            "test_lm_rep1_" + std::to_string(i) + ".sst",
            {{"r1_key" + std::to_string(i), "r1_val" + std::to_string(i), false}});
        all_created.push_back(path);
        lm.AddL0File(path);
    }
    CHECK(lm.MaybeCompact());
    std::string l1_after_first = lm.L1FilePath();
    CHECK(lm.L1FileCount() == 1);
 
    // 第二轮：再加 4 个新文件，应该和已有的 L1 文件一起合并
    for (int i = 0; i < 4; ++i) {
        std::string path = MakeL0File(
            "test_lm_rep2_" + std::to_string(i) + ".sst",
            {{"r2_key" + std::to_string(i), "r2_val" + std::to_string(i), false}});
        all_created.push_back(path);
        lm.AddL0File(path);
    }
    CHECK(lm.MaybeCompact());
 
    // 第一轮生成的 L1 文件应该已经被清理，替换成新的 L1 文件
    CHECK(!FileExists(l1_after_first));
    CHECK(lm.L1FileCount() == 1);
 
    // 新的 L1 文件应该同时包含两轮的全部数据
    SSTable l1(lm.L1FilePath());
    std::string val;
    for (int i = 0; i < 4; ++i) {
        CHECK(l1.Get("r1_key" + std::to_string(i), &val));
        CHECK(l1.Get("r2_key" + std::to_string(i), &val));
    }
 
    CleanupFiles({lm.L1FilePath()});
    std::cout << "PASS\n";
}
 
// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
 
int main() {
    std::cout << "=== LevelManager Unit Tests ===\n";
 
    TestNoCompactionBelowThreshold();
    TestCompactionTriggersAtThreshold();
    TestDuplicateKeysNewestWins();
    TestTombstoneClearedAfterCompaction();
    TestRepeatedCompactionAbsorbsIntoL1();
 
    std::cout << "\nAll tests passed.\n";
    return 0;
}
 