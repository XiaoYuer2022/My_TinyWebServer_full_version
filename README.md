1、程序的启动：直接运行build.sh脚本；选择性运行checklog.sh脚本来删除output目录下过旧的日志文件

2、启动流程：main函数中定义和调用WebServer类来启动程序，先定义了一个Config类来初始化配置，在启动程序。
2.1 配置文件：config.h主要是完成Config类的类接口和类属性定义，
2.2 config.cpp为属性设定默认值并使用行解析函数来获取用户输入
2.3 webserver.h定义类，webserver.cpp实现具体的功能。
