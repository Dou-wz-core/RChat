-- 为仅含 userid 和 message 的离线消息表补充自增主键；表中尚不能存在 id 列。
-- 主键用于标识确认批次，联合索引支持按用户顺序读取待收消息。
ALTER TABLE OfflineMessage ADD COLUMN id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
CREATE INDEX inbox ON OfflineMessage(userid, id);
