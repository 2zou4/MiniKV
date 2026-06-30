#include "skiplist.h"
#include <cassert>
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

// ──────────────────────────────────────────────
// 测试用例
// ──────────────────────────────────────────────
void TestBasicInsertAndGet(){
    std::cout<<"[TEST] BasicInsertAndGet ... ";
    SkipList sl;
    std::string val;

    sl.Insert("apple","fruit");
    sl.Insert("banana","yellow");
    sl.Insert("cherry","red");

    CHECK(sl.Get("apple",&val) && val=="fruit");
    CHECK(sl.Get("banana",&val) && val=="yellow");
    CHECK(sl.Get("cherry",&val) && val=="red");
    CHECK(!sl.Get("mango",&val));//不存在
    CHECK(sl.Size()==3);
    std::cout<<"PASS\n";
}
void TestUpdateExistingKey(){
    std::cout<<"[TEST] UpdateExistingKey ... ";
    SkipList sl;
    std::string val;

    sl.Insert("key","v1");
    CHECK(sl.Get("key",&val) && val=="v1");
    CHECK(sl.Size()==1);

    //更新同一个key
    sl.Insert("key","v2");
    CHECK(sl.Get("key",&val) && val=="v2");
    CHECK(sl.Size()==1);//不应增加节点数
    std::cout<<"PASS\n";
}
void TestLazyDelete(){
    std::cout << "[TEST] LazyDelete ... ";
    SkipList sl;
    std::string val;

    sl.Insert("x","123");
    CHECK(sl.Size()==1);

    //懒删除，节点仍然存在，但Get返回false
    CHECK(sl.Delete("x"));
    CHECK(sl.Size()==0);
    CHECK(!sl.Get("x",&val));

    //删除不存在的key应返回false
    CHECK(!sl.Delete("noneexistent"));
    std::cout << "PASS\n";
}
void TestReinsertAfterDelete(){
    std::cout << "[TEST] ReinsertAfterDelete ... ";
    SkipList sl;
    std::string val;

    sl.Insert("ghost","alive");
    sl.Delete("ghost");
    CHECK(!sl.Get("ghost",&val));
    CHECK(sl.Size()==0);

    //重新插入同一个key（复活懒删除节点）
    sl.Insert("ghost","reborn");
    CHECK(sl.Get("ghost",&val) && val =="reborn");
    CHECK(sl.Size()==1);

    std::cout << "PASS\n";
}

void TestOrdering(){
    std::cout << "[TEST] Ordering ... ";
    SkipList sl;

    //乱序插入，跳表内部应维持有序
    sl.Insert("mango","m");
    sl.Insert("apple","a");
    sl.Insert("cherry","c");
    sl.Insert("banana","b");

    //逐个Get验证有序性（通过内部结构间接验证）
    std::string val;
    CHECK(sl.Get("apple",&val) && val=="a");
    CHECK(sl.Get("banana",&val) && val=="b");
    CHECK(sl.Get("cherry",&val) && val=="c");
    CHECK(sl.Get("mango",&val) && val=="m");

    std::cout<<"PASS\n";
}
void TestEmpty(){
    std::cout << "[TEST] Empty ... ";
    SkipList sl;
    std::string val;

    CHECK(sl.Empty());
    CHECK(sl.Size()==0);
    CHECK(!sl.Get("any",&val));
    CHECK(!sl.Delete("any"));

    std::cout << "PASS\n";
}
void TestMemoryUsage(){
    std::cout << "[TEST] MemoryUsage ... ";
    SkipList sl;

    CHECK(sl.ApproximateMemoryUsage()==0);
    sl.Insert("key1","value1");
    CHECK(sl.ApproximateMemoryUsage()>0);

    size_t usage_before=sl.ApproximateMemoryUsage();
    sl.Insert("key2","value2");
    CHECK(sl.ApproximateMemoryUsage()>usage_before);

    std::cout << "PASS\n";
}
void TestLargeInsert(){
    std::cout << "[TEST] LargeInsert (1000 keys) ... ";
    SkipList sl;
    std::string val;

    //插入1000个key，验证查找正确
    for(int i=0;i<1000;i++)
    {
        sl.Insert(std::to_string(i),"v"+std::to_string(i));
    }
    CHECK(sl.Size()==1000);

    for(int i=0;i<1000;++i)
    {
        CHECK(sl.Get(std::to_string(i),&val));
        CHECK(val=="v"+std::to_string(i));
    }
    CHECK(!sl.Get("9999",&val));

    std::cout << "PASS\n";
}

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────

int main(){
    std::cout << "=== SkipList Unit Tests ===\n";

    TestBasicInsertAndGet();
    TestUpdateExistingKey();
    TestLazyDelete();
    TestReinsertAfterDelete();
    TestOrdering();
    TestEmpty();
    TestMemoryUsage();
    TestLargeInsert();

    std::cout << "\nAll tests passed.\n";
    return 0;
}