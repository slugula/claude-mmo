#include "ui/Panels.hpp"

#include "net/NetworkClient.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
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

// Paint a slot background + item label at the current cursor position, then
// consume the cell area as an InvisibleButton so callers can attach
// drag-drop sources/targets and context-menu popups to the same hit-region.
// Returns true when the cell was clicked this frame.
bool drawSlot(const char*                              idStr,
              const std::optional<shared::ItemStack>& slot,
              ImVec2                                   size) {
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
  // InvisibleButton gives us hover/click + drag-drop hooks on a real ImGui
  // item, instead of a dead Dummy.
  return ImGui::InvisibleButton(idStr, size);
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
//
// Per-slot interactions:
//   - Drag a non-empty slot onto another slot  -> MOVE_SLOT
//   - Right-click a non-empty slot             -> popup: Equip / Drop
//   - Hover                                    -> tooltip with itemId + qty
//
// The drag payload is the source slot index. Server validates everything.

void drawInventoryPanel(const shared::PlayerState& p, net::NetworkClient* netc) {
  if (!ImGui::Begin("Inventory")) { ImGui::End(); return; }

  const float cell = 44.0f;
  const float pad  = 4.0f;

  for (int r = 0; r < kInventoryRows; ++r) {
    for (int c = 0; c < kInventoryCols; ++c) {
      const int idx = r * kInventoryCols + c;
      std::optional<shared::ItemStack> slot;
      if (idx < static_cast<int>(p.inventory.size())) slot = p.inventory[idx];

      char idBuf[24];
      std::snprintf(idBuf, sizeof(idBuf), "##invslot_%d", idx);

      ImGui::PushID(idx);
      drawSlot(idBuf, slot, ImVec2(cell, cell));

      // ---- Drag source (only when the slot has an item) -------------------
      if (slot && netc &&
          ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        const int payload = idx;
        ImGui::SetDragDropPayload("INV_SLOT", &payload, sizeof(payload));
        ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
        ImGui::EndDragDropSource();
      }
      // ---- Drag target -----------------------------------------------------
      if (netc && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("INV_SLOT")) {
          int from = *static_cast<const int*>(pl->Data);
          if (from != idx) netc->sendMoveSlot(from, idx);
        }
        ImGui::EndDragDropTarget();
      }
      // ---- Right-click popup ----------------------------------------------
      if (slot && netc && ImGui::BeginPopupContextItem("inv_ctx")) {
        ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
        ImGui::Separator();
        if (ImGui::Selectable("Equip")) { netc->sendEquipItem(idx); }
        if (ImGui::Selectable("Drop"))  { netc->sendDropItem (idx); }
        ImGui::EndPopup();
      }
      // ---- Hover tooltip ---------------------------------------------------
      if (slot && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
        if (slot->quantity > 1) ImGui::Text("Quantity: %d", slot->quantity);
        ImGui::EndTooltip();
      }
      ImGui::PopID();

      if (c + 1 < kInventoryCols) ImGui::SameLine(0.0f, pad);
    }
  }
  ImGui::End();
}

// ---- Equipment -------------------------------------------------------------
//
// Right-click a populated slot to unequip — server moves the item back into
// the first free inventory slot.

void drawEquipmentPanel(const shared::PlayerState& p, net::NetworkClient* netc) {
  if (!ImGui::Begin("Equipment")) { ImGui::End(); return; }

  const float cell = 56.0f;
  const float pad  = 4.0f;
  for (int row = 0; row < 5; ++row) {
    for (int col = 0; col < 3; ++col) {
      const EquipCell* match = nullptr;
      for (const auto& e : kEquipGrid) {
        if (e.row == row && e.col == col) { match = &e; break; }
      }
      if (match && match->slotId && match->slotId[0] != '\0') {
        std::optional<shared::ItemStack> slot;
        auto it = p.equipped.find(match->slotId);
        if (it != p.equipped.end()) slot = it->second;

        char idBuf[32];
        std::snprintf(idBuf, sizeof(idBuf), "##eqslot_%s", match->slotId);
        ImGui::PushID(match->slotId);
        drawSlot(idBuf, slot, ImVec2(cell, cell));

        if (slot && netc && ImGui::BeginPopupContextItem("eq_ctx")) {
          ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
          ImGui::Separator();
          if (ImGui::Selectable("Remove")) {
            netc->sendUnequipItem(match->slotId);
          }
          ImGui::EndPopup();
        }
        if (slot && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
          ImGui::BeginTooltip();
          ImGui::Text("%s — %s", match->label, prettyItemId(slot->itemId).c_str());
          ImGui::EndTooltip();
        }
        ImGui::PopID();
      } else {
        ImGui::Dummy(ImVec2(cell, cell));
      }
      if (col + 1 < 3) ImGui::SameLine(0.0f, pad);
    }
  }
  ImGui::End();
}

