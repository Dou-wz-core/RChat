#include "db.hpp"
#include "common/settings.hpp"
#include <cstdlib>
#include <stdexcept>
namespace {
// 读取进程环境变量，未设置时使用默认值；已设置的空字符串仍按实际配置处理。
std::string setting(const char* name, const char* fallback) {
    return chat::settings::get(name, fallback);
}
}
MySQL::MySQL() : _conn(mysql_init(nullptr)) {
    if (!_conn) throw std::runtime_error("mysql_init failed");
    // 对连接、读、写分别设置超时，限制同步数据库调用的等待时间。
    unsigned timeout = 3;
    mysql_options(_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(_conn, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(_conn, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
}
MySQL::~MySQL() { if (_conn) mysql_close(_conn); }
bool MySQL::connect(bool reportErrors) {
    // 每个数据库对象独立读取连接参数，账号和密码不写入查询语句。
    const auto host = setting("CHAT_DB_HOST", "127.0.0.1");
    const auto user = setting("CHAT_DB_USER", "root");
    const auto password = setting("CHAT_DB_PASSWORD", "");
    const auto database = setting("CHAT_DB_NAME", "chat_udp");
    const auto portText = setting("CHAT_DB_PORT", "3306");
    // 转换后检查完整消费及端口范围，防止配置被截断为另一个端口。
    std::size_t used = 0; unsigned long port = std::stoul(portText, &used);
    if (used != portText.size() || port == 0 || port > 65535) throw std::invalid_argument("invalid CHAT_DB_PORT");
    if (!mysql_real_connect(_conn, host.c_str(), user.c_str(), password.c_str(), database.c_str(), static_cast<unsigned>(port), nullptr, 0)) {
        if (reportErrors) LOG_ERROR << "MySQL connection failed: " << mysql_error(_conn);
        return false;
    }
    // 使用 utf8mb4 处理中文和四字节字符，字符串转义也依据此连接字符集执行。
    if (mysql_set_character_set(_conn, "utf8mb4") != 0) {
        if (reportErrors) LOG_ERROR << "MySQL charset setup failed";
        return false;
    }
    return true;
}
bool MySQL::update(string sql) {
    if (mysql_query(_conn, sql.c_str())) { LOG_ERROR << "MySQL update failed: " << mysql_error(_conn); return false; }
    return true;
}
MYSQL_RES* MySQL::query(string sql) {
    if (mysql_query(_conn, sql.c_str())) { LOG_ERROR << "MySQL query failed: " << mysql_error(_conn); return nullptr; }
    // 将结果集读入客户端内存，调用方遍历完成后负责 mysql_free_result。
    return mysql_store_result(_conn);
}
MYSQL* MySQL::getConnection() { return _conn; }
std::string MySQL::escape(const std::string& value) {
    // 转义后最坏需要两倍长度，额外预留结尾空字符；返回时按实际字节数收缩。
    std::string result(value.size() * 2 + 1, '\0');
    auto length = mysql_real_escape_string(_conn, &result[0], value.data(), static_cast<unsigned long>(value.size()));
    result.resize(length); return result;
}
