#pragma once
#include<string>
#include<vector>

namespace minikv{
// ──────────────────────────────────────────────
// KVEntry：归并过程中通用的一条记录
// ──────────────────────────────────────────────
//
// 和 SSTable 内部的 Entry 概念一致（key/value/是否被删除），
// 这里单独定义一份，让 Compaction 模块不依赖 SSTable 的内部实现细节，
// 只依赖"一组有序的 KVEntry"这个抽象。
struct KVEntry{
    std::string key;
    std::string value;
    bool is_deleted;
};

 
// ──────────────────────────────────────────────
// KWayMerger：多路归并算法
// ──────────────────────────────────────────────
//
// 输入：多组"来源"（sources），每一组内部按 key 升序排好序
//      （对应现实中每个 SSTable 文件内部本身有序）。
//
// sources 的顺序即"新旧顺序"：sources[0] 是最新的文件，
// sources.back() 是最旧的文件。这是调用方的约定——
// 通常按 SSTable 生成时间从新到旧排列传入。
//
// 输出：一份合并后的有序结果，重复 key 只保留最新版本。
//
// drop_tombstones 参数：
//   true  —— 如果某个 key 最新版本是删除标记(is_deleted=true)，
//            直接丢弃，不写入结果（用于合并到最底层时，
//            确认这个 key 不可能再有更旧版本残留，可以彻底清理）
//   false —— tombstone 仍然保留在结果里（用于合并到中间层，
//            上面可能还有更旧的层依赖这条删除标记来确认"已删除"）
class KWayMerger{
    public:
    static std::vector<KVEntry>Merge
        (const std::vector<std::vector<KVEntry>>& sources, bool drop_tombstones);
    };

//新增
class Compaction{
    public:
    static bool CompactFiles(const std::vector<std::string>& input_paths,
                                        const std::string& output_path,
                                        bool drop_tombstones);
};
}



