#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <sys/socket.h>
#include <arpa/inet.h>
class http_conn {
public:
    // http请求方法
    enum METHOD {GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /*
    解析客户端请求时，主状态机的状态
    CHECK_STATE_REQUESTLINE：正在解析请求体
    CHECK_STATE_HEADER：正在解析请求头
    CHECK_STATE_BODY：正在解析请求体
     */
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE, CHECK_STATE_HEADER, CHECK_STATE_BODY};

    /*
    从状态机的可能状态
    LINE_OK：读取到一个完整的行
    LINE_BAD：行数据错误
    LINE_OPEN：行数据不完整
    */
    enum LINE_STATUS {LINE_OK, LINE_BAD, LINE_OPEN};

     /*
     服务器解析HTTP请求的结果
     NO_REQUEST：请求不完整，还需要继续读取客户端资源
     GET_REQUEST：读取到了完整的客户端请求
     BAD_REQUEST：客户端请求存在语法错误
     NO_RESOURCE：服务器没有对应资源
     FORBIDDEN_REQUEST：客户对资源没有足够的权限
     FILE_REQUEST：这是一个文件请求，获取文件成功
     INTERNAL_ERROR：服务器内部错误
     CLOSED_CONNECTION：客户端已经关闭连接
     */
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    static int m_epoll_fd;
    static int m_user_cnt;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 2048;
    http_conn(){}
    ~http_conn(){}
    void init(int client_fd, const struct sockaddr_in &client_addr); // 初始化新的客户端连接， 保存客户端socket文件描述符和地址，并添加到epoll中，设置socket为非阻塞
    
    void process(); // 解析http请求，生成响应报文
    void close_conn();
    bool read();
    bool write();

private:
    int m_sock_fd; // 客户端socket文件描述符
    struct sockaddr_in m_address; // 客户端地址
    char m_read_buf[READ_BUFFER_SIZE]; // 读缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
    int m_read_num; // 已经读了多少个字节
    int m_check_idx; // 当前访问到读缓冲区的哪个位置
    int m_line_idx; // 当前解析行的起始位置
    char* m_url; // 请求路径
    char* m_version; // HTTP版本
    char* m_host;
    bool m_keep_alive;
    METHOD m_method; // 请求方法

    CHECK_STATE m_check_state; // 主状态机当前所处的状态
    // 解析HTTP请求
    HTTP_CODE process_read();
    // 解析HTTP请求行， 获得请求方法，目标URL，HTTP版本:GET /index.html HTTP/1.1
    HTTP_CODE parse_http_request_line(char* text);
    // 解析HTTP请求头
    HTTP_CODE parse_http_header(char* text);
    // 解析HTTP请求体
    HTTP_CODE parse_http_body(char* text);
    // 解析一行，判断依据\r\n
    LINE_STATUS parse_line();
    HTTP_CODE do_request();
    void init();
};
/*添加文件描述符*/
void add_fd(int epoll_fd, int fd, bool one_shot);
/*删除文件描述符*/
void remove_fd(int epoll_fd, int fd);
/*重置EPOLLONESHOT*/
void modify_fd(int epoll_fd, int fd, int ev);
#endif