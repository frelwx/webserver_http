#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/epoll.h>
#include <exception>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "locker.h"
#include "thread_pool.h"
#include "http_conn.h"
constexpr int MAX_FD = 65535;
constexpr int MAX_EVENT_NUMBER = 1000;
/* 为信号sig_num注册处理函数 */
void add_sig(int sig_num, void(*handler)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig_num, &sa, NULL);
}

extern void add_fd(int epoll_fd, int fd, bool one_shot);
extern void remove_fd(int epoll_fd, int fd);
extern void modify_fd(int epoll_fd, int fd, int ev);
int main(int argc, char* argv[]) {
    if (argc <= 1) {
        printf("按照如下格式运行: %s port_number\n", basename(argv[0]));
        exit(-1);
    }
    int port = atoi(argv[1]);
    // SIGPIPE信号默认处理函数会终止进程，需要改成忽略
    add_sig(SIGPIPE, SIG_IGN);
    thread_pool<http_conn>* pool = NULL;
    try{
        pool = new thread_pool<http_conn>();
        printf("sssscnm\n");
    } catch(...) {
        // ...表示捕获任意类型的异常
        exit(-1);
    }
    
    http_conn* users = new http_conn[MAX_FD];
    // 协议族、传输方式、协议（IPV4、stream式传输放式，协议只有一种） PF_INET和AF_INET是同一个宏
    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    // 设置端口复用，否则服务器端口无法在time-wait期间复用
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // 设置地址
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    // 将字符串地址转换为32位大端序整数
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    // inet_aton("127.0.0.1", &address.sin_addr);
    printf("%s:%d\n", inet_ntoa(address.sin_addr), port);
    address.sin_port = htons(port);
    bind(listen_fd, (sockaddr*)&address, sizeof(address));

    // 监听，进入等待连接状态
    listen(listen_fd, 5);
    // 创建epoll
    epoll_event epoll_events[MAX_EVENT_NUMBER];
    int epoll_fd = epoll_create(998244353);
    http_conn::m_epoll_fd = epoll_fd;
    // 添加listen文件描述符到epoll
    add_fd(epoll_fd, listen_fd, false);
    while(1) {
        int num = epoll_wait(epoll_fd, epoll_events, MAX_EVENT_NUMBER, 0);
        if (num == -1) {
            perror("epoll_wait");
        }
        if (num !=0) {
            printf("num = %d\n", num);
        }
        for (int i = 0; i < num; ++i) {
            int sock_fd = epoll_events[i].data.fd;
            if (sock_fd == listen_fd) {
                // 有新连接
                struct sockaddr_in client_addr;
                socklen_t addr_len;
                int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &addr_len);
                if (http_conn::m_user_cnt >= MAX_FD) {
                    // 连接数满了
                    close(client_fd);
                    continue;
                }
                users[client_fd].init(client_fd, client_addr);
            } else if (epoll_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users[sock_fd].close_conn();
            } else if (epoll_events[i].events & EPOLLIN){
                if (users[sock_fd].read()) {
                    pool->append(&users[sock_fd]);
                } else {
                    users[sock_fd].close_conn();
                }
            } else if (epoll_events[i].events & EPOLLOUT) {
                if ( !users[sock_fd].write()) {
                    users[sock_fd].close_conn();
                }
            }
        }
    }
    close(epoll_fd);
    close(listen_fd);
    delete [] users;
}