服务器压力测试
===============
Webbench是有名的网站压力测试工具，它是由[Lionbridge](http://www.lionbridge.com)公司开发。

> * 测试处在相同硬件上，不同服务的性能以及不同硬件上同一个服务的运行状况。
> * 展示服务器的两项内容：每秒钟响应请求数和每秒钟传输数据量。




测试规则
------------
* 测试示例

    ```C++
	webbench -c 500  -t  30   http://127.0.0.1/phpionfo.php
    ```
* 参数

> * `-c` 表示客户端数
> * `-t` 表示时间


测试结果
---------
Webbench对服务器进行压力测试，经压力测试可以实现上万的并发连接.
> * 并发连接总数：10500
> * 访问服务器时间：5s
> * 每秒钟响应请求数：552852 pages/min
> * 每秒钟传输数据量：1031990 bytes/sec
> * 所有访问均成功

<div align=center><img src="https://github.com/twomonkeyclub/TinyWebServer/blob/master/root/testresult.png" height="201"/> </div>


## 注意：
编译webbench命令前，需要安装环境：

参考：[CSDN](https://beltxman.com/3874.html)

1、安装libtirpc-dev包，否则会报：`fatal error: rpc/types.h: No such file or directory`和`fatal error: netconfig.h: No such file or directory`
```bash
sudo apt-get install -y libtirpc-dev
# 建立软链接
sudo ln -s /usr/include/tirpc/rpc/types.h /usr/include/rpc
sudo ln -s /usr/include/tirpc/netconfig.h /usr/include
```
2.安装ctags，否则会报：`/bin/sh: 1: ctags: not found`
```bash
sudo apt-get install universal-ctags
```
