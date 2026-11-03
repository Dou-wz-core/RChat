#include "groupmodel.hpp"
#include "db/db.hpp"

// 创建群组
bool GroupModel::createGroup(Group &group)
{
    // 1.组装sql语句

    // 2.创建数据库连接对象
    MySQL mysql;
    if (mysql.connect())
    {
        // 群名和描述在拼接前分别转义，保留引号等字符的原始内容。
        const auto sql = "INSERT INTO AllGroup(name,description) VALUES('" + mysql.escape(group.getName()) +
            "','" + mysql.escape(group.getDesc()) + "')";
        // 3.执行sql语句
        if (mysql.update(sql))
        {
            // 4.获取插入数据的主键id
            group.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }
    return false;
}

// 加入群组
void GroupModel::addGroup(int groupid, int userid, const string& role)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into GroupUser(groupid, userid, role) values(%d, %d, '%s')",
             groupid, userid, role.c_str());

    // 2.创建数据库连接对象
    MySQL mysql;
    if (mysql.connect())
    {
        // 3.执行sql语句
        mysql.update(sql);
    }
}

// 查询userid用户所在的所有群组信息
vector<Group> GroupModel::queryGroups(int userid)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select a.id, a.name, a.description from AllGroup a inner join \
                  GroupUser b on a.id = b.groupid where b.userid = %d", userid);
    // 2.创建数据库连接对象
    MySQL mysql;
    vector<Group> groupVec;
    if (mysql.connect())
    {
        // 3.执行sql语句
        MYSQL_RES *res = mysql.query(sql);  
        if (res != nullptr)
        {
            MYSQL_ROW row;
            // 4.遍历结果集，获取群组信息
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                Group group;
                group.setId(atoi(row[0]));
                group.setName(row[1]);
                group.setDesc(row[2]);
                groupVec.push_back(group);
            }
            mysql_free_result(res);
        }
    }   
    // 5.查询并填充每个群组的用户信息
    for (Group &group : groupVec)
    {   
        // 组装sql语句
        sprintf(sql, "select a.id, a.name, a.state, b.role from User a inner join \
                      GroupUser b on a.id = b.userid where b.groupid = %d", group.getId());
        if (mysql.connect())
        {
            MYSQL_RES *res = mysql.query(sql);
            if (res != nullptr)
            {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res)) != nullptr)
                {
                    GroupUser user;
                    user.setId(atoi(row[0]));
                    user.setName(row[1]);
                    user.setState(row[2]);
                    user.setRole(row[3]);
                    group.getUsers().push_back(user);
                }
                mysql_free_result(res);
            }
        }
    }
    return groupVec;
}

// 查询群组成员id，除了userid自己。用于群聊业务给其他成员群发消息
vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    // 1.组装sql语句
    char sql[1024] = {0};   
    sprintf(sql, "select userid from GroupUser where groupid = %d and userid != %d", groupid, userid);
    // 2.创建数据库连接对象
    MySQL mysql;
    vector<int> idVec;
    if (mysql.connect())
    {
        // 3.执行sql语句        
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            // 4.遍历结果集，获取群组成员信息
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                idVec.push_back(atoi(row[0]));
            }
            mysql_free_result(res);
        }
    }
    return idVec;
}
