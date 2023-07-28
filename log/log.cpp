#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <sys/stat.h>
#include <pthread.h>
using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}
//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size)
{
    //如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    
    //strrchr函数返回 str 中最后一次出现字符 c 的位置。如果未找到该值，则函数返回一个空指针。
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};
    char catalogue_name[256]={0}; //定义和存储目录名

    //判断目录路径是绝对路径还是相对路径，并且如果路径不存在，要创建该路径
    /*
    if (p != NULL) { // 防止p为空指针
      strncpy(catalogue_name, file_name, p - file_name); // 复制子字符串到dir中
      catalogue_name[p-file_name] = '\0'; // 需要手动添加结束符
    } else {
      printf("没有找到斜杠,默认为当前目录，就无需检查目录是否存在了\n");
    }
    printf("路径名：%s\n",catalogue_name);
    //判断是绝对路径还是相对路径，但其实对于当前的需求（检查该目录是否存在，不存在就创建）而言，没必要
    if(catalogue_name[0] == '/' && p!=NULL) {
        //是绝对路径
        printf("是绝对路径\n");
    } else {
        printf("是相对路径\n");
    }
    struct stat file_buffer;
    if(stat(catalogue_name, &file_buffer) != 0) {
        
        printf("路径不存在\n");
        if (mkdir(catalogue_name, 0777) == -1) {
            printf("创建路径失败！\n");
            exit(1);
        } else {
            printf("路径已成功创建！\n");
        }
    }else {
        printf("路径存在\n");
    }
    */

    if (p != NULL) { // 防止p为空指针
        strncpy(catalogue_name, file_name, p - file_name); // 复制子字符串到dir中
        catalogue_name[p-file_name] = '\0'; // 需要手动添加结束符
        struct stat file_buffer;
        if(stat(catalogue_name, &file_buffer) != 0) {
            if (mkdir(catalogue_name, 0777) == -1) {
                perror("创建路径失败:");
                exit(1);
            } 
        }
    }
    //没有找到斜杠,默认为当前目录，就无需检查目录是否存在了\n");
    



    //如果init初始化函数的第一个参数file_name以纯文件名的形式给出，走if分支；如果以绝对路径给出，走else分支。
    if (p == NULL)
    {
        strcpy(log_name, file_name);
        snprintf(log_full_name, 300, "%d_%02d_%02d_%s.log", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        //从绝对路径中解析出日志文件名和目录名
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 300, "%s%d_%02d_%02d_%s.log", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[error]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    //写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++;

    //printf("[测试: m_today=%d,my_tm.tm_mday=%d]\n",m_today,my_tm.tm_mday);
    //要么是新的一天/要么是日志文件已经达到最大行数，就会创建新文件。一般情况的话，不会运行该段if代码
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //everyday log
    {
        //printf("[测试: 进入到if代码块了]\n");
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp); //关闭旧日志文件
        char tail[16] = {0};
       
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        //printf("[测试: tail=%s]\n",tail);
       //如果不是今天，就是新的一天，创建新日志文件
        if (m_today != my_tm.tm_mday)
        {
            //printf("[问题就出现在这-->]\n");
            //printf("[before: new_log=%s]\n",new_log);
            //printf("[测试：dir_name=%s,tail=%s,new_log=%s]\n",dir_name,tail,log_name);
            snprintf(new_log, 300, "%s%s%s.log", dir_name, tail, log_name);
            //printf("[after: new_log=%s]\n",new_log);
            m_today = my_tm.tm_mday;
            m_count = 0;
            //printf("[<--问题就出现在这]\n");
        }
        else
        {
            snprintf(new_log, 300, "%s%s%s_%lld.log", dir_name, tail, log_name, m_count / m_split_lines);
        }
        //printf("[测试: tail=%s,new_log=%s]\n",tail,new_log);
        m_fp = fopen(new_log, "a");
    }
 
    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

//为什么写完write_log后，要加一个fflush()呢？
/*
因为fput()和printf()函数不是真正的把数据输出到屏幕，
而是把输出送往缓存区，等其他线程来处理。
比如说：printf("hello"); sleep(5);printf("world");
和printf("hello");fflush(stdout)；sleep(5);printf("world");
两者的执行顺序不一样的。前者先执行sleep(5);在执行打印；后者是严格按照程序顺序来的。

加fflush的好处是，可以确保现有缓冲区的数据正确打印或存储到文件中，
这样的话，当系统突然崩溃时，就不会出现日志残缺的问题。
*/

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);    // 强制将缓冲区内的数据写入指定的文件
    m_mutex.unlock();
}
