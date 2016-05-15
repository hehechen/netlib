#pragma once
#include <unordered_set>
#include <boost/circular_buffer.hpp>
#include <boost/any.hpp>

#include "event_base.h"
#include "Ip4Addr.h"
#include "Buffer.h"
#include "TcpConn.h"
namespace netlib {
//Tcp服务器
    class TcpServer: private noncopyable {
    public:
        TcpServer(EventBases* bases,int idleSeconds = 0);
        //return 0 on sucess, errno on error
        int bind(const std::string& host, short port, bool reusePort=false);
        //idleSeconds:允许空闲的秒数(>0有效)；reusePort：复用端口
        static TcpServerPtr startServer(EventBases* bases, const std::string& host, 
                                short port, int idleSeconds = 0,bool reusePort=false);
        ~TcpServer() { delete listen_channel_; }
        Ip4Addr getAddr() { return addr_; }
        EventBase* getBase() { return base_; }

        void onConnCreate(const std::function<TcpConnPtr()>& cb);
        void onConnState(const TcpCallBack& cb);
        void onConnRead(const TcpCallBack& cb); 
        // 消息处理与Read回调冲突，只能调用一个
  //      void onConnMsg(CodecBase* codec, const MsgCallBack& cb) { codec_.reset(codec); msgcb_ = cb; assert(!readcb_); }
    private:
        EventBase* base_;
        EventBases* bases_;
        Ip4Addr addr_;
        Channel* listen_channel_;
        TcpCallBack statecb_, readcb_;
        MsgCallBack msgcb_;
        std::function<TcpConnPtr()> createcb_;
   //     std::unique_ptr<CodecBase> codec_;
        void handleAccept();
        //时间轮相关
        void handleState(const TcpConnPtr& con);
        void handleRead(const TcpConnPtr& con);
        typedef std::weak_ptr<TcpConn> WeakTcpConnectionPtr;
        struct Entry 
          {
            explicit Entry(const WeakTcpConnectionPtr& weakConn);

            ~Entry();

            WeakTcpConnectionPtr weakConn_;
          };
        int idleSeconds;
        int cur = 0;    //for debug
        typedef std::shared_ptr<Entry> EntryPtr;
        typedef std::weak_ptr<Entry> WeakEntryPtr;
        typedef std::unordered_set<EntryPtr> Bucket;
        typedef boost::circular_buffer<Bucket> WeakConnectionList;
        //环形缓冲，每个桶是一个unordered_set，每次析构一个桶会将所有的引用计数-1
        WeakConnectionList connectionBuckets_;      
    };
}