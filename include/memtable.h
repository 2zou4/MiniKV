#pragma once
 
#include <memory>
#include <string>
 
#include "skiplist.h"
#include "wal.h"
 
namespace minikv {

// ──────────────────────────────────────────────
// MemTable
// ──────────────────────────────────────────────
//
// 职责：封装 SkipList（内存有序存储）+ WAL（崩溃恢复），
// 对外提供统一的 Put / Get / Delete 接口。
//
// 写路径（Put / Delete）：
//   1. 先写 WAL（磁盘），确保即使程序崩溃也能恢复这条操作
//   2. 再写 SkipList（内存），完成真正的数据更新
//   两步顺序不能颠倒——参见 WAL 模块的设计说明。
//
// 读路径（Get）：
//   只读 SkipList，不碰 WAL。WAL 只在崩溃恢复时被读取一次。
//
// 构造时的恢复流程：
//   打开（或创建）WAL 文件 -> Replay 重放所有历史记录 -> 灌入一个新的 SkipList
//   这样 MemTable 一旦构造完成，内存状态就已经是崩溃前的样子。

class MemTable{
    public:
    // wal_path：该 MemTable 对应的 WAL 文件路径
    // flush_threshold：内存占用超过这个字节数，Full() 返回 true，
    //                  提示上层该把这个 MemTable 转成 Immutable 并刷盘了
    explicit MemTable(const std::string& wal_path,
                        size_t flush_threshold=4*1024*1024);
    ~MemTable()=default;

    MemTable(const MemTable&)=delete;
    MemTable& operator=(const MemTable&)=delete;

    // 写入/更新一条 key-value。先写 WAL 再写 SkipList。
    // 返回 false 表示 WAL 写入失败（磁盘问题等），此时不会写入 SkipList，
    // 保证"WAL 和内存不会不一致"。
    bool Put(const std::string& key, const std::string& value);

    // 懒删除一个 key。先写 WAL 的 Delete 记录，再对 SkipList 打删除标记。
    bool Delete(const std::string& key);

    // 查询一个 key，只读 SkipList，不涉及 WAL。
    bool Get(const std::string& key, std::string* value) const;

    // 当前 SkipList 的内存占用是否超过阈值，达到了该刷盘的时机。
    bool Full() const;

    // 当前有效数据条数（不含懒删除的）
    int Size() const;

    // 刷盘之后调用：清空 WAL（这些数据已经落到 SSTable 了，不再需要日志恢复）
    // 具体的"生成 SSTable"逻辑属于后续模块，这里只负责清理 WAL。
    void MarkFlushed();

private:
        std::unique_ptr<SkipList> table_;
        std::unique_ptr<WAL> wal_;
        size_t flush_threshold_;
};
}