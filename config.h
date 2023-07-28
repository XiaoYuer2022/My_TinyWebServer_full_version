#ifndef WEBSERVERCONFIG_H_
#define WEBSERVERCONFIG_H_

/*原http_conn.cpp里面的配置*/
//#define connfdET //边缘触发非阻塞
#define connfdLT //水平触发阻塞

#define listenfdET //边缘触发非阻塞
// #define listenfdLT //水平触发阻塞

/*原main.cpp里面的配置 两个都不选中，表示不开启日志功能*/
#define SYNLOG      //同步模式，必须先写完日志才能进行后续操作；
// #define ASYNLOG   //异步模式，服务器操作与日志写入在不同线程，使用阻塞队列等线程同步机制实现日志数据的传输


#define ServerPort 9999

//main函数中数据库信息的配置
#define mysql_url "localhost"
#define mysql_user "yuhao"
#define mysql_passwd "508506630.yh"
#define mysql_DBname "mydb"
#define mysql_port 3380
#define mysql_MaxConn 8

/*原log.h里面的配置*/
#define DEBUG_VERSION 1 //为1表示允许进行debug调试


//原http_conn.cpp里面的配置
//当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
#define doc_root  "root" //现在该路径有getcwd()函数自动获得

void Config_Setting();

#endif
