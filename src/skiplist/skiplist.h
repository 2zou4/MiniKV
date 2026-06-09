#pragma once

#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

namespace minikv{
    struct SkipListNode{
        std:: string key;
        std:: string value;
        bool is_delete; //懒删除标记

        std::vector<SkipListNode*> forward;

        SkipListNode(const std::string& k,
                    const std::string& v,
                    int level,
                    bool deleted=false)
                    :key(k),value(v),is_delete(deleted),
                    forward(level,nullptr) {}
    };
    class SkipList{
        public:
        static constexpr int MAX_LEVEL=12;
        static constexpr int BRANCH=4;  //升层概率1/4

        explicit SkipList();
        ~ SkipList();

        //禁止拷贝
        SkipList(const SkipList&)=delete;
        SkipList& operator=(const SkipList&)=delete;

        void Insert(const std::string& key, const std::string& value);

        bool Delete(const std::string& key);

        bool Get(const std::string& key,std::string* value) const;

        int Size() const {return size_;}
        bool Empty() const {return size_==0;}

        size_t ApproximateMemoryUsage() const;

        private:
        int RandomLevel();
        SkipListNode* FindGreaterOrEqual(const std::string& key,
                                        SkipListNode** prev) const;

        SkipListNode* head_;
        int cur_level_;
        int size_;
        size_t mem_usage_;

    };
}