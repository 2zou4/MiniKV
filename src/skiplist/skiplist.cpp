#include "skiplist.h"

#include <cstdlib>
#include <ctime>
#include <stdexcept>

namespace minikv{
    SkipList::SkipList()
        :cur_level_(1), size_(0), mem_usage_(0){
        head_=new SkipListNode("","",MAX_LEVEL);
        std::srand(static_cast<unsigned>(std::time(nullptr)));
    }
    SkipList::~SkipList(){
        SkipListNode* cur=head_;
        while(cur){
            SkipListNode* next=cur->forward[0];
            delete cur;
            cur=next;
        }
    }
    int SkipList::RandomLevel(){
        int level=1;
        while(level<MAX_LEVEL && (std::rand() % BRANCH)==0){
            ++level;
        }
        return level;
    }
    SkipListNode* SkipList::FindGreaterOrEqual(const std::string&key,
                                                SkipListNode** prev) const{
        SkipListNode* cur=head_;

        for(int i=cur_level_-1;i>=0;--i)
        {
            while(cur->forward[i]!=nullptr && cur->forward[i]->key<key)
            {
                cur=cur->forward[i];
            }
            if(prev)
            {
                prev[i]=cur;
            }
        }
        return cur->forward[0];
    }

    void SkipList::Insert(const std::string& key, const std::string& value)
    {
        SkipListNode* prev[MAX_LEVEL];
        SkipListNode* candidate=FindGreaterOrEqual(key,prev);
        if(candidate!=nullptr && candidate->key==key)
        {
            if(candidate->is_delete)
            {
                candidate->is_delete=false;
                ++size_;
                mem_usage_ +=value.size();
            }
            else
            {
                mem_usage_ +=value.size();
                mem_usage_ -=candidate->value.size();
            }
            candidate->value=value;
            return;
        }
        int new_level=RandomLevel();
        if(new_level>cur_level_)
        {
            for(int i=cur_level_; i<new_level;++i)
            {
                prev[i]=head_;
            }
            cur_level_=new_level;
        }
        SkipListNode* new_node=new SkipListNode(key,value,new_level);
        for(int i=0;i<new_level;++i)
        {
            new_node->forward[i]=prev[i]->forward[i];
            prev[i]->forward[i]=new_node;
        }
        ++size_;
        mem_usage_ +=key.size()+value.size()+new_level*sizeof(SkipListNode*)+sizeof(SkipListNode);
    }

}