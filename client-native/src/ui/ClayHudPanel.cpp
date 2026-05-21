// ClayHudPanel.cpp
// Full OSRS-style HUD panel (Inventory / Skills / Equipment) built with Clay.
// Visual layout is entirely Clay; click interactions and context-menu popups
// are handled via Clay_PointerOver queries after Clay_EndLayout().

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <clay.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "ui/ClayHudPanel.hpp"
#include "ui/NameRegistry.hpp"
#include "net/NetworkClient.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace ui {

// ── Palette ───────────────────────────────────────────────────────────────────
static constexpr Clay_Color kPanelBg      = {  18,  10,   3, 240 };
static constexpr Clay_Color kPanelBorder  = { 107,  79,  41, 200 };
static constexpr Clay_Color kTabActive    = {  55,  38,  12, 255 };
static constexpr Clay_Color kTabInactive  = {  28,  18,   5, 255 };
static constexpr Clay_Color kTabHover     = {  42,  28,   8, 255 };
static constexpr Clay_Color kTabText      = { 200, 170,  90, 255 };
static constexpr Clay_Color kTabTextActive= { 255, 210, 100, 255 };
static constexpr Clay_Color kSlotEmpty    = {  13,   6,   0, 220 };
static constexpr Clay_Color kSlotFilled   = {  30,  16,   4, 240 };
static constexpr Clay_Color kSlotBorder   = { 107,  79,  41, 180 };
static constexpr Clay_Color kSlotHover    = { 255, 152,  31, 255 };
static constexpr Clay_Color kItemText     = { 240, 206,  96, 255 };
static constexpr Clay_Color kQtyText      = { 255, 221,  68, 255 };
static constexpr Clay_Color kSlotLabel    = { 120, 100,  60, 200 };
static constexpr Clay_Color kSkillCard    = {  18,  10,   3, 230 };
static constexpr Clay_Color kXpBarBg      = {  20,  10,   0, 200 };
static constexpr Clay_Color kXpBarFill    = {  40, 180,  50, 230 };
static constexpr Clay_Color kOrange       = { 255, 140,  20, 255 };
static constexpr Clay_Color kWhite        = { 255, 255, 255, 255 };
static constexpr Clay_Color kGrey         = { 160, 160, 160, 180 };
static constexpr Clay_Color kDivider      = {  70,  50,  20, 200 };

// ── Skills meta ───────────────────────────────────────────────────────────────
static constexpr std::array<const char*, 5> kSkillOrder = {
    "hitpoints", "defence", "warrior", "gunner", "woodcutting",
};
static constexpr std::array<Clay_Color, 5> kSkillColors = {{
    { 220,  40,  40, 255 },  // hitpoints  — red
    {  60, 120, 220, 255 },  // defence    — blue
    { 200, 136,  44, 255 },  // warrior    — orange
    {   0, 207, 255, 255 },  // gunner     — cyan
    {  80, 144,  64, 255 },  // woodcutting — green
}};

// ── Equipment grid ────────────────────────────────────────────────────────────
struct EquipCell { int row, col; const char* slotId; const char* label; };
static constexpr std::array<EquipCell, 15> kEquipGrid = {{
    {0,0,"",          ""     }, {0,1,"head",     "Head" }, {0,2,"",        ""    },
    {1,0,"",          ""     }, {1,1,"neck",     "Neck" }, {1,2,"ammo",    "Ammo"},
    {2,0,"rightHand", "Main" }, {2,1,"body",     "Body" }, {2,2,"leftHand","Off" },
    {3,0,"",          ""     }, {3,1,"legs",     "Legs" }, {3,2,"",        ""    },
    {4,0,"hands",     "Hands"}, {4,1,"feet",     "Feet" }, {4,2,"ring",    "Ring"},
}};

