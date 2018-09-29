# httpd
A http file upload/download server, write by c language, windows platfrom

C 语言实现的http文件上传下载服务


系统平台：windows

开发工具：vs2010

开发语言：C


程序为单线程，使用I/O多路复用实现并发

抽取libevent的最最最基础框架，自己封装event

使用BSD tree.h的红黑树
