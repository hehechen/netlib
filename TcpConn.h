#pragma once
#include <boost/any.hpp>
#include "event_base.h"
#include "Ip4Addr.h"
#include "Buffer.h"
namespace netlib {

//Tcp连接，使用引用计数
    class TcpConn: public std::enable_shared_from_this<TcpConn>, private noncopyable {
    public:
        //Tcp连接的个状态
        enum State { Invalid=1, Handshaking, Connected, Closed, Failed, };
        //Tcp构造函数，实际可用的连接应当通过createConnection创建
        TcpConn();
        ~TcpConn();
        void attach(EventBase* base, int fd, Ip4Addr local, Ip4Addr peer);      //执行真正的初始化工作
        //可传入连接类型，返回智能指针
        //作为client
        static TcpConnPtr createConnection(EventBase* base, const std::string& host, short port,
                                   int timeout=0, const std::string& localip="") {
            TcpConnPtr con(new TcpConn); con->connect(base, host, port, timeout, localip); return con;
        }
        //作为server
        static TcpConnPtr createConnection(EventBase* base, int fd, Ip4Addr local, Ip4Addr peer) {
            TcpConnPtr con(new TcpConn); con->attach(base, fd, local, peer); return con;
        }
          
        void setContext(const boost::any& context_)
        { context = context_; }

        const boost::any& getContext() const
        { return context; }

        EventBase* getBase() { return base_; }
        State getState() { return state_; }
        //TcpConn的输入输出缓冲区
        Buffer& getInput() { return input_; }
        Buffer& getOutput() { return output_; }

        Channel* getChannel() { return channel_; }
        bool writable() { return channel_ ? channel_->writeEnabled(): false; }

        //发送数据
        void sendOutput() { send(output_); }
        void send(Buffer& msg);
        void send(const char* buf, size_t len);
        void send(const std::string& s) { send(s.data(), s.size()); }
        void send(const char* s) { send(s, strlen(s)); }

        //数据到达时回调
        void onRead(const TcpCallBack& cb) { assert(!readcb_); readcb_ = cb; };
        //当tcp缓冲区可写时回调
        void onWritable(const TcpCallBack& cb) { writablecb_ = cb;}
        //tcp状态改变时回调
        void onState(const TcpCallBack& cb) { statecb_ = cb; }
        //tcp空闲回调
 //       void addIdleCB(int idle, const TcpCallBack& cb);

        //消息回调，此回调与onRead回调冲突，只能够调用一个
        //codec所有权交给onMsg
  //      void onMsg(CodecBase* codec, const MsgCallBack& cb);
        //发送消息
        void sendMsg(Slice msg);

        //conn会在下个事件周期进行处理
        void close();
        //设置重连时间间隔，-1: 不重连，0:立即重连，其它：等待毫秒数，未设置不重连
    //    void setReconnectInterval(int milli) { reconnectInterval_ = milli; }

        //!慎用。立即关闭连接，清理相关资源，可能导致该连接的引用计数变为0，从而使当前调用者引用的连接被析构
        void closeNow() { if (channel_) channel_->close(); }

        //远程地址的字符串
        std::string str() { return peer_.toString(); }

    private:        
        EventBase* base_;
        Channel* channel_;
        Buffer input_, output_;     //缓冲区
        Ip4Addr local_, peer_;      //本地及对方的ip地址
        TimerId timeoutId_ = -1;         //注册连接超时事件的id
        State state_;               
        TcpCallBack readcb_, writablecb_, statecb_; //各种回调事件
        boost::any context;          //可以存放任意类型的东西(尽量存指针)
        std::string destHost_, localIp_;
        int destPort_, connectTimeout_, reconnectInterval_;
        int64_t connectedTime_;
        void handleRead(const TcpConnPtr& con);
        void handleWrite(const TcpConnPtr& con);
        ssize_t isend(const char* buf, size_t len);
        void cleanup(const TcpConnPtr& con);
        void connect(EventBase* base, const std::string& host, short port, int timeout, const std::string& localip);
  //      void reconnect();
        virtual int readImp(int fd, void* buf, size_t bytes) { return ::read(fd, buf, bytes); }
        virtual int writeImp(int fd, const void* buf, size_t bytes) { return ::write(fd, buf, bytes); }
        virtual int handleHandshake(const TcpConnPtr& con);
    };
}
