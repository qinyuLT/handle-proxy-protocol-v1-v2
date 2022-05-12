#include "handle-proxy-protocol-v1-v2.hpp"

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>
#include <unordered_map>
#include <mutex>

using namespace std;
using namespace muduo;
using namespace muduo::net;

// 经过代理的用户  : key为代理ip:port   value为用户真实ip:port;
// 未经过代理的用户: key 和 value一样，都是用户的真实 ip:port;
unordered_map<std::string, std::string> m_ClientIpMap;
mutex m_ClientIpLock;

// 接收到网络模块用户的数据，业务模块进行回调处理的函数
int handleMessage(const TcpConnectionPtr &session, const void* buff, std::size_t buff_size)
{
    int consume = 0;

    std::string ip_port = session->peerAddress().toIpPort();//session->GetRemoteIp() + ":" + std::to_string(session->GetRemotePort());

    bool is_in_client_map  = false; // 第一次 recv 到数据包，判断是否为代理，存入m_ClientIpMap，标识该连接已处理完第一个包
    std::string real_ip_port;       // 从代理协议获取用户的真实 ip port
    
    {
        lock_guard<mutex> guard(m_ClientIpLock);
        is_in_client_map = (m_ClientIpMap.find(ip_port) == m_ClientIpMap.end());
    }

    if (is_in_client_map) // 解析第一个数据包
    {
        int ret = HandleProxyProtocol(buff, buff_size, real_ip_port);
        // ret > 0 表示经过 HAProxy 以及 proxy protocol 的长度;
        cout << "HandleProxyProtocol return:" << ret << " real_ip_port:" << real_ip_port << endl;
        if (ret == -1) // 异常连接
        {
            cout << "Client proxy error " << ip_port << " buffSize = " << buff_size << endl;
            
            {
                lock_guard<mutex> guard(m_ClientIpLock);
                m_ClientIpMap.erase(ip_port);
            }
            session->shutdown();
            return -1;
        }
        else if (ret == 0) // 未经过 HAProxy 代理
        {
            real_ip_port = ip_port;
        }
        consume = ret;

        {
            lock_guard<mutex> guard(m_ClientIpLock);
            m_ClientIpMap.insert(make_pair(ip_port, real_ip_port));
        }
    }

    // handle client data from index of consume...
    if(buff_size > consume)
    {
        char *msg = (char*)buff+consume;
        cout << "handle user data: [" << msg << "]" << endl;
        session->send(msg);
    }
        
}


/*
 基于 muduo 网络库开发服务器程序
*/
class MTcpServer
{
public:
    MTcpServer(EventLoop *loop,                // 事件循环 
               const InetAddress &listenAddr,  // Listen IP PORT
               const string &nameArg)          // 服务器名字
        :_server(loop,listenAddr, nameArg),_loop(loop)
    {
        // 注册用户连接的创建与断开回调
        _server.setConnectionCallback(std::bind(&MTcpServer::onConnection, this, _1));
        // 注册用户的读写事件回调
        _server.setMessageCallback(std::bind(&MTcpServer::onMessage, this, _1, _2, _3));
        // 设置服务器端的线程数量n： 1个线程处理accept  n-1个线程处理连接的读写事件
        _server.setThreadNum(4);
    }

    // 开启事件循环
    void start()
    {
        _server.start();
    }

    // 处理用户连接的创建和断开
    void onConnection(const TcpConnectionPtr &conn)
    {
        std::string ip_port = conn->peerAddress().toIpPort();
        LOG_INFO << "example_proxy_protocol - " << ip_port << " -> "
             << conn->localAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");
        
        if(!conn->connected())
        {
            {
                lock_guard<mutex> guard(m_ClientIpLock);
                m_ClientIpMap.erase(ip_port);
            }

            conn->shutdown(); // close fd
        }
    }

    // 处理用户的读写事件
    void onMessage(const TcpConnectionPtr &conn, // 连接
                   Buffer *buf,                  // 缓冲区
                   Timestamp time)               // 接收到数据的时间信息

    {
        muduo::string msg(buf->retrieveAllAsString());
        LOG_INFO << conn->name() << " recv " << msg.size() << " bytes, "
                 << "data received at " << time.toString();
        
        // 在这里将网络模块的数据抛给业务模块进行处理
        handleMessage(conn, msg.c_str(), msg.size());
    }

private:
    TcpServer _server;
    EventLoop *_loop;

};


int main()
{
    cout << "start" << endl;

    EventLoop loop;
    InetAddress addr("0.0.0.0", 6000);

    MTcpServer server(&loop, addr, "example_proxy_protocol");
    server.start();
    loop.loop();

    return 0;
}