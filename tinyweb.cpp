#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

#include "cmdline.h"
#include "epollctl.hpp"
#include "handler.hpp"

using namespace std;

int *EpollFd;
int EpollInitCnt = 0;
std::mutex Mutex;
std::condition_variable Cond;
Handler handler;

// 获取系统有多少个可用的cpu
int GetNProcs() { return get_nprocs(); }

void SetNotBlock(int fd) {
  int oldOpt = fcntl(fd, F_GETFL);
  assert(oldOpt != -1);
  assert(fcntl(fd, F_SETFL, oldOpt | O_NONBLOCK) != -1);
}

int CreateListenSocket(const char *ip, int port, bool isReusePort) {
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);
  int sockFd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockFd < 0) {
    perror("socket failed");
    return -1;
  }
  int reuse = 1;
  if (setsockopt(sockFd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) != 0) {
    perror("setsockopt failed");
    return -1;
  }
  if (bind(sockFd, (sockaddr *)&addr, sizeof(addr)) != 0) {
    perror("bind failed");
    return -1;
  }
  if (listen(sockFd, 1024) != 0) {
    perror("listen failed");
    return -1;
  }
  return sockFd;
}

// 调用本函数之前需要把sockFd设置成非阻塞的
void LoopAccept(int sockFd, int maxConn, std::function<void(int clientFd)> clientAcceptCallBack) {
  while (maxConn--) {
    int clientFd = accept(sockFd, NULL, 0);
    if (clientFd > 0) {
      clientAcceptCallBack(clientFd);
      continue;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      perror("accept failed");
    }
    break;
  }
}

void waitSubReactor() {
  std::unique_lock<std::mutex> locker(Mutex);
  Cond.wait(locker, []() -> bool { return EpollInitCnt >= GetNProcs(); });
  return;
}

void subReactorNotifyReady() {
  {
    std::unique_lock<std::mutex> locker(Mutex);
    EpollInitCnt++;
  }
  Cond.notify_all();
}

void addToSubHandler(int &index, int clientFd) {
  index++;
  index %= GetNProcs();
  Conn *conn = new Conn(clientFd, EpollFd[index]);  // 轮询的方式添加到子Reactor线程中
  AddReadEvent(conn);                               // 监听可读事件
}

void mainHandler(const char *ip, int port) {
  waitSubReactor();  // 等待所有的从Reactor线程都启动完毕
  int sockFd = CreateListenSocket(ip, port, true);
  if (sockFd < 0) {
    return;
  }
  epoll_event events[2048];
  int epollFd = epoll_create(1024);
  if (epollFd < 0) {
    perror("epoll_create failed");
    return;
  }
  int index = 0;
  Conn conn(sockFd, epollFd);
  SetNotBlock(sockFd);
  AddReadEvent(&conn);
  while (true) {
    int num = epoll_wait(epollFd, events, 2048, -1);
    if (num < 0) {
      perror("epoll_wait failed");
      continue;
    }
    // 执行到这里就是有客户端连接到来
    LoopAccept(sockFd, 100000, [&index, epollFd](int clientFd) {
      SetNotBlock(clientFd);
      addToSubHandler(index, clientFd);  // 把连接迁移到subHandler线程中管理
    });
  }
}

void addHandler(HttpMessage *req, HttpMessage *resp) {
  // TODO
}

HttpMessage *handler(HttpMessage *req) {
  // 后面实现一个http handler 支持注册不同的路径和方法，没有注册的请求就返回404 not found。明天实现。
  HttpMessage *resp = new HttpMessage;
  //  resp->SetStatusCode(OK);
  //  resp->SetBody(R"({"code":0, "data":"hello world"})");

  return resp;
}

void subHandler(int threadId) {
  epoll_event events[2048];
  int epollFd = epoll_create(1024);
  if (epollFd < 0) {
    perror("epoll_create failed");
    return;
  }
  EpollFd[threadId] = epollFd;
  subReactorNotifyReady();
  while (true) {
    int num = epoll_wait(epollFd, events, 2048, -1);
    if (num < 0) {
      perror("epoll_wait failed");
      continue;
    }
    for (int i = 0; i < num; i++) {
      Conn *conn = (Conn *)events[i].data.ptr;
      auto releaseConn = [&conn]() {
        ClearEvent(conn);
        delete conn;
      };
      if (events[i].events & EPOLLIN) {  // 可读
        if (not conn->Read()) {          // 执行非阻塞读
          releaseConn();
          continue;
        }
        HttpMessage *req = conn->GetReq();
        if (req) {
          HttpMessage *resp = handler(req);
          conn->SetResp(resp);
          ModToWriteEvent(conn);  // 修改成只监控可写事件
          delete req;
          delete resp;
        }
      }
      if (events[i].events & EPOLLOUT) {  // 可写
        if (not conn->Write()) {          // 执行非阻塞写
          releaseConn();
          continue;
        }
        if (conn->FinishWrite()) {  // 完成了请求的应答写，则可以释放连接
          releaseConn();
        }
      }
    }
  }
}

void usage() {
  cout << "./TinyWeb -ip 0.0.0.0 -port 8080" << endl;
  cout << "options:" << endl;
  cout << "    -h,--help      print usage" << endl;
  cout << "    -ip,--ip       listen ip" << endl;
  cout << "    -port,--port   listen port" << endl;
  cout << endl;
}

int main(int argc, char *argv[]) {
  string ip;
  int64_t port;
  CmdLine::StrOptRequired(&ip, "ip");
  CmdLine::Int64OptRequired(&port, "port");
  CmdLine::SetUsage(usage);
  CmdLine::Parse(argc, argv);
  EpollFd = new int[GetNProcs()];
  handler.Register(kPost, "/add", addHandler);
  for (int i = 0; i < GetNProcs(); i++) {
    std::thread(subHandler, i).detach();  // 这里需要调用detach，让创建的线程独立运行
  }
  for (int i = 0; i < GetNProcs() - 1; i++) {
    std::thread(mainHandler, ip.c_str(), port).detach();  // 这里需要调用detach，让创建的线程独立运行
  }
  mainHandler(ip.c_str(), port);  // 主线程也陷入死循环中，监听客户端请求
  return 0;
}