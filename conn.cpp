#include "conn.h"
#include "logging.h"
#include <poll.h>
#include <fcntl.h>
#include "poller.h"


using namespace std;
namespace netlib {
int a;

//执行初始化工作
void TcpConn::attach(EventBase* base, int fd, Ip4Addr local, Ip4Addr peer)
{
    if((destPort_<=0 && state_ != State::Invalid) || (destPort_>=0 && state_ != State::Handshaking))
        CHEN_LOG(ERROR,"you should use a new TcpConn to attach. state: %d", state_);
    base_ = base;
    state_ = State::Handshaking;
    local_ = local;
    peer_ = peer;
    if (channel_) { delete channel_; }
    channel_ = new Channel(base, fd, kWriteEvent|kReadEvent);
    CHEN_LOG(DEBUG,"tcp constructed %s - %s fd: %d",
        local_.toString().c_str(),
        peer_.toString().c_str(),
        fd);
    TcpConnPtr con = shared_from_this();
    con->channel_->onRead([=] { con->handleRead(con); });
    con->channel_->onWrite([=] { con->handleWrite(con); });
}

void TcpConn::connect(EventBase* base, const string& host, short port, 
                        int timeout, const string& localip) {
    if(state_ != State::Invalid && state_ != State::Closed && state_ != State::Failed)
        CHEN_LOG(ERROR,"current state is bad state to connect. state: %d", state_);
    destHost_ = host;
    destPort_ = port;
    connectTimeout_ = timeout;
    connectedTime_ = sysutil::timeMilli();
    localIp_ = localip;
    Ip4Addr addr(host, port);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0)
        CHEN_LOG(ERROR,"socket failed");
    sysutil::setNonBlock(fd);
    int t = sysutil::addFdFlag(fd, FD_CLOEXEC);
    if(t != 0)
        CHEN_LOG(ERROR,"addFdFlag FD_CLOEXEC failed %d", t);
    int r = 0;
    if (localip.size()) {
        Ip4Addr addr(localip, 0);
        r = ::bind(fd,(struct sockaddr *)&addr.getAddr(),sizeof(struct sockaddr));
        CHEN_LOG(ERROR,"bind to %s failed error", addr.toString().c_str());
    }
    if (r == 0) {
        r = ::connect(fd, (sockaddr *) &addr.getAddr(), sizeof(sockaddr_in));
        if (r != 0 && errno != EINPROGRESS) {
            CHEN_LOG(ERROR,"connect to %s error", addr.toString().c_str());
        }
    }

    sockaddr_in local;
    socklen_t alen = sizeof(local);
    if (r == 0) {
        r = getsockname(fd, (sockaddr *) &local, &alen);
        if (r < 0) {
            CHEN_LOG(ERROR,"getsockname failed ");
        }
    }
    state_ = State::Handshaking;
    attach(base, fd, Ip4Addr(local), addr);
    if (timeout) {
        TcpConnPtr con = shared_from_this();
        timeoutId_ = base->runAfter(timeout, [con] {
            if (con->getState() == Handshaking) { con->channel_->close(); }
        });
    }
}
//关闭连接
void TcpConn::close() {
    if (channel_) {
        TcpConnPtr con = shared_from_this();
        getBase()->safeCall([con]{ if (con->channel_) con->channel_->close(); });
    }
}
//清理连接资源                             
void TcpConn::cleanup(const TcpConnPtr& con) {
    if (readcb_ && input_.size()) {
        readcb_(con);
    }
    if (state_ == State::Handshaking) {
        state_ = State::Failed;
    } else {
        state_ = State::Closed;
    }
    CHEN_LOG(DEBUG,"tcp closing %s - %s fd %d %d",
        local_.toString().c_str(),
        peer_.toString().c_str(),
        channel_ ? channel_->fd(): -1, errno);
    if(timeoutId_ > -1)
        getBase()->cancel(timeoutId_);
    if (statecb_) {
        statecb_(con);
    }
    // if (reconnectInterval_ >= 0 && !getBase()->exited()) { //reconnect
    //     reconnect();
    //     return;
    // }
    //channel may have hold TcpConnPtr, set channel_ to NULL before delete
    readcb_ = writablecb_ = statecb_ = nullptr;
    Channel* ch = channel_;
    channel_ = NULL;
    delete ch;
}
//读取数据并执行相应回调事件
void TcpConn::handleRead(const TcpConnPtr& con) {
    if (state_ == State::Handshaking && handleHandshake(con)) {
        return;
    }
    while(state_ == State::Connected) {
        input_.makeRoom();
        int rd = 0;
        if (channel_->fd() >= 0) {
            rd = readImp(channel_->fd(), input_.end(), input_.space());
            CHEN_LOG(DEBUG,"channel %lld fd %d readed %d bytes", (long long)channel_->id(), channel_->fd(), rd);
        }
        if (rd == -1 && errno == EINTR) {
            continue;
        } else if (rd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK) ) {
            if (readcb_ && input_.size()) {
                readcb_(con);
            }
            break;
        } else if (channel_->fd() == -1 || rd == 0 || rd == -1) {
            cleanup(con);
            break;
        } else { //rd > 0
            input_.addSize(rd);
        }
    }
}
//在初始化TcpConn时状态会被设为Handshaking，读写时发现还是Handshaking就会执行此函数
int TcpConn::handleHandshake(const TcpConnPtr& con) {
    if(state_ != Handshaking)
        CHEN_LOG(ERROR,"handleHandshaking called when state_=%d", state_);
    struct pollfd pfd;
    pfd.fd = channel_->fd();
    pfd.events = POLLOUT | POLLERR;
    int r = poll(&pfd, 1, 0);
    if (r == 1 && pfd.revents == POLLOUT) {
        channel_->enableReadWrite(true, false);
        state_ = State::Connected;
        if (state_ == State::Connected) {
            connectedTime_ = sysutil::timeMilli();
            CHEN_LOG(DEBUG,"tcp connected %s - %s fd %d",
                local_.toString().c_str(), peer_.toString().c_str(), channel_->fd());
            if (statecb_) {
                statecb_(con);
            }
        }
    } else {
        CHEN_LOG(DEBUG,"poll fd %d return %d revents %d", channel_->fd(), r, pfd.revents);
        cleanup(con);
        return -1;
    }
    return 0;
}

