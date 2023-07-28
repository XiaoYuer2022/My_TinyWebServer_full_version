#include "config.h"

Config::Config(){
    //端口号,默认9006
    PORT = 9006;

    //日志写入方式，默认同步   __为1表示同步写入日志，为0表示异步写入日志
    LOGWriteSYN = 1;
    
    //关闭日志,默认不关闭     __为1表示要写日志，为0表示不写日志
    write_log = 1;
    
    //开启debug模式 为1开启debug模式,为0关闭debug模式
    isDebug = 1;

    //优雅关闭链接，默认不使用   __为0表示不优雅的关闭链接，为1表示优雅地关闭链接
    OPT_LINGER = 1;

    //触发组合模式,默认listenfd LT + connfd LT
    TRIGMode = 3;        //__为0表示LT+LT ，为1表示LT+ET，为2表示ET+LT，为3表示ET+ET

    //数据库连接池数量,默认8
    sql_num = 8;

    //线程池内的线程数量,默认8
    thread_num = 8;

    //并发模型,默认是proactor __为0是proactor，为1是reactor
    actor_model = 1;
}
/**
 * 补充说明：listenmode: LT/ET : 主要在epoll处理的第一类事件：新到的用户连接，处理accept时用到；
 *          connectmode:LT/ET : 主要是在http的read_once()中用到，读取客户端发送的数据。
*/
void Config::parse_arg(int argc, char*argv[]){
    int opt;
    const char *str = "p:w:l:d:o:m:s:t:a:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            PORT = atoi(optarg);
            break;
        } 
        case 'w':
        {
            write_log = atoi(optarg);
            break;
        }
        case 'l':
        {
            LOGWriteSYN = atoi(optarg);
            break;
        }
        case 'd':
        {
            isDebug = atoi(optarg);
            break;
        }
        case 'o':
        {
            OPT_LINGER = atoi(optarg);
            break;
        }
        case 'm':
        {
            TRIGMode = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}