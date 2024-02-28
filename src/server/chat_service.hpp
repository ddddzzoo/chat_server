#ifndef CHAT_SERVICE_H
#define CHAT_SERVICE_H

#include <muduo/net/TcpConnection.h>

#include <functional>
#include <string>
#include <unordered_map>

#include "json.hpp"

using namespace std;
using namespace muduo;
using namespace muduo::net;
using json = nlohmann::json;

// 处理消息的事件回调方法类型
using MsgHandler =
    std::function<void(const TcpConnectionPtr& conn, json& js, Timestamp time)>;

class ChatService {
 public:
  // 获取单对象接口函数
  static ChatService& instance();

  // 处理登录业务
  void login(const TcpConnectionPtr& conn, json& js, Timestamp time);

  // 处理注册业务
  void reg(const TcpConnectionPtr& conn, json& js, Timestamp time);

  // 获取消息对应处理器
  MsgHandler getHandler(int msgid);

 private:
  ChatService();

  // 储存消息id和其对应业务处理方法
  unordered_map<int, MsgHandler> _msgHandlerMap;
};

#endif