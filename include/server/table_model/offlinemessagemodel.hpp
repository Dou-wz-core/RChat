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
    // id 为数据库中的消息主键，用来区分内容相同但分别存储的消息。
    struct Message { std::uint64_t id; std::string body; };
    // 存储用户的离线消息  
    bool insert(int userid, const std::string& msg);
    // 按主键递增查询待收消息，单次最多取 50 条，不在查询时删除记录。
    std::vector<Message> queryPending(int userid, std::size_t limit = 50);
    // 仅删除指定用户本批已确认的主键记录，保留批次发送期间新增的离线消息。
    bool acknowledge(int userid, const std::vector<std::uint64_t>& ids);
};


#endif
