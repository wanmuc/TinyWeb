#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "../cmdline.h"
#include "../httpcodec.hpp"
#include "../httpmessage.hpp"
#include "../packet.hpp"

using namespace std;

typedef struct Stat {
  int sum{0};
  int success{0};
  int failure{0};
  int spendms{0};
} Stat;

std::mutex Mutex;
Stat FinalStat;

int64_t port;
int64_t concurrency;
int64_t runtime;
std::string method;
std::string url;
std::string body;

bool getConnection(sockaddr_in &addr, int &sockFd) {
  sockFd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockFd < 0) {
    perror("socket failed");
    return false;
  }
  int ret = connect(sockFd, (sockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    perror("connect failed");
    close(sockFd);
    return false;
  }
  struct linger lin;
  lin.l_onoff = 1;
  lin.l_linger = 0;
  // 设置调用close关闭tcp连接时，直接发送RST包，tcp连接直接复位，进入到closed状态。
  if (0 == setsockopt(sockFd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin))) {
    return true;
  }
  perror("setsockopt failed");
  close(sockFd);
  return false;
}

// 用于阻塞IO模式下发送应答消息
bool SendReq(int fd, HttpMessage &req) {
  Packet pkt;
  HttpCodec codec;
  codec.Encode(&req, pkt);
  ssize_t sendLen = 0;
  while ((size_t)sendLen != pkt.UseLen()) {
    ssize_t ret = write(fd, pkt.DataRaw() + sendLen, pkt.UseLen() - sendLen);
    if (ret < 0) {
      if (errno == EINTR) continue;  // 中断的情况可以重试
      perror("write failed");
      return false;
    }
    sendLen += ret;
  }
  return true;
}

// 用于阻塞IO模式下接收请求消息
bool RecvResp(int fd, HttpMessage &resp) {
  HttpCodec codec;
  HttpMessage *temp;
  while (true) {  // 只要还没获取到一个完整的消息，则一直循环
    temp = codec.GetMessage();
    if (temp) break;
    ssize_t ret = read(fd, codec.Data(), codec.Len());
    if (ret <= 0) {
      if (errno == EINTR) continue;  // 中断的情况可以重试
      perror("read failed");
      return false;
    }
    codec.Decode(ret);
  }
  resp = *temp;
  delete temp;
  return true;
}

int64_t getSpendMs(timeval begin, timeval end) {
  end.tv_sec -= begin.tv_sec;
  end.tv_usec -= begin.tv_usec;
  if (end.tv_usec <= 0) {
    end.tv_sec -= 1;
    end.tv_usec += 1000000;
  }
  return end.tv_sec * 1000 + end.tv_usec / 1000;  //计算运行的时间，单位ms
}

void client(int theadId, Stat *curStat, int port, int concurrency) {
  int sum = 0;
  int success = 0;
  int failure = 0;
  int spendms = 0;
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(std::string("127.0.0." + std::to_string(theadId + 1)).c_str());
  HttpMessage req;
  req.SetMethodAndUrl(method, url);
  req.SetBody(body);
  concurrency /= 10;  // 每个线程的并发数
  int *sockFd = new int[concurrency];
  timeval end;
  timeval begin;
  gettimeofday(&begin, NULL);
  for (int i = 0; i < concurrency; i++) {
    if (not getConnection(addr, sockFd[i])) {
      sockFd[i] = 0;
      failure++;
    }
  }
  auto failureDeal = [&sockFd, &failure](int i) {
    close(sockFd[i]);
    sockFd[i] = 0;
    failure++;
  };
  for (int i = 0; i < concurrency; i++) {
    if (sockFd[i]) {
      if (not SendReq(sockFd[i], req)) {
        failureDeal(i);
      }
    }
  }
  for (int i = 0; i < concurrency; i++) {
    if (sockFd[i]) {
      HttpMessage resp;
      if (RecvResp(sockFd[i], resp)) {
        failureDeal(i);
        continue;
      }
      if (resp.GetStatusCode() != OK) {
        failureDeal(i);
        continue;
      }
      close(sockFd[i]);
      success++;
    }
  }
  delete[] sockFd;
  sum = success + failure;
  gettimeofday(&end, NULL);
  spendms = getSpendMs(begin, end);
  std::lock_guard<std::mutex> guard(Mutex);
  curStat->sum += sum;
  curStat->success += success;
  curStat->failure += failure;
  curStat->spendms += spendms;
}

void UpdateFinalStat(Stat stat) {
  FinalStat.sum += stat.sum;
  FinalStat.success += stat.success;
  FinalStat.failure += stat.failure;
  FinalStat.spendms += stat.spendms;
}

void usage() {
  cout << "./WebBench -port 8088 -concurrency 10000 -runtime 60 -method POST -url /add -body 'a=10&b=90'" << endl;
  cout << "options:" << endl;
  cout << "    -h,--help                    print usage" << endl;
  cout << "    -port,--port                 listen port" << endl;
  cout << "    -concurrency,--concurrency   concurrency" << endl;
  cout << "    -runtime,--runtime           run time, unit is second" << endl;
  cout << "    -method,--method             http request's method" << endl;
  cout << "    -url,--url                   http request's url" << endl;
  cout << "    -body,--body                 http request's body" << endl;
  cout << endl;
}

int main(int argc, char *argv[]) {
  CmdLine::Int64OptRequired(&port, "port");
  CmdLine::Int64OptRequired(&concurrency, "concurrency");
  CmdLine::Int64OptRequired(&runtime, "runtime");
  CmdLine::StrOptRequired(&method, "method");
  CmdLine::StrOptRequired(&url, "url");
  CmdLine::StrOptRequired(&body, "body");
  CmdLine::SetUsage(usage);
  CmdLine::Parse(argc, argv);

  timeval end;
  timeval runBeginTime;
  gettimeofday(&runBeginTime, NULL);
  int runRoundCount = 0;
  while (true) {
    Stat curStat;
    std::thread threads[10];
    for (int threadId = 0; threadId < 10; threadId++) {
      threads[threadId] = std::thread(client, threadId, &curStat, port, concurrency);
    }
    for (int threadId = 0; threadId < 10; threadId++) {
      threads[threadId].join();
    }
    runRoundCount++;
    curStat.spendms /= 10;  // 取平均耗时
    UpdateFinalStat(curStat);
    gettimeofday(&end, NULL);
    std::cout << "round " << runRoundCount << " spend " << curStat.spendms << " ms. " << std::endl;
    if (getSpendMs(runBeginTime, end) >= runtime * 1000) {
      break;
    }
    sleep(2);  // 间隔2秒，再发起下一轮压测，这样压测结果更稳定
  }
  std::cout << "total spend " << FinalStat.spendms << " ms. avg spend " << FinalStat.spendms / runRoundCount
            << " ms. sum[" << FinalStat.sum << "],success[" << FinalStat.success << "],failure[" << FinalStat.failure
            << "]" << std::endl;
  return 0;
}