void TcpConn::handleWrite(const TcpConnPtr& con) {
    if (state_ == State::Handshaking) {
        handleHandshake(con);
    } else if (state_ == State::Connected) {
        ssize_t sended = isend(output_.begin(), output_.size());    //发送缓冲区的数据
        output_.consume(sended);
        if (output_.empty() && writablecb_) {
            writablecb_(con);
        }
        if (output_.empty() && channel_->writeEnabled()) { // writablecb_ may write something
            channel_->enableWrite(false);
        }
    } else {
        CHEN_LOG(ERROR,"handle write unexpected");
    }
}

ssize_t TcpConn::isend(const char* buf, size_t len) {
    size_t sended = 0;
    while (len > sended) {
        ssize_t wd = writeImp(channel_->fd(), buf + sended, len - sended);
        CHEN_LOG(DEBUG,"channel %lld fd %d write %ld bytes", (long long)channel_->id(), channel_->fd(), wd);
        if (wd > 0) {
            sended += wd;
            continue;
        } else if (wd == -1 && errno == EINTR) {
            continue;
        } else if (wd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!channel_->writeEnabled()) {
                channel_->enableWrite(true);
            }
            break;
        } else {
            CHEN_LOG(ERROR,"write error: channel %lld fd %d wd %ld", 
                     (long long)channel_->id(), channel_->fd(), wd);
            break;
        }
    }
    return sended;
}
//发送缓冲中的数据
void TcpConn::send(Buffer& buf) {
    if (channel_) {
        if (channel_->writeEnabled()) { //just full
            output_.absorb(buf);
        } 
        if (buf.size()) {
            ssize_t sended = isend(buf.begin(), buf.size());
            buf.consume(sended);
        }
        if (buf.size()) {
            output_.absorb(buf);
            if (!channel_->writeEnabled()) {
                channel_->enableWrite(true);
            }
        }
    } else {
        CHEN_LOG(WARN,"connection %s - %s closed, but still writing %lu bytes",
            local_.toString().c_str(), peer_.toString().c_str(), buf.size());
    }
}
//output_为空就直接写fd，写不完就写到缓冲区里
void TcpConn::send(const char* buf, size_t len) {
    if (channel_) {
        if (output_.empty()) {
            ssize_t sended = isend(buf, len);
            buf += sended;
            len -= sended;
        }
        if (len) {
            output_.append(buf, len);
        }
    } else {
        CHEN_LOG(WARN,"connection %s - %s closed, but still writing %lu bytes",
            local_.toString().c_str(), peer_.toString().c_str(), len);
    }
}

// void TcpConn::onMsg(CodecBase* codec, const MsgCallBack& cb) {
//     assert(!readcb_);
//     codec_.reset(codec);
//     onRead([cb](const TcpConnPtr& con) {
//         int r = 1;
//         while (r) {
//             Slice msg;
//             r = con->codec_->tryDecode(con->getInput(), msg);
//             if (r < 0) {
//                 con->channel_->close();
//             } else if (r > 0) {
//                 trace("a msg decoded. origin len %d msg len %ld", r, msg.size());
//                 cb(con, msg);
//                 con->getInput().consume(r);
//             }
//         }
//     });
// }