// ── XP table ─────────────────────────────────────────────────────────────────
static constexpr std::array<int, 98> kXpTable = {
    83,174,276,388,512,650,801,969,1154,1358,1584,1833,2107,2411,2746,3115,3523,
    3973,4470,5018,5624,6291,7028,7842,8740,9730,10824,12031,13363,14833,16456,
    18247,20224,22406,24815,27473,30408,33648,37224,41171,45529,50339,55649,
    61512,67983,75127,83014,91721,101333,111945,123660,136594,150872,166636,
    184040,203254,224466,247886,273742,302288,333804,368599,407015,449428,
    496254,547953,605032,668051,737627,814445,899257,992895,1096278,1210421,
    1336443,1475581,1629200,1798808,1986068,2192818,2421087,2673114,2951373,
    3258594,3597792,3972294,4385776,4842295,5346332,5902831,6517253,7195629,
    7944614,8771558,9684577,10692629,11805606,13034431,
};
static int xpForLevel(int lvl) {
    if (lvl <= 1) return 0;
    if (lvl >= 99) return kXpTable[97];
    return kXpTable[lvl - 2];
}

// ── Layout constants ──────────────────────────────────────────────────────────
static constexpr int   kPanelW    = 232;
static constexpr int   kPanelH    = 460;
static constexpr int   kTabH      =  28;
static constexpr int   kPad       =   8;
static constexpr int   kCellSize  =  44;
static constexpr int   kCellGap   =   3;
static constexpr int   kInvCols   =   4;
static constexpr int   kInvRows   =   7;
static constexpr int   kEquipCols =   3;
static constexpr int   kEquipRows =   5;

// ── Per-frame interaction state (written during layout, read in HandleInput) ──
static int  s_activeTab      =  0;   // 0=inventory 1=skills 2=equipment
static int  s_hovInvSlot     = -1;   // inventory slot under pointer
static int  s_hovEquipIdx    = -1;   // kEquipGrid index under pointer
static int  s_ctxInvSlot     = -1;   // right-clicked inventory slot (-1 = none)
static int  s_ctxEquipIdx    = -1;   // right-clicked equip slot (-1 = none)

// ── Helpers ───────────────────────────────────────────────────────────────────
static std::string prettyId(const std::string& id) { return ui::itemName(id); }

static std::string fmtQty(int q) {
    char buf[16];
    if      (q >= 10000000) std::snprintf(buf,sizeof(buf),"%dM",  q/1000000);
    else if (q >=  1000000) std::snprintf(buf,sizeof(buf),"%.1fM",q/1000000.0f);
    else if (q >=    10000) std::snprintf(buf,sizeof(buf),"%dk",  q/1000);
    else if (q >=     1000) std::snprintf(buf,sizeof(buf),"%.1fk",q/1000.0f);
    else                    std::snprintf(buf,sizeof(buf),"%d",   q);
    return buf;
}

static const char* equipSlotForItem(const std::string& id) {
    if (id=="axe"||id=="iron_axe"||id=="pickaxe"||
        id=="bronze_sword"||id=="iron_sword"||
        id=="bronze_longsword"||id=="basic_chaingun") return "rightHand";
    if (id=="bronze_shield")   return "leftHand";
    if (id=="arrow"||id=="kinetic_charges") return "ammo";
    if (id=="leather_helm"||id=="bronze_helm")  return "head";
    if (id=="leather_body")    return "body";
    if (id=="leather_legs")    return "legs";
    if (id=="leather_gloves")  return "hands";
    if (id=="leather_boots")   return "feet";
    if (id=="gold_ring")       return "ring";
    if (id=="amulet")          return "neck";
    return "";
}
static const char* primaryVerb(const std::string& id) {
    const char* slot = equipSlotForItem(id);
    if (slot[0]) {
        bool hand = !std::strcmp(slot,"rightHand")||!std::strcmp(slot,"leftHand");
        return hand ? "Wield" : "Wear";
    }
    if (id=="shrimp"||id=="trout") return "Eat";
    return "";
}

// Temporary string storage — refreshed every frame before layout.
// Clay_String chars pointers must stay valid until clayRenderInternal finishes.
static char s_strBuf[8192];
static int  s_strOff = 0;

static Clay_String clayStr(const std::string& s) {
    int len = static_cast<int>(s.size());
    if (s_strOff + len + 1 > static_cast<int>(sizeof(s_strBuf))) {
        // Overflow safety: truncate.
        len = static_cast<int>(sizeof(s_strBuf)) - s_strOff - 1;
        if (len <= 0) return { false, 0, "" };
    }
    char* dst = s_strBuf + s_strOff;
    std::memcpy(dst, s.c_str(), len);
    dst[len] = '\0';
    s_strOff += len + 1;
    return { false, len, dst };
}

