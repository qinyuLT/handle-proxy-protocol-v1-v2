#include <iostream>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;

// PROXY protocol 协议 v2 版本获取用户 ipv4 地址
std::string ProxyProtocolGetClientIp_ipv4_v2(uint32_t src_addr, uint32_t dst_addr, uint16_t src_port, uint16_t dst_port)
{
    std::string real_ip_port;
    struct sockaddr_storage from; // client addr
    struct sockaddr_storage to;   // server addr or F5 addr

    ((struct sockaddr_in*)&from)->sin_family      = AF_INET;
    ((struct sockaddr_in*)&from)->sin_addr.s_addr = src_addr;
    ((struct sockaddr_in*)&from)->sin_port        = src_port;

    struct sockaddr_in* addr_v4 = (struct sockaddr_in*)&from;
    char str[INET_ADDRSTRLEN]   = {0};
    inet_ntop(AF_INET, &((struct sockaddr_in*)&from)->sin_addr, str, sizeof(str));

    real_ip_port.append(str);
    real_ip_port.append(":");
    real_ip_port.append(std::to_string(src_port));

    ((struct sockaddr_in*)&to)->sin_family      = AF_INET;
    ((struct sockaddr_in*)&to)->sin_addr.s_addr = dst_addr;
    ((struct sockaddr_in*)&to)->sin_port        = dst_port;

    addr_v4 = (struct sockaddr_in*)&to;
    memset(str, 0, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &((struct sockaddr_in*)&to)->sin_addr, str, sizeof(str));

    cout << "Proxy protocol v2 ipv4: form(client_ip) " << real_ip_port << " to " << str << ":" << std::to_string(dst_port) << endl;

    return real_ip_port;
}

// PROXY protocol 协议 v2 版本获取用户 ipv6 地址
std::string ProxyProtocolGetClientIp_ipv6_v2(uint8_t* src_addr, uint8_t* dst_addr, uint16_t src_port, uint16_t dst_port)
{
    std::string real_ip_port;
    struct sockaddr_storage from; // client addr
    struct sockaddr_storage to;   // server addr or F5 addr

    ((struct sockaddr_in6*)&from)->sin6_family = AF_INET6;
    memcpy(&((struct sockaddr_in6*)&from)->sin6_addr, src_addr, 16);
    ((struct sockaddr_in6*)&from)->sin6_port = src_port;

    struct sockaddr_in6* addr_v6 = (struct sockaddr_in6*)&from;
    char str[INET6_ADDRSTRLEN]   = {0};
    inet_ntop(AF_INET6, &((struct sockaddr_in6*)&from)->sin6_addr, str, sizeof(str));

    real_ip_port.append(str);
    real_ip_port.append(":");
    real_ip_port.append(std::to_string(src_port));

    ((struct sockaddr_in6*)&to)->sin6_family = AF_INET6;
    memcpy(&((struct sockaddr_in6*)&to)->sin6_addr, dst_addr, 16);
    ((struct sockaddr_in6*)&to)->sin6_port = dst_port;
    addr_v6 = (struct sockaddr_in6*)&to;
    memset(str, 0, INET6_ADDRSTRLEN);
    inet_ntop(AF_INET6, &((struct sockaddr_in6*)&to)->sin6_addr, str, sizeof(str));

    cout << "Proxy protocol v2 ipv6: form(client_ip) " << real_ip_port << " to " << str << ":" << std::to_string(dst_port) << endl;

    return real_ip_port;
}

// PROXY protocol 协议 v1 版本获取用户ip地址
std::string ProxyProtocolGetClientIp_v1(char* line)
{
    std::string real_ip_port;

    char* p = strtok(line, " ");
    int i = 0;
    while (p)
    {
        // PROXY
        if (i == 0)
        {
        }
        // TCP4 or TCP6
        else if (i == 1)
        {
        }
        // client_ip
        else if (i == 2)
        {
            real_ip_port.append(p);
        }
        // server_ip
        else if (i == 3)
        {
        }
        // client_port
        else if (i == 4)
        {
            real_ip_port.append(":");
            real_ip_port.append(p);
        }
        // server_port
        else if (i == 5)
        {
        }
        p = strtok(NULL, " ");
        i++;
    }

    cout << "Proxy protocol v1 client_ip " << real_ip_port << endl;
    return real_ip_port;
}

