#include "offlinemessagemodel.hpp"
#include <algorithm>
#include <cstdlib>
bool OfflineMsgModel::insert(int userid, const std::string& message) {
    // 消息可能含引号、反斜杠和较长 JSON，转义后用动态字符串构造 SQL。
    MySQL mysql;
    return mysql.connect() && mysql.update("INSERT INTO OfflineMessage(userid,message) VALUES(" +
        std::to_string(userid) + ",'" + mysql.escape(message) + "')");
}
std::vector<OfflineMsgModel::Message> OfflineMsgModel::queryPending(int userid, std::size_t limit) {
    MySQL mysql; std::vector<Message> messages;
    if (!mysql.connect()) return messages;
    // 主键顺序保证分批拉取时优先处理旧消息，查询结果同时保留确认所需的主键。
    auto result = mysql.query("SELECT id,message FROM OfflineMessage WHERE userid=" + std::to_string(userid) +
        " ORDER BY id LIMIT " + std::to_string(std::min<std::size_t>(limit, 50)));
    if (!result) return messages;
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        // 使用 MySQL 返回的字段长度复制正文，避免内嵌空字符截断字符串。
        auto lengths = mysql_fetch_lengths(result);
        messages.push_back({std::strtoull(row[0], nullptr, 10), std::string(row[1], lengths[1])});
    }
    mysql_free_result(result); return messages;
}
bool OfflineMsgModel::acknowledge(int userid, const std::vector<std::uint64_t>& ids) {
    if (ids.empty()) return true;
    std::string list;
    for (auto id : ids) { if (!list.empty()) list += ','; list += std::to_string(id); }
    MySQL mysql;
    // 同时限定收件人和本批主键，确认期间新插入的消息不会被这次删除匹配。
    return mysql.connect() && mysql.update("DELETE FROM OfflineMessage WHERE userid=" +
        std::to_string(userid) + " AND id IN (" + list + ")");
}