// ── Inventory tab ─────────────────────────────────────────────────────────────
static void buildInventoryTab(const shared::PlayerState* player) {
    CLAY(CLAY_ID("InvContent"), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .padding         = CLAY_PADDING_ALL(kPad),
            .childGap        = (uint16_t)kCellGap,
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        }
    }) {
        for (int row = 0; row < kInvRows; ++row) {
            CLAY(CLAY_IDI("InvRow", row), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kCellSize) },
                    .childGap        = (uint16_t)kCellGap,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                }
            }) {
                for (int col = 0; col < kInvCols; ++col) {
                    int idx = row * kInvCols + col;
                    const shared::ItemStack* item = nullptr;
                    if (player && idx < static_cast<int>(player->inventory.size())) {
                        const auto& opt = player->inventory[idx];
                        if (opt.has_value()) item = &opt.value();
                    }
                    bool filled  = (item != nullptr);
                    bool hovered = (s_hovInvSlot == idx);

                    CLAY(CLAY_IDI("InvSlot", idx), {
                        .layout = {
                            .sizing          = { CLAY_SIZING_FIXED(kCellSize),
                                                 CLAY_SIZING_FIXED(kCellSize) },
                            .childAlignment  = { .x = CLAY_ALIGN_X_CENTER,
                                                 .y = CLAY_ALIGN_Y_CENTER },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        },
                        .backgroundColor = filled ? kSlotFilled : kSlotEmpty,
                        .border = {
                            .color = hovered ? kSlotHover : kSlotBorder,
                            .width = CLAY_BORDER_ALL(1),
                        }
                    }) {
                        if (filled) {
                            // Item name (centered)
                            std::string name = prettyId(item->itemId);
                            CLAY_TEXT(clayStr(name), CLAY_TEXT_CONFIG({
                                .textColor = kItemText,
                                .fontSize  = 9,
                            }));

                            // Quantity: floating top-left overlay
                            if (item->quantity > 1) {
                                std::string qs = fmtQty(item->quantity);
                                CLAY(CLAY_IDI("InvQty", idx), {
                                    .floating = {
                                        .offset       = { 2.f, 2.f },
                                        .attachPoints = {
                                            .element = CLAY_ATTACH_POINT_LEFT_TOP,
                                            .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
                                        },
                                        .attachTo = CLAY_ATTACH_TO_PARENT,
                                    }
                                }) {
                                    CLAY_TEXT(clayStr(qs), CLAY_TEXT_CONFIG({
                                        .textColor = kQtyText,
                                        .fontSize  = 9,
                                    }));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// ── Skills tab ────────────────────────────────────────────────────────────────
static void buildSkillsTab(const shared::PlayerState* player) {
    int totalLevel = 0;
    for (const char* id : kSkillOrder) {
        if (!player) { ++totalLevel; continue; }
        auto it = player->skills.find(id);
        totalLevel += (it != player->skills.end()) ? it->second.level : 1;
    }

    CLAY(CLAY_ID("SkillsContent"), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .padding         = CLAY_PADDING_ALL(kPad),
            .childGap        = (uint16_t)kCellGap,
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        }
    }) {
        // 2-column card grid.  Build rows of 2 manually (5 skills → 3 rows).
        static constexpr int kSkillCols = 2;
        for (int row = 0; row * kSkillCols < static_cast<int>(kSkillOrder.size()); ++row) {
            CLAY(CLAY_IDI("SkillRow", row), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(66) },
                    .childGap        = (uint16_t)kCellGap,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                }
            }) {
                for (int col = 0; col < kSkillCols; ++col) {
                    int si = row * kSkillCols + col;
                    if (si >= static_cast<int>(kSkillOrder.size())) {
                        // Spacer to balance last row
                        CLAY(CLAY_IDI("SkillSpacer", si), {
                            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }
                        }) {}
                        continue;
                    }

                    const char* skillId = kSkillOrder[si];
                    int lvl = 1, xp = 0;
                    if (player) {
                        auto it = player->skills.find(skillId);
                        if (it != player->skills.end()) {
                            lvl = it->second.level;
                            xp  = it->second.xp;
                        }
                    }
                    int xpThis = xpForLevel(lvl);
                    int xpNext = (lvl < 99) ? xpForLevel(lvl + 1) : xpForLevel(99);
                    int xpRange = std::max(1, xpNext - xpThis);
                    float progress = (lvl >= 99) ? 1.0f :
                        std::clamp(float(xp - xpThis) / float(xpRange), 0.0f, 1.0f);

                    // Card
                    CLAY(CLAY_IDI("SkillCard", si), {
                        .layout = {
                            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                            .padding         = { 4, 4, 4, 4 },
                            .childGap        = 2,
                            .childAlignment  = { .x = CLAY_ALIGN_X_CENTER,
                                                 .y = CLAY_ALIGN_Y_TOP },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        },
                        .backgroundColor = kSkillCard,
                        .border = {
                            .color = kPanelBorder,
                            .width = CLAY_BORDER_ALL(1),
                        }
                    }) {
                        // Colored icon
                        CLAY(CLAY_IDI("SkillIcon", si), {
                            .layout = {
                                .sizing = { CLAY_SIZING_FIXED(14), CLAY_SIZING_FIXED(14) }
                            },
                            .backgroundColor = kSkillColors[si],
                        }) {}

                        // Skill name
                        CLAY_TEXT(clayStr(prettyId(skillId)), CLAY_TEXT_CONFIG({
                            .textColor = { 200, 170, 90, 255 },
                            .fontSize  = 9,
                        }));

                        // Level number
                        char lvlBuf[8];
                        std::snprintf(lvlBuf, sizeof(lvlBuf), "%d", lvl);
                        CLAY_TEXT(clayStr(lvlBuf), CLAY_TEXT_CONFIG({
                            .textColor = kItemText,
                            .fontSize  = 11,
                        }));

                        // XP bar background (grows to fill remaining width)
                        CLAY(CLAY_IDI("XpBarBg", si), {
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(4) },
                            },
                            .backgroundColor = kXpBarBg,
                        }) {
                            // XP fill
                            CLAY(CLAY_IDI("XpBarFill", si), {
                                .layout = {
                                    .sizing = { CLAY_SIZING_PERCENT(progress),
                                                CLAY_SIZING_GROW(0) },
                                },
                                .backgroundColor = kXpBarFill,
                            }) {}
                        }
                    }
                }
            }
        }

        // Total level footer
        CLAY(CLAY_ID("SkillTotalRow"), {
            .layout = {
                .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(20) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
            }
        }) {
            char totalBuf[48];
            std::snprintf(totalBuf, sizeof(totalBuf), "Total level: %d / %d",
                          totalLevel, 99 * 5);
            CLAY_TEXT(clayStr(totalBuf), CLAY_TEXT_CONFIG({
                .textColor = kOrange,
                .fontSize  = 10,
            }));
        }
    }
}

