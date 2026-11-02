#ifndef USER_H
#define USER_H

#include <string>
using namespace std;

// 提供业务模块的映射表
// User表的ORM类
class User {
public:
    User(int id = -1, string name = "", string password = "", string state = "offline") 
    : id(id), name(name), password(password), state(state) {};

    // 设置用户信息
    void setId(int value){id = value;}      
    void setName(const std::string& value){name = value;}
    void setState(const std::string& value){state = value;}
    void setPassword(const std::string& value){password = value;}

    // 获取用户信息
    int getId() const{ return this->id; }
    string getName() const{ return this->name; }
    string getsPwd() const{ return this->password; }
    string getState() const{ return this->state; }   

protected:
    int    id;      // 用户id
    string name;    // 用户名称
    string password;// 密码
    string state;   // 用户状态
};  

#endif