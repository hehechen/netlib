#pragma once
#include "common.h"
namespace netlib{
class Ip4Addr {
public:
    Ip4Addr(const std::string& host, short port);
    Ip4Addr(short port=0): Ip4Addr("", port) {}
    Ip4Addr(const struct sockaddr_in& addr): addr_(addr) {};
    std::string toString() const;
    std::string ip() const;
    short port() const;
    unsigned int ipInt() const;
    //if you pass a hostname to constructor, then use this to check error
    bool isIpValid() const;
    struct sockaddr_in& getAddr() { return addr_; }
    static std::string hostToIp(const std::string& host) { Ip4Addr addr(host, 0); return addr.ip(); }
private:
    struct sockaddr_in addr_;
};	
}