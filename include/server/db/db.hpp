#ifndef DB_H
#define DB_H

#include <mysql.h>
#include <string>
#include "common/log.hpp"
using namespace std;



// 数据库操作类
class MySQL
{
public:
    // 初始化数据库连接
    MySQL();
    // 释放数据库连接资源
    ~MySQL();
    // 连接数据库
    bool connect(bool reportErrors = true);
    // 更新操作
    bool update(string sql);
    // 查询操作
    MYSQL_RES *query(string sql);
    // 获取连接
    MYSQL* getConnection();
    // 按已建立连接的字符集转义 SQL 字符串内容，调用方仍需添加字符串引号。
    std::string escape(const std::string& value);
private:
    MYSQL *_conn;
};

#endif
