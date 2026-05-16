#pragma once

#include "shared/SharedTypes.hpp"

#include <deque>
#include <string>
#include <unordered_map>

namespace ui {

// Phase 8a UI surface: read-only panels driven directly off the latest
// PlayerState. Interactive bits (drag-drop, context menu, bank) are Phase 8b.
//
// All panels are stateless ImGui windows — they read PlayerState each frame
// and re-draw. The one exception is the chat log, which keeps a rolling
// transcript of per-tick chatMessage observations.

void drawSkillsPanel    (const shared::PlayerState& p);
void drawInventoryPanel (const shared::PlayerState& p);
void drawEquipmentPanel (const shared::PlayerState& p);

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
  void draw();

private:
  struct Entry {
    std::string text;
    bool        system = false;
  };
  static constexpr std::size_t kMax = 200;
  std::deque<Entry>                        entries_;
  // Per-player id -> last seen chatMessageTick, so we only append on change.
  std::unordered_map<std::string, int>     seenChatTick_;
};

}  // namespace ui
