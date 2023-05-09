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

  void Deal(HttpMessage* req, HttpMessage* resp) {
    std::string method;
    std::string url;
    req->GetMethodAndUrl(method, url);
    if (method == "GET") {
      if (get_handlers_.find(url) == get_handlers_.end()) {
        resp->SetStatusCode(NOT_FOUND);
      } else {
        get_handlers_[url](req, resp);
      }
    } else if (method == "POST") {
      if (post_handlers_.find(url) == post_handlers_.end()) {
        resp->SetStatusCode(NOT_FOUND);
      } else {
        post_handlers_[url](req, resp);
      }
    } else {
      resp->SetStatusCode(NOT_FOUND);
    }
  }

 private:
  std::map<std::string, std::function<void(HttpMessage* req, HttpMessage* resp)>> get_handlers_;
  std::map<std::string, std::function<void(HttpMessage* req, HttpMessage* resp)>> post_handlers_;
};
