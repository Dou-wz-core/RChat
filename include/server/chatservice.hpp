// 业务逻辑类，负责处理聊天服务器的业务逻辑，包括用户登录、注册、消息处理等功能。
#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include<unordered_map>
#include<mutex>
#include<functional>
#include "net/connection.hpp"
#include "net/protocol.hpp"
#include "common/log.hpp"
#include "json.hpp"
#include "public.hpp"
#include<iostream>
#include<functional>
#include<memory>
#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"
using namespace std;
using chat::net::ConnectionPtr;
using chat::net::RequestContext;
using json = nlohmann::json;

// 处理消息的事件回调方法类型
using MsgHandler = std::function<void(const ConnectionPtr&, json&, RequestContext)>;

// 聊天服务器业务类：聊天业务和用户业务
class ChatService
{
public:
    // 获取单例对象的接口函数
    static ChatService* instance(); // 获取单例对象
    // 登录业务
    void login(const ConnectionPtr& conn, json& js, RequestContext context);
    // 注册业务
    void reg(const ConnectionPtr& conn, json& js, RequestContext context);
    // 注销业务
    void logout(const ConnectionPtr& conn, json& js, RequestContext context);


    // 服务器异常退出，业务重置方法
    void reset();
    // 处理客户端断开连接
    void clientClose(const ConnectionPtr& conn);
    // 获取消息对应的处理器
    MsgHandler getHandler(int msgid);
    
    // 一对一聊天业务
    void oneChat(const ConnectionPtr& conn, json& js, RequestContext context);
    // 添加好友业务
    void addFriend(const ConnectionPtr& conn, json& js, RequestContext context);

    // 创建群组业务
    void createGroup(const ConnectionPtr& conn, json& js, RequestContext context);
    // 加入群组业务
    void addGroup(const ConnectionPtr& conn, json& js, RequestContext context);
    // 群组聊天业务
    void groupChat(const ConnectionPtr& conn, json& js, RequestContext context);
    void pullOffline(const ConnectionPtr& conn, json& js, RequestContext context);



private:
    // 存储 消息id 及其对应的 业务处理回调
    unordered_map<int, MsgHandler> _msgHandlerMap; 
    // 存储在线用户的通信连接
    unordered_map<int, ConnectionPtr> _userConnMap;
    // 互斥锁，保证_userConnMap的线程安全
    mutex _userConnMapMutex;
    std::unordered_map<int, bool> _offlineInFlight;
    bool sendOffline(const ConnectionPtr& conn, json response, int userid, RequestContext context);
    
    ChatService();
    ~ChatService();

    // User表操作类对象
    UserModel _userModel;
    // OfflineMsg表操作类对象
    OfflineMsgModel _offlineMsgModel;
    // Friend表操作类对象
    FriendModel _friendModel;
    // Group/GroupUser表操作类对象
    GroupModel _groupModel;
};


#endif
