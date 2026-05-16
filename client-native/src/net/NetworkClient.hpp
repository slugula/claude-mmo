#pragma once

#include "shared/SharedTypes.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Forward declaration so this header doesn't pull in ixwebsocket
namespace ix { class WebSocket; }

namespace net {

enum class Connection {
  Disconnected,
  LoggingIn,
  Connecting,
  Connected,
  Failed,
};

// Couples the server's auth (POST /auth/login -> JWT) with its WebSocket
// game channel. Runs all I/O on a background thread; the application drains
// `pendingMessages()` from the main thread once per frame.
class NetworkClient {
public:
  NetworkClient();
  ~NetworkClient();

  NetworkClient(const NetworkClient&)            = delete;
  NetworkClient& operator=(const NetworkClient&) = delete;

  // Posts /auth/login then opens the WebSocket. Returns immediately; check
  // status() / lastError() for progress. Background-threaded.
  void loginAndConnect(std::string host, int port,
                       std::string username, std::string password);

  void disconnect();

  // Send a MOVE_TO action to the server. Encoded as the canonical
  // `{ type: "actions", actions: [{ type: "MOVE_TO", targetX, targetY }] }`
  // wire format. Thread-safe; queues internally if the socket isn't open
  // yet.
  void sendMoveTo(int targetX, int targetY);

  // ---- State accessors (main-thread reads) --------------------------------

  Connection         status()      const { return status_.load(); }
  const std::string& lastError()   const { return lastError_; }    // only safe to read after status() reports Failed
  const std::string& playerId()    const { return playerId_; }
  const std::string& playerName()  const { return playerName_; }

  // Take ownership of all messages received since the last drain. The vector
  // contains raw JSON strings — parse with glaze on the main thread.
  std::vector<std::string> drainMessages();

private:
  void runLoginThread(std::string host, int port,
                      std::string username, std::string password);
  void onWsMessage(const std::string& msg);
  void onWsClose(int code, const std::string& reason);

  std::atomic<Connection>    status_{Connection::Disconnected};
  std::string                lastError_;
  std::string                playerId_;
  std::string                playerName_;
  std::string                token_;
  std::string                host_;
  int                        port_ = 0;

  std::unique_ptr<ix::WebSocket> ws_;
  std::mutex                     queueMtx_;
  std::vector<std::string>       queuedMessages_;
};

}  // namespace net
