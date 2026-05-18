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

// ---- OSRS colour constants --------------------------------------------------
constexpr ImU32 kSlotBg       = IM_COL32(13,  6,  0, 220);   // empty slot fill
constexpr ImU32 kSlotBgFilled = IM_COL32(30, 16,  4, 240);   // slot with item
constexpr ImU32 kSlotBorder   = IM_COL32(107, 79, 41, 180);  // default border
constexpr ImU32 kSlotHover    = IM_COL32(255,152, 31, 255);  // hover border (orange)
constexpr ImU32 kItemText     = IM_COL32(240,206, 96, 255);  // item name (gold)
constexpr ImU32 kQtyText      = IM_COL32(255,221, 68, 255);  // quantity (#ffdd44)
constexpr ImU32 kSlotLabel    = IM_COL32(120,100, 60, 200);  // empty-slot hint letter

// ---- XP thresholds per level -----------------------------------------------
// Index i = XP required to reach level (i+2). XP_TABLE[0]=83 means level 2
// requires 83 XP. Matches TypeScript's XP_TABLE exactly.
static constexpr std::array<int, 98> kXpTable = {
  83, 174, 276, 388, 512, 650, 801, 969, 1154, 1358, 1584, 1833, 2107, 2411,
  2746, 3115, 3523, 3973, 4470, 5018, 5624, 6291, 7028, 7842, 8740, 9730,
  10824, 12031, 13363, 14833, 16456, 18247, 20224, 22406, 24815, 27473,
  30408, 33648, 37224, 41171, 45529, 50339, 55649, 61512, 67983, 75127,
  83014, 91721, 101333, 111945, 123660, 136594, 150872, 166636, 184040,
  203254, 224466, 247886, 273742, 302288, 333804, 368599, 407015, 449428,
  496254, 547953, 605032, 668051, 737627, 814445, 899257, 992895, 1096278,
  1210421, 1336443, 1475581, 1629200, 1798808, 1986068, 2192818, 2421087,
  2673114, 2951373, 3258594, 3597792, 3972294, 4385776, 4842295, 5346332,
  5902831, 6517253, 7195629, 7944614, 8771558, 9684577, 10692629, 11805606,
  13034431,
};

// Returns the XP required to REACH the given level (level 1 = 0).
static int xpForLevel(int lvl) {
  if (lvl <= 1) return 0;
  if (lvl >= 99) return kXpTable[97];
  return kXpTable[lvl - 2];
}

// ---- Skill / equip meta ----------------------------------------------------
// Matches server's VISIBLE_SKILLS: hitpoints, defence, warrior, gunner, woodcutting
constexpr std::array<const char*, 5> kSkillOrder = {
  "hitpoints", "defence", "warrior", "gunner", "woodcutting",
};

// Skill icon placeholder colors — matches production TS client palette
constexpr std::array<ImU32, 5> kSkillColors = {
  IM_COL32(220,  40,  40, 255),  // hitpoints  — red     (#e06060)
  IM_COL32( 60, 120, 220, 255),  // defence    — blue    (#60a0e0)
  IM_COL32(200, 136,  44, 255),  // warrior    — orange  (#d4882c)
  IM_COL32(  0, 207, 255, 255),  // gunner     — cyan    (#00cfff)
  IM_COL32( 80, 144,  64, 255),  // woodcutting — green  (#509040)
};

constexpr int kInventoryCols = 4;
constexpr int kInventoryRows = 7;

// Maximum bank capacity shown in the fraction display.
constexpr int kMaxBankSlots = 400;

struct EquipCell {
  int         row, col;
  const char* slotId;
  const char* label;
};
constexpr std::array<EquipCell, 15> kEquipGrid = {{
  {0,0,"",         ""     }, {0,1,"head",     "Head" }, {0,2,"",        ""    },
  {1,0,"",         ""     }, {1,1,"neck",     "Neck" }, {1,2,"ammo",    "Ammo"},
  {2,0,"rightHand","Main" }, {2,1,"body",     "Body" }, {2,2,"leftHand","Off" },
  {3,0,"",         ""     }, {3,1,"legs",     "Legs" }, {3,2,"",        ""    },
  {4,0,"hands",    "Hands"}, {4,1,"feet",     "Feet" }, {4,2,"ring",    "Ring"},
}};

