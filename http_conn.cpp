#include "http_conn.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstdio>
#include <errno.h>
#include <cstring>
int http_conn::m_epoll_fd = -1;
int http_conn::m_user_cnt = 0;
void set_nonblock(int fd) {
    int flag = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}
void add_fd(int epoll_fd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    // 对方断开连接会同时触发EPOLLIN和EPOLLRDHUP，不需要特判接收到0字节表示断开
    event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot) {
        event.events |= EPOLLONESHOT; 
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    set_nonblock(fd);
}
void remove_fd(int epoll_fd, int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}
void modify_fd(int epoll_fd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLRDHUP | EPOLLONESHOT;

    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}
void http_conn::init(int client_fd, const struct sockaddr_in &client_addr) {
    m_sock_fd = client_fd;
    m_address = client_addr;
    int reuse = 1;
    setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    add_fd(m_epoll_fd, client_fd, true);
    m_user_cnt++;

    init();
}
void http_conn::init() {
    memset(m_read_buf, 0, sizeof(m_read_buf));
    memset(m_write_buf, 0, sizeof(m_read_buf));
    m_read_num = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_check_idx = 0;
    m_line_idx = 0;
    m_url = nullptr;
    m_version = nullptr;
    m_method = GET;
    m_host = nullptr;
    m_keep_alive = false;
}
void http_conn::close_conn() {
    if (m_sock_fd != -1) {
        remove_fd(m_epoll_fd, m_sock_fd);
        m_sock_fd = -1;
        m_user_cnt--;
        printf("closed!\n");
    }
}
bool http_conn::read() {
    if (m_read_num >= READ_BUFFER_SIZE) {
        return false;
    }
    int len = 0;
    while(1) {
        len = recv(m_sock_fd, m_read_buf + m_read_num, READ_BUFFER_SIZE - m_read_num, 0);
        if (len == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据已经读完了
                break;
            }
            // 出错
            return false;
        } else if (len == 0) {
            // 对方关闭了连接，收到EOF
            return false;
        } else if (len > 0) {
            m_read_num += len;
        }
    }
    printf("读到了数据:\n%s\n", m_read_buf);
    return true;
}
bool http_conn::write() {
    printf("一次性写完\n");
    return true;
}
void http_conn::process() {
    // 解析http请求
    HTTP_CODE res = process_read();
    if (res == NO_REQUEST) {
        modify_fd(m_epoll_fd, m_sock_fd, EPOLLIN);
        return;
    }
}
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = nullptr;
    while((line_status == parse_line()) == LINE_OK || (m_check_state == CHECK_STATE_BODY && line_status == LINE_OK)) {
        // 解析到了完整的一行数据或者请求体
        text = &m_read_buf[m_line_idx];
        m_line_idx = m_check_idx;
        printf("got 1 http line %s\n", text);
        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_http_request_line(text);
                if (ret == BAD_REQUEST) {
                    return ret;
                }
                break;
            } case CHECK_STATE_HEADER: {
                ret = parse_http_header(text);
                if (ret == BAD_REQUEST) {
                    return ret;
                } else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            } case CHECK_STATE_BODY: {
                ret = parse_http_body(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            } default: {
                return INTERNAL_ERROR;
            }
        }
        return NO_REQUEST;
    }
}

http_conn::HTTP_CODE http_conn::parse_http_request_line(char* text){
    // [GET /index.html HTTP/1.1]
    // 返回字符串 str1中第一个匹配字符串 str2中字符的字符，不包含'\0'
    m_url = strpbrk(text, " \t");
    // GET\0/index.html HTTP/1.1 把GET取出来
    *m_url = '\0';
    ++m_url;
    char* method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version = '\0';
    ++m_version;
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_http_header(char* text){
    
}
http_conn::HTTP_CODE http_conn::parse_http_body(char* text){
    
}
http_conn::LINE_STATUS http_conn::parse_line(){
    char tmp;
    for(; m_check_idx < m_read_num; ++m_check_idx) {
        if (m_read_buf[m_check_idx] == '\r') {
            if (m_check_idx + 1 == m_read_num) {
                return LINE_OPEN;
            } else if (m_read_buf[m_check_idx + 1] == '\n') {   
                // 把\r\n变成\0
                m_read_buf[m_check_idx] = '\0';
                m_read_buf[m_check_idx + 1] = '\0';
                m_check_idx += 2;
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (m_read_buf[m_check_idx] == '\n') {
            if (m_check_idx > 1 && m_read_buf[m_check_idx - 1] == '\r') {
                m_read_buf[m_check_idx - 1] = '\0';
                m_read_buf[m_check_idx] = '\0';
                ++m_check_idx;
                return LINE_OK;
            }
            return LINE_BAD;
        } 
    }
    return LINE_OPEN;
}
http_conn::HTTP_CODE http_conn::do_request() {

}