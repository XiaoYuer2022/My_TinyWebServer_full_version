

服务器压力测试
===============
Webbench是有名的网站压力测试工具，它是由[Lionbridge](http://www.lionbridge.com)公司开发。

> * 测试处在相同硬件上，不同服务的性能以及不同硬件上同一个服务的运行状况。
> * 展示服务器的两项内容：每秒钟响应请求数和每秒钟传输数据量。




测试规则
------------
* 测试示例

    ```C++
	//webbench -c 500  -t  30   http://127.0.0.1/phpionfo.php
    ./webbench -c 10500  -t 5   http://127.0.0.1:8008/
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

2023-02-15测试结果

    ```c++
    1、(listen)LT+ET模式下的效率是最高的[不开日志]     3250225 bytes/sec
    2、相较而言，LT+LT的读写效率就比较低
    3、开日志的效率要比关日志的效率低              
    4、同步日志要比异步日志的读写效率要"低"            [同步日志]194366 bytes/sec   [异步日志]505799 bytes/sec

    ```

