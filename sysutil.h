#pragma once

#include "common.h"
/*系统工具模块*/
namespace sysutil{
	int setNonBlock(int fd,bool value=true);
	int setReuseAddr(int fd, bool value=true);
	int setReusePort(int fd, bool value=true);
	int addFdFlag(int fd, int flag);
	int64_t timeMirco();
	int64_t timeMilli();
	struct in_addr getHostByName(const std::string& host);//根据主机名查找相关信息
	std::string format(const char* fmt, ...);		//返回格式化字符串
}