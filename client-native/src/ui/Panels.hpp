#pragma once

#include "shared/SharedTypes.hpp"

#include <deque>
#include <string>
#include <unordered_map>

namespace net { class NetworkClient; }

namespace ui {

// Phase 8a UI surface: read-only panels driven directly off the latest
// PlayerState. Interactive bits (drag-drop, context menu, bank) are Phase 8b.
//
// All panels are stateless ImGui windows — they read PlayerState each frame
// and re-draw. The one exception is the chat log, which keeps a rolling
// transcript of per-tick chatMessage observations.

void drawSkillsPanel    (const shared::PlayerState& p);
// Inventory + Equipment panels accept a NetworkClient so right-click
// "Drop / Equip / Remove" + drag-drop reordering can post actions back
// to the server. Pass nullptr for a strictly read-only render.
void drawInventoryPanel (const shared::PlayerState& p, net::NetworkClient* net);
void drawEquipmentPanel (const shared::PlayerState& p, net::NetworkClient* net);
// Optional bank panel. `open` is owned by the caller — pass &someBool from
// App; the panel will set it to false on close. Returns nothing.
void drawBankPanel      (const shared::PlayerState& p, net::NetworkClient* net,
                         bool* open);

// Rolling chat log. Append system messages via appendSystem(); call
// observePlayerChat() once per frame with the local player + a map of remote
// players (any state changes get logged as "Name: message").
class ChatLog {
public:
  void appendSystem(std::string line);
  // Detect new chatMessage(tick) on any player and append. Use the player's
  // playerName as the speaker. Caller passes in the union of (local + remote)
  // players keyed by id.
  void observePlayers(const std::unordered_map<std::string, shared::PlayerState>& players);
  // When `net` is non-null, an input field is drawn at the bottom of the
  // log; submitting it forwards the message to net->sendChat().
  void draw(net::NetworkClient* net);

private:
  struct Entry {
    std::string text;
    bool        system = false;
  };
  static constexpr std::size_t kMax = 200;
  std::deque<Entry>                        entries_;
  // Per-player id -> last seen chatMessageTick, so we only append on change.
  std::unordered_map<std::string, int>     seenChatTick_;
  // Buffer for the input field — kept across frames so partial typing
  // survives re-renders.
  char                                     inputBuf_[256] = {};
};

}  // namespace ui