// ── Equipment tab ─────────────────────────────────────────────────────────────
struct ItemStats { int mAtk=0,mStr=0,mDef=0,rAtk=0,rStr=0,rDef=0; };
static ItemStats statsForItem(const std::string& id) {
    if (id=="axe")              return { 4,0,0,0,0,0};
    if (id=="iron_axe")         return {-1,0,2,0,0,0};
    if (id=="pickaxe")          return { 6,0,1,0,0,0};
    if (id=="bronze_sword")     return { 6,3,0,0,0,0};
    if (id=="iron_sword")       return {10,5,0,0,0,0};
    if (id=="bronze_longsword") return { 8,7,0,0,0,0};
    if (id=="basic_chaingun")   return { 0,0,0,8,4,0};
    if (id=="bronze_shield")    return { 0,0,5,0,0,0};
    if (id=="leather_helm")     return { 0,0,1,0,0,0};
    if (id=="bronze_helm")      return { 0,0,3,0,0,0};
    if (id=="leather_body")     return { 0,0,2,0,0,0};
    if (id=="leather_legs")     return { 0,0,1,0,0,0};
    if (id=="leather_gloves")   return { 0,0,1,0,0,0};
    if (id=="leather_boots")    return { 0,0,1,0,0,0};
    if (id=="amulet")           return { 0,4,0,0,0,0};
    return {};
}

