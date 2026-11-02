#ifndef USERMODER_H
#define USERMODER_H

#include "user.hpp"
using namespace std; 

// User表的数据操作类
class UserModel {
public:
    // User表的增加方法
    bool insert(User &user);
    // User表的查询方法
    User query(int id);
    // User表的状态更新方法
    bool updateState(User user);
    // 重置所有用户状态
    bool resetState();
};


#endif