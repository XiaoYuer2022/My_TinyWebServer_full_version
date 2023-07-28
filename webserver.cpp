#include "webserver.h"

WebServer::WebServer()
{
    //http_conn类对象
    users = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int isDebug, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write_syn = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_write_log = close_log;
    m_actormodel = actor_model;
    m_is_debug = isDebug;
}

void WebServer::log_write()
{
    // m_write_log=1 表示允许写入日志； m_log_write_syn=1 表示同步写入日志
    if (1 == m_write_log)
    {
        //初始化日志 为0表示异步，为1表示同步
        if (0 == m_log_write_syn)
            Log::get_instance()->init("./output/ServerASYN.log", m_is_debug, m_write_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./output/ServerSYN.log", m_is_debug, m_write_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

//监听 socket套接字的初始化创建
void WebServer::eventListen()
{
    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);
    //关于linger结构体的说明：https://www.cnblogs.com/my_life/articles/5174585.html
    //优雅关闭连接 linger的意思是延迟关闭
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};//立即返回，不优雅的关闭
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        //1)这种方式下，在调用closesocket的时候不会立刻返回，内核会延迟一段时间，
            //这个时间就由l_linger得值来决定。
        //2)如果超时时间到达之前，发送完未发送的数据(包括FIN包)并得到另一端的确认，
            //closesocket会返回正确，socket描述符优雅性退出。
        //3)否则，closesocket会直接返回 错误值，未发送数据丢失，socket描述符被强制性退出。
            //需要注意的时，如果socket描述符被设置为非堵塞型，则closesocket会直接返回值。
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)); //设置socket套接字重用
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false); //添加定时触发信号
    utils.addsig(SIGTERM, utils.sig_handler, false); //添加kill pid信号
    utils.addsig(SIGINT, utils.sig_handler, false); //添加ctrl + c 信号

    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

//运行
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;
    LOG_INFO("+++++++++++++++++++++++++++A New Try+++++++++++++++++++++++");
    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);//-1表示为阻塞
        if (number < 0 && errno != EINTR)//EINTR表示中断错误，读或写时被中断时，会出现此错误。且errno置为4.
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        //LOG_DEBUG("[%s]line %d: the number is %d",__func__,__LINE__,number);
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                LOG_DEBUG("[%s]line %d: 事件1:处理新到的客户连接",__func__,__LINE__);
                bool flag = dealclientdata(); //事件1:处理新到的客户连接
                if (false == flag)
                    {
                        LOG_ERROR("%s", "dealclientdata failure");
                        continue;
                    }
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                LOG_DEBUG("[%s]line %d: 事件2:处理对端关闭或者fd被挂断",__func__,__LINE__);
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;//事件2:处理对端关闭或者fd被挂断，移除对应的定时器
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                //LOG_DEBUG("[%s]line %d: 事件3:处理pipefd里面的信号事件",__func__,__LINE__);
                bool flag = dealwithsignal(timeout, stop_server);//事件3:处理pipefd里面的信号事件
                if (false == flag)
                    LOG_ERROR("%s", "dealwithsignal failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                LOG_DEBUG("[%s]line %d: 事件4:处理收到的数据",__func__,__LINE__);
                dealwithread(sockfd); //事件4:处理收到的数据
                LOG_DEBUG("[%s]line %d: 事件4:处理收到的数据-->已结束",__func__,__LINE__);
            }
            else if (events[i].events & EPOLLOUT)
            {
                LOG_DEBUG("[%s]line %d: 事件5:处理发送的数据",__func__,__LINE__);
                dealwithwrite(sockfd);//事件5:处理发送的数据
                LOG_DEBUG("[%s]line %d: 事件5:处理发送的数据-->已结束",__func__,__LINE__);
            }
        }
        if (timeout)
        {
            utils.timer_handler();

            LOG_DEBUG("%s", "timer tick");

            timeout = false;
        }
    }
}

//在事件1处理新用户连接中被调用
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_user, m_passWord, m_databaseName);

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
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

//事件1:处理新到的客户连接
bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    // listen LT模式
    if (0 == m_LISTENTrigmode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    // listen ET模式
    else
    {
        while (1)
        {
            /**
             * 说明：这里有个accept error : EAGAIN,错误值为11，字符串描述为：Resource temporarily unavailable
             * LT水平触发来一个就处理一个。但是ET边沿触发不一样，如果连着来多个，边沿触发只会响应一次
             * 所以常规的做法是每次处理都用while循环包裹，一次连接处理完后在退出循环
             * 怎么判断while循环已经接收完数据？再次去接受数据时返回EAGAIN错误就是表示接收完了
            */
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                // LOG_ERROR("%s:errno is:%d", "accept error", errno);
                if(errno != EAGAIN) {
                    LOG_ERROR("[%s]line %d: %s: %s",__func__,__LINE__,"accept error", strerror(errno)); 
                    return false;
                }
                LOG_INFO("[%s]line %d: %s",__func__,__LINE__,"accept ET over");
                // break;
                return true;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                // break;
                return false;
            }
            timer(connfd, client_address);
        }
        return true;
    }
    return false;
}

//事件2:处理对端关闭或者fd被挂断，移除对应的定时器
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

//事件3:处理pipefd里面的信号事件
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGINT:
            {
                stop_server = true;
                LOG_ERROR("server killed By ctrl+c !!!");
                break;
            }
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                LOG_ERROR("server killed By kill+pid !!!");
                break;
            }
            }
        }
    }
    return true;
}

//事件4:处理收到的数据
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);//第二个参数m_state: 读为0, 写为1

        while (true)
        {
            //在线程中调用了read_once()/或write()后，不管有没有被正确读取，improv都会被置1。如果正确读取，timer_flag=0；没有正确读取，则timer_flag=1
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor 这个是原raw的“读”模式
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

//事件5:处理发送的数据
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            //在线程中调用了read_once()/或write()后，不管有没有被正确读取，improv都会被置1。如果正确读取，timer_flag=0；没有正确读取，则timer_flag=1
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}
