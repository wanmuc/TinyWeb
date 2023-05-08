#pragma once

#include <functional>
#include <map>
#include <string>

#include "httpmessage.hpp"

enum Method {
  kGet = 1,   // get 方法
  kPost = 2,  // post 方法
};

class Handler {
 public:
  void Register(Method method, std::string url, std::function<void(HttpMessage* req, HttpMessage* resp)> handler) {
    if (method == kGet) {
      get_handlers_[url] = handler;
    } else if (method == kPost) {
      post_handlers_[url] = handler;
    }
  }

 private:
  std::map<std::string, std::function<void(HttpMessage* req, HttpMessage* resp)>> get_handlers_;
  std::map<std::string, std::function<void(HttpMessage* req, HttpMessage* resp)>> post_handlers_;
};
