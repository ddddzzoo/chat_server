#include "chat_service.hpp"

#include <muduo/base/Logging.h>

#include "../public.hpp"

using namespace muduo;

// 获取单例函数对象
ChatService& ChatService::instance() {
  static ChatService chatService;
  return chatService;
}

// 注册消息以及对应Handler回调操作
ChatService::ChatService() {
  // 用户基本业务管理相关事件处理回调注册
  _msgHandlerMap.insert(
      {LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
  _msgHandlerMap.insert(
      {REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
  _msgHandlerMap.insert(
      {ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
  _msgHandlerMap.insert(
      {ADD_FRIEND_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});

  // 群组业务相关事件处理回调注册
  _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup,
                                                     this, _1, _2, _3)});
  _msgHandlerMap.insert(
      {ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
  _msgHandlerMap.insert(
      {GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});
}

// 处理登录业务 id pwd
void ChatService::login(const TcpConnectionPtr& conn, json& js,
                        Timestamp time) {
  int id = js["id"].get<int>();
  string pwd = js["password"];

  User user = _userModel.query(id);
  if (user.getId() == id && user.getPassword() == pwd) {
    // 当前用户已经登录
    if (user.getState() == "online") {
      // 不允许用户重复登录
      json response;
      response["msgid"] = LOGIN_MSG_ACK;
      response["errno"] = 2;
      response["errmsg"] = "该账号已登录，请重新输入账号";
      conn->send(response.dump());
    }
    else {
      // 用户登录成功 记录用户连接信息
      {
        lock_guard<mutex> lock(_connMutex);
        _userConnMap.insert({id, conn});
      }

      // 更新用户状态 offline =》 online
      user.setState("online");
      _userModel.updateState(user);

      json response;
      response["msgid"] = LOGIN_MSG_ACK;
      response["errno"] = 0;
      response["id"] = user.getId();
      response["name"] = user.getName();

      // 查询该用户是否有离线消息
      vector<string> vec = _offlineMsgModel.query(id);

      if (!vec.empty()) {
        response["offlinemsg"] = vec;
        // 删除该用户的离线消息
        _offlineMsgModel.remove(id);
      }

      // 查询用户好友信息
      vector<User> userVec = _friendModel.query(id);
      if (!userVec.empty()) {
        vector<string> vec2;
        for (User& user : userVec) {
          json js;
          js["id"] = user.getId();
          js["name"] = user.getName();
          js["state"] = user.getState();
          vec2.push_back(js.dump());
        }
        response["friends"] = vec2;
      }

      conn->send(response.dump());
    }
  }
  else {
    // 登录失败:用户不存在 用户名或密码错误
    json response;
    response["msgid"] = LOGIN_MSG_ACK;
    response["errno"] = 1;
    response["errmsg"] = "用户名或密码错误";
    conn->send(response.dump());
  }
}

// 处理注册业务
void ChatService::reg(const TcpConnectionPtr& conn, json& js, Timestamp time) {
  string name = js["name"];
  string pwd = js["password"];

  User user;
  user.setName(name);
  user.setPassword(pwd);
  bool state = _userModel.insert(user);

  if (state) {
    // 注册成功
    json response;
    response["msgid"] = REG_MSG_ACK;
    response["errno"] = 0;
    response["id"] = user.getId();
    conn->send(response.dump());
  }
  else {
    // 注册失败
    json response;
    response["msgid"] = REG_MSG_ACK;
    response["errno"] = 1;
    conn->send(response.dump());
  }
}

// 获取消息对应处理器
MsgHandler ChatService::getHandler(int msgid) {
  // 记录错误日志，msgid没有对应事件处理回调
  auto it = _msgHandlerMap.find(msgid);
  if (it == _msgHandlerMap.end()) {
    // 返回一个默认的处理器，空操作
    return [=](const TcpConnectionPtr& conn, json& js, Timestamp) {
      LOG_ERROR << "msgid:" << msgid << " can not find handler!";
    };
  }
  else {
    return _msgHandlerMap[msgid];
  }
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr& conn) {
  User user;
  {
    lock_guard<mutex> lock(_connMutex);
    for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it) {
      if (it->second == conn) {
        // 从map表中删除用户的连接信息
        user.setId(it->first);
        _userConnMap.erase(it);
        break;
      }
    }
  }
  // 更新用户状态信息
  if (user.getId() != -1) {
    user.setState("offline");
    _userModel.updateState(user);
  }
}

// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr& conn, json& js,
                          Timestamp time) {
  int toid = js["toid"].get<int>();

  {
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(toid);
    if (it != _userConnMap.end()) {
      // toid在线，转发消息 服务器主动推送消息给用户
      it->second->send(js.dump());
      return;
    }
  }

  // toid 不在线，存储离线消息
  _offlineMsgModel.insert(toid, js.dump());
}

// 服务器异常退出，重置用户state
void ChatService::reset() {
  // 将online状态用户改为offline
  _userModel.resetState();
}

// 添加好友业务 msgid id friendid
void ChatService::addFriend(const TcpConnectionPtr& conn, json& js,
                            Timestamp time) {
  int userid = js["id"].get<int>();
  int friendid = js["friendid"].get<int>();

  // 存储好友信息
  _friendModel.insert(userid, friendid);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr& conn, json& js,
                              Timestamp time) {
  int userid = js["id"].get<int>();
  string name = js["groupname"];
  string desc = js["groupdesc"];

  // 存储新创建的群组信息
  Group group(-1, name, desc);
  if (_groupModel.creatGroup(group)) {
    // 存储群组创建人信息
    _groupModel.addGroup(userid, group.getId(), "creator");
  }
}

// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr& conn, json& js,
                           Timestamp time) {
  int userid = js["id"].get<int>();
  int groupid = js["groupid"].get<int>();
  _groupModel.addGroup(userid, groupid, "normal");
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr& conn, json& js,
                            Timestamp time) {
  int userid = js["id"].get<int>();
  int groupid = js["groupid"].get<int>();
  vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

  lock_guard<mutex> lock(_connMutex);
  for (int id : useridVec) {
    auto it = _userConnMap.find(id);
    if (it != _userConnMap.end()) {
      // 转发群消息
      it->second->send(js.dump());
    }
    else {
      // 查询toid是否在线
      User user = _userModel.query(id);
      if (user.getState() == "online") {
      }
      else {
        // 存储离线群消息
        _offlineMsgModel.insert(id, js.dump());
      }
    }
  }
}