static void buildEquipmentTab(const shared::PlayerState* player) {
    CLAY(CLAY_ID("EquipContent"), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .padding         = CLAY_PADDING_ALL(kPad),
            .childGap        = (uint16_t)kCellGap,
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        }
    }) {
        // Grid: 5 rows × 3 cols
        for (int row = 0; row < kEquipRows; ++row) {
            CLAY(CLAY_IDI("EqRow", row), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kCellSize) },
                    .childGap        = (uint16_t)kCellGap,
                    .childAlignment  = { .x = CLAY_ALIGN_X_CENTER,
                                         .y = CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                }
            }) {
                for (int col = 0; col < kEquipCols; ++col) {
                    const EquipCell* m = nullptr;
                    for (const auto& e : kEquipGrid)
                        if (e.row == row && e.col == col) { m = &e; break; }

                    if (!m || !m->slotId || m->slotId[0] == '\0') {
                        // Invisible spacer
                        CLAY(CLAY_IDI("EqSpacer", row*3+col), {
                            .layout = { .sizing = { CLAY_SIZING_FIXED(kCellSize),
                                                    CLAY_SIZING_FIXED(kCellSize) } }
                        }) {}
                        continue;
                    }

                    // Find equipped item for this slot
                    const shared::ItemStack* item = nullptr;
                    if (player) {
                        auto it = player->equipped.find(m->slotId);
                        if (it != player->equipped.end()) item = &it->second;
                    }

                    int gridIdx = row * kEquipCols + col;
                    bool hovered = (s_hovEquipIdx == gridIdx);

                    CLAY(CLAY_IDI("EqSlot", gridIdx), {
                        .layout = {
                            .sizing          = { CLAY_SIZING_FIXED(kCellSize),
                                                 CLAY_SIZING_FIXED(kCellSize) },
                            .childAlignment  = { .x = CLAY_ALIGN_X_CENTER,
                                                 .y = CLAY_ALIGN_Y_CENTER },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        },
                        .backgroundColor = item ? kSlotFilled : kSlotEmpty,
                        .border = {
                            .color = hovered ? kSlotHover : kSlotBorder,
                            .width = CLAY_BORDER_ALL(1),
                        }
                    }) {
                        if (item) {
                            std::string name = prettyId(item->itemId);
                            CLAY_TEXT(clayStr(name), CLAY_TEXT_CONFIG({
                                .textColor = kItemText,
                                .fontSize  = 9,
                            }));
                        } else {
                            // Slot label letter (e.g. "H" for Head)
                            char letter[2] = { m->label[0], '\0' };
                            CLAY_TEXT(clayStr(letter), CLAY_TEXT_CONFIG({
                                .textColor = kSlotLabel,
                                .fontSize  = 11,
                            }));
                        }
                    }
                }
            }
        }

        // Divider
        CLAY(CLAY_ID("EqDivider"), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) }
            },
            .backgroundColor = kDivider,
        }) {}

        // Stat bonuses
        ItemStats tot;
        if (player) {
            for (const auto& [slotKey, stack] : player->equipped) {
                ItemStats s = statsForItem(stack.itemId);
                tot.mAtk+=s.mAtk; tot.mStr+=s.mStr; tot.mDef+=s.mDef;
                tot.rAtk+=s.rAtk; tot.rStr+=s.rStr; tot.rDef+=s.rDef;
            }
        }

        static const Clay_String kAtkBonuses = CLAY_STRING("Attack Bonuses");
        static const Clay_String kDefBonuses = CLAY_STRING("Defence Bonuses");
        CLAY(CLAY_ID("StatBlock"), {
            .layout = {
                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .padding         = { 2, 2, 4, 2 },
                .childGap        = 3,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            }
        }) {
            CLAY_TEXT(kAtkBonuses, CLAY_TEXT_CONFIG({ .textColor=kOrange, .fontSize=10 }));

            char buf[64];
            std::snprintf(buf,sizeof(buf),"Melee  Atk: %+d   Str: %+d", tot.mAtk, tot.mStr);
            CLAY_TEXT(clayStr(buf),  CLAY_TEXT_CONFIG({ .textColor=kWhite, .fontSize=9 }));
            std::snprintf(buf,sizeof(buf),"Ranged Atk: %+d   Str: %+d", tot.rAtk, tot.rStr);
            CLAY_TEXT(clayStr(buf),  CLAY_TEXT_CONFIG({ .textColor=kWhite, .fontSize=9 }));

            CLAY_TEXT(kDefBonuses, CLAY_TEXT_CONFIG({ .textColor=kOrange, .fontSize=10 }));

            std::snprintf(buf,sizeof(buf),"Melee Def:  %+d   Ranged: %+d", tot.mDef, tot.rDef);
            CLAY_TEXT(clayStr(buf),  CLAY_TEXT_CONFIG({ .textColor=kWhite, .fontSize=9 }));
        }
    }
}

