#include "wal.h"

#include <cstring>

namespace minikv{
// ──────────────────────────────────────────────
// 构造 / 析构
// ──────────────────────────────────────────────
//
// 打开模式说明：
//   std::ios::in | std::ios::out | std::ios::binary
//     —— 可读可写，二进制模式（避免 Windows 下 \n 被转换成 \r\n）
//   std::ios::app 不能和 std::ios::in 一起用于随机读写场景，
//   所以这里手动 seek 到文件末尾来实现"追加"效果，
//   同时保留读文件开头做 Replay 的能力。
//
// 如果文件不存在，先用 out 模式创建一次，再重新以读写模式打开。
    WAL::WAL(const std::string& filename) : filename_(filename){
        // 确保文件存在：不存在则创建一个空文件
        std::fstream create(filename_,std::ios::out | std::ios::app);
        create.close();

        file_.open(filename_,std::ios::in | std::ios::out | std::ios::binary);
    }

    WAL::~WAL(){
        if(file_.is_open()){
            file_.close();
        }
    }

// ──────────────────────────────────────────────
// WriteRecord：Put 和 Delete 共用的底层写入逻辑
// ──────────────────────────────────────────────
//
// 按照头文件里定义的二进制格式，依次写入：
//   type(1B) -> key_len(4B) -> value_len(4B) -> key -> value
//
// 每次写完立即 flush()，把数据从 C++ 流缓冲区推给操作系统。
// 这一步很关键：如果不 flush，数据可能还停留在用户态缓冲区里，
// 程序崩溃时和内存数据一起丢失，WAL 就失去了意义。
//
// 注意：flush() 只保证数据到了操作系统页缓存，不保证落盘到物理磁盘
// （更彻底的保证需要 fsync，会显著拖慢写入速度，леveldb 默认也不每次 fsync）。
// 这里选择 flush 作为工程上的折中
    bool WAL::WriteRecord(RecordType type, const std::string& key, const std::string& value)
    {
        if(!file_.is_open())
        {
            return false;
        }
        // 写之前先把读写指针移到文件末尾，保证是追加而不是覆盖
        file_.seekp(0,std::ios::end);

        uint8_t type_byte=static_cast<uint8_t>(type);
        uint32_t key_len=static_cast<uint32_t>(key.size());
        uint32_t value_len=static_cast<uint32_t>(value.size());

        file_.write(reinterpret_cast<const char*>(&type_byte),sizeof(type_byte));
        file_.write(reinterpret_cast<const char*>(&key_len),sizeof(key_len));
        file_.write(reinterpret_cast<const char*>(&value_len),sizeof(value_len));
        file_.write(key.data(),key.size());
        file_.write(value.data(),value.size());

        file_.flush();

        return file_.good();
    }

    bool WAL::AppendPut(const std::string& key,const std::string& value)
    {
        return WriteRecord(RecordType::kPut,key,value);
    }

    bool WAL::AppendDelete(const std::string& key)
    {
        // Delete 记录不需要 value，传空字符串，value_len 会被记录为 0
        return WriteRecord(RecordType::kDelete,key,"");
    }

// ──────────────────────────────────────────────
// Replay：从头读取日志文件，重放所有记录
// ──────────────────────────────────────────────
//
// 核心逻辑：循环读取「固定头部 9 字节 -> 变长 key -> 变长 value」，
// 每读完一条完整记录就调用一次 callback。
//
// 关键的健壮性处理：如果读到文件末尾时数据不完整
// （比如只读到了头部，或者 key 都没读全），
// 说明这是上次崩溃时写了一半的记录，直接丢弃，不算错误，
// 循环正常结束，之前已经重放的记录不受影响。
    bool WAL::Replay(const ReplayCallback& callback)
    {
        if(!file_.is_open())
        {
            return false;
        }

        // 从文件开头开始读
        file_.clear();// 清除之前操作可能设置的 eof/fail 标志
        file_.seekg(0,std::ios::beg);

        while(true)
        {
            uint8_t type_byte;
            uint32_t key_len;
            uint32_t value_len;

            // 读固定头部：9 字节。读不满说明到文件尾或记录损坏，停止重放。
            file_.read(reinterpret_cast<char*>(&type_byte),sizeof(type_byte));
            if(file_.gcount()!=sizeof(type_byte)) break;

            file_.read(reinterpret_cast<char*>(&key_len),sizeof(key_len));
            if(file_.gcount()!=sizeof(key_len)) break;

            file_.read(reinterpret_cast<char*>(&value_len),sizeof(value_len));
            if(file_.gcount()!=sizeof(value_len)) break;

            // 读变长 key
            std::string key(key_len,'\0');
            file_.read(&key[0],key_len);
            if(static_cast<uint32_t>(file_.gcount())!=key_len) break;

            // 读变长 value（Delete 记录 value_len 为 0，这里读 0 字节直接跳过）
            std::string value(value_len,'\0');
            if(value_len>0)
            {
                file_.read(&value[0],value_len);
                if(static_cast<uint32_t>(file_.gcount())!=value_len) break;
            }
            // 记录完整读出，调用回调交给 MemTable 处理
            RecordType type=static_cast<RecordType>(type_byte);
            callback(type,key,value);
        }

        // 清除文件流的 eof 标志，让 file_ 之后还能继续用于 Append
        file_.clear();
        return true;
    }
// ──────────────────────────────────────────────
// Clear：清空日志文件
// ──────────────────────────────────────────────
//
// 用途：MemTable 刷盘成 SSTable 之后，这些数据已经持久化到 SSTable 了，
// 对应的 WAL 记录就不再需要用于恢复，可以清空腾出磁盘空间，
// 避免 WAL 文件无限增长。
     bool WAL::Clear()
     {
        if(file_.is_open())
        {
            file_.close();
        }

        // 以 out|trunc 模式重新打开，trunc 会清空文件内容
        std::fstream truncate(filename_,std::ios::out | std::ios::trunc);
        truncate.close();

        file_.open(filename_,std::ios::in | std::ios::out | std::ios::binary);
        return file_.is_open();
     }

}