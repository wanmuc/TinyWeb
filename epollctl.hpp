#pragma once

#include "conn.hpp"

inline void AddReadEvent(Conn *conn) {
  epoll_event event;
  event.data.ptr = (void *)conn;
  event.events = EPOLLIN;
  assert(epoll_ctl(conn->EpollFd(), EPOLL_CTL_ADD, conn->Fd(), &event) != -1);
}

inline void ModToWriteEvent(Conn *conn, bool isET = false) {
  epoll_event event;
  event.data.ptr = (void *)conn;
  event.events = EPOLLOUT;
  if (isET) event.events |= EPOLLET;
  assert(epoll_ctl(conn->EpollFd(), EPOLL_CTL_MOD, conn->Fd(), &event) != -1);
}

inline void ClearEvent(Conn *conn, bool isClose = true) {
  assert(epoll_ctl(conn->EpollFd(), EPOLL_CTL_DEL, conn->Fd(), NULL) != -1);
  if (isClose) close(conn->Fd());  // close操作需要EPOLL_CTL_DEL之后调用，否则调用epoll_ctl()删除fd会失败
}