// "bronze_sword" -> "Bronze sword"
std::string prettyItemId(const std::string& id) {
  if (id.empty()) return {};
  std::string out;
  out.reserve(id.size());
  bool cap = true;
  for (char ch : id) {
    if (ch == '_' || ch == '-') { out.push_back(' '); cap = false; }
    else if (cap) { out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))); cap = false; }
    else           out.push_back(ch);
  }
  return out;
}

// Format a quantity for display: 1,500,000 → "1.5M", 2500 → "2.5k", etc.
std::string fmtQty(int q) {
  char buf[16];
  if (q >= 10000000)     std::snprintf(buf, sizeof(buf), "%dM",  q / 1000000);
  else if (q >= 1000000) std::snprintf(buf, sizeof(buf), "%.1fM", q / 1000000.0f);
  else if (q >= 10000)   std::snprintf(buf, sizeof(buf), "%dk",  q / 1000);
  else if (q >= 1000)    std::snprintf(buf, sizeof(buf), "%.1fk", q / 1000.0f);
  else                   std::snprintf(buf, sizeof(buf), "%d",   q);
  return buf;
}

// ---- Item definition helpers (mirrors src/data/items.json) -----------------
// Returns the equip slot string for an item, or "" if not equippable.
const char* equipSlotForItem(const std::string& id) {
  // rightHand — weapons and tools
  if (id == "axe"           || id == "iron_axe"       ||
      id == "pickaxe"       || id == "bronze_sword"    ||
      id == "iron_sword"    || id == "bronze_longsword"||
      id == "basic_chaingun") return "rightHand";
  // leftHand
  if (id == "bronze_shield") return "leftHand";
  // ammo
  if (id == "arrow" || id == "kinetic_charges") return "ammo";
  // armour
  if (id == "leather_helm"  || id == "bronze_helm")   return "head";
  if (id == "leather_body")                           return "body";
  if (id == "leather_legs")                           return "legs";
  if (id == "leather_gloves")                         return "hands";
  if (id == "leather_boots")                          return "feet";
  if (id == "gold_ring")                              return "ring";
  if (id == "amulet")                                 return "neck";
  return "";
}

// ---- Equipment stat bonuses (mirrors src/data/items.json) -----------------
struct ItemStats { int mAtk=0, mStr=0, mDef=0, rAtk=0, rStr=0, rDef=0; };
ItemStats statsForItem(const std::string& id) {
  if (id=="axe")               return { 4, 0, 0, 0, 0, 0};
  if (id=="iron_axe")          return {-1, 0, 2, 0, 0, 0};
  if (id=="pickaxe")           return { 6, 0, 1, 0, 0, 0};
  if (id=="bronze_sword")      return { 6, 3, 0, 0, 0, 0};
  if (id=="iron_sword")        return {10, 5, 0, 0, 0, 0};
  if (id=="bronze_longsword")  return { 8, 7, 0, 0, 0, 0};
  if (id=="basic_chaingun")    return { 0, 0, 0, 8, 4, 0};
  if (id=="bronze_shield")     return { 0, 0, 5, 0, 0, 0};
  if (id=="leather_helm")      return { 0, 0, 1, 0, 0, 0};
  if (id=="bronze_helm")       return { 0, 0, 3, 0, 0, 0};
  if (id=="leather_body")      return { 0, 0, 2, 0, 0, 0};
  if (id=="leather_legs")      return { 0, 0, 1, 0, 0, 0};
  if (id=="leather_gloves")    return { 0, 0, 1, 0, 0, 0};
  if (id=="leather_boots")     return { 0, 0, 1, 0, 0, 0};
  if (id=="amulet")            return { 0, 4, 0, 0, 0, 0};
  return {};
}

// Returns true for cooked food items that can be eaten.
bool isFood(const std::string& id) {
  return id == "shrimp" || id == "trout";
}

