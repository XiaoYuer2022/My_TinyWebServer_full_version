#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
    //C++11以后,使用局部变量懒汉不用加锁
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
        //问题1：针对单例模式，可以1）用static创建静态对象成员；也可以2）用静态成员指针和new来创建唯一实例
        //但问题在于，用new的话处于手动在程序末尾加上delete，否则会内存泄漏。
        //不如就使用静态对象成员(例如本代码)，生命周期是整个程序执行完，系统自动释放。

        /*问题2：返回为一个地址就需要一个指针去接收，如果用户用delete去处理就会造成段错误。所以如果
        *   函数返回一个引用会比较好，这样delete就会失效。此时注意，接收必须用引用，而不能是对象，用对象接收时相当于一个拷贝构造。
        *   可以考虑：1）对拷贝构造函数私有化；2）禁用拷贝构造函数；3）或者禁用重载运算符
        *   参考链接：https://blog.csdn.net/zhaojiazb/article/details/130210056
        *   禁用拷贝构造函数：Log(Log& obj) = delete;
        */
    }

    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
        return NULL;
    }
    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int is_debug, int write_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);

private:
    Log();
    virtual ~Log();
    void *async_write_log()
    {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
        return NULL;
    }

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;
    block_queue<string> *m_log_queue; //阻塞队列
    bool m_is_async;                  //是否同步标志位
    locker m_mutex;
public:
    int m_write_log; //__为0表示要不写日志，为1表示写日志
    int m_is_debug;  //__为0表示关闭debug模式，为1表示开启debug模式
};

#define LOG_DEBUG(format, ...) if(0 == Log::get_instance()->m_write_log) {} else{if(0 == Log::get_instance()->m_is_debug){} else {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}}
#define LOG_INFO(format, ...) if(0 == Log::get_instance()->m_write_log)  {} else{Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == Log::get_instance()->m_write_log)  {} else{Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == Log::get_instance()->m_write_log) {} else{Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif
