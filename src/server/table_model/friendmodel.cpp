#include"friendmodel.hpp"

// 添加好友关系
void FriendModel::insert(int userid, int friendid)
{
    // 1.获取数据库连接
    MySQL mysql;
    if (mysql.connect())
    {
        // 2.构造SQL语句
        char sql[1024] = {0};
        snprintf(sql, sizeof(sql), "insert into Friend values(%d, %d)", userid, friendid);
        // 3.执行SQL语句
        if (mysql.update(sql))
            LOG_DEBUG << "添加好友关系成功: " << userid << " -> " << friendid;
        else
            LOG_ERROR << "添加好友关系失败: " << userid << " -> " << friendid;
    }
}

// 查询好友列表
vector<User> FriendModel::query(int userid)
{
    vector<User> friends; // 用于存储好友列表
    // 1.获取数据库连接
    MySQL mysql;
    if (mysql.connect())
    {
        // 2.构造SQL语句，两表联查
        char sql[1024] = {0};
        snprintf(sql, sizeof(sql), 
        "select u.id, u.name, u.state from User u inner join Friend f on u.id = f.friendid where f.userid = %d", userid);
        // 3.执行SQL语句并获取结果集
        MYSQL_RES* res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                friends.push_back(user);
            }
            mysql_free_result(res); // 释放结果集
        }
        else
            LOG_ERROR << "查询好友列表失败: " << userid;
    }
    return friends; // 返回好友列表
}   