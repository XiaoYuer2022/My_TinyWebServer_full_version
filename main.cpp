#include "config.h"
#include "log/log.h"
int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "yuhao";
    string passwd = "508506630.yh";
    string databasename = "mydb";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    //初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWriteSYN, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.write_log,config.isDebug,config.actor_model);
    

    //日志
    server.log_write();

    //数据库
    server.sql_pool();

    //线程池
    server.thread_pool();

    //触发模式
    server.trig_mode();

    //监听 socket套接字的初始化创建
    server.eventListen();
    //运行
    server.eventLoop();

    return 0;
}