#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h> 
#include <assert.h>  //做检查表达式之用
#include <sys/epoll.h> //在旧内核中用select来做事件触发，新内核可以采用epoll

//select通过轮询来处理事件，且最多只能处理1024个fd事件。
//epoll只有三个函数;epoll_create(int size); int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);  
//epoll_create()创建的红黑树的监听节点数量
//epoll_ctl(),第一个参数epfd是epoll_create()的返回值，op指操作类型：
    //EPOLL_CTL_ADD 添加fd到监听红黑树epfd，往事件表中注册事件
    //EPOLL_CTL_MOD 修改epfd上的注册事件属性
    //EPOLL_CTL_DEL 从红黑树epfd删除注册事件，取消监听
//fd是待监听的fd
//*event是个结构体指针，结构体嵌套联合体，即该结构体有一个数据(数据是一个联合体)+一个事件标志量构成。
/*
这个事件的取=取值主要有：
EPOLLIN    文件描述符中有数据可以读取（事件）
EPOLLOUT    文件描述符可以写入数据（事件）
EPOLLPRI   表示对应的文件描述符有紧急的数据可读
EPOLLERR   表示对应的文件描述符发生错误
EPOLLHUP   表示对应的文件描述符被挂断
EPOLLRDHUP (since Linux 2.6.17)  对端关闭连接（事件）
EPOLLET   使用边沿触发（即仅在事件发生（例如有数据可读）时epoll_wait返回一次，即时数据没有读取完，
        第二次调用epoll_wait时仍然阻塞）。默认状态下epoll使用水平触发，即如果有文件描述符的数据没有读取完则每次调用epoll_wait均不会阻塞。
EPOLLONESHOT (since Linux 2.6.2) 该fd仅生效一次。只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到epoll队列里

 */
//数据为触发的事件类型，联合体为文件描述符fd+用户数据*ptr。
//int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);


//LT和ET : 低电平触发和边沿触发。指LT指明只要有数据来，就会触发epoll_wait()，如果有数据，但你没处理。下次epool_wait()仍会触发。
//ET：当事件从无到有时，才会触发。
//select和epoll只能工作在低效率的LT状态，但是epoll可以选择工作在ET状态。
#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./timer/lst_timer.h"
#include "./http/http_conn.h"
#include "./log/log.h"
#include "./CGImysql/sql_connection_pool.h"
#include "config.h"


#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define TIMESLOT 5             //最小超时单位

/*
EPOLLONESHOT介绍
1、一个线程读取某个socket上的数据后开始处理数据，在处理过程中该socket上又有新数据可读，
此时另一个线程被唤醒读取，此时出现两个线程处理同一个socket
2、我们期望的是一个socket连接在任一时刻都只被一个线程处理，通过epoll_ctl对该文件描述符
注册epolloneshot事件，一个线程处理socket时，其他线程将无法处理，当该线程处理完后，需要通过epoll_ctl重置epolloneshot事件
*/


//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);//针对connfd，开启EPOLLONESHOT，因为我们希望每个socket在任意时刻都只被一个线程处理
extern int removefd(int epollfd, int fd);
extern int setnonblocking(int fd);

//全局变量定义区
static int pipefd[2];
static sort_timer_lst timer_lst;//设置定时器相关参数
static int epollfd = 0;
volatile sig_atomic_t flagCtrlC = 0;//我的信号捕捉测试标志量




static void my_handler(int sig){ // can be called asynchronously
  flagCtrlC = 1; // set flagCtrlC
}
//信号处理函数
void sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);
}

//定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(client_data *user_data)
{
    assert(user_data);
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    close(user_data->sockfd);
    http_conn::m_user_count--;
    LOG_INFO("close fd %d", user_data->sockfd);
}

void show_error(int connfd, const char *info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}