// 处理 PROXY protocol 协议
int HandleProxyProtocol(const void *buff, std::size_t buff_size, std::string &real_ip_port)
{
    // 返回用户真实数据流的下标，去除 PROXY protocol 头
    int index = 0;
    int ret   = buff_size;

    const char v2sig[] = "\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A";
    union
    {
        struct
        {
            char line[108];
        } v1;
        struct
        {
            uint8_t sig[12];
            uint8_t ver_cmd;
            uint8_t fam;
            uint16_t len;
            union
            {
                struct
                { /* for TCP/UDP over IPv4, len = 12 */
                    uint32_t src_addr;
                    uint32_t dst_addr;
                    uint16_t src_port;
                    uint16_t dst_port;
                } ip4;
                struct
                { /* for TCP/UDP over IPv6, len = 36 */
                    uint8_t src_addr[16];
                    uint8_t dst_addr[16];
                    uint16_t src_port;
                    uint16_t dst_port;
                } ip6;
                struct
                { /* for AF_UNIX sockets, len = 216 */
                    uint8_t src_addr[108];
                    uint8_t dst_addr[108];
                } unx;
            } addr;
        } v2;
    } hdr;
    
    if (buff_size < sizeof(hdr.v2.addr.ip4))
    {
        // TODO 第一个代理协议包没有收取完成（v2.ipv4），直接断开连接
        return -1;
    }

    // 需要拷贝到 hdr 数据的长度，防止 buff 过长导致溢出
    int cpy_len = buff_size > sizeof(hdr) ? sizeof(hdr) : buff_size;
    memcpy(&hdr, buff, cpy_len);

    if (ret >= 16 && memcmp(&hdr.v2, v2sig, 12) == 0 && (hdr.v2.ver_cmd & 0xF0) == 0x20)
    {
        index = 16 + ntohs(hdr.v2.len);
        if (ret < index)
            return -1; /* truncated or too large header */

        switch (hdr.v2.ver_cmd & 0xF)
        {
        case 0x01:                 /* PROXY command */
            if (hdr.v2.fam & 0x11) /* TCPv4 */
            {
                real_ip_port = ProxyProtocolGetClientIp_ipv4_v2(hdr.v2.addr.ip4.src_addr, hdr.v2.addr.ip4.dst_addr, hdr.v2.addr.ip4.src_port, hdr.v2.addr.ip4.dst_port);
                cout << "v2 tcp v4:" << real_ip_port << endl;
                goto done;
            }
            else if (hdr.v2.fam & 0x21) /* TCPv6 */
            {
                real_ip_port = ProxyProtocolGetClientIp_ipv6_v2(hdr.v2.addr.ip6.src_addr, hdr.v2.addr.ip6.dst_addr, hdr.v2.addr.ip6.src_port, hdr.v2.addr.ip6.dst_port);
                cout << "v2 tcp v6:" << real_ip_port << endl;
                goto done;
            }
            else /* other protols close session*/
            {
                return -1;
            }
            /* unsupported protocol, keep local connection address */
            // break;
        case 0x00: /* LOCAL command */
            /* keep local connection address for LOCAL */
            return -1;
        default:
            return -1; /* not a supported command */
        }
    }
    else if (ret >= 8 && memcmp(hdr.v1.line, "PROXY", 5) == 0)
    {
        char* end = (char*)memchr(hdr.v1.line, '\r', ret - 1);
        if (!end || end[1] != '\n')
            return -1;                 /* partial or invalid header */
        *end  = '\0';                  /* terminate the string to ease parsing */
        index = end + 2 - hdr.v1.line; /* skip header + CRLF */
        /* parse the V1 header using favorite address parsers like inet_pton.
         * return -1 upon error, or simply fall through to accept.
         */
        cout << "v1 --> index:" << index << " str:" << hdr.v1.line << endl;
        real_ip_port = ProxyProtocolGetClientIp_v1(hdr.v1.line);
    }
    else
    {
        /* Without agency agreement*/
        /*没有经过代理协议的 正常的数据包*/
        cout << "without proxy protocol!" << endl;
        return 0;
    }

done:
    cout << "HandleProxyProtocol done!" << endl;

    cout << "HandleProxyProtocol index:" << index << " buffSize " << buff_size << " real_ip_port:" << real_ip_port << endl;
    return index;
}
