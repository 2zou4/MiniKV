#include "compaction.h"

#include "sstable.h"

#include<queue>
#include<vector>

namespace minikv{
    namespace{
        // ──────────────────────────────────────────────
// 堆节点：代表"某个来源当前游标指向的那条记录"
// ──────────────────────────────────────────────
//
// 堆里始终维护"每个来源当前最前面还没被消费的记录"，
// 每次弹出堆顶（全局最小 key），就相当于按 key 升序逐条产出结果，
struct HeapNode
{
    std::string key;
    size_t source_idx;// 这条记录来自 sources 里的第几组
    size_t item_idx;// 在那一组内的下标
};

// std::priority_queue 默认是大顶堆，要实现小顶堆需要反转比较逻辑：
// operator() 返回 true 表示 "a 的优先级低于 b"（a 应该排在 b 后面）。
//
// 排序规则：
//   1. 先按 key 升序（key 小的优先级高，应该先被弹出）
//   2. key 相同时，按 source_idx 升序（约定 source_idx 越小代表越新，
//      新的应该优先被弹出——这样同一个 key 的多个版本里，
//      最先弹出的就是我们要保留的"最新版本"）
struct HeapCompare{
    bool operator()(const HeapNode& a, const HeapNode& b) const{
        if(a.key!=b.key){
            return a.key>b.key;
        }
        return a.source_idx>b.source_idx;
    }
};
    }// namespace
// ──────────────────────────────────────────────
// Merge
// ──────────────────────────────────────────────
std::vector<KVEntry> KWayMerger::Merge(
    const std::vector<std::vector<KVEntry>>& sources, bool drop_tombstones){
        std::vector<KVEntry> result;
        std::priority_queue<HeapNode, std::vector<HeapNode>, HeapCompare> heap;
        // 初始化：把每个非空来源的第一条记录放入堆
        for(size_t i=0;i<sources.size();++i){
            if(!sources[i].empty()){
                heap.push({sources[i][0].key,i,0});
            }
        }

        while(!heap.empty()){
            // 堆顶就是当前全局最小 key，且如果有多个来源同时持有这个 key，
        // 由于 HeapCompare 里 source_idx 越小优先级越高，
        // 堆顶必然是这些重复项里 source_idx 最小（即最新）的那一个——
        // 这就是我们要保留的版本。
            HeapNode winner=heap.top();
            heap.pop();
            const std::string& current_key=winner.key;

            // 把堆里其余"同一个 key、但来自更旧来源"的重复项也都弹出，
        // 只推进它们各自的游标，不使用它们的值——
        // 这一步保证同一个 key 不会在结果里出现多次。
            while(!heap.empty() && heap.top().key==current_key){
                HeapNode dup=heap.top();
                heap.pop();

                size_t next=dup.item_idx+1;
                if(next<sources[dup.source_idx].size()){
                    heap.push({sources[dup.source_idx][next].key,dup.source_idx,next});
                }
            }

            // 推进 winner 自己的游标
            size_t winner_next=winner.item_idx+1;
            if(winner_next < sources[winner.source_idx].size()){
                heap.push({sources[winner.source_idx][winner_next].key, winner.source_idx,winner_next});
            }

            // 决定这条记录要不要写进结果
            const KVEntry& entry=sources[winner.source_idx][winner.item_idx];
            if(entry.is_deleted && drop_tombstones){
                continue;
            }
            result.push_back(entry);
        }
        return result;
    }

    bool Compaction::CompactFiles(const std::vector<std::string>& input_paths,
                                        const std::string& output_path,
                                        bool drop_tombstones){
        std::vector<std::vector<KVEntry>> sources;
        sources.reserve(input_paths.size());

        for(const auto& path:input_paths){
            SSTable sst(path);

            std::vector<KVEntry> entries;
            bool ok=sst.ForEach([&](const std::string& k, const std::string& v, bool del){
                entries.push_back({k,v,del});
            });

            if(!ok){
                return false;// 某个输入文件读取失败，整体合并中止
            }

            sources.push_back(std::move(entries));
        }

        std::vector<KVEntry> merged=KWayMerger::Merge(sources,drop_tombstones);

        std::vector<SSTable::Entry> output_entries;
        output_entries.reserve(merged.size());
        for(const auto& e:merged){
            output_entries.push_back({e.key,e.value,e.is_deleted});
        }
        return SSTable::BuildFromEntries(output_entries,output_path);
    }


}