// Returns the primary left-click verb for an inventory item.
// "Wield" for hand-slot weapons/tools, "Wear" for armour/ammo,
// "Eat" for food, "" if there is no primary action.
const char* primaryInventoryVerb(const std::string& id) {
  const char* slot = equipSlotForItem(id);
  if (slot[0] != '\0') {
    const bool isHandSlot = (std::strcmp(slot, "rightHand") == 0 ||
                              std::strcmp(slot, "leftHand")  == 0);
    return isHandSlot ? "Wield" : "Wear";
  }
  if (isFood(id)) return "Eat";
  return "";
}

// Draw a single item slot at the current cursor position.
// Returns true when the invisible-button was clicked.
bool drawSlot(const char* id,
              const std::optional<shared::ItemStack>& slot,
              ImVec2 sz) {
  ImVec2 p  = ImGui::GetCursorScreenPos();
  ImVec2 p2 { p.x + sz.x, p.y + sz.y };
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const bool filled = slot && !slot->itemId.empty();
  dl->AddRectFilled(p, p2, filled ? kSlotBgFilled : kSlotBg);  // no rounding

  bool clicked = ImGui::InvisibleButton(id, sz);
  const bool hovered = ImGui::IsItemHovered();

  dl->AddRect(p, p2, hovered ? kSlotHover : kSlotBorder);  // no rounding

  if (filled) {
    const std::string label = prettyItemId(slot->itemId);
    ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    ImVec2 tp { p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f - 5.0f };
    dl->AddText(tp, kItemText, label.c_str());
    if (slot->quantity > 1) {
      const std::string qs = fmtQty(slot->quantity);
      dl->AddText(ImVec2(p.x + 3.0f, p.y + 2.0f), kQtyText, qs.c_str());
    }
  }
  return clicked;
}

// Draw an empty equipment slot placeholder — shows the first letter of the
// slot label centered in grey, so users can see what goes where.
void drawEmptyEquipSlot(const char* label, ImVec2 sz) {
  ImVec2 p  = ImGui::GetCursorScreenPos();
  ImVec2 p2 { p.x + sz.x, p.y + sz.y };
  ImDrawList* dl = ImGui::GetWindowDrawList();

  dl->AddRectFilled(p, p2, kSlotBg);
  dl->AddRect(p, p2, kSlotBorder);

  // First letter centered
  if (label && label[0] != '\0') {
    char ch[2] = { label[0], '\0' };
    ImVec2 ts = ImGui::CalcTextSize(ch);
    dl->AddText(
      ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f),
      kSlotLabel, ch);
  }

  // Consume the space so layout advances correctly.
  ImGui::Dummy(sz);
}

