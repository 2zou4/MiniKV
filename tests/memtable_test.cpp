#include "memtable.h"
 
#include <cstdio>
#include <iostream>
#include <string>
 
using namespace minikv;
 
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " #cond \
                      << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; \
            std::exit(1); \
        } \
    } while (0)
 
static const char* kTestFile = "test_memtable.wal";
 
static void RemoveTestFile() {
    std::remove(kTestFile);
}
 
// ──────────────────────────────────────────────
// 测试用例
// ──────────────────────────────────────────────
void TestBasicPutGet(){
    std::cout << "[TEST] BasicPutGet ... ";
    RemoveTestFile();

    MemTable mt(kTestFile);
    std::string val;

    CHECK(mt.Put("apple","fruit"));
    CHECK(mt.Get("apple",&val) && val=="fruit");
    CHECK(!mt.Get("missing", &val));
    CHECK(mt.Size()==1);

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestPutUpdate(){
    std::cout << "[TEST] PutUpdate ... ";
    RemoveTestFile();
 
    MemTable mt(kTestFile);
    std::string val;

    mt.Put("key","v1");
    CHECK(mt.Get("key",&val) && val=="v1");

    mt.Put("key","v2");
    CHECK(mt.Get("key", &val) && val=="v2");
    CHECK(mt.Size()==1);// 更新不增加条数
 
    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestDelete(){
    std::cout << "[TEST] Delete ... ";
    RemoveTestFile();
 
    MemTable mt(kTestFile);
    std::string val;

    mt.Put("x","123");
    CHECK(mt.Get("x",&val));

    mt.Delete("x");
    CHECK(!mt.Get("x",&val));
    CHECK(mt.Size()==0);

    RemoveTestFile();
    std::cout << "PASS\n";
}
// ──────────────────────────────────────────────
// 核心测试：崩溃恢复
// ──────────────────────────────────────────────
//
// 用两个独立作用域模拟"程序运行 -> 崩溃/退出 -> 重启"：
// 第一个 MemTable 对象析构后，第二个对象用同一个 WAL 文件构造，
// 应该能自动恢复第一阶段写入的所有数据。
void TestCrashRecovery(){
    std::cout << "[TEST] CrashRecovery ... ";
    RemoveTestFile();

    {
        MemTable mt(kTestFile);
        mt.Put("apple","fruit");
        mt.Put("banana","yellow");
        mt.Delete("apple");
        mt.Put("cherry","red");
        // 作用域结束，mt 析构，模拟进程退出/崩溃
    }

    // "重启"：用同一个 WAL 文件重新构造 MemTable
    MemTable mt2(kTestFile);
    std::string val;

    CHECK(!mt2.Get("apple",&val));
    CHECK(mt2.Get("banana",&val) && val=="yellow");
    CHECK(mt2.Get("cherry",&val) && val=="red");
    CHECK(mt2.Size()==2);// banana, cherry

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestFreshStartEmptyWAL(){
    std::cout << "[TEST] FreshStartEmptyWAL ... ";
    RemoveTestFile();
 
    // WAL 文件不存在，第一次启动
    MemTable mt(kTestFile);
    CHECK(mt.Size()==0);

    std::string val;
    CHECK(!mt.Get("anything",&val));

    RemoveTestFile();
    std::cout << "PASS\n";
}

// ──────────────────────────────────────────────
// Full() 阈值判断
// ──────────────────────────────────────────────
void TestFullThreshold(){
    std::cout << "[TEST] FullThreshold ... ";
    RemoveTestFile();
 
    // 设置一个很小的阈值，方便触发
    MemTable mt(kTestFile,50);

    CHECK(!mt.Full());// 空的时候肯定没满
 
    // 持续写入，直到超过阈值
    for(int i=0;i<20 && !mt.Full();++i)
    {
        mt.Put("key"+std::to_string(i),"value"+std::to_string(i));
    }

    CHECK(mt.Full());// 阈值很小，写几条就应该超过了
 
    RemoveTestFile();
    std::cout << "PASS\n";
}

// ──────────────────────────────────────────────
// MarkFlushed：清空 WAL 后不应再恢复出旧数据
// ──────────────────────────────────────────────
void TestMarkFlushedClearsWAL(){
    std::cout << "[TEST] MarkFlushedClearsWAL ... ";
    RemoveTestFile();

    {
        MemTable mt(kTestFile);
        mt.Put("old_data","should_be_gone");
        mt.MarkFlushed();// 模拟已经刷盘成 SSTable，WAL 被清空
    }

    // 模拟已经刷盘成 SSTable，WAL 被清空
    MemTable mt2(kTestFile);
    std::string val;
    CHECK(!mt2.Get("old_data",&val));
    CHECK(mt2.Size()==0);

    RemoveTestFile();
    std::cout << "PASS\n";
}

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
int main(){
    std::cout << "=== MemTable Unit Tests ===\n";

    TestBasicPutGet();
    TestPutUpdate();
    TestDelete();
    TestCrashRecovery();
    TestFreshStartEmptyWAL();
    TestFullThreshold();
    TestMarkFlushedClearsWAL();

    std::cout << "\nAll tests passed.\n";
    return 0;
}