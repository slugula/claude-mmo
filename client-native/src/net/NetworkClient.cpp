#include "net/NetworkClient.hpp"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketMessage.h>

#include <cstdio>
#include <thread>

namespace net {

namespace {

// Tiny JSON helpers — we use these for the auth response and outgoing action
// payloads to avoid pulling glaze into this translation unit. State /init
// parsing happens in App, which uses glaze.

// Very minimal field extractor. Looks for "key":"<value>" in `body`. Returns
// the empty string if not found.
std::string extractJsonString(const std::string& body, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  auto k = body.find(needle);
  if (k == std::string::npos) return {};
  auto colon = body.find(':', k + needle.size());
  if (colon == std::string::npos) return {};
  auto q1 = body.find('"', colon + 1);
  if (q1 == std::string::npos) return {};
  auto q2 = body.find('"', q1 + 1);
  if (q2 == std::string::npos) return {};
  return body.substr(q1 + 1, q2 - q1 - 1);
}

}  // namespace

NetworkClient::NetworkClient() {
  ix::initNetSystem();
}

NetworkClient::~NetworkClient() {
  disconnect();
  ix::uninitNetSystem();
}

void NetworkClient::loginAndConnect(std::string host, int port,
                                    std::string username, std::string password) {
  if (status_.load() == Connection::Connected) return;
  status_   = Connection::LoggingIn;
  lastError_.clear();
  host_     = std::move(host);
  port_     = port;
  std::thread([this, u = std::move(username), p = std::move(password)]() mutable {
    runLoginThread(host_, port_, std::move(u), std::move(p), false);
  }).detach();
}

void NetworkClient::registerAndConnect(std::string host, int port,
                                       std::string username, std::string password) {
  if (status_.load() == Connection::Connected) return;
  status_   = Connection::LoggingIn;
  lastError_.clear();
  host_     = std::move(host);
  port_     = port;
  std::thread([this, u = std::move(username), p = std::move(password)]() mutable {
    runLoginThread(host_, port_, std::move(u), std::move(p), true);
  }).detach();
}

void NetworkClient::runLoginThread(std::string host, int port,
                                   std::string username, std::string password,
                                   bool registerFirst) {
  ix::HttpClient http;
  auto args = http.createRequest();
  args->extraHeaders["Content-Type"] = "application/json";
  std::string body = "{\"username\":\"" + username + "\",\"password\":\"" + password + "\"}";

  // ---- Optional: POST /auth/register first --------------------------------
  if (registerFirst) {
    const std::string regUrl = "http://" + host + ":" + std::to_string(port) + "/auth/register";
    auto regResp = http.post(regUrl, body, args);
    if (!regResp) {
      lastError_ = "no response from server";
      status_    = Connection::Failed;
      return;
    }
    if (regResp->statusCode != 200 && regResp->statusCode != 201) {
      lastError_ = "registration failed (" + std::to_string(regResp->statusCode) + "): " + regResp->body;
      status_    = Connection::Failed;
      return;
    }
    // Registration succeeded — now fall through to login.
  }

  // ---- HTTP POST /auth/login ---------------------------------------------
  const std::string url = "http://" + host + ":" + std::to_string(port) + "/auth/login";
  auto resp = http.post(url, body, args);

  if (!resp) {
    lastError_ = "no response from server";
    status_    = Connection::Failed;
    return;
  }
  if (resp->statusCode != 200) {
    lastError_ = "login failed (" + std::to_string(resp->statusCode) + "): " + resp->body;
    status_    = Connection::Failed;
    return;
  }

  token_      = extractJsonString(resp->body, "token");
  playerId_   = extractJsonString(resp->body, "playerId");
  playerName_ = extractJsonString(resp->body, "username");
  if (token_.empty() || playerId_.empty()) {
    lastError_ = "login response missing token or playerId";
    status_    = Connection::Failed;
    return;
  }

  // ---- WebSocket connect --------------------------------------------------
  status_ = Connection::Connecting;
  ws_     = std::make_unique<ix::WebSocket>();
  const std::string wsUrl = "ws://" + host + ":" + std::to_string(port) + "/?token=" + token_;
  ws_->setUrl(wsUrl);
  // Keep the connection alive without ping/pong noise; the 200 ms tick
  // traffic is plenty.
  ws_->disablePerMessageDeflate();
  ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    switch (msg->type) {
      case ix::WebSocketMessageType::Open:
        status_ = Connection::Connected;
        std::fprintf(stdout, "[Net] WebSocket open\n");
        break;
      case ix::WebSocketMessageType::Message:
        onWsMessage(msg->str);
        break;
      case ix::WebSocketMessageType::Close:
        onWsClose(static_cast<int>(msg->closeInfo.code), msg->closeInfo.reason);
        break;
      case ix::WebSocketMessageType::Error:
        lastError_ = "ws error: " + msg->errorInfo.reason;
        status_    = Connection::Failed;
        std::fprintf(stderr, "[Net] WebSocket error: %s\n", msg->errorInfo.reason.c_str());
        break;
      default:
        break;
    }
  });
  ws_->start();
}

void NetworkClient::disconnect() {
  if (ws_) {
    ws_->stop();
    ws_.reset();
  }
  status_ = Connection::Disconnected;
}

