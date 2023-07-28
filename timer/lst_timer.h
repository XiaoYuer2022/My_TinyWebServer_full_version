#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include "../log/log.h"

class util_timer;
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

//有序的双向时间链表
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;
    void (*cb_func)(client_data *); //函数指针，相当于留了一个函数接口
    //你可以在外部实现一个函数，然后将该函数名赋给此函数指针
    client_data *user_data;
    util_timer *prev;
    util_timer *next;
};

class sort_timer_lst
{
public:
    sort_timer_lst() : head(NULL), tail(NULL) {}
    ~sort_timer_lst()
    {
        util_timer *tmp = head;
        while (tmp)
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    //添加新结点到时间双向链表中去
    void add_timer(util_timer *timer)
    {
        LOG_INFO("%s", "添加一个时间节点");
        if (!timer)
        {
            return;
        }
        if (!head)
        {
            head = tail = timer;
            return;
        }
        if (timer->expire < head->expire) //有序的双向时间链表，按时间从小到大排序
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head); //如果timer的剩余时间大于头head,就插入到head后面合适的位置去
    }
    //调整时间双向链表，按时间从小到大排序
    void adjust_timer(util_timer *timer) //调整定时器定时的时间
    {
        if (!timer)
        {
            return;
        }
        util_timer *tmp = timer->next;
        //假如timer已经是排序链表的最后一个节点，或者在timer->expire加了3个单位后，仍然满足排序要求，就不调整链表位置了
        if (!tmp || (timer->expire < tmp->expire))
        {
            return;
        }
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }
    //删除指定时间节点
    void del_timer(util_timer *timer)
    {
        LOG_INFO("%s", "删除一个时间节点！");
        if (!timer)
        {
            return;
        }
        //双向链表此时只有一个节点
        if ((timer == head) && (timer == tail))
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        if (timer == tail)
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
    
    //tick()函数主要是查看排序双向链表上的首节点是否超时，超时就调用cb_func来清空资源，并删除该链表节点
    void tick()
    {
        if (!head)
        {
            LOG_INFO("%s", "时间双向链表已经为空了，无需再调整！");
            return;
        }
        LOG_INFO("%s", "timer tick");
        time_t cur = time(NULL);
        util_timer *tmp = head;
        while (tmp)
        {
            //头结点还没超时，就不管他，让它继续运行
            if (cur < tmp->expire)
            {
                break;
            }
            //如果头结点已经删除，就调用cd_func函数来删除节点上的fd和socket
            tmp->cb_func(tmp->user_data);
            head = tmp->next;
            if (head)
            {
                head->prev = NULL; //因为是头结点嘛，prev指针肯定要值为NULL
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    void add_timer(util_timer *timer, util_timer *lst_head)
    {
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;
        while (tmp)
        {
            //将timer插在prev和tmp之间
            if (timer->expire < tmp->expire)
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        //如果tmp为空，表示将timer放在time_lst的最后一个
        if (!tmp)
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

private:
    util_timer *head;
    util_timer *tail;
};

#endif
