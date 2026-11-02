#ifndef GROUP_HPP
#define GROUP_HPP

#include<string>
#include<vector>
#include"groupuser.hpp"
using namespace std;

// Group表的ORM类:(Object-Relational Mapping)将数据库中的表映射为C++对象的类，提供了对数据库表的操作接口。
class Group
{
public:
    Group(int id = 0, const string& name = "", const string& desc = "") 
    : _id(id), _name(name), _desc(desc) {} 

    // 设置群组信息
    void setId(int id) { _id = id; }
    void setName(const string& name) { _name = name; }
    void setDesc(const string& desc) { _desc = desc; }

    // 获取群组信息
    int getId() const { return _id; }
    const string& getName() const { return _name; }
    const string& getDesc() const { return _desc; }
    vector<GroupUser>& getUsers() { return _users; }

private:
    int _id;        // 群组ID
    string _name;   // 群组名称
    string _desc;   // 群组描述
    vector<GroupUser> _users; // 群组成员列表
};

#endif // GROUP_HPP