// void Config_Setting()
// {
// 	FILE* fp;
// 	//读取配置文件
// 	mxml_node_t *xml, *sys, *title;
// 	fp = fopen("config.xml","r");
// 	if(fp == NULL)
// 	{
// 		printf("open obu.xml failed! ---- %s::%s->%d", __FILE__, __FUNCTION__, __LINE__);
// 		exit(1);
// 	}
// 	xml = mxmlLoadFile( NULL, fp, MXML_TEXT_CALLBACK);
// 	fclose(fp);
// 	// SYS 系统基本配置
// 	sys = mxmlFindElement( xml, xml, "SYS", NULL, NULL, MXML_DESCEND);	
// 	title = mxmlFindElement(sys, xml, "ServerPort", NULL, NULL, MXML_DESCEND);
// 	if(mxmlGetText( title, NULL) != NULL) {
//         printf("设置端口：port=%d\n",atoi(mxmlGetText(title, NULL)));
//         port = atoi(mxmlGetText(title, NULL));
//     }
		
	
// 	// LOG 日志属性配置
// 	sys = mxmlFindElement( xml, xml, "LOG", NULL, NULL, MXML_DESCEND);
// 	title = mxmlFindElement( sys, xml, "ASYNLOG", NULL, NULL, MXML_DESCEND);
// 	if(mxmlGetText( title, NULL) != NULL && atoi(mxmlGetText( title, NULL)) != 0 ) {
// 		printf(" 已设置为异步日志\n");
// 		//#define ASYNLOG   //异步模式，服务器操作与日志写入在不同线程，使用阻塞队列等线程同步机制实现日志数据的传输
//         const int asynlog = 1;
// 	} else {
// 		printf("已设置为边缘触发\n");
// 		//#define SYNLOG      //同步模式，必须先写完日志才能进行后续操作；
//         const int synlog = 1;
// 	}
// 	title = mxmlFindElement( sys, xml, "DEBUG_OPEN", NULL, NULL, MXML_DESCEND);
// 	if(mxmlGetText( title, NULL) != NULL && atoi(mxmlGetText( title, NULL)) != 0 ) {
// 		printf("已设置为debug模式\n");
// 		#define DEBUG_VERSION   
// 	} else {
// 		printf("release模式\n");
// 	}

// 	// MYSQL 数据库属性配置
// 	sys = mxmlFindElement( xml, xml, "MYSQL", NULL, NULL, MXML_DESCEND);
// 	title = mxmlFindElement( sys, xml, "mysql_url", NULL, NULL, MXML_DESCEND);
// 	if(mxmlGetText( title, NULL) != NULL) {
// 		printf("mysql_url=%s\n",(mxmlGetText( title, NULL)));
//         #define mysql_url (mxmlGetText( title, NULL))
// 	} 
// 	title = mxmlFindElement( sys, xml, "mysql_user", NULL, NULL, MXML_DESCEND);
// 	if(mxmlGetText( title, NULL) != NULL) {
// 		printf("mysql_user=%s\n",(mxmlGetText( title, NULL)));
//         #define mysql_user (mxmlGetText( title, NULL))
// 	}
// 	title = mxmlFindElement( sys, xml, "mysql_passwd", NULL, NULL, MXML_DESCEND);
// 	if(mxmlGetText( title, NULL) != NULL) {
// 		printf("mysql_passwd=%s\n",(mxmlGetText( title, NULL)));
//         #define mysql_passwd (mxmlGetText( title, NULL))
// 	}
// 	title = mxmlFindElement( sys, xml, "mysql_DBname", NULL, NULL, MXML_DESCEND);
// 	if(mxmlGetText( title, NULL) != NULL) {
// 		printf("mysql_DBname=%s\n",(mxmlGetText( title, NULL)));
//         #define mysql_DBname (mxmlGetText( title, NULL))
// 	}
// 	title = mxmlFindElement( sys, xml, "mysql_port", NULL, NULL, MXML_DESCEND);
// 	if(mxmlGetText( title, NULL) != NULL) {
// 		printf("mysql_port=%s\n",(mxmlGetText( title, NULL)));
//         #define mysql_port (mxmlGetText( title, NULL))
// 	}	
// 	title = mxmlFindElement( sys, xml, "mysql_MaxConn", NULL, NULL, MXML_DESCEND);
// 	if(mxmlGetText( title, NULL) != NULL) {
// 		printf("mysql_MaxConn=%s\n",(mxmlGetText( title, NULL)));
//         #define mysql_MaxConn (mxmlGetText( title, NULL))
// 	}

// 	// ROOTFILE 根文件路径配置
// 	sys = mxmlFindElement( xml, xml, "ROOTFILE", NULL, NULL, MXML_DESCEND);
// 	title = mxmlFindElement( sys, xml, "doc_root", NULL, NULL, MXML_DESCEND);
// 	if(mxmlGetText( title, NULL) != NULL) {
// 		printf("doc_root=%s\n",(mxmlGetText( title, NULL)));
//         #define doc_root (mxmlGetText( title, NULL))
// 	} 

// 	mxmlDelete(xml);
// }

