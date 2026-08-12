#include "db.h"
 
#include <atomic>
#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>
 
#include "sstable.h"
 
using namespace minikv;
 
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " #cond << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; \
            std::exit(1); \
        } \
    } while (0)
 
static const char* kWalPath = "test_db_concurrency.wal";
static const char* kDir     = ".";
 
static std::string MakeSSTFile(const std::string& name,
                                const std::vector<SSTable::Entry>& entries) {
    std::string path = std::string(kDir) + "/" + name;
    SSTable::BuildFromEntries(entries, path);
    return path;
}

void TestConcurrentPutsAllVisible() {
    std::cout << "[TEST] ConcurrentPutsAllVisible ... ";
    std::remove(kWalPath);
 
    DB db(kWalPath, kDir);
 
    constexpr int kThreads = 8;
    constexpr int kKeysPerThread = 200;
 
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&db, t]() {
            for (int i = 0; i < kKeysPerThread; ++i) {
                std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
                std::string val = "v" + std::to_string(t) + "_" + std::to_string(i);
                db.Put(key, val);
            }
        });
    }
    for (auto& w : workers) w.join();
 
    // 逐一验证：8 个线程 x 200 个 key，一条都不能少、不能错
    std::string val;
    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kKeysPerThread; ++i) {
            std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
            std::string expected = "v" + std::to_string(t) + "_" + std::to_string(i);
            CHECK(db.Get(key, &val));
            CHECK(val == expected);
        }
    }
 
    std::remove(kWalPath);
    std::cout << "PASS\n";
}

// ──────────────────────────────────────────────
// 测试二：并发读取 + 并发 Compaction，不崩溃、不读到损坏数据
// ──────────────────────────────────────────────
//
// 这正是我们在原理阶段讨论过的经典竞态：Compaction 线程正在删除
// 旧的 L0/L1 物理文件、更新文件列表，读线程同一时刻在遍历文件列表、
// 打开文件读取。如果没有锁保护，读线程可能在文件被删除的瞬间
// 尝试打开它，导致读取失败或行为未定义。
//
// 验证方式：跑一段时间的高强度并发读 + 并发 Compaction，
// 主线程全程不应该崩溃、不应该抛异常，最终数据仍然可查且正确。
 
void TestConcurrentGetDuringCompaction() {
    std::cout << "[TEST] ConcurrentGetDuringCompaction ... ";
    std::remove(kWalPath);
 
    DB db(kWalPath, kDir);
    std::vector<std::string> created_files;
 
    // 预先准备远超过阈值的 L0 文件，确保 Compaction 有得忙
    for (int i = 0; i < 20; ++i) {
        std::string path = MakeSSTFile(
            "test_concurrency_l0_" + std::to_string(i) + ".sst",
            {{"stable_key", "stable_value", false},
             {"k" + std::to_string(i), "v" + std::to_string(i), false}});
        created_files.push_back(path);
        db.Levels().AddL0File(path);  // 初始状态搭建，单线程阶段，无需加锁
    }
 
    std::atomic<bool> stop{false};
    std::atomic<int>  compaction_rounds{0};
 
    // Compaction 线程：只要 L0 还有文件就不断尝试触发合并
    std::thread compactor([&]() {
        while (!stop.load()) {
            if (db.MaybeCompact()) {
                ++compaction_rounds;
            }
            std::this_thread::yield();
        }
    });
 
    // 多个读线程：疯狂查询，只关心"不崩溃、结果自洽"
    constexpr int kReaderThreads = 6;
    std::vector<std::thread> readers;
    for (int r = 0; r < kReaderThreads; ++r) {
        readers.emplace_back([&]() {
            std::string val;
            for (int i = 0; i < 500; ++i) {
                // stable_key 应该自始至终都能查到（它在每个文件里都有）
                bool found = db.Get("stable_key", &val);
                if (found) {
                    CHECK(val == "stable_value");
                }
                // 查一个肯定不存在的 key，只验证不崩溃
                db.Get("definitely_not_here", &val);
            }
        });
    }
 
    for (auto& r : readers) r.join();
    stop.store(true);
    compactor.join();
 
    // 收尾：确认最终状态自洽——stable_key 依然能查到
    std::string val;
    CHECK(db.Get("stable_key", &val) && val == "stable_value");
 
    // 清理：这次 Compaction 期间生成的所有 L1 文件、以及可能还没被
    // 完全吸收的 L0 残留文件
    for (const auto& path : created_files) std::remove(path.c_str());
    std::string l1_path = db.Levels().L1FilePath();
    if (!l1_path.empty()) std::remove(l1_path.c_str());
    std::remove(kWalPath);
 
    std::cout << "PASS (compaction_rounds=" << compaction_rounds.load() << ")\n";
}

// ──────────────────────────────────────────────
// 测试三：并发混合读写（Put / Delete / Get 交错），不崩溃
// ──────────────────────────────────────────────
 
void TestConcurrentMixedWorkload() {
    std::cout << "[TEST] ConcurrentMixedWorkload ... ";
    std::remove(kWalPath);
 
    DB db(kWalPath, kDir);
 
    constexpr int kThreads = 6;
    constexpr int kOpsPerThread = 300;
    constexpr int kKeySpace = 20;  // 故意让多个线程操作同一批 key，制造真实竞争
 
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&db, t]() {
            std::string val;
            for (int i = 0; i < kOpsPerThread; ++i) {
                std::string key = "shared_key_" + std::to_string(i % kKeySpace);
                if (i % 3 == 0) {
                    db.Delete(key);
                } else {
                    db.Put(key, "val_from_t" + std::to_string(t) + "_" + std::to_string(i));
                }
                db.Get(key, &val);  // 只验证不崩溃，不校验具体内容（结果本身就是竞争的）
            }
        });
    }
    for (auto& w : workers) w.join();
 
    // 收尾：主线程做一次确定性的写入 + 校验，确认加锁后的 DB 依然功能正常
    for (int i = 0; i < kKeySpace; ++i) {
        db.Put("final_key_" + std::to_string(i), "final_value_" + std::to_string(i));
    }
    std::string val;
    for (int i = 0; i < kKeySpace; ++i) {
        CHECK(db.Get("final_key_" + std::to_string(i), &val));
        CHECK(val == "final_value_" + std::to_string(i));
    }
 
    std::remove(kWalPath);
    std::cout << "PASS\n";
}
 
// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
 
int main() {
    std::cout << "=== DB Concurrency Unit Tests ===\n";
 
    TestConcurrentPutsAllVisible();
    TestConcurrentGetDuringCompaction();
    TestConcurrentMixedWorkload();
 
    std::cout << "\nAll tests passed.\n";
    return 0;
}