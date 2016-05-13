#include "sysutil.h"
#include "TimeStamp.h"

using namespace sysutil;
using namespace std;
namespace sysutil{
	//value为true时设置NonBlock，否则反之
	int setNonBlock(int fd,bool value)
	{
	    int flags = fcntl(fd, F_GETFL, 0);
	    if (flags < 0) {
	        CHEN_LOG(ERROR,"setNonBlock error");
	    }
	    if (value) {
	        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	    }
	    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);	
	}
    //使处于2MSL状态的端口能被重新使用
    int setReuseAddr(int fd, bool value)
    {
        int flag = value;
        int len = sizeof flag;
        return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, len);
    }

    int setReusePort(int fd, bool value) {
        int flag = value;
        int len = sizeof flag;
        return setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &flag, len);
    }

	int addFdFlag(int fd, int flag)
	{
		int ret = fcntl(fd, F_GETFD);
   		return fcntl(fd, F_SETFD, ret | flag);
	}
	//返回当前时间
	int64_t timeMirco()
	{
		return netlib::TimeStamp::now().getMicrosecondsSinceEpoch();
	}
	int64_t timeMilli()		{	return timeMirco()/1000;	}

    struct in_addr getHostByName(const std::string& host) {
        struct in_addr addr;
        char buf[1024];
        struct hostent hent;
        struct hostent* he = NULL;
        int herrno = 0;
        memset(&hent, 0, sizeof hent);
        int r = gethostbyname_r(host.c_str(), &hent, buf, sizeof buf, &he, &herrno);
        if (r == 0 && he && he->h_addrtype==AF_INET) {
            addr = *reinterpret_cast<struct in_addr*>(he->h_addr);
        } else {
            addr.s_addr = INADDR_NONE;
        }
        return addr;
    }	
    //返回格式化字符串
    string format(const char* fmt, ...) {
    char buffer[500];
    unique_ptr<char[]> release1;
    char* base;
    for (int iter = 0; iter < 2; iter++) {
        int bufsize;
        if (iter == 0) {
            bufsize = sizeof(buffer);
            base = buffer;
        } else {
            bufsize = 30000;
            base = new char[bufsize];
            release1.reset(base);
        }
        char* p = base;
        char* limit = base + bufsize;
        if (p < limit) {
            va_list ap;
            va_start(ap, fmt);
            p += vsnprintf(p, limit - p, fmt, ap);
            va_end(ap);
        }
        // Truncate to available space if necessary
        if (p >= limit) {
            if (iter == 0) {
                continue;       // Try again with larger buffer
            } else {
                p = limit - 1;
                *p = '\0';
            }
        }
        break;
    }
    return base;
}
}