// ---- Inventory tab (internal) -----------------------------------------------
void drawInventoryTab(const shared::PlayerState& p, net::NetworkClient* netc,
                      UiHoverState* hover) {
  constexpr float kCell = 44.0f;
  constexpr float kPad  =  3.0f;

  // Centre the 4-column grid in the available content width.
  const float avail   = ImGui::GetContentRegionAvail().x;
  const float gridW   = kInventoryCols * kCell + (kInventoryCols - 1) * kPad;
  const float startX  = ImGui::GetCursorPosX() + std::max(0.0f, (avail - gridW) * 0.5f);

  for (int r = 0; r < kInventoryRows; ++r) {
    ImGui::SetCursorPosX(startX);
    for (int c = 0; c < kInventoryCols; ++c) {
      const int idx = r * kInventoryCols + c;
      std::optional<shared::ItemStack> slot;
      if (idx < static_cast<int>(p.inventory.size())) slot = p.inventory[idx];

      char idbuf[24];
      std::snprintf(idbuf, sizeof(idbuf), "##inv%d", idx);

      ImGui::PushID(idx);
      const bool clicked = drawSlot(idbuf, slot, ImVec2(kCell, kCell));

      // Left-click: equip only if the item has an equip slot.
      if (clicked && slot && netc) {
        if (equipSlotForItem(slot->itemId)[0] != '\0')
          netc->sendEquipItem(idx);
        // else: non-equippable — left-click does nothing (right-click has actions)
      }

      if (slot && netc && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("INV_SLOT", &idx, sizeof(idx));
        ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
        ImGui::EndDragDropSource();
      }
      if (netc && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("INV_SLOT")) {
          int from = *static_cast<const int*>(pl->Data);
          if (from != idx) netc->sendMoveSlot(from, idx);
        }
        ImGui::EndDragDropTarget();
      }
      if (slot && netc && ImGui::BeginPopupContextItem("##inv_ctx")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.82f, 0.50f, 1.0f));
        ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
        // Primary verb first (Wield / Wear / Eat) — only shown when applicable
        const char* pverb = primaryInventoryVerb(slot->itemId);
        if (pverb[0] != '\0') {
          if (ImGui::Selectable(pverb)) netc->sendEquipItem(idx);
        }
        if (ImGui::Selectable("Drop"))    netc->sendDropItem(idx);
        if (ImGui::Selectable("Examine")) netc->sendExamine(slot->itemId);
        ImGui::EndPopup();
      }
      if (slot && ImGui::IsItemHovered()) {
        // Top-left context info
        if (hover) {
          hover->kind     = UiHoverState::Kind::InventoryItem;
          hover->verb     = primaryInventoryVerb(slot->itemId);
          hover->itemName = prettyItemId(slot->itemId);
        }
        // ImGui tooltip (item details)
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
          ImGui::BeginTooltip();
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.82f, 0.50f, 1.0f));
          ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
          ImGui::PopStyleColor();
          if (slot->quantity > 1) ImGui::Text("Qty: %s", fmtQty(slot->quantity).c_str());
          ImGui::EndTooltip();
        }
      }
      ImGui::PopID();
      if (c + 1 < kInventoryCols) ImGui::SameLine(0.0f, kPad);
    }
  }
}

