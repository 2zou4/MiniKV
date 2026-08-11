#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace minikv {

// ──────────────────────────────────────────────
// LevelManager
// ──────────────────────────────────────────────
//
// 职责：记录哪些 SSTable 文件属于 L0、哪些属于 L1，
// 判断什么时候该触发 Compaction，并执行"把 L0 的一批文件
// 和 L1 的旧文件合并成一个新的 L1 文件"这个完整流程。
//
// 范围说明（重要，避免过度设计）：
//   这个类只负责"层级管理 + 触发 + 执行合并 + 清理旧文件"，
//   不负责跨层级的统一查询（MemTable + L0 + L1 一起 Get）——
//   那是一个独立的正确性问题（需要正确处理"某一层的 tombstone
//   应该让查询停止，而不是继续往更旧的层找"），留给下一个模块。
//
// 简化说明：
//   真实的 LSM-Tree（如 leveldb）通常有 L0～L6 共 7 层，
//   每一层触发合并的条件、文件数量都不同，逻辑复杂。
//   这里为了教学清晰，简化成两层：
//     L0：MemTable 刷盘直接产生的文件，允许多个、key 范围可能重叠
//     L1：最底层，任意时刻只有 0 或 1 个文件，是 L0 反复合并的归宿
//   这个简化不影响核心机制的学习——分层触发、多路归并、
//   tombstone 在底层被清除——这些关键设计在两层结构里同样成立，
//   只是没有 leveldb 那样多层级联的复杂度。

class LevelManager {
public:
    // L0 文件数达到这个阈值就应该触发一次合并
    static constexpr size_t kL0CompactionTrigger = 4;

    // dir：新生成的合并文件要写到哪个目录下
    explicit LevelManager(const std::string& dir);

    // MemTable 刷盘生成一个新的 SSTable 文件后，调用这个函数登记到 L0
    void AddL0File(const std::string& file_path);

    // 检查 L0 文件数是否达到阈值；达到就执行一次真正的合并
    // （L0 全部文件 + 现有 L1 文件，一起合并成一个新的 L1 文件），
    // 并清理掉参与合并的旧物理文件。
    //
    // 返回 true 表示这次调用真的执行了一次合并，false 表示还没到触发条件。
    bool MaybeCompact();

    // 查询接口，仅供测试/观察用：当前各层有多少个文件
    size_t L0FileCount() const { return l0_files_.size(); }
    size_t L1FileCount() const { return l1_files_.size(); }

    // 当前 L1 文件路径（可能为空字符串，表示还没有 L1 文件）
    std::string L1FilePath() const;

    // 当前 L0 文件路径列表，追加顺序（旧→新）。
    // 跨层查询时需要"从新到旧"访问，调用方自行反向遍历。
    const std::vector<std::string>& L0FilePaths() const { return l0_files_; }

private:
    std::string              dir_;
    std::vector<std::string> l0_files_;  // 追加顺序 = 从旧到新
    std::vector<std::string> l1_files_;  // 本实现中最多 1 个元素
    int                      next_file_id_ = 0;

    // 生成一个新的、不会和已有文件重名的输出路径
    std::string NextFilePath(int level);
};

}  // namespace minikv