#include "level_manager.h"
 
#include <cstdio>
 
#include "compaction.h"
 
namespace minikv {

    LevelManager::LevelManager(const std::string& dir):dir_(dir){}

    void LevelManager::AddL0File(const std::string& file_path){
        l0_files_.push_back(file_path);
    }

    std::string LevelManager::NextFilePath(int level){
        return dir_+"/L"+std::to_string(level)+"_"+
                std::to_string(next_file_id_++)+"sst";
    }

    std::string LevelManager::L1FilePath()const{
        return l1_files_.empty()?"":l1_files_[0];
    }


    // ──────────────────────────────────────────────
// MaybeCompact
// ──────────────────────────────────────────────
//
// 步骤：
//   1. 判断触发条件：L0 文件数是否达到 kL0CompactionTrigger
//   2. 组装 CompactFiles 需要的 input_paths，顺序至关重要：
//      必须"最新的排最前面"。l0_files_ 是按追加顺序存的（旧→新），
//      所以要反向遍历；现有的 L1 文件永远是全局最旧的，排在最后。
//   3. 因为 L1 在本实现里就是最底层，drop_tombstones 恒为 true——
//      这里合并完，可以确定不会再有更旧的数据残留，
//      tombstone 已经完成使命，可以放心清除。
//   4. 合并成功后：删除所有参与合并的旧物理文件，
//      清空 l0_files_，把新生成的文件设为唯一的 L1 文件。
    bool LevelManager::MaybeCompact(){
        if(l0_files_.size()<kL0CompactionTrigger){
            return false;
        }

        std::vector<std::string> input_paths;
        input_paths.reserve(l0_files_.size()+l1_files_.size());

        // L0 文件：反向遍历，让追加顺序里最后加入（最新）的排在最前面
        for(auto it=l0_files_.rbegin();it!=l0_files_.rend();++it){
            input_paths.push_back(*it);
        }
        // L1 文件（如果存在）：全局最旧，排在最后
        if(!l1_files_.empty()){
            input_paths.push_back(l1_files_[0]);
        }

        std::string output_path=NextFilePath(1);

        // L1 是本实现里的最底层，drop_tombstones 恒为 true
        bool ok=Compaction::CompactFiles(input_paths,output_path,true);
        if(!ok) return false;

        // 合并成功：清理旧的物理文件，更新层级状态
        for(const auto& path:l0_files_){
            std::remove(path.c_str());
        }
        if(!l1_files_.empty()){
            std::remove(l1_files_[0].c_str());
        }

        l0_files_.clear();
        l1_files_={output_path};

        return true;
    }
}