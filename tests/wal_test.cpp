#include "wal.h"
 
#include <cstdio>
#include <iostream>
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

static const char* kTestFile="tes_wal.log";

// 每次测试用独立文件名，避免互相干扰；测试结束删除
static void RemoveTestFile()
{
    std::remove(kTestFile);
}

// ──────────────────────────────────────────────
// 测试用例
// ──────────────────────────────────────────────
void TestAppendAndReplay()
{
    std::cout << "[TEST] AppendAndReplay ... ";
    RemoveTestFile();
    {
        WAL wal(kTestFile);
        wal.AppendPut("apple","fruit");
        wal.AppendPut("banana","yellow");
        wal.AppendDelete("apple");
    }// WAL 析构，文件关闭，模拟"正常关闭程序"

    // 重新打开同一个文件，模拟"重启程序后恢复"
    WAL wal2(kTestFile);
    std::vector<std::pair<RecordType, std::string>> replayed;

    bool ok=wal2.Replay([&](RecordType type, const std::string& key, const std::string& value){
        replayed.emplace_back(type,key);
        if(type==RecordType::kPut){
            CHECK(!value.empty()||key=="");// Put 应该带 value
        }
    });

    CHECK(ok);
    CHECK(replayed.size()==3);
    CHECK(replayed[0].first==RecordType::kPut && replayed[0].second=="apple");
    CHECK(replayed[1].first==RecordType::kPut && replayed[1].second=="banana");
    CHECK(replayed[2].first==RecordType::kDelete && replayed[2].second=="apple");

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestReplayValueCorrect()
{
    std::cout << "[TEST] ReplayValuesCorrect ... ";
    RemoveTestFile();

    {
        WAL wal(kTestFile);
        wal.AppendPut("key1","value1");
        wal.AppendPut("key2","value2");
    }

    WAL wal2(kTestFile);
    std::vector<std::string> keys, values;

    wal2.Replay([&](RecordType type,const std::string& key, const std::string& value){
        CHECK(type==RecordType::kPut);
        keys.push_back(key);
        values.push_back(value);
    });

    CHECK(keys.size()==2);
    CHECK(keys[0]=="key1" && values[0]=="value1");
    CHECK(keys[1]=="key2" && values[1]=="value2");

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestEmptyFileReplay()
{
    std::cout << "[TEST] EmptyFileReplay ... ";
    RemoveTestFile();

    WAL wal(kTestFile);
    int count=0;
    bool ok=wal.Replay([&](RecordType, const std::string&, const std::string&){
        ++count;
    });

    CHECK(ok);
    CHECK(count==0);// 空文件重放，不应触发任何回调

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestDeleteRecordHashEmptyValue()
{
    std::cout << "[TEST] DeleteRecordHasEmptyValue ... ";
    RemoveTestFile();

    {
        WAL wal(kTestFile);
        wal.AppendDelete("gone");
    }

    WAL wal2(kTestFile);
    bool called=false;
    wal2.Replay([&](RecordType type, const std::string& key, const std::string& value){
        called=true;
        CHECK(type==RecordType::kDelete);
        CHECK(key=="gone");
        CHECK(value.empty());// Delete 记录的 value 恒为空
    });

    CHECK(called);
    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestClearWipesFile()
{
    std::cout << "[TEST] ClearWipesFile ... ";
    RemoveTestFile();

    WAL wal(kTestFile);
    wal.AppendPut("a","1");
    wal.AppendPut("b","2");

    bool cleared=wal.Clear();
    CHECK(cleared);

    int count=0;
    wal.Replay([&](RecordType, const std::string&, const std::string&){
        ++count;
    });
    CHECK(count==0);// 清空后重放应该什么都没有

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestAppendAfterClear()
{
    std::cout << "[TEST] AppendAfterClear ... ";
    RemoveTestFile();

    WAL wal(kTestFile);
    wal.AppendPut("old","data");
    wal.Clear();
    wal.AppendPut("new","data2");

    int count=0;
    wal.Replay([&](RecordType, const std::string& key, const std::string&){
        ++count;
        CHECK(key=="new");// 清空前的记录不应该还在
    });
    CHECK(count==1);

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestSimulatedCrashRecovery()
{
    std::cout << "[TEST] SimulatedCrashRecovery ... ";
    RemoveTestFile();
 
    // 模拟场景：程序运行时写入若干条记录，
    // 中途"崩溃"（这里用直接析构模拟，不做优雅关闭），
    // 重启后应该能恢复所有已经 flush 过的记录。
    {
        WAL wal(kTestFile);
        for(int i=0;i<100;++i)
        {
            wal.AppendPut("key"+std::to_string(i),"value"+std::to_string(i));
        }// 不做任何特殊清理，直接让 wal 在作用域结束时析构
        // 模拟"进程结束/崩溃后重启"
    }

    WAL wal2(kTestFile);
    int count=0;
    wal2.Replay([&](RecordType type, const std::string& key, const std::string& value){
        CHECK(type==RecordType::kPut);
        CHECK(key=="key"+std::to_string(count));
        CHECK(value=="value"+std::to_string(count));
        ++count;
    });

    CHECK(count==100);// 100 条记录全部恢复

    RemoveTestFile();
    std::cout << "PASS\n";
}

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
int main()
{
    std::cout << "=== WAL Unit Tests ===\n";
    
    TestAppendAndReplay();
    TestReplayValueCorrect();
    TestEmptyFileReplay();
    TestDeleteRecordHashEmptyValue();
    TestClearWipesFile();
    TestAppendAfterClear();
    TestSimulatedCrashRecovery();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
