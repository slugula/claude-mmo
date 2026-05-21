// ClayChatLog.cpp
// Bottom-left ATTACH_TO_ROOT floating chat panel. 560×210.
// Scrollable message area + global keyboard capture text input.
// System messages: gold {255,224,102}. Player chat: white.

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <clay.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "ui/ClayChatLog.hpp"
#include "net/NetworkClient.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>

namespace ui {

// ── State ─────────────────────────────────────────────────────────────────────
struct ChatEntry {
    std::string text;
    bool        system = false;   // true → gold, false → white
};

static constexpr std::size_t kMax = 200;

static std::deque<ChatEntry>                        s_entries;
static std::unordered_map<std::string, int>         s_seenChatTick;
static char                                         s_inputBuf[256] = {};
static int                                          s_inputLen      = 0;
static bool                                         s_autoScroll    = false;
static std::string                                  s_inputDisplay;

// ── Dimensions ────────────────────────────────────────────────────────────────
static constexpr float kW      = 560.f;
static constexpr float kH      = 210.f;
static constexpr float kInputH = 28.f;
static constexpr float kMsgH   = kH - kInputH - 1.f;  // minus 1px divider
static constexpr float kPadX   = 6.f;
static constexpr float kPadY   = 4.f;

// ── Colours ───────────────────────────────────────────────────────────────────
static constexpr Clay_Color kBg         = {  10,   5,   0, 200 };
static constexpr Clay_Color kBorder     = {  80,  60,  40, 200 };
static constexpr Clay_Color kInputBg    = {   5,   3,   0, 220 };
static constexpr Clay_Color kSysText    = { 255, 224, 102, 255 };
static constexpr Clay_Color kPlayerText = { 255, 255, 255, 255 };

// ── Helper ────────────────────────────────────────────────────────────────────
static Clay_String cstr(const char* s) {
    return { false, static_cast<int>(strlen(s)), s };
}

// ── API ───────────────────────────────────────────────────────────────────────
void chatAppendSystem(std::string line) {
    s_entries.push_back({ std::move(line), true });
    while (s_entries.size() > kMax) s_entries.pop_front();
    s_autoScroll = true;
}

void chatObservePlayers(
    const std::unordered_map<std::string, shared::PlayerState>& players)
{
    for (const auto& [id, pl] : players) {
        if (pl.chatMessage.empty() || pl.chatMessageTick <= 0) continue;
        auto it = s_seenChatTick.find(id);
        if (it != s_seenChatTick.end() && it->second >= pl.chatMessageTick) continue;
        s_seenChatTick[id] = pl.chatMessageTick;
        s_entries.push_back({ pl.playerName + ": " + pl.chatMessage, false });
        while (s_entries.size() > kMax) s_entries.pop_front();
        s_autoScroll = true;
    }
}

// ── Build ─────────────────────────────────────────────────────────────────────
void buildChatLog(float /*screenW*/, float screenH,
                  const shared::PlayerState* player,
                  net::NetworkClient* netc)
{
    // ── Keyboard capture ──────────────────────────────────────────────────────
    if (netc) {
        const ImGuiIO& io = ImGui::GetIO();
        // Only steal input when no ImGui widget is actively focused.
        if (!ImGui::IsAnyItemActive()) {
            for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
                ImWchar ch = io.InputQueueCharacters[i];
                if (ch >= 32 && ch < 127 && s_inputLen < (int)(sizeof(s_inputBuf) - 1)) {
                    s_inputBuf[s_inputLen++] = static_cast<char>(ch);
                    s_inputBuf[s_inputLen]   = '\0';
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && s_inputLen > 0) {
                s_inputBuf[--s_inputLen] = '\0';
            }
            if ((ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                 ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && s_inputLen > 0) {
                netc->sendChat(s_inputBuf);
                s_inputBuf[0] = '\0';
                s_inputLen    = 0;
            }
        }
    }

    // ── Auto-scroll to bottom when new messages arrive ────────────────────────
    if (s_autoScroll) {
        Clay_ScrollContainerData sd =
            Clay_GetScrollContainerData(Clay_GetElementId(CLAY_STRING("ChatMessages")));
        if (sd.found &&
            sd.contentDimensions.height > sd.scrollContainerDimensions.height) {
            sd.scrollPosition->y =
                -(sd.contentDimensions.height - sd.scrollContainerDimensions.height);
        }
        s_autoScroll = false;
    }

    // ── Input display string: "Name: buffer|" ─────────────────────────────────
    s_inputDisplay.clear();
    if (netc) {
        const char* name = (player && !player->playerName.empty())
            ? player->playerName.c_str() : "Player";
        s_inputDisplay += name;
        s_inputDisplay += ": ";
        s_inputDisplay += s_inputBuf;
        s_inputDisplay += '|';
    }

    // ── Position: 12px from left, 12px from bottom ────────────────────────────
    const float ox = 12.f;
    const float oy = screenH - kH - 12.f;

    // ── Layout ────────────────────────────────────────────────────────────────
    CLAY(CLAY_ID("ChatAnchor"), {
        .floating = {
            .offset       = { ox, oy },
            .zIndex       = 30,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_LEFT_TOP,
                .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {
        CLAY(CLAY_ID("ChatBox"), {
            .layout = {
                .sizing          = { CLAY_SIZING_FIXED(kW), CLAY_SIZING_FIXED(kH) },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
            .backgroundColor = kBg,
            .cornerRadius    = CLAY_CORNER_RADIUS(2),
            .border = {
                .color = kBorder,
                .width = CLAY_BORDER_ALL(1),
            },
        }) {
            // ── Scrollable message area ───────────────────────────────────────
            CLAY(CLAY_ID("ChatMessages"), {
                .layout = {
                    .sizing          = { CLAY_SIZING_FIXED(kW - 2.f),
                                         CLAY_SIZING_FIXED(kMsgH) },
                    .padding         = { (uint16_t)kPadX, (uint16_t)kPadX,
                                         (uint16_t)kPadY, (uint16_t)kPadY },
                    .childGap        = 2,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                },
                .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
            }) {
                int idx = 0;
                for (const auto& e : s_entries) {
                    Clay_String cs  = cstr(e.text.c_str());
                    Clay_Color  col = e.system ? kSysText : kPlayerText;
                    CLAY(CLAY_IDI("ChatEntry", idx++), {
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                        },
                    }) {
                        CLAY_TEXT(cs, CLAY_TEXT_CONFIG({
                            .textColor = col,
                            .fontSize  = 11,
                        }));
                    }
                }
            }

            // ── 1px divider ───────────────────────────────────────────────────
            CLAY(CLAY_ID("ChatDivider"), {
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(kW - 2.f), CLAY_SIZING_FIXED(1.f) },
                },
                .backgroundColor = kBorder,
            }) {}

            // ── Input line ────────────────────────────────────────────────────
            if (netc) {
                CLAY(CLAY_ID("ChatInputRow"), {
                    .layout = {
                        .sizing         = { CLAY_SIZING_FIXED(kW - 2.f),
                                             CLAY_SIZING_FIXED(kInputH) },
                        .padding        = { (uint16_t)kPadX, (uint16_t)kPadX, 6, 6 },
                        .childAlignment = { .x = CLAY_ALIGN_X_LEFT,
                                             .y = CLAY_ALIGN_Y_CENTER },
                    },
                    .backgroundColor = kInputBg,
                }) {
                    Clay_String inputCs = cstr(s_inputDisplay.c_str());
                    CLAY_TEXT(inputCs, CLAY_TEXT_CONFIG({
                        .textColor = kPlayerText,
                        .fontSize  = 11,
                    }));
                }
            }
        }
    }
}

} // namespace ui
