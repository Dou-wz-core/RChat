#include "db.hpp"
#include <cstdlib>
#include <stdexcept>
namespace {
std::string setting(const char* name, const char* fallback) {
    const char* value = std::getenv(name); return value ? value : fallback;
}
}
MySQL::MySQL() : _conn(mysql_init(nullptr)) {
    if (!_conn) throw std::runtime_error("mysql_init failed");
    unsigned timeout = 3;
    mysql_options(_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(_conn, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(_conn, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
}
MySQL::~MySQL() { if (_conn) mysql_close(_conn); }
bool MySQL::connect() {
    const auto host = setting("CHAT_DB_HOST", "127.0.0.1");
    const auto user = setting("CHAT_DB_USER", "root");
    const auto password = setting("CHAT_DB_PASSWORD", "");
    const auto database = setting("CHAT_DB_NAME", "chat_udp");
    const auto portText = setting("CHAT_DB_PORT", "3306");
    std::size_t used = 0; unsigned long port = std::stoul(portText, &used);
    if (used != portText.size() || port == 0 || port > 65535) throw std::invalid_argument("invalid CHAT_DB_PORT");
    if (!mysql_real_connect(_conn, host.c_str(), user.c_str(), password.c_str(), database.c_str(), static_cast<unsigned>(port), nullptr, 0)) {
        LOG_ERROR << "MySQL connection failed: " << mysql_error(_conn); return false;
    }
    if (mysql_set_character_set(_conn, "utf8mb4") != 0) { LOG_ERROR << "MySQL charset setup failed"; return false; }
    return true;
}
bool MySQL::update(string sql) {
    if (mysql_query(_conn, sql.c_str())) { LOG_ERROR << "MySQL update failed: " << mysql_error(_conn); return false; }
    return true;
}
MYSQL_RES* MySQL::query(string sql) {
    if (mysql_query(_conn, sql.c_str())) { LOG_ERROR << "MySQL query failed: " << mysql_error(_conn); return nullptr; }
    return mysql_store_result(_conn);
}
MYSQL* MySQL::getConnection() { return _conn; }
std::string MySQL::escape(const std::string& value) {
    std::string result(value.size() * 2 + 1, '\0');
    auto length = mysql_real_escape_string(_conn, &result[0], value.data(), static_cast<unsigned long>(value.size()));
    result.resize(length); return result;
}
