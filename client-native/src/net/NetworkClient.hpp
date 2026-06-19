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
  void loginAndConnect   (std::string host, int port,
                          std::string username, std::string password);

  // Posts /auth/register (creates account) then logs in automatically.
  void registerAndConnect(std::string host, int port,
                          std::string username, std::string password);

  void disconnect();

  // Send a single GameAction. Body is a pre-encoded JSON object — e.g.
  // `{"type":"CHOP_TREE","tileX":5,"tileY":7}`. We wrap it in the canonical
  // `{"type":"actions","actions":[<body>]}` envelope and ship it. Threadsafe;
  // silently drops if the WebSocket isn't connected.
  void sendActionRaw(const std::string& body);

  // Typed helpers — each builds its body and forwards to sendActionRaw().
  void sendMoveTo      (int targetX, int targetY);
  void sendChopTree    (int tileX,   int tileY);
  void sendMineRock    (int tileX,   int tileY);
  void sendFish        (int tileX,   int tileY);
  void sendUseFacility (int tileX,   int tileY);
  void sendEatFood     (int slotIndex);
  void sendAttackNpc   (const std::string& npcId);
  void sendTalkTo      (const std::string& npcId);
  void sendTakeItem    (const std::string& droppedItemId);
  void sendDropItem    (int slotIndex);
  void sendMoveSlot    (int fromSlot, int toSlot);
  void sendMoveBankSlot(int fromSlot, int toSlot);
  void sendEquipItem   (int slotIndex);
  void sendUnequipItem (const std::string& equipSlot);
  void sendChat        (const std::string& message);
  // tileX/tileY = the chest tile so the server can turn the player to face it
  // (-1 = none, e.g. the debug Open-bank button).
  void sendOpenBank    (int tileX = -1, int tileY = -1);
  void sendDepositItem (int slotIndex, int quantity);
  void sendDepositAll  ();
  void sendDepositWorn ();
  void sendWithdrawItem(int bankSlot, int quantity);
  void sendCloseBank  ();
  void sendExamine    (const std::string& itemId);
  // Top-level interest-management message (not a GameAction): how many tiles
  // of entities the server should sync around this player.
  void sendSetViewRadius(int radius);
  // Top-level streaming message: how many 64-tile chunks of terrain the client
  // wants streamed around it (its draw distance). Server streams within +1.
  void sendSetChunkRadius(int radiusChunks);

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
                      std::string username, std::string password,
                      bool registerFirst = false);
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
