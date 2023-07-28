#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port , string user, string passWord, string databaseName,
              int logwritesyn , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int isDebug, int actor_model);

    //这几个函数在main函数中被实例化的WebServer对象调用
    void log_write();
    void sql_pool();
    void thread_pool();
    void trig_mode();
    void eventListen();
    void eventLoop(); //主程序中的while死循环，epoll_wait等待五类事件的处理

    //定时器相关的函数
    void timer(int connfd, struct sockaddr_in client_address); //在事件1处理新用户连接中被调用，事件1的子事件
    void adjust_timer(util_timer *timer); //在事件4和5读写事件中被调用 若有数据传输，则将定时器往后延迟3个单位
    
    //五类事件
    bool dealclientdata();//事件1:处理新到的客户连接
    void deal_timer(util_timer *timer, int sockfd);//事件2:处理对端关闭或者fd被挂断，移除对应的定时器
    bool dealwithsignal(bool& timeout, bool& stop_server);//事件3:处理pipefd里面的信号事件
    void dealwithread(int sockfd);//事件4:处理收到的数据
    void dealwithwrite(int sockfd);//事件5:处理发送的数据

//主要有四个池：http处理池、数据库池、线程池、定时器池
/*
最大的是线程池，每个线程中包含一个http处理对象，每个http处理对象需要一个对应的mysql对象。线程池（8个）>http服务>数据库对象（8个）
*/
public:
    //基础
    int m_port;
    char *m_root;
    int m_log_write_syn;//为1表示同步写入日志，为0表示异步写入日志
    int m_write_log;//为1表示要写日志，为0表示不写日志
    int m_actormodel; //0为proactor并发模式，1为reactor并发模式
    int m_is_debug; //为1则开启debug模式

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    //数据库相关
    connection_pool *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER;//为0表示不优雅的关闭链接，为1表示优雅地关闭链接
    int m_TRIGMode;//为0表示listenfd LT + connfd LT ，为1表示LT+ET，为2表示ET+LT，为2表示ET+ET
    int m_LISTENTrigmode; //处理新到的用户连接时：为0表示水平触发，不需要一次性读完；为1表示边沿触发，必须搭配while死循环使用，一次性读完
    int m_CONNTrigmode;//在proactor并发模式下，处理http的接收用户数据时：为0表示水平触发，不需要一次性读完；为1表示边沿触发，必须搭配while死循环使用，一次性读完

    //定时器相关
    client_data *users_timer;
    Utils utils;
};
#endif
