# handle-proxy-protocol-v1-v2
## 背景
在基于Tcp协议，实现C/S模式的服务端时，用户的链接经过反向代理（比如haproxy/nginx），之后转发到我们的服务端，这个时候服务端只能拿到代理服务的ip port，如果想要拿到用户端真实的 ip port，就需要在代理层通过 PROXY-prococol 协议，将用户真实的 ip port 转发给后端的应用服务。
* PROXY-prococol 是在tcp三次握手成功之后，发送一个 PROXY 协议包到业务服务
* 代理层是否发送 PROXY 协议包，需要代理层进行配置
* PROXY 协议包只会发一次，并且会在用户第一帧数据之前到达后端业务服务端
  
## 功能实现
* 业务服务不依赖代理层是否启用 PROXY-prococol 协议，对启用或者不启用或者直连的情况都兼容
* 兼容 PROXY-prococol 协议 v1/v2两个版本，均能拿到客户端真实的ip port 
* 已在线上服务正常运行

## 如何使用
- 引入头文件  handle-proxy-protocol-v1-v2.hpp
- 在业务模块处理网络数据的回调函数中调用 HandleProxyProtocol 函数，具体参照example_muduo_tcp_server.cpp 文件 handleMessage 函数的实现
- PROXY 协议包只会发一次，所以在业务模块需要维护一个map，仅在一个链接第一次收到数据包的时候才调用 HandleProxyProtocol 函数

## demo 演示
- example_muduo_tcp_server.cpp 基于muduo网络库实现了一个简单的Tcp服务，监听6000端口，如需运行，需自行安装muduo库
- demo 中网络通信采用简单的json格式，线上服务采用的是自定义协议头加消息体的形式，都是支持的，只要你的协议头与 PROXY-prococol 协议头不产生冲突就可以
- 192.168.18.128：代理层以haproxy和nginx作为演示，监听本机的8080端口，转发给本机的业务服务6000端口
- 192.168.18.129：用于客户端进行telnet测试
- demo演示截图在imgs文件夹下，如何加载不出来，请自行下载查看
- 编译
```c
g++ -o server example_muduo_tcp_server.cpp -lmuduo_net -lmuduo_base -lpthread
```

## 代理层配置 PROXY-prococol 协议
* haproxy 配置
  
```c
global
    log 127.0.0.1 local2
    ulimit-n 800000
    chroot /var/lib/haproxy
    pidfile /var/run/haproxy.pid
    maxconn 4000
    user haproxy
    group haproxy
    daemon
    stats socket /var/lib/haproxy/stats

defaults
    mode tcp                    # 配置 tcp 模式
    log global
    option dontlognull
    retries 3
    maxconn 6000
    timeout queue 1m
    timeout connect 1000s
    timeout client 150000m
    timeout server 150000m
    timeout check 10s

frontend forward8080
    bind *:8080                  # 代理服务监听 8080 端口
    default_backend server6000

backend server6000
                                 # 后端业务服务监控 6000 端口
    #server server1 127.0.0.1:6000 send-proxy maxconn 3000    # v1 版本
    server server1 127.0.0.1:6000 send-proxy-v2 maxconn 3000  # v2 版本

```
![](https://github.com/qinyuLT/handle-proxy-protocol-v1-v2/blob/main/imgs/haproxy_proxy_protocol_v1.jpg)
![](https://github.com/qinyuLT/handle-proxy-protocol-v1-v2/blob/main/imgs/haproxy_proxy_protocol_v2.jpg)
        
* nginx 配置
```c
stream{
    upstream backServer
    {
        server 127.0.0.1:6000;    # 后端业务服务地址
    }

    server{
        listen 8080;              # 代理服务监听 8080 端口
        proxy_pass backServer;
        proxy_protocol      on;   # 开启 proxy protocol v1 转发
    }
}

```
![](https://github.com/qinyuLT/handle-proxy-protocol-v1-v2/blob/main/imgs/nginx_proxy_protocol_v1.jpg)
* TODO 未能找到 nginx 如何配置 PROXY protocol v2版本，线上服务用的是 haproxy，如果有小伙伴知道neginx如何配置 v2版本，欢迎留言。

