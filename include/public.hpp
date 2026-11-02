#ifndef PUBLIC_H
#define PUBLIC_H

/*
server端和client端的公共文件
*/
enum EnMsgType
{
    ERROR_MSG = 0,      // 通用错误响应
    LOGIN_MSG,          // 登录消息
    LOGIN_MSG_ACK,      // 登录响应消息
    REG_MSG,            // 注册消息   
    REG_MSG_ACK,        // 注册响应消息

    ONE_CHAT_MSG,       // 聊天消息：5
    ADD_FRIEND_MSG,     // 添加好友消息
    ADD_FRIEND_MSG_ACK, // 添加好友响应消息
    LOGOUT_MSG,         // 注销请求
    LOGOUT_MSG_ACK,     // 注销响应

    CREATE_GROUP_MSG,   // 创建群组请求：10
    CREATE_GROUP_MSG_ACK, // 创建群组响应
    ADD_GROUP_MSG,      // 加入群组请求
    ADD_GROUP_MSG_ACK,  // 加入群组响应
    GROUP_CHAT_MSG,     // 群聊消息
    GROUP_CHAT_MSG_ACK, // 群聊响应：15
    OFFLINE_PULL_MSG,   // 16: 拉取下一批离线消息
    OFFLINE_BATCH_MSG,  // 17: 离线消息批次响应
    ONE_CHAT_MSG_ACK    // 18: 单聊受理响应（不代表对方已读）
};

//ACK 消息是服务器返回给客户端的响应消息，不是服务器接收的请求消息，所以服务端不需要为 ACK 绑定 handler


#endif
