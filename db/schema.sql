-- Local UDP development schema. This file is never applied automatically.
CREATE DATABASE IF NOT EXISTS chat_udp CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE chat_udp;
CREATE TABLE IF NOT EXISTS `User` (
    id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    password VARCHAR(128) NOT NULL,
    state ENUM('online','offline') NOT NULL DEFAULT 'offline'
);
CREATE TABLE IF NOT EXISTS Friend (
    userid INT NOT NULL, friendid INT NOT NULL,
    PRIMARY KEY(userid, friendid)
);
CREATE TABLE IF NOT EXISTS AllGroup (
    id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(128) NOT NULL, description VARCHAR(512) NOT NULL
);
CREATE TABLE IF NOT EXISTS GroupUser (
    groupid INT NOT NULL, userid INT NOT NULL,
    role ENUM('creator','normal') NOT NULL DEFAULT 'normal',
    PRIMARY KEY(groupid, userid)
);
CREATE TABLE IF NOT EXISTS OfflineMessage (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    userid INT NOT NULL, message MEDIUMTEXT NOT NULL,
    INDEX inbox(userid, id)
);
