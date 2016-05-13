#include "event_base.h"
#include <iostream>
using namespace std;
using namespace netlib;
int main() {
    EventBase base;
    TcpServerPtr svr = TcpServer::startServer(&base, "", 99);
    if(svr == NULL)
    	CHEN_LOG(ERROR,"start tcp server failed");
    svr->onConnRead([](const TcpConnPtr& con) {
        con->send(con->getInput());
    });
    base.loop();
}