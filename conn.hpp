#pragma once

#include "httpcodec.hpp"

class Conn {
 public:
  Conn(int fd, int epoll_fd) : fd_(fd), epoll_fd_(epoll_fd) {}
  bool Read() {
    do {
      ssize_t ret = read(fd_, codec_.Data(), codec_.Len());  // 一次最多读取4K
      if (ret == 0) {
        perror("peer close connection");
        return false;
      }
      if (ret < 0) {
        if (EINTR == errno) continue;
        if (EAGAIN == errno or EWOULDBLOCK == errno) return true;
        perror("read failed");
        return false;
      }
      codec_.Decode(ret);
    } while (true);
  }
  bool Write() {
    do {
      if (send_len_ == (ssize_t)send_pkt_.UseLen()) return true;
      ssize_t ret = write(fd_, send_pkt_.DataRaw() + send_len_, send_pkt_.UseLen() - send_len_);
      if (ret < 0) {
        if (EINTR == errno) continue;
        if (EAGAIN == errno && EWOULDBLOCK == errno) return true;
        perror("write failed");
        return false;
      }
      send_len_ += ret;
    } while (true);
  }
  HttpMessage* GetReq() { return codec_.GetMessage(); }
  void SetResp(HttpMessage* resp) { codec_.Encode(resp, send_pkt_); }
  bool FinishWrite() { return send_len_ == (ssize_t)send_pkt_.UseLen(); }
  int Fd() { return fd_; }
  int EpollFd() { return epoll_fd_; }

 private:
  int fd_{0};            // 关联的客户端连接fd
  int epoll_fd_{0};      // 关联的epoll实例的fd
  ssize_t send_len_{0};  // 要发送的应答数据的长度
  Packet send_pkt_;      // 发送应答数据的二进制缓冲区
  HttpMessage* req_;     // http请求消息
  HttpMessage* resp_;    // http应答消息
  HttpCodec codec_;      // http协议的编解码
};