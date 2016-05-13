#include "sysutil.h"
#include "Ip4Addr.h"
using namespace std;

namespace netlib{
Ip4Addr::Ip4Addr(const string& host, short port) {
    memset(&addr_, 0, sizeof addr_);
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    if (host.size()) {
        addr_.sin_addr = sysutil::getHostByName(host);
    } else {
        addr_.sin_addr.s_addr = INADDR_ANY;
    }
    if (addr_.sin_addr.s_addr == INADDR_NONE){
        CHEN_LOG(ERROR,"cannot resove %s to ip", host.c_str());
    }
}

string Ip4Addr::toString() const {
    uint32_t uip = addr_.sin_addr.s_addr;
    return sysutil::format("%d.%d.%d.%d:%d",
        (uip >> 0)&0xff,
        (uip >> 8)&0xff,
        (uip >> 16)&0xff,
        (uip >> 24)&0xff,
        ntohs(addr_.sin_port));
}

string Ip4Addr::ip() const { 
    uint32_t uip = addr_.sin_addr.s_addr;
    return sysutil::format("%d.%d.%d.%d",
        (uip >> 0)&0xff,
        (uip >> 8)&0xff,
        (uip >> 16)&0xff,
        (uip >> 24)&0xff);
}

short Ip4Addr::port() const {
    return ntohs(addr_.sin_port);
}

unsigned int Ip4Addr::ipInt() const { 
    return ntohl(addr_.sin_addr.s_addr);
}
bool Ip4Addr::isIpValid() const {
    return addr_.sin_addr.s_addr != INADDR_NONE;
}	
}