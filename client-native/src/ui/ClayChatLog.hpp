#pragma once
// ClayChatLog — bottom-left ATTACH_TO_ROOT floating chat panel.
// 560×210, scrollable message area, global-keyboard-capture input line.
// System messages: gold {255,224,102}. Player chat: white.
//
// Usage:
//   chatAppendSystem("text")          — from anywhere (app, server messages)
//   chatObservePlayers(allPlayers)    — once per tick to pick up chat messages
//   buildChatLog(w, h, player, netc) — between Clay_BeginLayout / Clay_EndLayout

#include "shared/SharedTypes.hpp"
#include <string>
#include <unordered_map>

namespace net { class NetworkClient; }

namespace ui {

// Append a system (gold) message.
void chatAppendSystem(std::string line);

// Detect new chatMessage(tick) on any player and append as a player (white)
// line. currentTick = the server tick of the state being observed; a message is
// only logged if it was sent on (about) this tick AND the speaker is in chat
// range (server blanks chatMessage out of range), so messages sent while out of
// earshot are never logged late when the speaker walks back into range.
void chatObservePlayers(
    const std::unordered_map<std::string, shared::PlayerState>& players,
    int currentTick);

// Build the Clay chat panel layout. Call between Clay_BeginLayout / Clay_EndLayout.
// netc == nullptr suppresses the input field (e.g. before login).
void buildChatLog(float screenW, float screenH,
                  const shared::PlayerState* player,
                  net::NetworkClient* netc);

} // namespace ui
