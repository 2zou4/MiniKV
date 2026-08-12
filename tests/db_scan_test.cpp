#include "db.h"
 
#include <cstdio>
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
 
static const char* kWalPath = "test_scan.wal";
static const char* kDir     = ".";
 
static std::string MakeSSTFile(const std::string& name,
                                const std::vector<SSTable::Entry>& entries) {
    std::string path = std::string(kDir) + "/" + name;
    SSTable::BuildFromEntries(entries, path);
    return path;
}

void TestScanMemTableOnly() {
    std::cout << "[TEST] ScanMemTableOnly ... ";
    std::remove(kWalPath);
 
    DB db(kWalPath, kDir);
    db.Put("banana", "b");
    db.Put("apple", "a");
    db.Put("cherry", "c");
 
    auto result = db.Scan();
    CHECK(result.size() == 3);
    CHECK(result[0].key == "apple"  && result[0].value == "a");
    CHECK(result[1].key == "banana" && result[1].value == "b");
    CHECK(result[2].key == "cherry" && result[2].value == "c");
 
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestScanExcludesDeletedKeys() {
    std::cout << "[TEST] ScanExcludesDeletedKeys ... ";
    std::remove(kWalPath);
 
    DB db(kWalPath, kDir);
    db.Put("a", "1");
    db.Put("b", "2");
    db.Delete("b");
    db.Put("c", "3");
 
    auto result = db.Scan();
    CHECK(result.size() == 2);  // "b" 应该被排除
    CHECK(result[0].key == "a");
    CHECK(result[1].key == "c");
 
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestScanAcrossMemTableAndL0DedupsToNewest() {
    std::cout << "[TEST] ScanAcrossMemTableAndL0DedupsToNewest ... ";
    std::remove(kWalPath);
 
    // L0 文件里有个旧版本
    std::string l0 = MakeSSTFile("test_scan_l0.sst",
                                  {{"key", "old_from_disk", false},
                                   {"only_in_l0", "disk_val", false}});
 
    DB db(kWalPath, kDir);
    db.Levels().AddL0File(l0);
    db.Put("key", "new_from_memtable");  // MemTable 里是更新的版本
 
    auto result = db.Scan();
    CHECK(result.size() == 2);
 
    // key 应该取 MemTable 的新版本，only_in_l0 只在磁盘上，也要出现
    bool found_key = false, found_disk_only = false;
    for (const auto& e : result) {
        if (e.key == "key") {
            CHECK(e.value == "new_from_memtable");
            found_key = true;
        }
        if (e.key == "only_in_l0") {
            CHECK(e.value == "disk_val");
            found_disk_only = true;
        }
    }
    CHECK(found_key && found_disk_only);
 
    std::remove(l0.c_str());
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestScanDeletedInMemTableHidesL0Value() {
    std::cout << "[TEST] ScanDeletedInMemTableHidesL0Value ... ";
    std::remove(kWalPath);
 
    // L0 里有真实值，MemTable 里这个 key 被删除了
    std::string l0 = MakeSSTFile("test_scan_l0_del.sst",
                                  {{"gone", "old_disk_value", false}});
 
    DB db(kWalPath, kDir);
    db.Levels().AddL0File(l0);
    db.Delete("gone");
 
    auto result = db.Scan();
    for (const auto& e : result) {
        CHECK(e.key != "gone");  // 绝不能出现——这正是 Get 里验证过的同一个坑
    }
 
    std::remove(l0.c_str());
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestScanRangeQuery() {
    std::cout << "[TEST] ScanRangeQuery ... ";
    std::remove(kWalPath);
 
    DB db(kWalPath, kDir);
    for (char c = 'a'; c <= 'j'; ++c) {
        db.Put(std::string(1, c), std::string(1, c) + "_value");
    }
 
    // [c, g) —— 应该包含 c, d, e, f，不包含 g
    auto result = db.Scan("c", "g");
    CHECK(result.size() == 4);
    CHECK(result[0].key == "c");
    CHECK(result[1].key == "d");
    CHECK(result[2].key == "e");
    CHECK(result[3].key == "f");
 
    // 只设下界
    auto result2 = db.Scan("h", "");
    CHECK(result2.size() == 3);  // h, i, j
    CHECK(result2[0].key == "h");
 
    // 只设上界
    auto result3 = db.Scan("", "c");
    CHECK(result3.size() == 2);  // a, b
    CHECK(result3[0].key == "a");
    CHECK(result3[1].key == "b");
 
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestScanEmptyDB() {
    std::cout << "[TEST] ScanEmptyDB ... ";
    std::remove(kWalPath);
 
    DB db(kWalPath, kDir);
    auto result = db.Scan();
    CHECK(result.empty());
 
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestScanThreeLayersTogether() {
    std::cout << "[TEST] ScanThreeLayersTogether ... ";
    std::remove(kWalPath);
 
    DB db(kWalPath, kDir);
 
    // 造 4 个 L0 文件触发一次 Compaction，让数据同时分布在 L0 和 L1
    for (int i = 0; i < 4; ++i) {
        std::string path = MakeSSTFile("test_scan_seed_" + std::to_string(i) + ".sst",
                                        {{"l1_key" + std::to_string(i),
                                          "l1_val" + std::to_string(i), false}});
        db.Levels().AddL0File(path);
    }
    CHECK(db.MaybeCompact());  // 触发合并，数据落到 L1
 
    // 再加一个新的 L0 文件
    std::string l0 = MakeSSTFile("test_scan_new_l0.sst",
                                  {{"l0_key", "l0_val", false}});
    db.Levels().AddL0File(l0);
 
    // MemTable 里再加点数据
    db.Put("mem_key", "mem_val");
 
    auto result = db.Scan();
    // 4 个 L1 key + 1 个 L0 key + 1 个 MemTable key = 6 条
    CHECK(result.size() == 6);
 
    std::remove(l0.c_str());
    std::string l1_path = db.Levels().L1FilePath();
    if (!l1_path.empty()) std::remove(l1_path.c_str());
    std::remove(kWalPath);
    std::cout << "PASS\n";
}
 
// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
 
int main() {
    std::cout << "=== DB Scan Unit Tests ===\n";
 
    TestScanMemTableOnly();
    TestScanExcludesDeletedKeys();
    TestScanAcrossMemTableAndL0DedupsToNewest();
    TestScanDeletedInMemTableHidesL0Value();
    TestScanRangeQuery();
    TestScanEmptyDB();
    TestScanThreeLayersTogether();
 
    std::cout << "\nAll tests passed.\n";
    return 0;
}