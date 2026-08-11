#include "db.h"
 
#include "sstable.h"
 
namespace minikv {

    DB::DB(const std::string& wal_path, const std::string& sstable_dir)
        :memtable_(std::make_unique<MemTable>(wal_path)),
        levels_(sstable_dir){}

    bool DB::Put(const std::string& key, const std::string& value){
        return memtable_->Put(key,value);
    }

    bool DB::Delete(const std::string& key){
        return memtable_->Delete(key);
    }

// ──────────────────────────────────────────────
// Get：跨层查询的核心
// ──────────────────────────────────────────────
//
// 查找顺序：MemTable（最新）-> L0 文件（新到旧）-> L1 文件（最旧）。
//
// 每一步都用三态的 Find，而不是二态的 Get，原因见 lookup_result.h：
//   kFound    -> 直接返回，查找成功
//   kDeleted  -> 直接返回"不存在"，绝不能继续往更旧的层找
//                （否则会让已经删除的数据从旧层里"复活"）
//   kNotFound -> 这一层没有任何相关记录，继续查下一层
    bool DB::Get(const std::string& key, std::string* value) const{
        // 第一步：MemTable（最新数据，内存里）
        LookupResult r=memtable_->Find(key,value);
        if(r==LookupResult::kFound) return true;
        if(r==LookupResult::kDeleted) return false;

        // 第二步：L0 文件，从新到旧
        // LevelManager 里 L0FilePaths() 是追加顺序（旧→新），要反向遍历
        const auto& l0_paths=levels_.L0FilePaths();
        for(auto it=l0_paths.rbegin();it!=l0_paths.rend();++it){
            SSTable sst(*it);
            r=sst.Find(key,value);
            if (r == LookupResult::kFound)   return true;
            if (r == LookupResult::kDeleted) return false;
            // kNotFound：继续查下一个更旧的 L0 文件
        }

        // 第三步：L1 文件（如果存在），这是最底层，没有更旧的层可查了
        std::string l1_path=levels_.L1FilePath();
        if(!l1_path.empty()){
            SSTable sst(l1_path);
            r=sst.Find(key,value);
            if (r == LookupResult::kFound) return true;
            // kDeleted 或 kNotFound 到这里都是"没找到"，没有更旧的层了
        }
        return false;
    }
}