// ---- Skills tab (internal) ---------------------------------------------------
void drawSkillsTab(const shared::PlayerState& p) {
  int      totalLevel = 0;
  for (const char* id : kSkillOrder) {
    auto it = p.skills.find(id);
    totalLevel += (it != p.skills.end()) ? it->second.level : 1;
  }

  // 2-column card grid — cards fill the available panel width with 4px side
  // padding on each side so they breathe against the panel border.
  constexpr int   kCols    = 2;
  constexpr float kCardH   = 60.0f;
  constexpr float kIconSz  = 14.0f;
  constexpr float kPad     =  3.0f;
  constexpr float kSidePad =  4.0f;

  const int   numSkills = static_cast<int>(kSkillOrder.size());
  const float avail     = ImGui::GetContentRegionAvail().x;
  // Cards expand to fill the panel; minimum 44 px so they stay readable.
  const float kCardW    = std::max(44.0f,
                                   (avail - 2.0f * kSidePad - (kCols - 1) * kPad)
                                   / static_cast<float>(kCols));
  const float startX    = ImGui::GetCursorPosX() + kSidePad;

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kPad, kPad));

  for (int i = 0; i < numSkills; ++i) {
    // Start each row at the centred X position.
    if (i % kCols == 0) ImGui::SetCursorPosX(startX);

    const char* skillId = kSkillOrder[i];
    auto it = p.skills.find(skillId);
    const int  lvl = (it != p.skills.end()) ? it->second.level : 1;
    const int  xp  = (it != p.skills.end()) ? it->second.xp    : 0;

    // XP progress within this level
    const int  xpThisLvl  = xpForLevel(lvl);
    const int  xpNextLvl  = (lvl < 99) ? xpForLevel(lvl + 1) : xpForLevel(99);
    const int  xpRange    = std::max(1, xpNextLvl - xpThisLvl);
    const int  xpIntoLvl  = xp - xpThisLvl;
    const float progress  = (lvl >= 99) ? 1.0f :
        std::clamp(static_cast<float>(xpIntoLvl) / static_cast<float>(xpRange), 0.0f, 1.0f);

    // Card background
    ImGui::PushID(i);
    ImVec2 cardPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cardPos,
                      ImVec2(cardPos.x + kCardW, cardPos.y + kCardH),
                      IM_COL32(18, 10, 3, 230));
    dl->AddRect(cardPos,
                ImVec2(cardPos.x + kCardW, cardPos.y + kCardH),
                IM_COL32(107, 79, 41, 180));

    // Invisible button covers the whole card for hover detection.
    ImGui::InvisibleButton("##card", ImVec2(kCardW, kCardH));
    const bool hovered = ImGui::IsItemHovered();

    // Clip all custom drawing inside the card boundaries.
    dl->PushClipRect(cardPos,
                     ImVec2(cardPos.x + kCardW, cardPos.y + kCardH), true);

    // Icon placeholder (colored square, centered)
    const float iconX = cardPos.x + (kCardW - kIconSz) * 0.5f;
    const float iconY = cardPos.y + 4.0f;
    dl->AddRectFilled(ImVec2(iconX, iconY),
                      ImVec2(iconX + kIconSz, iconY + kIconSz),
                      kSkillColors[i]);
    dl->AddRect(ImVec2(iconX, iconY),
                ImVec2(iconX + kIconSz, iconY + kIconSz),
                IM_COL32(0, 0, 0, 160));

    // Skill name (centered, clipped to card width)
    const std::string name = prettyItemId(skillId);
    ImVec2 nameSz = ImGui::CalcTextSize(name.c_str());
    dl->AddText(ImVec2(cardPos.x + (kCardW - nameSz.x) * 0.5f,
                       iconY + kIconSz + 2.0f),
                IM_COL32(200, 170, 90, 255),
                name.c_str());

    // Level number (gold, centered)
    char lvlBuf[8];
    std::snprintf(lvlBuf, sizeof(lvlBuf), "%d", lvl);
    ImVec2 lvlSz = ImGui::CalcTextSize(lvlBuf);
    dl->AddText(ImVec2(cardPos.x + (kCardW - lvlSz.x) * 0.5f,
                       iconY + kIconSz + 12.0f),
                IM_COL32(240, 206, 96, 255),
                lvlBuf);

    // XP progress bar along the bottom of the card
    const float barY  = cardPos.y + kCardH - 6.0f;
    const float barX0 = cardPos.x + 2.0f;
    const float barX1 = cardPos.x + kCardW - 2.0f;
    const float barW  = barX1 - barX0;
    dl->AddRectFilled(ImVec2(barX0, barY), ImVec2(barX1, barY + 4.0f),
                      IM_COL32(20, 10, 0, 200));
    dl->AddRectFilled(ImVec2(barX0, barY), ImVec2(barX0 + barW * progress, barY + 4.0f),
                      IM_COL32(40, 180, 50, 230));

    dl->PopClipRect();

    // Hover tooltip
    if (hovered) {
      ImGui::BeginTooltip();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.0f, 1.0f));
      ImGui::TextUnformatted(name.c_str());
      ImGui::PopStyleColor();
      ImGui::Text("Level: %d", lvl);
      ImGui::Text("XP: %d", xp);
      if (lvl < 99) {
        ImGui::Text("Next level: %d XP", xpNextLvl);
        ImGui::Text("Remaining: %d XP", std::max(0, xpNextLvl - xp));
      } else {
        ImGui::TextUnformatted("Max level");
      }
      ImGui::EndTooltip();
    }

    ImGui::PopID();
    if ((i + 1) % kCols != 0) ImGui::SameLine(0.0f, kPad);
  }

  ImGui::PopStyleVar();  // ItemSpacing

  // Spacing before total level (no Separator to avoid the horizontal rule
  // artifact that spans the full panel width in incomplete rows).
  ImGui::Dummy(ImVec2(0.0f, 4.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.65f, 0.15f, 1.0f));
  ImGui::Text("Total level: %d / %d", totalLevel, 99 * 5);
  ImGui::PopStyleColor();
}