// void TcpConn::sendMsg(Slice msg) {
//     codec_->encode(msg, getOutput());
//     sendOutput();
// }

TcpServer::TcpServer(EventBases* bases):
base_(bases->allocBase()),
bases_(bases),
listen_channel_(NULL),
createcb_([]{ return TcpConnPtr(new TcpConn); })
{
}
//绑定并监听端口
int TcpServer::bind(const std::string &host, short port, bool reusePort) {
    addr_ = Ip4Addr(host, port);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int r = sysutil::setReuseAddr(fd);
    if(r != 0)
        CHEN_LOG(ERROR,"set socket reuse option failed");
    r = sysutil::setReusePort(fd, reusePort);
    if(r != 0)
        CHEN_LOG(ERROR,"set socket reuse port option failed");  
    r = sysutil::addFdFlag(fd, FD_CLOEXEC);
    if(r != 0)
        CHEN_LOG(ERROR,"addFdFlag FD_CLOEXEC failed");  
    r = ::bind(fd,(struct sockaddr *)&addr_.getAddr(),sizeof(struct sockaddr));
    if (r) {
        close(fd);
        CHEN_LOG(ERROR,"bind to %s failed ", addr_.toString().c_str());
        return errno;
    }
    r = listen(fd, 20);
    if(r != 0)
        CHEN_LOG(ERROR,"listen failed");
    CHEN_LOG(INFO,"fd %d listening at %s", fd, addr_.toString().c_str());
    listen_channel_ = new Channel(base_, fd, kReadEvent);
    listen_channel_->onRead([this]{ handleAccept(); });
    return 0;
}

TcpServerPtr TcpServer::startServer(EventBases* bases, const std::string& host, short port, bool reusePort) {
    TcpServerPtr p(new TcpServer(bases));
    int r = p->bind(host, port, reusePort);
    if (r) {
        CHEN_LOG(ERROR,"bind to %s:%d failed ", host.c_str(), port);
    }
    return r == 0 ? p : NULL;
}
//TcpServer不断循环accept，accept后选择一个线程接管
void TcpServer::handleAccept() {
    struct sockaddr_in raddr;
    socklen_t rsz = sizeof(raddr);
    int lfd = listen_channel_->fd();
    int cfd;
    while (lfd >= 0 && (cfd = accept(lfd,(struct sockaddr *)&raddr,&rsz))>=0) {
        sockaddr_in peer, local;
        socklen_t alen = sizeof(peer);
        int r = getpeername(cfd, (sockaddr*)&peer, &alen);
        if (r < 0) {
            CHEN_LOG(ERROR,"get peer name failed");
            continue;
        }
        r = getsockname(cfd, (sockaddr*)&local, &alen);
        if (r < 0) {
            CHEN_LOG(ERROR,"getsockname failed ");
            continue;
        }
        r = sysutil::addFdFlag(cfd, FD_CLOEXEC);
        if(r != 0)
            CHEN_LOG(ERROR,"addFdFlag FD_CLOEXEC failed");
        EventBase* b = bases_->allocBase();
        auto addcon = [=] {
            TcpConnPtr con = createcb_();
            con->attach(b, cfd, local, peer);
            if (statecb_) {
                con->onState(statecb_);
            }
            if (readcb_) {
                con->onRead(readcb_);
            }
            // if (msgcb_) {
            //     con->onMsg(codec_->clone(), msgcb_);
            // }
        };
        if (b == base_) {
            addcon();
        } else {
            b->safeCall(move(addcon));
        }
    }
    if (lfd >= 0 && errno != EAGAIN && errno != EINTR) {
        CHEN_LOG(WARN,"accept return %d  %d %s", cfd, errno, strerror(errno));
    }
}

// HSHAPtr HSHA::startServer(EventBase* base, const std::string& host, short port, int threads) {
//     HSHAPtr p = HSHAPtr(new HSHA(threads));
//     p->server_ = TcpServer::startServer(base, host, port);
//     return p->server_ ? p : NULL;
// }

// void HSHA::onMsg(CodecBase* codec, const RetMsgCallBack& cb) {
//     server_->onConnMsg(codec, [this, cb](const TcpConnPtr& con, Slice msg) {
//         std::string input = msg;
//         threadPool_.addTask([=]{
//             std::string output = cb(con, input);
//             server_->getBase()->safeCall([=] {if (output.size()) con->sendMsg(output); });
//         });
//     });
// }

}