// ── Public: build layout ──────────────────────────────────────────────────────
void clayHudBuildLayout(const shared::PlayerState* player) {
    // Reset string scratch buffer every frame.
    s_strOff   = 0;
    s_hovInvSlot  = -1;
    s_hovEquipIdx = -1;

    // Recompute hover state from last frame's bounding boxes before building
    // this frame's layout (Clay uses previous-frame positions for PointerOver).
    if (s_activeTab == 0) {
        for (int i = 0; i < kInvCols * kInvRows; ++i)
            if (Clay_PointerOver(CLAY_IDI("InvSlot", i))) { s_hovInvSlot = i; break; }
    } else if (s_activeTab == 2) {
        for (int i = 0; i < kEquipCols * kEquipRows; ++i)
            if (Clay_PointerOver(CLAY_IDI("EqSlot", i))) { s_hovEquipIdx = i; break; }
    }

    static const Clay_String kTabs[3] = {
        CLAY_STRING("Inventory"),
        CLAY_STRING("Skills"),
        CLAY_STRING("Equipment"),
    };

    CLAY(CLAY_ID("HudAnchor"), {
        .floating = {
            .offset       = { -14.f, -14.f },
            .zIndex       = 10,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_RIGHT_BOTTOM,
                .parent  = CLAY_ATTACH_POINT_RIGHT_BOTTOM,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {
        CLAY(CLAY_ID("HudPanel"), {
            .layout = {
                .sizing          = { CLAY_SIZING_FIXED(kPanelW), CLAY_SIZING_FIXED(kPanelH) },
                .childGap        = 0,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
            .backgroundColor = kPanelBg,
            .cornerRadius    = CLAY_CORNER_RADIUS(4),
            .border = {
                .color = kPanelBorder,
                .width = CLAY_BORDER_ALL(1),
            }
        }) {
            // ── Tab bar ───────────────────────────────────────────────────────
            CLAY(CLAY_ID("TabBar"), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(kTabH) },
                    .childGap        = 1,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                },
                .backgroundColor = { 10, 6, 2, 255 },
            }) {
                for (int i = 0; i < 3; ++i) {
                    bool active  = (s_activeTab == i);
                    bool hovered = Clay_PointerOver(CLAY_IDI("HudTab", i));
                    CLAY(CLAY_IDI("HudTab", i), {
                        .layout = {
                            .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER,
                                                .y = CLAY_ALIGN_Y_CENTER },
                        },
                        .backgroundColor = active  ? kTabActive :
                                           hovered ? kTabHover  : kTabInactive,
                    }) {
                        CLAY_TEXT(kTabs[i], CLAY_TEXT_CONFIG({
                            .textColor = active ? kTabTextActive : kTabText,
                            .fontSize  = 10,
                        }));
                    }
                }
            }

            // ── Tab content ───────────────────────────────────────────────────
            if      (s_activeTab == 0) buildInventoryTab(player);
            else if (s_activeTab == 1) buildSkillsTab(player);
            else                       buildEquipmentTab(player);
        }
    }
}

