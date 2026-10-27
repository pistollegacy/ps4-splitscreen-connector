/**
 * @file ps4_socket.c
 * @brief PS4 Socket Wrapper with UDP/TCP Support
 * 
 * Provides cross-platform socket abstraction optimized for PS4.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/net.h>
#include <ps4/netinet/in.h>
#include <ps4/netinet/tcp.h>
#include <ps4/arpa/inet.h>
#include <ps4/unistd.h>
#include <ps4/fcntl.h>
#include <ps4/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Socket Types
// -----------------------------------------------------------------------------
typedef enum {
    SOCKET_TYPE_UDP = 0,
    SOCKET_TYPE_TCP = 1,
} socket_type_t;

typedef struct {
    int fd;
    socket_type_t type;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    bool nonblocking;
    uint32_t send_buffer_size;
    uint32_t recv_buffer_size;
} ps4_socket_t;

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------
static int ps4_sockerr_to_errno(int sce_err) {
    // SCE net errors are negative errno values
    if (sce_err < 0) return -sce_err;
    return sce_err;
}

static void set_socket_buffers(int fd, uint32_t send_size, uint32_t recv_size) {
    if (send_size > 0) {
        sceNetSetsockopt(fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDBUF, &send_size, sizeof(send_size));
    }
    if (recv_size > 0) {
        sceNetSetsockopt(fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVBUF, &recv_size, sizeof(recv_size));
    }
}

static void set_nonblocking(int fd, bool nonblock) {
    int flags = sceNetFcntl(fd, SCE_NET_F_GETFL, 0);
    if (flags >= 0) {
        if (nonblock) flags |= SCE_NET_O_NONBLOCK;
        else flags &= ~SCE_NET_O_NONBLOCK;
        sceNetFcntl(fd, SCE_NET_F_SETFL, flags);
    }
}

static void disable_nagle(int fd) {
    int on = 1;
    sceNetSetsockopt(fd, SCE_NET_IPPROTO_TCP, SCE_NET_TCP_NODELAY, &on, sizeof(on));
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

ps4_socket_t* ps4_socket_create(socket_type_t type, uint32_t send_buf, uint32_t recv_buf) {
    int domain = SCE_NET_AF_INET;
    int sock_type = (type == SOCKET_TYPE_UDP) ? SCE_NET_SOCK_DGRAM : SCE_NET_SOCK_STREAM;
    int protocol = (type == SOCKET_TYPE_UDP) ? SCE_NET_IPPROTO_UDP : SCE_NET_IPPROTO_TCP;

    int fd = sceNetSocket("ps4splitscreen", domain, sock_type, protocol);
    if (fd < 0) return NULL;

    ps4_socket_t* sock = calloc(1, sizeof(ps4_socket_t));
    if (!sock) {
        sceNetSocketClose(fd);
        return NULL;
    }

    sock->fd = fd;
    sock->type = type;
    sock->send_buffer_size = send_buf;
    sock->recv_buffer_size = recv_buf;
    sock->nonblocking = false;

    set_socket_buffers(fd, send_buf, recv_buf);
    
    if (type == SOCKET_TYPE_TCP) {
        disable_nagle(fd);
    }

    return sock;
}

void ps4_socket_destroy(ps4_socket_t* sock) {
    if (!sock) return;
    if (sock->fd >= 0) {
        sceNetSocketClose(sock->fd);
    }
    free(sock);
}

int ps4_socket_bind(ps4_socket_t* sock, uint16_t port, const char* bind_ip) {
    if (!sock) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    memset(&sock->local_addr, 0, sizeof(sock->local_addr));
    sock->local_addr.sin_family = SCE_NET_AF_INET;
    sock->local_addr.sin_port = sceNetHtons(port);
    
    if (bind_ip && strlen(bind_ip) > 0) {
        sceNetInetPton(SCE_NET_AF_INET, bind_ip, &sock->local_addr.sin_addr);
    } else {
        sock->local_addr.sin_addr.s_addr = SCE_NET_INADDR_ANY;
    }

    int ret = sceNetBind(sock->fd, (struct sockaddr*)&sock->local_addr, sizeof(sock->local_addr));
    return ret < 0 ? ps4_sockerr_to_errno(ret) : PS4SPLITSCREEN_OK;
}

int ps4_socket_connect(ps4_socket_t* sock, const char* ip, uint16_t port, uint32_t timeout_ms) {
    if (!sock || sock->type != SOCKET_TYPE_TCP) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    memset(&sock->remote_addr, 0, sizeof(sock->remote_addr));
    sock->remote_addr.sin_family = SCE_NET_AF_INET;
    sock->remote_addr.sin_port = sceNetHtons(port);
    sceNetInetPton(SCE_NET_AF_INET, ip, &sock->remote_addr.sin_addr);

    // Set non-blocking for timeout
    bool was_nonblocking = sock->nonblocking;
    if (!was_nonblocking) {
        set_nonblocking(sock->fd, true);
        sock->nonblocking = true;
    }

    int ret = sceNetConnect(sock->fd, (struct sockaddr*)&sock->remote_addr, sizeof(sock->remote_addr));
    
    if (ret < 0) {
        int err = ps4_sockerr_to_errno(ret);
        if (err == SCE_NET_EINPROGRESS || err == SCE_NET_EALREADY) {
            // Wait for connection with timeout
            struct pollfd pfd = { .fd = sock->fd, .events = SCE_NET_POLLOUT };
            ret = sceNetPoll(&pfd, 1, timeout_ms);
            if (ret > 0) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                sceNetGetsockopt(sock->fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &so_error, &len);
                if (so_error == 0) {
                    ret = PS4SPLITSCREEN_OK;
                } else {
                    ret = -so_error;
                }
            } else if (ret == 0) {
                ret = -SCE_NET_ETIMEDOUT;
            } else {
                ret = -SCE_NET_ECONNREFUSED;
            }
        }
    }

    // Restore blocking mode
    if (!was_nonblocking) {
        set_nonblocking(sock->fd, false);
        sock->nonblocking = false;
    }

    return ret < 0 ? ps4_sockerr_to_errno(ret) : PS4SPLITSCREEN_OK;
}

int ps4_socket_set_remote(ps4_socket_t* sock, const char* ip, uint16_t port) {
    if (!sock || sock->type != SOCKET_TYPE_UDP) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    memset(&sock->remote_addr, 0, sizeof(sock->remote_addr));
    sock->remote_addr.sin_family = SCE_NET_AF_INET;
    sock->remote_addr.sin_port = sceNetHtons(port);
    sceNetInetPton(SCE_NET_AF_INET, ip, &sock->remote_addr.sin_addr);

    return PS4SPLITSCREEN_OK;
}

int ps4_socket_send(ps4_socket_t* sock, const void* data, size_t size) {
    if (!sock || !data || size == 0) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    if (size > PS4SPLITSCREEN_MAX_PAYLOAD_SIZE) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    ssize_t sent = 0;
    if (sock->type == SOCKET_TYPE_UDP) {
        sent = sceNetSendto(sock->fd, data, size, 0, 
                           (struct sockaddr*)&sock->remote_addr, sizeof(sock->remote_addr));
    } else {
        sent = sceNetSend(sock->fd, data, size, 0);
    }

    return sent < 0 ? ps4_sockerr_to_errno(sent) : (int)sent;
}

int ps4_socket_sendto(ps4_socket_t* sock, const void* data, size_t size, const char* ip, uint16_t port) {
    if (!sock || sock->type != SOCKET_TYPE_UDP) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    struct sockaddr_in dest = {0};
    dest.sin_family = SCE_NET_AF_INET;
    dest.sin_port = sceNetHtons(port);
    sceNetInetPton(SCE_NET_AF_INET, ip, &dest.sin_addr);

    ssize_t sent = sceNetSendto(sock->fd, data, size, 0, (struct sockaddr*)&dest, sizeof(dest));
    return sent < 0 ? ps4_sockerr_to_errno(sent) : (int)sent;
}

int ps4_socket_recv(ps4_socket_t* sock, void* buffer, size_t max_size, struct sockaddr_in* out_src) {
    if (!sock || !buffer || max_size == 0) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    socklen_t addr_len = out_src ? sizeof(*out_src) : 0;
    ssize_t received = 0;

    if (sock->type == SOCKET_TYPE_UDP) {
        received = sceNetRecvfrom(sock->fd, buffer, max_size, 0, 
                                  out_src ? (struct sockaddr*)out_src : NULL, 
                                  out_src ? &addr_len : NULL);
    } else {
        received = sceNetRecv(sock->fd, buffer, max_size, 0);
    }

    return received < 0 ? ps4_sockerr_to_errno(received) : (int)received;
}

int ps4_socket_set_nonblocking(ps4_socket_t* sock, bool nonblock) {
    if (!sock) return PS4SPLITSCREEN_ERR_INVALID_PARAM;
    
    set_nonblocking(sock->fd, nonblock);
    sock->nonblocking = nonblock;
    return PS4SPLITSCREEN_OK;
}

int ps4_socket_get_fd(const ps4_socket_t* sock) {
    return sock ? sock->fd : -1;
}

bool ps4_socket_is_connected(const ps4_socket_t* sock) {
    if (!sock) return false;
    if (sock->type == SOCKET_TYPE_UDP) return true; // UDP is "connectionless"
    
    int error = 0;
    socklen_t len = sizeof(error);
    int ret = sceNetGetsockopt(sock->fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &error, &len);
    return ret >= 0 && error == 0;
}

// -----------------------------------------------------------------------------
// Poll/Select Helpers
// -----------------------------------------------------------------------------

int ps4_socket_poll(ps4_socket_t** sockets, int count, int timeout_ms, bool* out_readable, bool* out_writable) {
    if (!sockets || count <= 0) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    struct pollfd* pfds = alloca(count * sizeof(struct pollfd));
    for (int i = 0; i < count; i++) {
        pfds[i].fd = sockets[i]->fd;
        pfds[i].events = 0;
        if (out_readable) pfds[i].events |= SCE_NET_POLLIN;
        if (out_writable) pfds[i].events |= SCE_NET_POLLOUT;
    }

    int ret = sceNetPoll(pfds, count, timeout_ms);
    if (ret < 0) return ps4_sockerr_to_errno(ret);

    if (out_readable || out_writable) {
        for (int i = 0; i < count; i++) {
            if (out_readable) out_readable[i] = (pfds[i].revents & SCE_NET_POLLIN) != 0;
            if (out_writable) out_writable[i] = (pfds[i].revents & SCE_NET_POLLOUT) != 0;
        }
    }

    return ret; // Number of ready sockets
}