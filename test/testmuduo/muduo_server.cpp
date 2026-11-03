/*
muudo网络库主要提供两个类
1. TcpServer - TCP服务器类
2. TcpClient - TCP客户端类

epoll + 线程池 + 回调函数
好处：把网络I/O操作和业务逻辑处理分离开来，
       网络I/O操作由muduo库完成，业务逻辑处理由用户自己实现。
       用户的连接和断开    用户的可读写事件
*/ 

#include "muduo/net/TcpServer.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpConnection.h"
#include "muduo/net/Buffer.h"
#include "iostream"
#include <functional>
#include <string>
using namespace std;

/*  基于muduo网络库开发服务器程序
1.组合TcpServer对象
2.创建EventLoop事件循环对象
3.明确TcpServer构造函数需要什么参数，输出ChatServer的构造函数
4.在ChatServer的构造函数中注册处理连接的回调函数和处理读写事件的回调函数
5.设置合适的服务器端线程数量，muduo库会自己分配I/O线程和worker线程
6.在main函数中创建EventLoop事件循环对象，创建ChatServer对象，
  调用start()方法，最后调用loop()
*/

class ChatServer
{
public:
    // 明确TcpServer构造函数需要什么参数，输出ChatServer的构造函数
    ChatServer(muduo::net::EventLoop* loop,     // 事件循环对象
               const muduo::net::InetAddress& listenAddr,   // 监听地址和端口
               const string& nameArg)   // 服务器名字
        : _loop(loop),
          _server(loop, listenAddr, nameArg)
    {
        // 给服务器注册用户连接的创建和断开回调
        _server.setConnectionCallback(std::bind(&ChatServer::onConnection,
                                      this, std::placeholders::_1));
        // 给服务器注册用户读写事件回调
        _server.setMessageCallback(std::bind(&ChatServer::onMessage, 
            this, std::placeholders::_1,
            std::placeholders::_2, 
            std::placeholders::_3));

        // 设置服务器端的线程数量 1个I/O线程 3个worker线程
        _server.setThreadNum(4);
    }

    // 开启事件循环
    void start()
    {
        _server.start();
    }

private:
    // 专门处理用户的连接创建和断开
    void onConnection(const muduo::net::TcpConnectionPtr& conn)
    {
        if (conn->connected())
        {
            cout << "new connection: " << conn->peerAddress().toIpPort() << endl;
        }
        else
        {
            cout << "connection closed: " << conn->peerAddress().toIpPort() << endl;
            // conn->shutdown(); // 主动关闭连接
            // loop->quit();  // 退出事件循环
        }
    }
    // 专门处理用户的读写事件
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buffer,
                   muduo::Timestamp)
    {
        string buf = buffer->retrieveAllAsString();
        cout << "recv data: " << buf << " from connection: "<< 
        conn->peerAddress().toIpPort() << endl;

        conn->send(buf);
    }

    // 组合EventLoop事件循环对象的指针
    muduo::net::EventLoop* _loop;
    // 组合TcpServer对象
    muduo::net::TcpServer _server;
    // TcpServer(EventLoop* loop,const InetAddress& listenAddr,const string& nameArg,...)
            
            
};

int main()
{
    // 1.创建事件循环对象
    muduo::net::EventLoop loop;

    // 2.创建服务器对象
    muduo::net::InetAddress addr("127.0.0.1", 6000);
    ChatServer server(&loop, addr, "ChatServer");

    // 3.启动服务器
    server.start(); // 开始监听，并将监听描述符注册到 epoll 事件循环。

    // 4.启动事件循环
    loop.loop();    // epoll_wait以阻塞方式等待新用户连接、已连接用户的读写事件等



    return 0;

}