// ── Public: handle input ──────────────────────────────────────────────────────
void clayHudHandleInput(const shared::PlayerState* player,
                        net::NetworkClient*         netc,
                        UiHoverState*               hover,
                        bool leftClicked,
                        bool rightClicked) {
    // ── Tab switching ────────────────────────────────────────────────────────
    if (leftClicked) {
        for (int i = 0; i < 3; ++i) {
            if (Clay_PointerOver(CLAY_IDI("HudTab", i))) {
                s_activeTab = i;
                break;
            }
        }
    }

    // ── Inventory interactions ────────────────────────────────────────────────
    if (s_activeTab == 0 && s_hovInvSlot >= 0) {
        if (hover && player && s_hovInvSlot < static_cast<int>(player->inventory.size())) {
            const auto& opt = player->inventory[s_hovInvSlot];
            if (opt.has_value()) {
                hover->kind     = UiHoverState::Kind::InventoryItem;
                hover->verb     = primaryVerb(opt->itemId);
                hover->itemName = prettyId(opt->itemId);
            }
        }

        if (leftClicked && netc && player &&
            s_hovInvSlot < static_cast<int>(player->inventory.size())) {
            const auto& opt = player->inventory[s_hovInvSlot];
            if (opt.has_value() && equipSlotForItem(opt->itemId)[0])
                netc->sendEquipItem(s_hovInvSlot);
        }

        if (rightClicked && player &&
            s_hovInvSlot < static_cast<int>(player->inventory.size())) {
            const auto& opt = player->inventory[s_hovInvSlot];
            if (opt.has_value()) {
                s_ctxInvSlot = s_hovInvSlot;
                char popupId[32];
                std::snprintf(popupId, sizeof(popupId), "##cctx_inv%d", s_ctxInvSlot);
                ImGui::OpenPopup(popupId);
            }
        }
    }

    // ── Equipment interactions ─────────────────────────────────────────────────
    if (s_activeTab == 2 && s_hovEquipIdx >= 0) {
        int row = s_hovEquipIdx / kEquipCols;
        int col = s_hovEquipIdx % kEquipCols;
        const EquipCell* m = nullptr;
        for (const auto& e : kEquipGrid)
            if (e.row == row && e.col == col) { m = &e; break; }

        if (m && m->slotId && m->slotId[0]) {
            const shared::ItemStack* item = nullptr;
            if (player) {
                auto it = player->equipped.find(m->slotId);
                if (it != player->equipped.end()) item = &it->second;
            }
            if (hover && item) {
                hover->kind     = UiHoverState::Kind::EquipSlot;
                hover->itemName = prettyId(item->itemId);
            } else if (hover && !item) {
                hover->kind      = UiHoverState::Kind::EmptyEquipSlot;
                hover->slotLabel = m->label;
            }

            if (leftClicked && netc && item)
                netc->sendUnequipItem(m->slotId);

            if (rightClicked && item) {
                s_ctxEquipIdx = s_hovEquipIdx;
                char popupId[32];
                std::snprintf(popupId, sizeof(popupId), "##cctx_eq%d", s_ctxEquipIdx);
                ImGui::OpenPopup(popupId);
            }
        }
    }

    // ── ImGui context-menu popups ─────────────────────────────────────────────
    // Inventory slot right-click menu
    if (s_ctxInvSlot >= 0) {
        char popupId[32];
        std::snprintf(popupId, sizeof(popupId), "##cctx_inv%d", s_ctxInvSlot);
        if (ImGui::BeginPopup(popupId)) {
            if (player && s_ctxInvSlot < static_cast<int>(player->inventory.size())) {
                const auto& opt = player->inventory[s_ctxInvSlot];
                if (opt.has_value()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.82f, 0.50f, 1.0f));
                    ImGui::TextUnformatted(prettyId(opt->itemId).c_str());
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                    const char* pv = primaryVerb(opt->itemId);
                    if (pv[0] && netc) {
                        if (ImGui::Selectable(pv)) netc->sendEquipItem(s_ctxInvSlot);
                    }
                    if (netc && ImGui::Selectable("Drop"))    netc->sendDropItem(s_ctxInvSlot);
                    if (netc && ImGui::Selectable("Examine")) netc->sendExamine(opt->itemId);
                }
            }
            ImGui::EndPopup();
        } else {
            s_ctxInvSlot = -1;
        }
    }

    // Equipment slot right-click menu
    if (s_ctxEquipIdx >= 0) {
        char popupId[32];
        std::snprintf(popupId, sizeof(popupId), "##cctx_eq%d", s_ctxEquipIdx);
        if (ImGui::BeginPopup(popupId)) {
            int row = s_ctxEquipIdx / kEquipCols;
            int col = s_ctxEquipIdx % kEquipCols;
            const EquipCell* m = nullptr;
            for (const auto& e : kEquipGrid)
                if (e.row == row && e.col == col) { m = &e; break; }
            if (m && m->slotId && player) {
                auto it = player->equipped.find(m->slotId);
                if (it != player->equipped.end()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.82f, 0.50f, 1.0f));
                    ImGui::TextUnformatted(prettyId(it->second.itemId).c_str());
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                    if (netc && ImGui::Selectable("Remove"))  netc->sendUnequipItem(m->slotId);
                    if (netc && ImGui::Selectable("Examine")) netc->sendExamine(it->second.itemId);
                }
            }
            ImGui::EndPopup();
        } else {
            s_ctxEquipIdx = -1;
        }
    }
}

} // namespace ui
