#include "httpd.h"


#define INVALID_PORT        (uint16_t)(-1)

ret_code_t network_init()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    return SUCC;
}

ret_code_t network_unint()
{
    WSACleanup();
    return SUCC;
}

ret_code_t network_listen(uint16_t *port, SOCKET *fd)
{
    struct sockaddr_in addr;
    BOOL optval = TRUE;
    int ret;

    *fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (INVALID_SOCKET == *fd)
    {
        log_error("{%s:%d} create socket fail. WSAGetLastError=%d", __FUNCTION__, __LINE__, WSAGetLastError());
        exit(1);
    }

    ret = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
    if (SOCKET_ERROR == ret)
    {
        closesocket(*fd);
        log_error("{%s:%d} setsockopt fail. WSAGetLastError=%d", __FUNCTION__, __LINE__, WSAGetLastError());
        exit(1);
    }

    do 
    {
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(*port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        ret = bind(*fd, (struct sockaddr*)&addr, sizeof(addr));
        if (SOCKET_ERROR == ret)
        {
            (*port)++;
        }
        else
        {
            break;
        }
    } while (*port < INVALID_PORT);

    if (INVALID_PORT == *port)
    {
        *port = 80;
        closesocket(*fd);
        log_error("{%s:%d} bind fail. WSAGetLastError=%d", __FUNCTION__, __LINE__, WSAGetLastError());
        exit(1);
    }

    ret = listen(*fd, SOMAXCONN);
    if (SOCKET_ERROR == ret)
    {
        closesocket(*fd);
        log_error("{%s:%d} listen fail. WSAGetLastError=%d", __FUNCTION__, __LINE__, WSAGetLastError());
        exit(1);
    }

    log_info("{%s:%d} Running on http://%s:%d/ success, socket = %d", __FUNCTION__, __LINE__, inet_ntoa(addr.sin_addr), *port, *fd);
    return SUCC;
}

ret_code_t network_accept(SOCKET sfd, struct in_addr* addr, SOCKET *cfd)
{
    struct sockaddr_in addr_;
    int len = sizeof(addr_);

    *cfd = accept(sfd, (struct sockaddr*)&addr_, &len);
    if (INVALID_SOCKET == *cfd)
    {
        log_error("{%s:%d} accept fail. WSAGetLastError=%d", __FUNCTION__, __LINE__, WSAGetLastError());
        return FAIL;
    }
    *addr = addr_.sin_addr;
    log_info("{%s:%d} An new client connect. ip = %s, socket=%d", __FUNCTION__, __LINE__, inet_ntoa(addr_.sin_addr), *cfd);
    return SUCC;
}

ret_code_t network_read(SOCKET fd, void *buf, int32_t size)
{
    int ret = 0;
    int offset = 0;

    while (TRUE)
    {
        ret = recv(fd, buf, size-offset, 0);
        if (ret == SOCKET_ERROR)
        {
            log_error("{%s:%d} recv fail. socket=%d WSAGetLastError=%d", __FUNCTION__, __LINE__, fd, WSAGetLastError());
            return DISC;
        }
        else if (ret == 0) // the connection has been gracefully closed
        {
            log_info("{%s:%d} Disconnect. socket=%d", __FUNCTION__, __LINE__, fd);
            return DISC;
        }
        else if (ret+offset == size)
        {
            return SUCC;
        }
        else if (ret+offset < size)
        {
            offset += ret;
            log_debug("{%s:%d} Continue to read. socket=%d process:%d%%", __FUNCTION__, __LINE__, fd, offset*100/size);
            continue;
        }
        else
        {
            log_error("{%s:%d} unknown fail. socket=%d", __FUNCTION__, __LINE__, fd);
            break;
        }
    }
    return FAIL;
}

ret_code_t network_write(SOCKET fd, void *buf, uint32_t size)
{
    if (size != send(fd, buf, size, 0))
    {
        log_error("{%s:%d} send fail. socket=%d, WSAGetLastError=%d", __FUNCTION__, __LINE__, fd, WSAGetLastError());
        return FAIL;
    }

    return SUCC;
}