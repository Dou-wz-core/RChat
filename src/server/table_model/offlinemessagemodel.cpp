#include "offlinemessagemodel.hpp"
#include <algorithm>
#include <cstdlib>
bool OfflineMsgModel::insert(int userid, const std::string& message) {
    MySQL mysql;
    return mysql.connect() && mysql.update("INSERT INTO OfflineMessage(userid,message) VALUES(" +
        std::to_string(userid) + ",'" + mysql.escape(message) + "')");
}
std::vector<OfflineMsgModel::Message> OfflineMsgModel::queryPending(int userid, std::size_t limit) {
    MySQL mysql; std::vector<Message> messages;
    if (!mysql.connect()) return messages;
    auto result = mysql.query("SELECT id,message FROM OfflineMessage WHERE userid=" + std::to_string(userid) +
        " ORDER BY id LIMIT " + std::to_string(std::min<std::size_t>(limit, 50)));
    if (!result) return messages;
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
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
    return mysql.connect() && mysql.update("DELETE FROM OfflineMessage WHERE userid=" +
        std::to_string(userid) + " AND id IN (" + list + ")");
}
