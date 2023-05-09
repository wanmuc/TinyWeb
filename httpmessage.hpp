#pragma once

#include <map>
#include <string>

// 目前只支持4个状态码
enum HttpStatusCode {
  OK = 200,                     //请求成功
  BAD_REQUEST = 400,            //错误的请求，目前body只支持json格式，非json格式返回这个错误码
  NOT_FOUND = 404,              //请求失败，未找到相关资源
  INTERNAL_SERVER_ERROR = 500,  //内部服务错误
};

// http消息
typedef struct HttpMessage {
  void SetHeader(const std::string &key, const std::string &value) { headers_[key] = value; }
  void SetBody(const std::string &body) {
    body_ = body;
    SetHeader("Content-Type", "application/json");
    SetHeader("Content-Length", std::to_string(body_.length()));
  }
  void SetStatusCode(HttpStatusCode statusCode) {
    if (OK == statusCode) {
      first_line_ = "HTTP/1.1 200 OK";
    } else if (BAD_REQUEST == statusCode) {
      first_line_ = "HTTP/1.1 400 Bad Request";
    } else if (NOT_FOUND == statusCode) {
      first_line_ = "HTTP/1.1 404 Not Found";
    } else {
      first_line_ = "HTTP/1.1 500 Internal Server Error";
    }
  }
  std::string GetHeader(const std::string &key) {
    if (headers_.find(key) == headers_.end()) return "";
    return headers_[key];
  }
  void GetMethodAndUrl(std::string &method, std::string &url) {
    int32_t spaceCount = 0;
    for (size_t i = 0; i < first_line_.size(); i++) {
      if (first_line_[i] == ' ') {
        spaceCount++;
        continue;
      }
      if (spaceCount == 0) method += first_line_[i];
      if (spaceCount == 1) url += first_line_[i];
    }
  }
  void ParserBody() {
    std::string key;
    std::string value;
    bool is_key = true;
    auto add_param = [this, &is_key](std::string &key, std::string &value) {
      if (key != "" && value != "") {
        params_[key] = value;
      }
      key = "";
      value = "";
      is_key = true;
    };
    for (size_t i = 0; i < body_.size(); i++) {
      if (body_[i] == '&') {
        add_param(key, value);
      } else if (body_[i] == '=') {
        is_key = false;
      } else {
        if (is_key) {
          key += body_[i];
        } else {
          value += body_[i];
        }
      }
    }
    add_param(key, value);
  }
  void GetParam(std::string key, int64_t &value, int64_t default_value) {
    if (params_.find(key) == params_.end()) {
      value = default_value;
    } else {
      value = std::atoll(params_[key].c_str());
    }
  }

  std::string first_line_;  // 对于请求来说是request_line，对于应答来说是status_line
  std::map<std::string, std::string> headers_;
  std::string body_;
  std::map<std::string, std::string> params_;  // 参数集合
} HttpMessage;