void NetworkClient::onWsMessage(const std::string& msg) {
  std::lock_guard<std::mutex> lock(queueMtx_);
  queuedMessages_.push_back(msg);
}

void NetworkClient::onWsClose(int code, const std::string& reason) {
  std::fprintf(stdout, "[Net] WebSocket close %d: %s\n", code, reason.c_str());
  status_    = Connection::Disconnected;
  lastError_ = "ws closed: " + reason;
}

void NetworkClient::sendActionRaw(const std::string& body) {
  if (!ws_ || status_.load() != Connection::Connected) return;
  std::string out;
  out.reserve(body.size() + 32);
  out.append("{\"type\":\"actions\",\"actions\":[");
  out.append(body);
  out.append("]}");
  ws_->send(out);
}

namespace {

// JSON-escape a string. Covers control chars, quotes, and backslash —
// enough for chat / item-id / npc-id payloads we control.
std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b";  break;
      case '\f': out += "\\f";  break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

}  // namespace

void NetworkClient::sendMoveTo(int targetX, int targetY) {
  char buf[96];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"MOVE_TO\",\"targetX\":%d,\"targetY\":%d}",
                targetX, targetY);
  sendActionRaw(buf);
}

void NetworkClient::sendChopTree(int tileX, int tileY) {
  char buf[96];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"CHOP_TREE\",\"tileX\":%d,\"tileY\":%d}",
                tileX, tileY);
  sendActionRaw(buf);
}

void NetworkClient::sendMineRock(int tileX, int tileY) {
  char buf[96];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"MINE_ROCK\",\"tileX\":%d,\"tileY\":%d}",
                tileX, tileY);
  sendActionRaw(buf);
}

void NetworkClient::sendAttackNpc(const std::string& npcId) {
  sendActionRaw("{\"type\":\"ATTACK_NPC\",\"npcId\":\"" + jsonEscape(npcId) + "\"}");
}

void NetworkClient::sendTalkTo(const std::string& npcId) {
  sendActionRaw("{\"type\":\"TALK_TO\",\"npcId\":\"" + jsonEscape(npcId) + "\"}");
}

void NetworkClient::sendTakeItem(const std::string& droppedItemId) {
  sendActionRaw("{\"type\":\"TAKE_ITEM\",\"droppedItemId\":\"" + jsonEscape(droppedItemId) + "\"}");
}

void NetworkClient::sendDropItem(int slotIndex) {
  char buf[64];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"DROP_ITEM\",\"slotIndex\":%d}", slotIndex);
  sendActionRaw(buf);
}

void NetworkClient::sendMoveSlot(int fromSlot, int toSlot) {
  char buf[96];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"MOVE_SLOT\",\"fromSlot\":%d,\"toSlot\":%d}",
                fromSlot, toSlot);
  sendActionRaw(buf);
}

void NetworkClient::sendEquipItem(int slotIndex) {
  char buf[64];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"EQUIP_ITEM\",\"slotIndex\":%d}", slotIndex);
  sendActionRaw(buf);
}

void NetworkClient::sendUnequipItem(const std::string& equipSlot) {
  sendActionRaw("{\"type\":\"UNEQUIP_ITEM\",\"slot\":\"" + jsonEscape(equipSlot) + "\"}");
}

void NetworkClient::sendChat(const std::string& message) {
  sendActionRaw("{\"type\":\"SEND_CHAT\",\"message\":\"" + jsonEscape(message) + "\"}");
}

void NetworkClient::sendOpenBank() {
  sendActionRaw("{\"type\":\"OPEN_BANK\"}");
}

void NetworkClient::sendDepositItem(int slotIndex, int quantity) {
  char buf[96];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"DEPOSIT_ITEM\",\"slotIndex\":%d,\"quantity\":%d}",
                slotIndex, quantity);
  sendActionRaw(buf);
}

void NetworkClient::sendDepositAll()  { sendActionRaw("{\"type\":\"DEPOSIT_ALL\"}"); }
void NetworkClient::sendDepositWorn() { sendActionRaw("{\"type\":\"DEPOSIT_WORN\"}"); }
void NetworkClient::sendCloseBank()   { sendActionRaw("{\"type\":\"CLOSE_BANK\"}"); }

void NetworkClient::sendExamine(const std::string& itemId) {
  // The server looks up the item description and sends it back as a chat
  // message. If the server doesn't support EXAMINE yet, this is a no-op.
  char buf[128];
  std::snprintf(buf, sizeof(buf), "{\"type\":\"EXAMINE\",\"itemId\":\"%s\"}", itemId.c_str());
  sendActionRaw(buf);
}

void NetworkClient::sendWithdrawItem(int bankSlot, int quantity) {
  char buf[96];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"WITHDRAW_ITEM\",\"bankSlot\":%d,\"quantity\":%d}",
                bankSlot, quantity);
  sendActionRaw(buf);
}

std::vector<std::string> NetworkClient::drainMessages() {
  std::lock_guard<std::mutex> lock(queueMtx_);
  std::vector<std::string> out;
  out.swap(queuedMessages_);
  return out;
}

}  // namespace net
