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

  // Run on a detached thread so we don't block the render loop. The
  // background thread updates atomic status_ and string fields, which are
  // safe to read on the main thread (lastError_/playerId_/playerName_ are
  // only read once status reaches Failed or Connected).
  std::thread([this, u = std::move(username), p = std::move(password)]() mutable {
    runLoginThread(host_, port_, std::move(u), std::move(p));
  }).detach();
}

void NetworkClient::runLoginThread(std::string host, int port,
                                   std::string username, std::string password) {
  // ---- HTTP POST /auth/login ---------------------------------------------
  ix::HttpClient http;
  auto args = http.createRequest();
  args->extraHeaders["Content-Type"] = "application/json";

  // Hand-build the request body. The server's express.json() parser accepts
  // any conformant JSON; we keep escaping minimal because username/password
  // are user-typed ASCII.
  std::string body = "{\"username\":\"" + username + "\",\"password\":\"" + password + "\"}";

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

void NetworkClient::sendMoveTo(int targetX, int targetY) {
  if (!ws_ || status_.load() != Connection::Connected) return;
  // Wire format: { type: "actions", actions: [{ type: "MOVE_TO", targetX, targetY }] }
  char buf[160];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"actions\",\"actions\":[{\"type\":\"MOVE_TO\",\"targetX\":%d,\"targetY\":%d}]}",
                targetX, targetY);
  ws_->send(buf);
}

std::vector<std::string> NetworkClient::drainMessages() {
  std::lock_guard<std::mutex> lock(queueMtx_);
  std::vector<std::string> out;
  out.swap(queuedMessages_);
  return out;
}

}  // namespace net
