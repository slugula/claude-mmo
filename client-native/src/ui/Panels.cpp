#include "ui/Panels.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace ui {

namespace {

// OSRS skill order. Skills the server hasn't sent yet just show as level 1 / 0
// xp so the panel layout doesn't shuffle.
constexpr std::array<const char*, 9> kSkillOrder = {
  "hitpoints",
  "attack",
  "strength",
  "defence",
  "ranged",
  "magic",
  "prayer",
  "woodcutting",
  "fishing",
};

constexpr int kInventoryCols = 4;
constexpr int kInventoryRows = 7;       // 4 * 7 = 28 slots, OSRS layout

// Equipment slot grid (5 rows x 3 cols).
struct EquipCell {
  int         row;
  int         col;
  const char* slotId;        // empty = blank cell
  const char* label;
};

constexpr std::array<EquipCell, 15> kEquipGrid = {{
  {0, 0, "",          ""        }, {0, 1, "head",      "Head"   }, {0, 2, "",          ""        },
  {1, 0, "",          ""        }, {1, 1, "neck",      "Neck"   }, {1, 2, "ammo",      "Ammo"    },
  {2, 0, "rightHand", "Main"    }, {2, 1, "body",      "Body"   }, {2, 2, "leftHand",  "Off"     },
  {3, 0, "",          ""        }, {3, 1, "legs",      "Legs"   }, {3, 2, "",          ""        },
  {4, 0, "hands",     "Hands"   }, {4, 1, "feet",      "Feet"   }, {4, 2, "ring",      "Ring"    },
}};

// Pretty-format an itemId ("bronze_sword" -> "Bronze sword") for the
// placeholder text labels. Until Phase 8b adds an icon atlas this is the
// player-visible representation.
std::string prettyItemId(const std::string& id) {
  if (id.empty()) return {};
  std::string out;
  out.reserve(id.size());
  bool capitalize = true;
  for (char c : id) {
    if (c == '_' || c == '-') {
      out.push_back(' ');
      capitalize = false;
    } else if (capitalize) {
      out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
      capitalize = false;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

void drawSlot(const std::optional<shared::ItemStack>& slot, ImVec2 size) {
  const ImU32 border = IM_COL32(70, 70, 70, 255);
  const ImU32 fill   = IM_COL32(35, 35, 35, 255);
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), fill);
  dl->AddRect      (p, ImVec2(p.x + size.x, p.y + size.y), border);
  if (slot && !slot->itemId.empty()) {
    const std::string label = prettyItemId(slot->itemId);
    ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    ImVec2 textPos { p.x + (size.x - textSize.x) * 0.5f,
                     p.y + (size.y - textSize.y) * 0.5f - 6.0f };
    dl->AddText(textPos, IM_COL32(220, 200, 120, 255), label.c_str());
    if (slot->quantity > 1) {
      char qty[16];
      std::snprintf(qty, sizeof(qty), "%d", slot->quantity);
      dl->AddText(ImVec2(p.x + 3, p.y + 2), IM_COL32(255, 230, 100, 255), qty);
    }
  }
  ImGui::Dummy(size);
}

}  // namespace

// ---- Skills ---------------------------------------------------------------

void drawSkillsPanel(const shared::PlayerState& p) {
  if (!ImGui::Begin("Skills")) { ImGui::End(); return; }

  int totalLevel = 0;
  long long totalXp = 0;
  for (const char* id : kSkillOrder) {
    auto it = p.skills.find(id);
    if (it != p.skills.end()) {
      totalLevel += it->second.level;
      totalXp    += it->second.xp;
    } else {
      totalLevel += 1;
    }
  }

  if (ImGui::BeginTable("skills_tbl", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
    ImGui::TableSetupColumn("Skill",  ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Level",  ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("XP",     ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();
    for (const char* id : kSkillOrder) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(prettyItemId(id).c_str());
      auto it = p.skills.find(id);
      const int   lvl = (it != p.skills.end()) ? it->second.level : 1;
      const int   xp  = (it != p.skills.end()) ? it->second.xp    : 0;
      ImGui::TableSetColumnIndex(1); ImGui::Text("%d", lvl);
      ImGui::TableSetColumnIndex(2); ImGui::Text("%d", xp);
    }
    ImGui::EndTable();
  }
  ImGui::Separator();
  ImGui::Text("Total level: %d", totalLevel);
  ImGui::Text("Total XP:    %lld", totalXp);
  ImGui::End();
}

// ---- Inventory -------------------------------------------------------------

void drawInventoryPanel(const shared::PlayerState& p) {
  if (!ImGui::Begin("Inventory")) { ImGui::End(); return; }

  const float cell = 44.0f;
  const float pad  = 4.0f;
  for (int r = 0; r < kInventoryRows; ++r) {
    for (int c = 0; c < kInventoryCols; ++c) {
      const int idx = r * kInventoryCols + c;
      std::optional<shared::ItemStack> slot;
      if (idx < static_cast<int>(p.inventory.size())) slot = p.inventory[idx];
      drawSlot(slot, ImVec2(cell, cell));
      if (c + 1 < kInventoryCols) ImGui::SameLine(0.0f, pad);
    }
  }
  ImGui::End();
}

// ---- Equipment -------------------------------------------------------------

void drawEquipmentPanel(const shared::PlayerState& p) {
  if (!ImGui::Begin("Equipment")) { ImGui::End(); return; }

  const float cell = 56.0f;
  const float pad  = 4.0f;
  for (int row = 0; row < 5; ++row) {
    for (int col = 0; col < 3; ++col) {
      // Find the cell for (row, col)
      const EquipCell* match = nullptr;
      for (const auto& e : kEquipGrid) {
        if (e.row == row && e.col == col) { match = &e; break; }
      }
      if (match && match->slotId && match->slotId[0] != '\0') {
        std::optional<shared::ItemStack> slot;
        auto it = p.equipped.find(match->slotId);
        if (it != p.equipped.end()) slot = it->second;
        drawSlot(slot, ImVec2(cell, cell));
      } else {
        ImGui::Dummy(ImVec2(cell, cell));
      }
      if (col + 1 < 3) ImGui::SameLine(0.0f, pad);
    }
  }
  ImGui::End();
}

// ---- Chat log --------------------------------------------------------------

void ChatLog::appendSystem(std::string line) {
  entries_.push_back({ std::move(line), true });
  while (entries_.size() > kMax) entries_.pop_front();
}

void ChatLog::observePlayers(const std::unordered_map<std::string, shared::PlayerState>& players) {
  for (const auto& [id, p] : players) {
    if (p.chatMessage.empty() || p.chatMessageTick <= 0) continue;
    auto it = seenChatTick_.find(id);
    if (it != seenChatTick_.end() && it->second == p.chatMessageTick) continue;
    seenChatTick_[id] = p.chatMessageTick;
    std::string speaker = p.playerName.empty() ? id : p.playerName;
    entries_.push_back({ speaker + ": " + p.chatMessage, false });
    while (entries_.size() > kMax) entries_.pop_front();
  }
}

void ChatLog::draw() {
  if (!ImGui::Begin("Chat")) { ImGui::End(); return; }
  ImGui::BeginChild("chat_scroll", ImVec2(0, 0), false,
                    ImGuiWindowFlags_HorizontalScrollbar);
  for (const auto& e : entries_) {
    const ImVec4 color = e.system ? ImVec4(1.0f, 0.92f, 0.30f, 1.0f)
                                  : ImVec4(1.0f, 1.0f,  1.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", e.text.c_str());
    ImGui::PopStyleColor();
  }
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
    ImGui::SetScrollHereY(1.0f);
  }
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace ui
