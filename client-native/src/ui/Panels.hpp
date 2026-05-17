#pragma once

#include "shared/SharedTypes.hpp"

#include <deque>
#include <string>
#include <unordered_map>

namespace net { class NetworkClient; }

namespace ui {

// Right-side HUD: fixed window containing Inventory / Skills / Equipment tabs.
void drawHudPanel  (const shared::PlayerState& p, net::NetworkClient* net);

// Bank panel — centred modal.  `open` is owned by the caller.
void drawBankPanel (const shared::PlayerState& p, net::NetworkClient* net,
                    bool* open);

// Rolling chat log. Append system messages via appendSystem(); call
// observePlayers() once per frame with the local+remote player map to
// pick up new chat lines from the server.
class ChatLog {
public:
  void appendSystem(std::string line);
  // Detect new chatMessage(tick) on any player and append.
  void observePlayers(const std::unordered_map<std::string, shared::PlayerState>& players);
  // Fixed bottom-left ImGui window with scrolling history + optional input.
  // Pass nullptr for net to suppress the input field (e.g. before login).
  void draw(net::NetworkClient* net);

private:
  struct Entry {
    std::string text;
    bool        system = false;
  };
  static constexpr std::size_t kMax = 200;
  std::deque<Entry>                        entries_;
  std::unordered_map<std::string, int>     seenChatTick_;
  char                                     inputBuf_[256] = {};
};

}  // namespace ui
