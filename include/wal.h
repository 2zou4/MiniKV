#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <string>

namespace minikv
{
// ──────────────────────────────────────────────
// WAL 记录格式
// ──────────────────────────────────────────────
//
// 每条记录在磁盘上的二进制布局：
//
//   ┌─────────┬──────────┬────────────┬─────────┬───────────┐
//   │ type(1B)│key_len(4B)│value_len(4B)│ key(变长) │ value(变长)│
//   └─────────┴──────────┴────────────┴─────────┴───────────┘
//
// type: 0 = Put, 1 = Delete
// key_len / value_len：小端序 uint32_t，记录 key/value 各自的字节数
// Delete 记录没有 value，value_len 写 0，不占用任何字节
//
// 为什么不用文本格式（比如一行一条 "PUT key value"）？
//   二进制定长头部（1+4+4=9字节）可以快速跳过整条记录做校验/恢复，
//   而且不用担心 key/value 里包含换行符、空格等文本格式的转义问题。
    enum class RecordType: uint8_t{
        kPut=0,
        kDelete=1,
    };

    // ──────────────────────────────────────────────
// WAL 类
// ──────────────────────────────────────────────
//
// 职责单一：只负责"追加写日志"和"重放日志"，
// 不关心 MemTable 内部结构，靠回调函数解耦。
//
// 使用方式：
//   写入时：
//     WAL wal("data.wal");
//     wal.AppendPut("key", "value");
//     wal.AppendDelete("key");
//
//   重放时（程序重启后）：
//     WAL wal("data.wal");
//     wal.Replay([&](RecordType type, const std::string& key,
//                     const std::string& value) {
//         if (type == RecordType::kPut) memtable.Insert(key, value);
//         else memtable.Delete(key);
//     });
    class WAL{
    public:
        explicit WAL(const std::string& filename);
        ~WAL();

        WAL(const WAL&)=delete;
        WAL& operator=(const WAL&)=delete;
        // 追加一条 Put 记录，写入后立即 flush 到操作系统缓冲区
        // 返回 false 表示写入失败（磁盘满、权限问题等）
        bool AppendPut(const std::string& key,const std::string& value);

        // 追加一条 Delete 记录（value 部分不写）
        bool AppendDelete(const std::string& key);

        // 重放日志：从文件开头读到结尾，对每条合法记录调用 callback
        // 用于程序重启后恢复 MemTable 状态
        //
        // callback 签名：void(RecordType type, const string& key, const string& value)
        // Delete 记录的 value 恒为空字符串
        //
        // 若遇到文件末尾数据不完整（比如上次崩溃时正好写到一半），
        // 该条不完整记录会被丢弃，之前的记录仍然有效——
        // 这也是为什么每条写入后要立即 flush：最大限度减少这种半条记录的窗口。
        using ReplayCallback=
        std::function<void(RecordType, const std::string&, const std::string&)>;
        bool Replay(const ReplayCallback& callback);

        // 清空日志文件内容（MemTable 刷盘成 SSTable 后，对应的 WAL 就不再需要了）
        bool Clear();

    private:
        std::string filename_;
        std::fstream file_;

        // 内部写入一条记录的公共逻辑，Put/Delete 都调用它
        bool WriteRecord(RecordType type, const std::string& key, const std::string& value);


    };
} // namespace minikv
