#!/bin/bash

# filename="webserver_full"
# if [ -e "$filename" ] ; then
#     echo "文件已经存在，无需make"
# else
#     make server
# fi

make

pid=`ps -ax|grep webserver_full |grep -v grep | awk '{print $1}'`
# -z 是进行空字符检查
if [ -z "$pid" ] ; then
    nohup ./webserver_full > /dev/null 2>&1 &
    pid_now=`ps -ax|grep webserver_full |grep -v grep | awk '{print $1}'`
    echo -e "\033[0;32mNo webserver_full is running, but running with [$pid_now] now !!!\033[0m"
else
    echo -e "\033[0;31mNotice! Has webserver_full [$pid] is running , killed !\033[0m"
    kill $pid
    sleep 1s
    nohup ./webserver_full > /dev/null 2>&1 &
    pid_now2=`ps -ax|grep webserver_full |grep -v grep | awk '{print $1}'`
    echo -e "\033[0;32msuccess to running with [$pid_now2] now !!!\033[0m"
fi 