// ---- Equipment tab (internal) -----------------------------------------------
void drawEquipmentTab(const shared::PlayerState& p, net::NetworkClient* netc,
                      UiHoverState* hover) {
  constexpr float kCell  = 44.0f;   // matches inventory + skill card width
  constexpr float kPad   =  3.0f;
  constexpr int   kGridCols = 3;

  // Centre the 3-column grid in the available content width.
  const float avail  = ImGui::GetContentRegionAvail().x;
  const float gridW  = kGridCols * kCell + (kGridCols - 1) * kPad;
  const float startX = ImGui::GetCursorPosX() + std::max(0.0f, (avail - gridW) * 0.5f);

  for (int row = 0; row < 5; ++row) {
    ImGui::SetCursorPosX(startX);
    for (int col = 0; col < 3; ++col) {
      const EquipCell* m = nullptr;
      for (const auto& e : kEquipGrid) {
        if (e.row == row && e.col == col) { m = &e; break; }
      }

      if (m && m->slotId && m->slotId[0] != '\0') {
        // Named slot — show item if equipped, or empty-slot label placeholder
        std::optional<shared::ItemStack> slot;
        auto it = p.equipped.find(m->slotId);
        if (it != p.equipped.end()) slot = it->second;

        char idbuf[32];
        std::snprintf(idbuf, sizeof(idbuf), "##eq%s", m->slotId);
        ImGui::PushID(m->slotId);

        if (slot) {
          const bool clicked = drawSlot(idbuf, slot, ImVec2(kCell, kCell));

          // Left-click: unequip immediately.
          if (clicked && netc)
            netc->sendUnequipItem(m->slotId);

          if (netc && ImGui::BeginPopupContextItem("##eq_ctx")) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.82f, 0.50f, 1.0f));
            ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
            if (ImGui::Selectable("Remove")) netc->sendUnequipItem(m->slotId);
            if (ImGui::Selectable("Examine")) netc->sendExamine(slot->itemId);
            ImGui::EndPopup();
          }
          if (ImGui::IsItemHovered()) {
            // Top-left context info
            if (hover) {
              hover->kind     = UiHoverState::Kind::EquipSlot;
              hover->itemName = prettyItemId(slot->itemId);
            }
            // ImGui tooltip
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
              ImGui::BeginTooltip();
              ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.82f, 0.50f, 1.0f));
              ImGui::Text("%s", prettyItemId(slot->itemId).c_str());
              ImGui::PopStyleColor();
              ImGui::TextDisabled("(%s slot)", m->label);
              ImGui::EndTooltip();
            }
          }
        } else {
          // Empty slot — show letter placeholder
          drawEmptyEquipSlot(m->label, ImVec2(kCell, kCell));
          if (ImGui::IsItemHovered() && hover) {
            hover->kind      = UiHoverState::Kind::EmptyEquipSlot;
            hover->slotLabel = m->label;
          }
        }
        ImGui::PopID();
      } else {
        // True spacer (corners) — invisible
        ImGui::Dummy(ImVec2(kCell, kCell));
      }
      if (col + 1 < 3) ImGui::SameLine(0.0f, kPad);
    }
  }

  // ---- Aggregated equipment bonuses ----------------------------------------
  ItemStats tot;
  for (const auto& [slotKey, stack] : p.equipped) {
    ItemStats s = statsForItem(stack.itemId);
    tot.mAtk += s.mAtk; tot.mStr += s.mStr; tot.mDef += s.mDef;
    tot.rAtk += s.rAtk; tot.rStr += s.rStr; tot.rDef += s.rDef;
  }
  ImGui::Separator();
  constexpr ImVec4 kOrange {1.0f, 0.55f, 0.0f, 1.0f};
  ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
  ImGui::TextUnformatted("Attack Bonuses");
  ImGui::PopStyleColor();
  ImGui::Text("Melee Atk:  %+d   Str: %+d", tot.mAtk, tot.mStr);
  ImGui::Text("Ranged Atk: %+d   Str: %+d", tot.rAtk, tot.rStr);
  ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
  ImGui::TextUnformatted("Defence Bonuses");
  ImGui::PopStyleColor();
  ImGui::Text("Melee Def:  %+d   Ranged: %+d", tot.mDef, tot.rDef);
}

}  // namespace

// ---- Public: HUD panel ------------------------------------------------------

