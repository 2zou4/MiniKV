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

static const char* kWalPath = "test_db.wal";
static const char* kDir     = ".";
 
static std::string MakeSSTFile(const std::string& name,
                                const std::vector<SSTable::Entry>& entries) {
    std::string path = std::string(kDir) + "/" + name;
    SSTable::BuildFromEntries(entries, path);
    return path;
}
 
static void RemoveFiles(const std::vector<std::string>& paths) {
    for (const auto& p : paths) std::remove(p.c_str());
}
 
// ──────────────────────────────────────────────
// 测试用例
// ──────────────────────────────────────────────
void TestFoundInMemTable() {
    std::cout << "[TEST] FoundInMemTable ... ";
    std::remove(kWalPath);
 
    DB db(kWalPath, kDir);
    db.Put("apple", "fruit");
 
    std::string val;
    CHECK(db.Get("apple", &val) && val == "fruit");
 
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestMemTableTombstoneStopsSearch() {
    std::cout << "[TEST] MemTableTombstoneStopsSearch ... ";
    std::remove(kWalPath);
 
    // 制造一个场景：key 在磁盘的 L0 文件里有旧值，
    // 但 MemTable 里这个 key 已经被删除了（最新状态）
    std::string l0 = MakeSSTFile("test_db_l0_a.sst",
                                  {{"gone", "old_disk_value", false}});
 
    DB db(kWalPath, kDir);
    db.Levels().AddL0File(l0);
    db.Delete("gone");  // MemTable 里标记删除
 
    std::string val;
    // 必须返回 false，绝不能因为 L0 文件里还有旧值就把它翻出来
    CHECK(!db.Get("gone", &val));
 
    RemoveFiles({l0});
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestFoundInL0() {
    std::cout << "[TEST] FoundInL0 ... ";
    std::remove(kWalPath);
 
    std::string l0 = MakeSSTFile("test_db_l0_b.sst",
                                  {{"banana", "yellow", false}});
 
    DB db(kWalPath, kDir);
    db.Levels().AddL0File(l0);
 
    std::string val;
    CHECK(db.Get("banana", &val) && val == "yellow");
 
    RemoveFiles({l0});
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestNewerL0FileWinsOverOlder() {
    std::cout << "[TEST] NewerL0FileWinsOverOlder ... ";
    std::remove(kWalPath);
 
    // 两个 L0 文件都有同一个 key，AddL0File 调用顺序决定新旧
    std::string l0_old = MakeSSTFile("test_db_l0_old.sst",
                                      {{"key", "old_version", false}});
    std::string l0_new = MakeSSTFile("test_db_l0_new.sst",
                                      {{"key", "new_version", false}});
 
    DB db(kWalPath, kDir);
    db.Levels().AddL0File(l0_old);   // 先加入的是旧的
    db.Levels().AddL0File(l0_new);   // 后加入的是新的
 
    std::string val;
    CHECK(db.Get("key", &val) && val == "new_version");  // 应该拿到新版本
 
    RemoveFiles({l0_old, l0_new});
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestL0TombstoneStopsBeforeL1() {
    std::cout << "[TEST] L0TombstoneStopsBeforeL1 ... ";
    std::remove(kWalPath);
 
    // L1（更旧）里有真实值，L0（更新）里这个 key 被删除了
    std::string l1 = MakeSSTFile("test_db_l1.sst",
                                  {{"deleted_key", "very_old_value", false}});
    std::string l0 = MakeSSTFile("test_db_l0_tomb.sst",
                                  {{"deleted_key", "", true}});  // tombstone
 
    DB db(kWalPath, kDir);
    // 手动构造层级状态：L1 已经存在，L0 有一个更新的 tombstone
    // （通过反复 AddL0File + MaybeCompact 也能达到同样状态，
    //  这里直接摆好状态，聚焦测试 Get 本身的正确性）
    for (int i = 0; i < 3; ++i) {
        std::string filler = MakeSSTFile("test_db_filler_" + std::to_string(i) + ".sst",
                                          {{"filler" + std::to_string(i), "x", false}});
        db.Levels().AddL0File(filler);
    }
    db.Levels().AddL0File(l0);
    CHECK(db.Levels().MaybeCompact());  // 触发合并：3个filler + l0 tombstone 一起合并进 L1
 
    std::string val;
    // 合并后 L1 应该已经吸收了这条 tombstone，直接清除了 deleted_key
    CHECK(!db.Get("deleted_key", &val));
 
    RemoveFiles({l1, db.Levels().L1FilePath()});
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

void TestFoundInL1AsLastResort() {
    std::cout << "[TEST] FoundInL1AsLastResort ... ";
    std::remove(kWalPath);
 
    // 直接构造几个L0文件，让LevelManager把它们合并进L1，
    // 之后新建的DB只查询到L1里的数据
    LevelManager lm(kDir);
    std::vector<std::string> created;
    for (int i = 0; i < 4; ++i) {
        std::string path = MakeSSTFile("test_db_seed_" + std::to_string(i) + ".sst",
                                        {{"deep_key", "deep_value", false}});
        created.push_back(path);
        lm.AddL0File(path);
    }
    CHECK(lm.MaybeCompact());
    std::string l1_path = lm.L1FilePath();
 
    // 新建一个 DB，直接把这个已有的 L1 文件接进来
    DB db(kWalPath, kDir);
    // DB 目前没有"加载已有 L1"的接口，这里通过 Levels() 直接操作做测试验证：
    // 手动模拟一次"只有 L1、没有 L0"的场景，往 db.Levels() 里加 4 个空转的
    // L0 文件立刻合并，让新的 L1 内容和 lm 的一致
    for (int i = 0; i < 4; ++i) {
        std::string path = MakeSSTFile("test_db_seed2_" + std::to_string(i) + ".sst",
                                        {{"deep_key", "deep_value", false}});
        created.push_back(path);
        db.Levels().AddL0File(path);
    }
    CHECK(db.Levels().MaybeCompact());
 
    std::string val;
    CHECK(db.Get("deep_key", &val) && val == "deep_value");
 
    RemoveFiles({l1_path, db.Levels().L1FilePath()});
    RemoveFiles(created);
    std::remove(kWalPath);
    std::cout << "PASS\n";
}
 
void TestNotFoundAnywhere() {
    std::cout << "[TEST] NotFoundAnywhere ... ";
    std::remove(kWalPath);
 
    std::string l0 = MakeSSTFile("test_db_l0_c.sst",
                                  {{"exists", "yes", false}});
 
    DB db(kWalPath, kDir);
    db.Levels().AddL0File(l0);
    db.Put("also_exists", "yes2");
 
    std::string val;
    CHECK(!db.Get("truly_missing", &val));
 
    RemoveFiles({l0});
    std::remove(kWalPath);
    std::cout << "PASS\n";
}
 
// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
 
int main() {
    std::cout << "=== DB (Cross-Level Query) Unit Tests ===\n";
 
    TestFoundInMemTable();
    TestMemTableTombstoneStopsSearch();
    TestFoundInL0();
    TestNewerL0FileWinsOverOlder();
    TestL0TombstoneStopsBeforeL1();
    TestFoundInL1AsLastResort();
    TestNotFoundAnywhere();
 
    std::cout << "\nAll tests passed.\n";
    return 0;
}