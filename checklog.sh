#!/bin/bash

LOG_DIR=`pwd`/output
# 删除7天前的备份文件
find $LOG_DIR  -type f -name "*.log" -mtime +7 -delete

ret_code=$?
if [ $ret_code -ne 0 ];then
    echo "删除失败"
else
    echo "删除成功"
fi