void drawHudPanel(const shared::PlayerState& p, net::NetworkClient* net,
                  UiHoverState* hover) {
  const ImGuiIO& io = ImGui::GetIO();
  constexpr float kW      = 232.0f;
  // Equipment tab has bonus stats panel; skills now 5 skills in 2 columns.
  constexpr float kHudH   = 460.0f;
  constexpr float kPadX   = 12.0f;
  constexpr float kPadY   = 12.0f;

  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x - kW - kPadX, io.DisplaySize.y - kHudH - kPadY),
      ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(kW, kHudH), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.94f);
  ImGui::Begin("##hud", nullptr,
      ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize    |
      ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

  if (ImGui::BeginTabBar("##tabs")) {
    if (ImGui::BeginTabItem("Inventory")) {
      drawInventoryTab(p, net, hover);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Skills")) {
      drawSkillsTab(p);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Equipment")) {
      drawEquipmentTab(p, net, hover);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

// ---- Public: Bank panel -----------------------------------------------------

void drawBankPanel(const shared::PlayerState& p, net::NetworkClient* net, bool* open) {
  if (!open || !*open) return;

  const ImGuiIO& io = ImGui::GetIO();
  constexpr float kW = 460.0f;
  constexpr float kH = 440.0f;
  ImGui::SetNextWindowPos(
      ImVec2((io.DisplaySize.x - kW) * 0.5f, (io.DisplaySize.y - kH) * 0.5f),
      ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(kW, kH), ImGuiCond_Always);

  if (!ImGui::Begin("Bank##panel", open,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End(); return;
  }

  if (ImGui::Button("Deposit All"))  { if (net) net->sendDepositAll();  }
  ImGui::SameLine();
  if (ImGui::Button("Deposit Worn")) { if (net) net->sendDepositWorn(); }
  ImGui::SameLine();
  // Usage fraction: X / max
  ImGui::TextDisabled("%d / %d items", static_cast<int>(p.bank.size()), kMaxBankSlots);
  ImGui::Separator();

  constexpr int   kCols = 8;
  constexpr float kCell = 44.0f;
  constexpr float kPad  =  3.0f;

  ImGui::BeginChild("##bk_grid", ImVec2(0, kCell * 5 + 16), true);
  for (int i = 0; i < static_cast<int>(p.bank.size()); ++i) {
    const auto& slot = p.bank[i];
    char idbuf[24];
    std::snprintf(idbuf, sizeof(idbuf), "##bk%d", i);
    ImGui::PushID(i);
    drawSlot(idbuf, slot, ImVec2(kCell, kCell));
    if (slot && net && ImGui::BeginPopupContextItem("##bkctx")) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.82f, 0.50f, 1.0f));
      ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
      ImGui::PopStyleColor();
      ImGui::Separator();
      if (ImGui::Selectable("Withdraw 1"))   net->sendWithdrawItem(i, 1);
      if (ImGui::Selectable("Withdraw All")) net->sendWithdrawItem(i, slot->quantity);
      if (ImGui::Selectable("Examine"))      net->sendExamine(slot->itemId);
      ImGui::EndPopup();
    }
    if (slot && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
      ImGui::BeginTooltip();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.82f, 0.50f, 1.0f));
      ImGui::Text("%s", prettyItemId(slot->itemId).c_str());
      ImGui::PopStyleColor();
      if (slot->quantity > 1) ImGui::Text("Qty: %s", fmtQty(slot->quantity).c_str());
      ImGui::EndTooltip();
    }
    ImGui::PopID();
    if ((i + 1) % kCols != 0) ImGui::SameLine(0.0f, kPad);
  }
  ImGui::EndChild();

  ImGui::Separator();
  ImGui::TextUnformatted("Inventory  (right-click to deposit)");
  ImGui::BeginChild("##bk_inv", ImVec2(0, kCell * 2 + 12), true);
  for (int i = 0; i < static_cast<int>(p.inventory.size()); ++i) {
    const auto& slot = p.inventory[i];
    char idbuf[24];
    std::snprintf(idbuf, sizeof(idbuf), "##bi%d", i);
    ImGui::PushID(i);
    drawSlot(idbuf, slot, ImVec2(kCell, kCell));
    if (slot && net && ImGui::BeginPopupContextItem("##bictx")) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.82f, 0.50f, 1.0f));
      ImGui::TextUnformatted(prettyItemId(slot->itemId).c_str());
      ImGui::PopStyleColor();
      ImGui::Separator();
      if (ImGui::Selectable("Deposit 1"))   net->sendDepositItem(i, 1);
      if (ImGui::Selectable("Deposit All")) net->sendDepositItem(i, slot->quantity);
      ImGui::EndPopup();
    }
    ImGui::PopID();
    if ((i + 1) % kCols != 0) ImGui::SameLine(0.0f, kPad);
  }
  ImGui::EndChild();

  ImGui::End();

  // Close on click-outside: if the bank is open but the user clicked somewhere
  // that isn't any ImGui window, close it and tell the server.
  if (*open &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
      !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
    *open = false;
    if (net) net->sendCloseBank();
  }
}

