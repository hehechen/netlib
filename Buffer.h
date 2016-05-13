#pragma once

//模仿libevent的evbuffer

// struct buf_iovec{
// 	void *iov_base;
// 	int len;
// };

// struct buffer_chain{
// 	struct buffer_chain *next;
// 	//这个缓冲区块的总长度
// 	size_t buffer_len;
// 	//buffer_chain已存数据的字节数，所以要从buffer+off开始写数据
// 	size_t off;
// 	unsigned char *buffer;
// };
// class Buffer{
// public:
//   void append(const char* p,size_t len);	//添加数据
//   //不用复制，直接从缓冲区读取
//   //读取长度为len的数据，从chunk_start开始读取，读到iovec_out里
//   int peek(int len,buffer_chain *chunk_start,buf_iovec *iovec_out,int n_vec);
//   //从buffer复制数据
//   int buffer_copyout(int len)
// };
#include "common.h"
#include "slice.h"
namespace netlib{
struct Buffer {
    Buffer(): buf_(NULL), b_(0), e_(0), cap_(0), exp_(512) {}
    ~Buffer() { delete[] buf_; }
    void clear() { delete[] buf_; buf_ = NULL; cap_ = 0; b_ = e_ = 0; }
    size_t size() const { return e_ - b_; }
    bool empty() const  { return e_ == b_; }
    char* data() const  { return buf_ + b_; }
    char* begin() const  { return buf_ + b_; }
    char* end() const  { return buf_ + e_; }
    char* makeRoom(size_t len);
    void makeRoom() { if (space() < exp_) expand(0);}
    size_t space() const  { return cap_ - e_; }
    void addSize(size_t len) { e_ += len; }
    char* allocRoom(size_t len) { char* p = makeRoom(len); addSize(len); return p; }
    Buffer& append(const char* p, size_t len) { memcpy(allocRoom(len), p, len); return *this; }
    Buffer& append(Slice slice) { return append(slice.data(), slice.size()); }
    Buffer& append(const char* p) { return append(p, strlen(p)); }
    template<class T> Buffer& appendValue(const T& v) { append((const char*)&v, sizeof v); return *this; }
    Buffer& consume(size_t len) { b_ += len; if (size() == 0) clear(); return *this; }
    Buffer& absorb(Buffer& buf);
    void setSuggestSize(size_t sz) { exp_ = sz; }
    Buffer(const Buffer& b) { copyFrom(b); }
    Buffer& operator=(const Buffer& b) { delete[] buf_; buf_ = NULL; copyFrom(b); return *this; }
    operator Slice () { return Slice(data(), size()); }
private:
    char* buf_;
    size_t b_, e_, cap_, exp_;
    void moveHead() { std::copy(begin(), end(), buf_); e_ -= b_; b_ = 0; }
    void expand(size_t len);
    void copyFrom(const Buffer& b);
};
}



