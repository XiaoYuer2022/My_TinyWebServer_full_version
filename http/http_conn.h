#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    //检查状态、检查头、检查内容
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    //代码中解析部分只实现了4种状态（特别标注部分），处理结果部分实现了7种
    enum HTTP_CODE
    {
        NO_REQUEST, //【解析】：请求不完整，需要继续读取请求报文数据：跳转主线程继续监测读事件
        GET_REQUEST,//【解析】：获得了完整的HTTP请求：调用do_request完成请求资源映射
        BAD_REQUEST,//【解析】：HTTP请求报文有语法错误:跳转process_write完成响应报文
        NO_RESOURCE,//请求资源不存在:跳转process_write完成响应报文
        FORBIDDEN_REQUEST,//请求资源禁止访问，没有读取权限:跳转process_write完成响应报文
        FILE_REQUEST,//请求资源可以正常访问:跳转process_write完成响应报文
        INTERNAL_ERROR,//【解析】：服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    //初始化连接,外部调用初始化套接字地址
    void init(int sockfd, const sockaddr_in &addr, char *, int, string user, string passwd, string sqlname);
    //关闭连接，关闭一个连接，客户总量减一
    void close_conn(bool real_close = true);
    //调用process_read和process_write 该函数被线程自动调用
    void process();
    //循环读取客户数据，直到无数据可读或对方关闭连接 webserver.cpp调用
    bool read_once();
    //webserver.cpp调用
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;


private:
    void init();//完成对变量的初始化。init(*)和write()要调用它
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();
    void unmap();

    /*
    add_response是一个子函数，
    被add_status_line()、add_content_length()、add_content_type()、
    add_linger()、add_blank_line()、add_content(const char *content)所调用
    */
    bool add_response(const char *format, ...);

    //以下6个函数全是单行函数，函数体只有add_response()一个函数
    bool add_status_line(int status, const char *title);
    bool add_content_length(int content_length);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char *content);

    /**
     * //add_headers（）这个函数就3行，分别调用了
     * //add_content_length(content_len)、
     * //add_linger()、add_blank_line()
    */
    bool add_headers(int content_length);

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];
    long m_read_idx;
    long m_checked_idx;
    int m_start_line;
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    CHECK_STATE m_check_state;
    METHOD m_method;
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    long m_content_length;
    bool m_linger;
    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send;
    int bytes_have_send;
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;//在proactor并发模式下，处理http的接收用户数据时：为0表示水平触发，不需要一次性读完；为1表示边沿触发，必须搭配while死循环使用，一次性读完
    // int m_close_log;
    // int m_is_debug;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
