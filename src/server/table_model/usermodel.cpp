#include"usermodel.hpp"
#include"db.hpp"
#include<iostream>
using namespace std;

// User用户表的数据操作方法

// User表的增加用户方法
bool UserModel::insert(User &user)
{
    // 组装sql语句
    
    MySQL mysql;
    if (mysql.connect())
    {
        // 用户提供的字符串按当前连接字符集转义，动态 SQL 不受固定缓冲区长度限制。
        const auto sql = "INSERT INTO `User`(name,password,state) VALUES('" + mysql.escape(user.getName()) +
            "','" + mysql.escape(user.getsPwd()) + "','" + mysql.escape(user.getState()) + "')";
        if (mysql.update(sql))
        {
            // 获取插入成功的用户数据生成的主键id
            // 注册时只有name和password，id是自增生成返回的
            user.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }
    return false;
}

// User表的查询方法
User UserModel::query(int id)
{
    // 根据用户id查询用户信息
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select * from `User` where id = %d", id);
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            // 从结果集中获取一行数据，判断是否有数据
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                User user(atoi(row[0]), row[1], row[2], row[3]);
                mysql_free_result(res);
                return user;
            }
        }
    }
    return User();
}

// User表的更新方法
bool UserModel::updateState(User user)
{
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "update `User` set state = '%s' where id = %d", user.getState().c_str(), user.getId());
    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
        {
            return true;
        }
    }
    return false;   
}

// 重置用户的状态信息
bool UserModel::resetState()
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    return mysql.update("update `User` set state = 'offline'");
}