int main(int argc, char *argv[]) //argc是命令行中包含程序名在内的参数数量，argv[0]就是程序名
{
    //日志对象的创建，日志文件名的后缀.log在write_log()函数中设置
    #ifdef ASYNLOG 
        Log::get_instance()->init("output/WebServerASYN", 2000, 800000, 8); //异步日志模型
    #endif

    #ifdef SYNLOG
        Log::get_instance()->init("output/WebServerSYN", 2000, 800000, 0); //懒汉模式，同步日志模型
    #endif

    // if (argc != 1)
    // {
    //     printf("usage: ./%s\n", basename(argv[0]));
    //     LOG_ERROR("%s", "[main.cpp]:usage error!");
    //     return 1;
    // }
    /*可以考虑在这里加一个命令行参数解析代码块，getopts函数*/
  

    //LOG_DEBUG("%s","111111111");
    //局部变量定义区
    
    connection_pool *connPool = NULL; //创建数据库连接池
    threadpool<http_conn> *pool = NULL;  //创建线程池
    http_conn *users = NULL;
    int listenfd = 0;
    int ret = -1;
    int flag = 1;
    bool stop_server = false;
    client_data *users_timer = new client_data[MAX_FD];
    bool timeout = false;
    epoll_event events[MAX_EVENT_NUMBER];//创建内核事件表
    //LOG_DEBUG("%s","2222222222");
    signal(SIGINT, my_handler); //SIGINT是ctrl+C信号
    addsig(SIGPIPE, SIG_IGN);
    //LOG_DEBUG("%s","333333333333");

    //数据库池的对象创建
    connPool = connection_pool::GetInstance();
    connPool->init(mysql_url, mysql_user, mysql_passwd, mysql_DBname, mysql_port, mysql_MaxConn);   //连接到数据库

    //线程池的对象创建
    try
    {
        pool = new threadpool<http_conn>(connPool);
    }
    catch (...)
    {
        LOG_ERROR("%s", "[main.c]:new the threadpool<http_conn> failed!");
        return 1;
    }
    //LOG_DEBUG("%s","4444444444444");
    //http_conn池的对象创建
    users = new http_conn[MAX_FD];  //获取到数据库中的mydb->user下的所有数据（也就是所有用户名，用户密码）
    assert(users); //assert是做程序诊断用的，如果输入的参数不满足其要求，assert会将错误信息传输到标准错误中，并停止程序
    users->initmysql_result(connPool);//初始化数据库读取表

    listenfd = socket(PF_INET, SOCK_STREAM, 0); //在宏定义中，AF_INET和PF_INET一样,TCP通信
    assert(listenfd >= 0);//如果listenfd小于0，程序退出
    //struct linger tmp={1,0};
    //SO_LINGER若有数据待发送，延迟关闭
    //setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    //LOG_DEBUG("%s","55555555555");
    struct sockaddr_in address;
    //port = atoi(argv[1]);
    int port = ServerPort;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));  //允许IP地址的复用
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    
    epollfd = epoll_create(5);//创建内核事件表
    assert(epollfd != -1);

    addfd(epollfd, listenfd, false);//false表示不是一次性的
    http_conn::m_epollfd = epollfd;

    //创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd); //创建了两个socketfd，并用管道通信
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);

    addsig(SIGALRM, sig_handler, false); //SIGALRM 定时器信号
    addsig(SIGTERM, sig_handler, false); //SIGTERM 是kill pid信号；说明：SIGKILL是kill -9 pid该信号会无条件杀死进程，不能被信号处理函数捕捉
    //而SIGTERM是kill pid，会先清理程序资源，再退出程序

    // LOG_DEBUG("%s","6666666666666");
    //补充说明 终止程序的信号说明
    //SIGINT  : 是ctrl+C发出的，对前台进程和其所处在统一进程组中的进程有效
    //SIGTERM : 有kill pid发出，只对当前进程有效，子进程不会收到。父进程死亡后，子进程认init进程为父
    //SIGKILL : kill -9 pid,该信号不能被阻塞，所以一定会生效。但这样也会导致无法清理资源的问题
    //上述两个信号可以设置自定义的信号行为

    
    alarm(TIMESLOT);
    LOG_INFO("%s","+++++++++++++++++++++++++++A New Try+++++++++++++++++++++++");
    while (!stop_server)
    {
        /*
        epoll_wait运行的原理：等侍注册在epfd上的socket fd的事件的发生，如果发生则将发生的sokct fd和事件类型放入到events数组中。并且，将注册在epfd上的socket fd的事件类型给清空。

        所以如果下一个循环你还要关注这个socket fd的话，则需要用epoll_ctl(epfd,EPOLL_CTL_MOD,listenfd,&ev)来重新设置socket fd的事件类型。这时不用EPOLL_CTL_ADD,因为socket fd并未清空，只是事件类型清空。这一步非常重要。
        */
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1); //-1表示为阻塞
        if (number < 0 && errno != EINTR) //EINTR表示中断错误，读或写时被中断时，会出现此错误。且errno置为4.
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        LOG_DEBUG("[%s]line %d: the number is %d",__func__,__LINE__,number);
        
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;  //注意，events.data.fd的取值有listenfd和pipefd[0]
            //处理新到的客户连接
            if (sockfd == listenfd)
            {
                LOG_DEBUG("[%s]line %d: 事件1:处理新到的客户连接",__func__,__LINE__);
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
#ifdef listenfdLT
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                if (connfd < 0)
                {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    //show_error(connfd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                users[connfd].init(connfd, client_address);//再把fd事件通过addfd()函数注册到epollfd中去

                //初始化client_data数据
                //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                util_timer *timer = new util_timer;
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_lst.add_timer(timer);
#endif

#ifdef listenfdET
// 考虑这种情况：多个连接同时到达，服务器的 TCP 就绪队列瞬间积累多个就绪连接，由于是边缘触发模式，
// epoll 只会通知一次，accept 只处理一个连接，导致 TCP 就绪队列中剩下的连接都得不到处理。
/*
解决办法是用 while 循环抱住 accept 调用，处理完 TCP 就绪队列中的所有连接后再退出循环。如何知道
是否处理完就绪队列中的所有连接呢？ accept  返回 -1 并且 errno 设置为 EAGAIN (错误值为11)就表示所有连接都处理完。
*/
                while (1)
                {
                    //说明：这里有个accept error,错误值为11，字符串描述为：Resource temporarily unavailable
                    //LT水平触发来一个就处理一个。但是ET边沿触发不一样，如果连着来多个，边沿触发只会响应一次
                    // 所以常规的做法是每次处理都用while循环包裹，一次连接处理完后在退出循环
                    int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                    if (connfd < 0)
                    {
                        if(errno != EAGAIN) {
                           LOG_ERROR("[%s]line %d: %s: %s",__func__,__LINE__,"accept error", strerror(errno)); 
                        }
                        break;
                    }
                    if (http_conn::m_user_count >= MAX_FD)
                    {
                        //show_error(connfd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                    }
                    users[connfd].init(connfd, client_address);

                    //初始化client_data数据
                    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                    users_timer[connfd].address = client_address;
                    users_timer[connfd].sockfd = connfd;
                    util_timer *timer = new util_timer;
                    timer->user_data = &users_timer[connfd];
                    timer->cb_func = cb_func;
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    users_timer[connfd].timer = timer;
                    timer_lst.add_timer(timer);
                }
                continue;
#endif
            }

            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))//若事件为对端关闭连接/对应文件描述符被挂断/文件描述符发生错误三种事件
            {
                LOG_DEBUG("[%s]line %d: 事件2:处理对端关闭或者fd被挂断",__func__,__LINE__);
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);

                if (timer)
                {
                    timer_lst.del_timer(timer);
                }
            }

            //处理信号
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))//通过管道进行的socket通信，有数据输入
            {
                LOG_DEBUG("[%s]line %d: 事件3:处理pipefd里面的信号事件",__func__,__LINE__);
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        //LOG_DEBUG("[%s]line %d: 信号 为%d",__func__,__LINE__,signals[i]);
                        switch (signals[i])
                        { 
                            case SIGALRM:
                            {
                                /*用timeout变量标记有定时任务需要处理,
                                * 但不立即处理定时任务，
                                * 这是因为定时任务的优先级不是很高，
                                * 我们优先处理其他更重要的任务*/
                                timeout = true;
                                break;
                            }
                            case SIGTERM: //使用kill+pid的方式关闭服务器
                            {
                                stop_server = true;
                                LOG_INFO("%s","[main.c]:stop_server By 'kill + pid'!");
                            }
                        }
                    }
                }
            }

            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN) //对应的文件描述符上有数据输入。调用http的read_once一次性读完
            {
                LOG_DEBUG("[%s]line %d: 事件4:处理收到的数据",__func__,__LINE__);
                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].read_once())
                {
                    
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    //若监测到读事件，将该事件放入请求队列
                    pool->append(users + sockfd);

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
                LOG_DEBUG("[%s]line %d: 事件4:处理收到的数据-->已结束",__func__,__LINE__);
            }
            else if (events[i].events & EPOLLOUT) //对应文件描述符由数据输出。调用http的write函数
            {
                LOG_DEBUG("[%s]line %d: 事件5:处理发送的数据",__func__,__LINE__);
                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].write())
                {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));               

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
                LOG_DEBUG("[%s]line %d: 事件5:处理发送的数据-->已结束",__func__,__LINE__);
            }
            else {
                LOG_ERROR("[%s]line %d: 事件6: 未定义的事件处理项",__func__,__LINE__);
            }
        }
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
        if(flagCtrlC)
        {
            stop_server = true;
            LOG_INFO("%s","[main.c]:stop server by 'Ctrl+C'!");  
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    LOG_INFO("%s","[main.c]:资源回收完毕，已退出!");
    return 0;
}