// ---- ChatLog ----------------------------------------------------------------

void ChatLog::appendSystem(std::string line) {
  entries_.push_back({ std::move(line), true });
  while (entries_.size() > kMax) entries_.pop_front();
}

void ChatLog::observePlayers(
    const std::unordered_map<std::string, shared::PlayerState>& players) {
  for (const auto& [id, pl] : players) {
    if (pl.chatMessage.empty() || pl.chatMessageTick <= 0) continue;
    auto it = seenChatTick_.find(id);
    if (it != seenChatTick_.end() && it->second == pl.chatMessageTick) continue;
    seenChatTick_[id] = pl.chatMessageTick;
    std::string speaker = pl.playerName.empty() ? id : pl.playerName;
    entries_.push_back({ speaker + ": " + pl.chatMessage, false });
    while (entries_.size() > kMax) entries_.pop_front();
  }
}

void ChatLog::draw(net::NetworkClient* netc) {
  const ImGuiIO& io = ImGui::GetIO();
  constexpr float kW    = 420.0f;
  constexpr float kH    = 175.0f;
  constexpr float kPadX = 12.0f;
  constexpr float kPadY = 12.0f;
  ImGui::SetNextWindowPos(
      ImVec2(kPadX, io.DisplaySize.y - kH - kPadY),
      ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(kW, kH), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.82f);

  if (!ImGui::Begin("##chat", nullptr,
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End(); return;
  }

  const float reserveH = netc ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
  ImGui::BeginChild("##chat_scroll", ImVec2(0, -reserveH), false,
                    ImGuiWindowFlags_HorizontalScrollbar);
  for (const auto& e : entries_) {
    const ImVec4 col = e.system
        ? ImVec4(1.0f, 0.92f, 0.30f, 1.0f)
        : ImVec4(1.0f, 1.0f,  1.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextWrapped("%s", e.text.c_str());
    ImGui::PopStyleColor();
  }
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
    ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();

  if (netc) {
    // Auto-focus: if a printable character was typed and no other widget is
    // active, redirect keyboard input to the chat field immediately.
    // We store the triggering character and inject it via the InputText
    // CallbackAlways hook — this avoids the "select-all on first focus" issue
    // that would replace the char if we pre-seeded the external buffer.
    if (!ImGui::IsAnyItemActive() && io.InputQueueCharacters.Size > 0) {
      const ImWchar ch = io.InputQueueCharacters[0];
      if (ch >= 32 && ch < 127)
        pendingAutoFocusChar_ = static_cast<char>(ch);
      ImGui::SetKeyboardFocusHere(0);
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    // Callback: inject pending char once the widget is active so it appears
    // immediately without triggering a select-all.
    auto chatCallback = [](ImGuiInputTextCallbackData* data) -> int {
      char* pending = static_cast<char*>(data->UserData);
      if (*pending != '\0') {
        data->InsertChars(data->CursorPos, pending, pending + 1);
        *pending = '\0';
      }
      return 0;
    };
    if (ImGui::InputText("##chat_in", inputBuf_, sizeof(inputBuf_),
                         ImGuiInputTextFlags_EnterReturnsTrue |
                         ImGuiInputTextFlags_CallbackAlways,
                         chatCallback, &pendingAutoFocusChar_)) {
      if (inputBuf_[0] != '\0') {
        netc->sendChat(inputBuf_);
      }
      inputBuf_[0] = '\0';
      ImGui::SetKeyboardFocusHere(-1);
    }
  }
  ImGui::End();
}

}  // namespace ui
