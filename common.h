#pragma once

#include <iostream>
#include <memory>
#include <queue>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>//套接字编程
#include <netinet/in.h>//地址
#include <fcntl.h>
#include <arpa/inet.h>
#include <string.h>
#include <string>
#include <map>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
//不是#include <linux/capability.h>。。不知道别人是怎么通过编译的
#include <sys/capability.h>
#include <sys/syscall.h>    
#include <signal.h>
#include <sys/sendfile.h>
#include <sys/timerfd.h>  
#include <pthread.h>
#include <sys/epoll.h>

//'\'后面不要加注释
/**
 *FTPD_LOG - 日志宏
 *输出日期，时间，日志级别，源码文件，行号，信息
 */
 //定义一个日志宏
#define DEBUG 0
#define INFO  1
#define WARN  2
#define ERROR 3
#define CRIT  4 

static const char* LOG_STR[] = { 
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "CRIT"
};

#define CHEN_LOG(level, format, ...) do{ \
    char _msg[1024];                        \
    char _buf[32];                                   \
    sprintf(_msg, format, ##__VA_ARGS__);             \
    if (level >= 0) {\
        time_t now = time(NULL);                      \
        strftime(_buf, sizeof(_buf), "%Y%m%d %H:%M:%S", localtime(&now)); \
        fprintf(stdout, "[%s] [%s] [file:%s] [line:%d] %s\n",_buf,LOG_STR[level],__FILE__,__LINE__, _msg); \
        fflush (stdout); \
    }\
     if (level >= ERROR) {\
        perror(_msg);    \
        ::exit(-1); \
    } \
} while(0)

//避免污染命名空间
namespace noncopyable_{
    class noncopyable
    {
    protected:
        noncopyable()   {}
        ~noncopyable()  {}
    private:
        noncopyable(const noncopyable&);
        const noncopyable& operator=(const noncopyable&);
    };
}

typedef noncopyable_::noncopyable noncopyable;

namespace netlib{
    class Channel;
    class TcpConn;
    class TcpServer;
    class IdleIdImp;
    class EventsImp;
    class EventBase;
    class EventBases;
    class TimerHeap;
    class Slice;
    #define SafeQueue std::queue
    typedef int64_t TimerId;
    typedef std::unique_ptr<IdleIdImp> IdleId;
    typedef std::function<void()> Task;    
    typedef std::shared_ptr<TcpConn> TcpConnPtr;
    typedef std::shared_ptr<TcpServer> TcpServerPtr;
    typedef std::function<void(const TcpConnPtr&)> TcpCallBack;
    typedef std::function<void(const TcpConnPtr&, Slice msg)> MsgCallBack;
    struct AutoContext: noncopyable {
        void* ctx;
        Task ctxDel;
        AutoContext():ctx(0) {}
        template<class T> T& context() {
            if (ctx == NULL) {
                ctx = new T();
                ctxDel = [this] { delete (T*)ctx; };
            }
            return *(T*)ctx;
        }
        ~AutoContext() { if (ctx) ctxDel(); }
    };
}

