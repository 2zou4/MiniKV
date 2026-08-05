#include "sstable.h"
 
#include <cstdio>
#include <iostream>
 
#include "skiplist.h"
 
using namespace minikv;
 
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " #cond \
                      << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; \
            std::exit(1); \
        } \
    } while (0)
 
static const char* kTestFile = "test_sstable.sst";
 
static void RemoveTestFile() {
    std::remove(kTestFile);
}
 
// ──────────────────────────────────────────────
// 测试用例
// ──────────────────────────────────────────────

void TestBasicBuildAndGet(){
    std::cout << "[TEST] BasicBuildAndGet ... ";
    RemoveTestFile();

    SkipList sl;
    sl.Insert("apple", "fruit");
    sl.Insert("banana", "yellow");
    sl.Insert("cherry", "red");

    CHECK(SSTable::BuildFromSkipList(sl,kTestFile));

    SSTable sst(kTestFile);
    std::string val;

    CHECK(sst.Get("apple", &val) && val=="fruit");
    CHECK(sst.Get("banana", &val) && val=="yellow");
    CHECK(sst.Get("cherry", &val) && val=="red");
    CHECK(!sst.Get("mango", &val));

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestTombstonePreserved(){
    std::cout << "[TEST] TombstonePreserved ... ";
    RemoveTestFile();

    SkipList sl;
    sl.Insert("gone","old_value");
    sl.Delete("gone");
    sl.Insert("still_here","value");

    CHECK(SSTable::BuildFromSkipList(sl,kTestFile));

    SSTable sst(kTestFile);
    std::string val;

    // tombstone 应该让 Get 返回 false（视为不存在），而不是意外恢复旧值
    CHECK(!sst.Get("gone", &val));
    CHECK(sst.Get("still_here", &val) && val=="value");

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestMultipleBlocks(){
    std::cout << "[TEST] MultipleBlocks (跨越多个Data Block) ... ";
    RemoveTestFile();

    SkipList sl;
    // 插入的数量超过 kBlockRecordCount（16），确保数据会跨越多个 Data Block，
    // 验证 Index Block 的多条索引项和二分查找逻辑是正确的
    for(int i=0;i<100;++i){
        char buf[16];
        snprintf(buf,sizeof(buf),"key%03d",i);// 保证字符串比较顺序=数字顺序
        sl.Insert(buf,"value"+std::to_string(i));
    }

    CHECK(SSTable::BuildFromSkipList(sl,kTestFile));

    SSTable sst(kTestFile);
    std::string val;

    for(int i=0;i<100;++i){
        char buf[16];
        snprintf(buf,sizeof(buf),"key%03d",i);
        CHECK(sst.Get(buf, &val));
        CHECK(val=="value"+std::to_string(i));
    }

    CHECK(!sst.Get("key999",&val));// 不存在
    CHECK(!sst.Get("aaa",&val));// 比所有 key 都小

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestEmptySkipList(){
    std::cout << "[TEST] EmptySkipList ... ";
    RemoveTestFile();

    SkipList sl;
    CHECK(SSTable::BuildFromSkipList(sl,kTestFile));

    SSTable sst(kTestFile);
    std::string val;
    CHECK(!sst.Get("anything",&val));

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestBloomFilterSkipsNonexistentKey(){
    std::cout << "[TEST] BloomFilterSkipsNonexistentKey ... ";
    RemoveTestFile();

    SkipList sl;
    for(int i=0;i<50;++i){
        sl.Insert("resl"+std::to_string(i),"value");
    }
    CHECK(SSTable::BuildFromSkipList(sl,kTestFile));

    SSTable sst(kTestFile);
    std::string val;

    // 查询一批肯定不存在的 key，验证不会崩溃、且大多数能被正确判定为不存在
    int found=0;
    for(int i=0;i<50;++i){
        if(sst.Get("fake"+std::to_string(i),&val)){
            ++found;// 理论上应为 0，允许极少数 Bloom Filter 误判
        }
    }
    CHECK(found==0);

    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestOpenNonexistentFile(){
    std::cout << "[TEST] OpenNonexistentFile ... ";

    SSTable sst("this_file_does_not_exist.sst");
    std::string val;
    CHECK(!sst.Get("anything",&val));// 不应该崩溃，优雅返回 false

    std::cout << "PASS\n";
}

//新增 用于测试ForEach BuildFromEntries
void TestForEachMatchesContent() {
    std::cout << "[TEST] ForEachMatchesContent ... ";
    RemoveTestFile();
    SkipList sl;
    sl.Insert("a", "1");
    sl.Insert("b", "2");
    sl.Delete("b");          // b 被删除，变成 tombstone
    sl.Insert("c", "3");
    CHECK(SSTable::BuildFromSkipList(sl, kTestFile));

    SSTable sst(kTestFile);
    std::vector<std::string> keys;
    std::vector<bool> deleted;
    bool ok = sst.ForEach([&](const std::string& k, const std::string&, bool del) {
        keys.push_back(k);
        deleted.push_back(del);
    });
    CHECK(ok);
    CHECK(keys.size() == 3);              // a, b, c 都在，包括被删除的 b
    CHECK(keys[0] == "a" && !deleted[0]);
    CHECK(keys[1] == "b" && deleted[1]);  // b 是 tombstone，deleted 标记应为 true
    CHECK(keys[2] == "c" && !deleted[2]);
    RemoveTestFile();
    std::cout << "PASS\n";
}

void TestBuildFromEntriesDirectly() {
    std::cout << "[TEST] BuildFromEntriesDirectly ... ";
    RemoveTestFile();
    std::vector<SSTable::Entry> entries = {
        {"x", "10", false},
        {"y", "20", false},
        {"z", "", true},        // 直接构造一条 tombstone，不经过 SkipList::Delete
    };
    CHECK(SSTable::BuildFromEntries(entries, kTestFile));

    SSTable sst(kTestFile);
    std::string val;
    CHECK(sst.Get("x", &val) && val == "10");
    CHECK(sst.Get("y", &val) && val == "20");
    CHECK(!sst.Get("z", &val));  // tombstone，Get 应该返回 false
    RemoveTestFile();
    std::cout << "PASS\n";
}

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────

int main(){
    std::cout << "=== SSTable Unit Tests ===\n";
 
    TestBasicBuildAndGet();
    
    TestForEachMatchesContent();        // ← 新增
    TestBuildFromEntriesDirectly();     // ← 新增

    TestTombstonePreserved();
    TestMultipleBlocks();
    TestEmptySkipList();
    TestBloomFilterSkipsNonexistentKey();
    TestOpenNonexistentFile();
 
    std::cout << "\nAll tests passed.\n";
    return 0;
}