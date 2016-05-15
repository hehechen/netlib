#include "TcpServer.h"
using namespace std;
using namespace netlib;
namespace netlib{

	TcpServer::Entry::Entry(const WeakTcpConnectionPtr& weakConn)
          		: weakConn_(weakConn)
    {
    }

    TcpServer::Entry::~Entry()
    {
 		TcpConnPtr conn = weakConn_.lock();   //提升为shared_ptr
        if (conn)
        {
        	conn->close();
        }   	
    }

	TcpServer::TcpServer(EventBases* bases,int idleSeconds):
	base_(bases->allocBase()),
	bases_(bases),
	listen_channel_(NULL),
	createcb_([]{ return TcpConnPtr(new TcpConn); }),
	idleSeconds(idleSeconds),
	connectionBuckets_(idleSeconds)
	{
	    if(idleSeconds)
	        {
	        base_->runAfter(1000000,[this]{        //每一秒使时间轮转一格
	            connectionBuckets_.push_back(Bucket());
	            cur++;
	            if(cur >= this->idleSeconds)
	                cur -= this->idleSeconds;
	            cout <<cur<<"cur------------"<<endl;
	        },1);
	    }
	    //注册相关回调事件
	    statecb_ = [this](const TcpConnPtr& con){
	        handleState(con);
	    };      
	    readcb_ = [this](const TcpConnPtr& con){
	        handleRead(con);
	    };
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

	TcpServerPtr TcpServer::startServer(EventBases* bases, const std::string& host, 
	                                    short port,int idleSeconds, bool reusePort) {
	    TcpServerPtr p(new TcpServer(bases,idleSeconds));
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
	                CHEN_LOG(DEBUG,"register statecb-----");
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

	void TcpServer::onConnCreate(const std::function<TcpConnPtr()>& cb){ createcb_ = cb; }
	void TcpServer::onConnState(const TcpCallBack& cb) 
	{ 
	    auto readcb = [this,cb](const TcpConnPtr& con){
	        handleState(con);
	        cb(con);
	    };      
	    statecb_ = readcb; 
	}
	void TcpServer::onConnRead(const TcpCallBack& cb) 
	{//有消息到来，在执行用户注册的回调前先更新时间轮状态
	    auto readcb = [this,cb](const TcpConnPtr& con){
	        handleRead(con);
	        cb(con);
	    };  
	    readcb_ = readcb; 
	    assert(!msgcb_); 
	}

	void TcpServer::handleState(const TcpConnPtr& con)
	{
	    if(idleSeconds)
	    {
	        if(con->getState() ==  con->Connected)
	        {
	            EntryPtr entry(new Entry(con));
	            connectionBuckets_.back().insert(entry);
	            WeakEntryPtr weakEntry(entry);
	            con->setContext(weakEntry);
	            CHEN_LOG(INFO,"setContext---------");
	        }
	    }    
	}
	void TcpServer::handleRead(const TcpConnPtr& con)
	{
	    if(idleSeconds)
	     {
	        CHEN_LOG(DEBUG,"update timewheeling");
	        assert(!con->getContext().empty());
	        WeakEntryPtr weakEntryPtr(boost::any_cast<WeakEntryPtr>(con->getContext()));
	        EntryPtr entry(weakEntryPtr.lock());
	        if (entry)
	        {
	            connectionBuckets_.back().insert(entry);
	        }
	    }  
	}
}