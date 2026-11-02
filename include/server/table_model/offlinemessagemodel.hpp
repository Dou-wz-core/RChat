#ifndef OFFLINEMESSAGEMODEL_H
#define OFFLINEMESSAGEMODEL_H
#include"db.hpp"
#include<string>
#include<vector>
#include<cstdint>
using namespace std;

// 离线消息表的操作类
class OfflineMsgModel
{
public:
    struct Message { std::uint64_t id; std::string body; };
    // 存储用户的离线消息  
    bool insert(int userid, const std::string& msg);
    std::vector<Message> queryPending(int userid, std::size_t limit = 50);
    // Only acknowledge the actual snapshot, never delete a whole user's inbox.
    bool acknowledge(int userid, const std::vector<std::uint64_t>& ids);
};


#endif
