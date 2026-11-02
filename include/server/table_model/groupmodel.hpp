#ifndef GROUPMODEL_H
#define GROUPMODEL_H

#include "group.hpp"
#include <string>
#include <vector>
using namespace std;

// Group/GroupUser表的数据操作类
class GroupModel 
{
public:
    // 创建群组
    bool createGroup(Group &group);
    // 加入群组
    void addGroup(int groupid, int userid, const string& role);
    // 查询userid用户所在的所有群组信息
    vector<Group> queryGroups(int userid);
    // 查询群组成员id，除了userid自己。用于群聊业务给其他成员群发消息
    vector<int> queryGroupUsers(int userid, int groupid);

};


#endif // GROUPMODEL_H