// ---- Bank ------------------------------------------------------------------
//
// 8 columns x N rows. Right-click a populated slot for Withdraw 1 / Withdraw
// all. Deposit-side actions live as top-row buttons. The bank's "open"
// state is purely client-side — there's no server flag, so the App owns
// the bool and the panel closes by writing through `open`.

void drawBankPanel(const shared::PlayerState& p, net::NetworkClient* netc, bool* open) {
  if (!open || !*open) return;
  if (!ImGui::Begin("Bank", open)) { ImGui::End(); return; }

  if (ImGui::Button("Deposit all"))   { if (netc) netc->sendDepositAll();  }
  ImGui::SameLine();
  if (ImGui::Button("Deposit worn"))  { if (netc) netc->sendDepositWorn(); }
  ImGui::SameLine();
  ImGui::TextDisabled("(%d slots)", static_cast<int>(p.bank.size()));
  ImGui::Separator();

  constexpr int   kCols = 8;
  constexpr float kCell = 44.0f;
  constexpr float kPad  = 3.0f;

  // Inventory mini-strip for one-click deposit. 4 cols x 7 rows on the
  // right side wouldn't fit alongside the bank grid in a small window, so
  // we put the bank above and the inventory deposit list below.
  ImGui::BeginChild("bank_grid", ImVec2(0, kCell * 6 + 12), true);
  for (int i = 0; i < static_cast<int>(p.bank.size()); ++i) {
    const auto& slot = p.bank[i];
    char idBuf[24];
    std::snprintf(idBuf, sizeof(idBuf), "##bankslot_%d", i);
    ImGui::PushID(i);
    drawSlot(idBuf, slot, ImVec2(kCell, kCell));
    if (slot && netc && ImGui::BeginPopupContextItem("bank_ctx")) {
      ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
      ImGui::Separator();
      if (ImGui::Selectable("Withdraw 1"))   netc->sendWithdrawItem(i, 1);
      if (ImGui::Selectable("Withdraw all")) netc->sendWithdrawItem(i, slot->quantity);
      ImGui::EndPopup();
    }
    if (slot && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
      ImGui::BeginTooltip();
      ImGui::Text("%s  (%d)", prettyItemId(slot->itemId).c_str(), slot->quantity);
      ImGui::EndTooltip();
    }
    ImGui::PopID();
    if ((i + 1) % kCols != 0) ImGui::SameLine(0.0f, kPad);
  }
  ImGui::EndChild();

  ImGui::Separator();
  ImGui::TextUnformatted("Inventory  (right-click to Deposit)");
  ImGui::BeginChild("bank_inv", ImVec2(0, kCell * 2 + 12), true);
  for (int i = 0; i < static_cast<int>(p.inventory.size()); ++i) {
    const auto& slot = p.inventory[i];
    char idBuf[24];
    std::snprintf(idBuf, sizeof(idBuf), "##binv_%d", i);
    ImGui::PushID(i);
    drawSlot(idBuf, slot, ImVec2(kCell, kCell));
    if (slot && netc && ImGui::BeginPopupContextItem("binv_ctx")) {
      ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
      ImGui::Separator();
      if (ImGui::Selectable("Deposit 1"))   netc->sendDepositItem(i, 1);
      if (ImGui::Selectable("Deposit all")) netc->sendDepositItem(i, slot->quantity);
      ImGui::EndPopup();
    }
    ImGui::PopID();
    if ((i + 1) % kCols != 0) ImGui::SameLine(0.0f, kPad);
  }
  ImGui::EndChild();

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

void ChatLog::draw(net::NetworkClient* netc) {
  if (!ImGui::Begin("Chat")) { ImGui::End(); return; }
  // Reserve room for the input field at the bottom when a network client
  // is wired up.
  const float reserveH = netc
      ? (ImGui::GetFrameHeightWithSpacing())
      : 0.0f;
  ImGui::BeginChild("chat_scroll", ImVec2(0, -reserveH), false,
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
  if (netc) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##chat_in", inputBuf_, sizeof(inputBuf_),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
      if (inputBuf_[0] != '\0') {
        netc->sendChat(inputBuf_);
        // Mirror locally so the speaker sees it immediately; remote
        // observers see it via the next chatMessageTick.
        entries_.push_back({ std::string("You: ") + inputBuf_, false });
        while (entries_.size() > kMax) entries_.pop_front();
      }
      inputBuf_[0] = '\0';
      ImGui::SetKeyboardFocusHere(-1);   // refocus input for fast follow-ups
    }
  }
  ImGui::End();
}

}  // namespace ui
