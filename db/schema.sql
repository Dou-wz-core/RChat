-- 创建聊天业务使用的数据库及表，统一使用 utf8mb4 字符集。
CREATE DATABASE IF NOT EXISTS chat_udp CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE chat_udp;
CREATE TABLE IF NOT EXISTS `User` (
    id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    password VARCHAR(128) NOT NULL,
    state ENUM('online','offline') NOT NULL DEFAULT 'offline'
);
-- 好友关系按发送者和好友组成联合主键，同方向的关系不能重复插入。
CREATE TABLE IF NOT EXISTS Friend (
    userid INT NOT NULL, friendid INT NOT NULL,
    PRIMARY KEY(userid, friendid)
);
CREATE TABLE IF NOT EXISTS AllGroup (
    id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(128) NOT NULL, description VARCHAR(512) NOT NULL
);
-- 同一用户在一个群组内只有一条成员记录，角色区分创建者和普通成员。
CREATE TABLE IF NOT EXISTS GroupUser (
    groupid INT NOT NULL, userid INT NOT NULL,
    role ENUM('creator','normal') NOT NULL DEFAULT 'normal',
    PRIMARY KEY(groupid, userid)
);
-- 离线消息以独立主键标识，支持收到传输确认后按批次精确删除。
-- 联合索引匹配按 userid 过滤并按 id 排序的待收消息查询。
CREATE TABLE IF NOT EXISTS OfflineMessage (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    userid INT NOT NULL, message MEDIUMTEXT NOT NULL,
    INDEX inbox(userid, id)
);
