#ifndef GROUPUSER_H
#define GROUPUSER_H

#include "user.hpp"

// GroupUser类继承自User类，表示群组中的用户，增加了角色属性
class GroupUser : public User
{
public:

    // 设置群组用户角色
    void setRole(string value) { role = value; }

    // 获取群组用户角色
    string getRole() const { return this->role; }

private:
    string role; // 群组用户角色
};


#endif // GROUPUSER_H