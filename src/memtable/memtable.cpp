#include "memtable.h"

namespace minikv{
// ──────────────────────────────────────────────
// 构造函数：崩溃恢复的入口
// ──────────────────────────────────────────────
//
// 步骤：
//   1. 创建一个空的 SkipList
//   2. 打开（或创建）WAL 文件
//   3. 调用 Replay，把 WAL 里所有历史记录重新灌入 SkipList
//
// 如果是第一次启动（WAL 文件不存在或是空的），Replay 不会触发任何回调，
// SkipList 保持空白，效果等同于全新创建一个 MemTable。
//
// 如果是崩溃后重启，WAL 里有历史记录，Replay 会把它们一条条重放，
// 重放完成后 SkipList 的状态就和崩溃前一致。

MemTable:: MemTable(const std::string& wal_path, size_t flush_threshold)
    : table_(std::make_unique<SkipList>()),
    wal_(std::make_unique<WAL>(wal_path)),
    flush_threshold_(flush_threshold){

        wal_->Replay([this](RecordType type, const std::string& key, const std::string& value){
            if(type==RecordType::kPut)
            {
                table_->Insert(key,value);
            }
            else
            {
                table_->Delete(key);
            }
        });
    }
// ──────────────────────────────────────────────
// Put
// ──────────────────────────────────────────────
//
// 先写 WAL，成功后再写 SkipList。
// 如果 WAL 写入失败，直接返回 false，不动 SkipList——
// 这样保证 WAL 和内存要么都更新，要么都不更新，不会出现
// "内存里有这条数据，但 WAL 里没记录"的不一致状态。
    bool MemTable::Put(const std::string& key, const std::string& value){
        if(!wal_->AppendPut(key,value))
        {
            return false;
        }
        table_->Insert(key,value);
        return true;
    }

// ──────────────────────────────────────────────
// Delete
// ──────────────────────────────────────────────
//
// 同样的顺序：先写 WAL 的 Delete 记录，再对 SkipList 打懒删除标记。
    bool MemTable::Delete(const std::string& key){
        if(!wal_->AppendDelete(key))
        {
            return false;
        }
        table_-> Delete(key);
        return true;
    }

// ──────────────────────────────────────────────
// Get
// ──────────────────────────────────────────────
//
// 只读 SkipList。运行时的数据权威来源是内存，
// WAL 只在崩溃恢复（构造函数里）被读取一次。
    bool MemTable::Get(const std::string& key, std::string* value) const{
        return table_->Get(key,value);
    }

// ──────────────────────────────────────────────
// Full / Size / MarkFlushed
// ──────────────────────────────────────────────
    bool MemTable::Full() const{
        return table_->ApproximateMemoryUsage()>=flush_threshold_;
    }

    int MemTable::Size() const{
        return table_->Size();
    }

    void MemTable::MarkFlushed(){
        // 数据已经落到 SSTable，WAL 不再需要用于恢复，清空腾出磁盘空间。
        wal_->Clear();
    }

}