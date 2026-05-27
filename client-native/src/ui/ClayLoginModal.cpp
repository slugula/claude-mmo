// ClayLoginModal.cpp
// Full-Clay login and join modals with manual text-field input.

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <clay.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "ui/ClayLoginModal.hpp"
#include "net/NetworkClient.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ui {

// ── Shared colours ────────────────────────────────────────────────────────────
static constexpr Clay_Color kOverlay   = {   0,   0,   0, 190 };
static constexpr Clay_Color kBg        = {  26,  13,   0, 255 };
static constexpr Clay_Color kBorder    = { 139, 108,  62, 255 };
static constexpr Clay_Color kTitle     = { 255, 152,  31, 255 };
static constexpr Clay_Color kLabel     = { 200, 162,  97, 255 };
static constexpr Clay_Color kInputBg   = {  12,   6,   0, 255 };
static constexpr Clay_Color kInputBdr  = {  80,  58,  28, 200 };
static constexpr Clay_Color kActBdr    = { 255, 152,  31, 255 };
static constexpr Clay_Color kText      = { 240, 206,  96, 255 };
static constexpr Clay_Color kWhite     = { 255, 255, 255, 255 };
static constexpr Clay_Color kRowBg     = {  14,   7,   1, 255 };  // unified row bg
static constexpr Clay_Color kBtnBg     = {  32,  16,   4, 255 };  // close to kBg, barely visible
static constexpr Clay_Color kBtnHov    = {  52,  30,   8, 255 };
static constexpr Clay_Color kBtnActBg  = {  72,  44,  14, 255 };
static constexpr Clay_Color kGrey      = { 130, 110,  80, 200 };
static constexpr Clay_Color kStatusYel = { 200, 162,  97, 255 };
static constexpr Clay_Color kStatusRed = { 255,  80,  80, 255 };

// ── Temp string scratch ───────────────────────────────────────────────────────
static char s_buf[8192];
static int  s_boff = 0;

static Clay_String cs(const char* s) {
    int len = static_cast<int>(std::strlen(s));
    if (s_boff + len + 1 > static_cast<int>(sizeof(s_buf)))
        len = static_cast<int>(sizeof(s_buf)) - s_boff - 1;
    if (len <= 0) return { false, 0, "" };
    char* dst = s_buf + s_boff;
    std::memcpy(dst, s, len); dst[len] = '\0';
    s_boff += len + 1;
    return { false, len, dst };
}

// ── Helper: build a masked display string ─────────────────────────────────────
static std::string masked(const char* src) {
    return std::string(std::strlen(src), '*');
}

// ─────────────────────────────────────────────────────────────────────────────
// LOGIN MODAL
// ─────────────────────────────────────────────────────────────────────────────

// Field indices
static constexpr int kFHost = 0;
static constexpr int kFPort = 1;
static constexpr int kFUser = 2;
static constexpr int kFPass = 3;
static constexpr int kFCount= 4;

#ifdef PRODUCTION_BUILD
// In production builds the host/port fields are hidden; Tab only cycles user↔pass.
static constexpr int kFFirst = kFUser;
static constexpr int kFLast  = kFPass;
#else
static constexpr int kFFirst = kFHost;
static constexpr int kFLast  = kFPass;
#endif
static int  s_loginActive  = kFUser; // default focus on username
static bool s_registerMode = false;

#ifdef PRODUCTION_BUILD
static char s_fHost[256] = PRODUCTION_HOST;
static char s_fPort[8]   = "8080";           // kept for LoginFormState; not shown in UI
#else
static char s_fHost[256] = "localhost";
static char s_fPort[8]   = "8080";
#endif
static char s_fUser[32]  = {};
static char s_fPass[64]  = {};

static int  s_fLens[kFCount] = {
    static_cast<int>(std::strlen(s_fHost)),
    static_cast<int>(std::strlen(s_fPort)),
    0, 0
};

static LoginFormState s_loginState;

const LoginFormState& loginFormState() { return s_loginState; }

void loginClearPassword() {
    std::memset(s_fPass,               0, sizeof(s_fPass));
    std::memset(s_loginState.password, 0, sizeof(s_loginState.password));
    s_fLens[kFPass] = 0;
}

void loginClearPassword() {
    std::memset(s_fPass,               0, sizeof(s_fPass));
    std::memset(s_loginState.password, 0, sizeof(s_loginState.password));
    s_fLens[kFPass] = 0;
}

static void loginCaptureKeys() {
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsAnyItemActive()) return;

    // Tab: cycle fields (host/port skipped in production builds)
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        s_loginActive++;
        if (s_loginActive > kFLast) s_loginActive = kFFirst;
    }

    // Backspace
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        int fi = s_loginActive;
        if (s_fLens[fi] > 0) {
            char* arr = (fi==kFHost) ? s_fHost : (fi==kFPort) ? s_fPort
                      : (fi==kFUser) ? s_fUser : s_fPass;
            arr[--s_fLens[fi]] = '\0';
        }
    }

    // Enter: submit
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        s_loginState.submitted = true;
    }

    // Character input
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        ImWchar ch = io.InputQueueCharacters[i];
        if (ch < 32 || ch >= 127) continue;
        int fi  = s_loginActive;
        char* arr   = (fi==kFHost) ? s_fHost : (fi==kFPort) ? s_fPort
                    : (fi==kFUser) ? s_fUser : s_fPass;
        int maxLen  = (fi==kFHost) ? 255 : (fi==kFPort) ? 7
                    : (fi==kFUser) ? 31  : 63;
        if (s_fLens[fi] < maxLen) {
            arr[s_fLens[fi]++] = static_cast<char>(ch);
            arr[s_fLens[fi]]   = '\0';
        }
    }

    // Keep form state in sync
    std::strncpy(s_loginState.host,     s_fHost, sizeof(s_loginState.host)-1);
    s_loginState.port = std::atoi(s_fPort);
    std::strncpy(s_loginState.username, s_fUser, sizeof(s_loginState.username)-1);
    std::strncpy(s_loginState.password, s_fPass, sizeof(s_loginState.password)-1);
    s_loginState.registerMode = s_registerMode;
}

// Helper: render one form row (label + input box).
// Must be called inside a parent with TOP_TO_BOTTOM layout.
static void loginFormRow(int fieldIdx, const char* label,
                         bool isPassword, bool leftClicked) {
    bool active  = (s_loginActive == fieldIdx);
    bool hovered = Clay_PointerOver(CLAY_IDI("LoginInput", fieldIdx));

    if (leftClicked && hovered) s_loginActive = fieldIdx;

    const char* arr = (fieldIdx==kFHost) ? s_fHost : (fieldIdx==kFPort) ? s_fPort
                    : (fieldIdx==kFUser) ? s_fUser : s_fPass;
    std::string display = isPassword ? masked(arr) : arr;
    if (active) display += '|'; // cursor

    // Row: no background — sits transparently on the modal bg.
    // Label and input are plain children with no individual backgrounds.
    CLAY(CLAY_IDI("LoginRow", fieldIdx), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(26) },
            .childGap        = 6,
            .childAlignment  = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
        },
    }) {
        // Label
        CLAY(CLAY_IDI("LoginLabel", fieldIdx), {
            .layout = {
                .sizing         = { CLAY_SIZING_FIXED(78), CLAY_SIZING_GROW(0) },
                .padding        = { 6, 0, 0, 0 },
                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER },
            }
        }) {
            CLAY_TEXT(cs(label), CLAY_TEXT_CONFIG({ .textColor = kLabel, .fontSize = 0 }));
        }
        // Input cell — dark inset background, orange border when focused
        Clay_Color inputBg  = active ? Clay_Color{ 22, 12, 2, 255 } : kInputBg;
        Clay_Color inputBdr = active ? kActBdr : kInputBdr;
        CLAY(CLAY_IDI("LoginInput", fieldIdx), {
            .layout = {
                .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .padding        = { 6, 6, 0, 0 },
                .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
            },
            .backgroundColor = inputBg,
            .cornerRadius    = CLAY_CORNER_RADIUS(2),
            .border          = { .color = inputBdr, .width = CLAY_BORDER_ALL(1) },
        }) {
            CLAY_TEXT(cs(display.c_str()), CLAY_TEXT_CONFIG({
                .textColor = kText, .fontSize = 0,
            }));
        }
    }
}

void buildLoginModal(float screenW, float screenH,
                     bool leftClicked,
                     net::NetworkClient* netc) {
    s_boff = 0;
    s_loginState.submitted = false; // reset each frame

    loginCaptureKeys();

    const net::Connection status = netc ? netc->status() : net::Connection::Disconnected;
    const bool busy = (status == net::Connection::LoggingIn ||
                       status == net::Connection::Connecting);

    // Button hover detection (uses prev-frame bounds)
    bool loginHov  = Clay_PointerOver(CLAY_ID("LoginBtnLogin"));
    bool regHov    = Clay_PointerOver(CLAY_ID("LoginBtnReg"));
    bool connectHov= Clay_PointerOver(CLAY_ID("LoginBtnConnect"));

    if (leftClicked) {
        if (loginHov)   s_registerMode = false;
        if (regHov)     s_registerMode = true;
        if (connectHov && !busy) s_loginState.submitted = true;
    }

    // ── Full-screen overlay ───────────────────────────────────────────────────
    CLAY(CLAY_ID("LoginOverlay"), {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(screenW), CLAY_SIZING_FIXED(screenH) },
        },
        .backgroundColor = kOverlay,
        .floating = {
            .offset       = { 0.f, 0.f },
            .zIndex       = 60,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_LEFT_TOP,
                .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {}

    // ── Border shell — own floating element, background + border only ─────────
    // Sized to match the content box. No children means no child can ever
    // inherit or appear to have a border from this element.
    CLAY(CLAY_ID("LoginBox"), {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(300.f), CLAY_SIZING_FIXED(258.f) },
        },
        .backgroundColor = kBg,
        .cornerRadius    = CLAY_CORNER_RADIUS(4),
        .floating = {
            .offset       = { 0.f, 0.f },
            .zIndex       = 61,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_CENTER_CENTER,
                .parent  = CLAY_ATTACH_POINT_CENTER_CENTER,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        },
        .border = { .color = kBorder, .width = CLAY_BORDER_ALL(1) },
    }) {}

    // ── Content — separate floating element on top, no border ─────────────────
    CLAY(CLAY_ID("LoginContent"), {
        .layout = {
            .sizing          = { CLAY_SIZING_FIXED(300.f), CLAY_SIZING_FIT(0) },
            .padding         = { 14, 14, 14, 14 },
            .childGap        = 6,
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .floating = {
            .offset       = { 0.f, 0.f },
            .zIndex       = 62,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_CENTER_CENTER,
                .parent  = CLAY_ATTACH_POINT_CENTER_CENTER,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {
        // ── Title ─────────────────────────────────────────────────────────────
        CLAY(CLAY_ID("LoginTitle"), {
            .layout = {
                .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
            }
        }) {
            CLAY_TEXT(CLAY_STRING("Project L"), CLAY_TEXT_CONFIG({
                .textColor = kTitle, .fontSize = 0,
            }));
        }

        // ── Form rows ─────────────────────────────────────────────────────────
#ifndef PRODUCTION_BUILD
        loginFormRow(kFHost, "Host",     false, leftClicked);
        loginFormRow(kFPort, "Port",     false, leftClicked);
#endif
        loginFormRow(kFUser, "Username", false, leftClicked);
        loginFormRow(kFPass, "Password", true,  leftClicked);

        // ── Mode toggle buttons ───────────────────────────────────────────────
        CLAY(CLAY_ID("LoginModeRow"), {
            .layout = {
                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28) },
                .childGap        = 6,
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            }
        }) {
            CLAY(CLAY_ID("LoginBtnLogin"), {
                .layout = {
                    .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                },
                .backgroundColor = !s_registerMode ? kBtnActBg
                                 : (loginHov       ? kBtnHov : kBtnBg),
                .cornerRadius    = CLAY_CORNER_RADIUS(2),
            }) {
                CLAY_TEXT(CLAY_STRING("Existing User"), CLAY_TEXT_CONFIG({
                    .textColor = kWhite, .fontSize = 0,
                }));
            }
            CLAY(CLAY_ID("LoginBtnReg"), {
                .layout = {
                    .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                },
                .backgroundColor = s_registerMode ? kBtnActBg
                                 : (regHov        ? kBtnHov : kBtnBg),
                .cornerRadius    = CLAY_CORNER_RADIUS(2),
            }) {
                CLAY_TEXT(CLAY_STRING("New Account"), CLAY_TEXT_CONFIG({
                    .textColor = kWhite, .fontSize = 0,
                }));
            }
        }

        // ── Connect / Create Account button ───────────────────────────────────
        {
            const char* btnLabel = s_registerMode ? "Create Account" : "Connect";
            CLAY(CLAY_ID("LoginBtnConnect"), {
                .layout = {
                    .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                },
                .backgroundColor = busy       ? Clay_Color{ 35, 20, 7, 160 }
                                 : connectHov ? kBtnHov : kBtnBg,
                .cornerRadius    = CLAY_CORNER_RADIUS(2),
            }) {
                CLAY_TEXT(cs(btnLabel), CLAY_TEXT_CONFIG({
                    .textColor = busy ? kGrey : kWhite, .fontSize = 0,
                }));
            }
        }

        // ── Status text ───────────────────────────────────────────────────────
        if (status == net::Connection::LoggingIn) {
            CLAY_TEXT(CLAY_STRING("Authenticating..."),
                CLAY_TEXT_CONFIG({ .textColor = kStatusYel, .fontSize = 0 }));
        } else if (status == net::Connection::Connecting) {
            CLAY_TEXT(CLAY_STRING("Connecting..."),
                CLAY_TEXT_CONFIG({ .textColor = kStatusYel, .fontSize = 0 }));
        } else if (status == net::Connection::Failed) {
            const std::string& err = netc ? netc->lastError() : std::string{};
            const std::string  msg = err.empty() ? "Connection failed" : err;
            CLAY_TEXT(cs(msg.c_str()),
                CLAY_TEXT_CONFIG({ .textColor = kStatusRed, .fontSize = 0 }));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// JOIN MODAL (name picker for new accounts)
// ─────────────────────────────────────────────────────────────────────────────

static char s_joinName[13] = {};
static int  s_joinLen      = 0;
static JoinFormState s_joinState;

const JoinFormState& joinFormState() { return s_joinState; }

static void joinCaptureKeys() {
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsAnyItemActive()) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && s_joinLen > 0)
        s_joinName[--s_joinLen] = '\0';

    if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        if (s_joinLen > 0) s_joinState.submitted = true;
    }

    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        ImWchar ch = io.InputQueueCharacters[i];
        if (ch >= 127 || s_joinLen >= 12) continue;
        // Allow alphanumeric and space only
        char c = static_cast<char>(ch);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == ' ') {
            s_joinName[s_joinLen++] = c;
            s_joinName[s_joinLen]   = '\0';
        }
    }
    std::strncpy(s_joinState.name, s_joinName, sizeof(s_joinState.name)-1);
}

void buildJoinModal(float screenW, float screenH, bool leftClicked) {
    s_boff = 0;
    s_joinState.submitted = false;

    joinCaptureKeys();

    const bool nameOk    = (s_joinLen > 0);
    bool confirmHov      = Clay_PointerOver(CLAY_ID("JoinBtnConfirm"));
    bool inputHov        = Clay_PointerOver(CLAY_ID("JoinInput"));

    if (leftClicked && confirmHov && nameOk) s_joinState.submitted = true;

    // Build cursor display
    std::string display = std::string(s_joinName) + '|';

    // ── Full-screen overlay (on top of login overlay) ─────────────────────────
    CLAY(CLAY_ID("JoinOverlay"), {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(screenW), CLAY_SIZING_FIXED(screenH) },
        },
        .backgroundColor = kOverlay,
        .floating = {
            .offset   = { 0.f, 0.f },
            .zIndex   = 70,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_LEFT_TOP,
                .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {}

    // ── Centred dialog ────────────────────────────────────────────────────────
    CLAY(CLAY_ID("JoinDialog"), {
        .floating = {
            .offset   = { 0.f, 0.f },
            .zIndex   = 71,
            .attachPoints = {
                .element = CLAY_ATTACH_POINT_CENTER_CENTER,
                .parent  = CLAY_ATTACH_POINT_CENTER_CENTER,
            },
            .attachTo = CLAY_ATTACH_TO_ROOT,
        }
    }) {
        CLAY(CLAY_ID("JoinBox"), {
            .layout = {
                .sizing          = { CLAY_SIZING_FIXED(300.f), CLAY_SIZING_FIT(0) },
                .padding         = { 16, 16, 16, 16 },
                .childGap        = 10,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
            .backgroundColor = kBg,
            .cornerRadius    = CLAY_CORNER_RADIUS(4),
            .border = { .color = kBorder, .width = CLAY_BORDER_ALL(1) }
        }) {
            // Title
            CLAY(CLAY_ID("JoinTitle"), {
                .layout = {
                    .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                }
            }) {
                CLAY_TEXT(CLAY_STRING("Welcome to Project L!"), CLAY_TEXT_CONFIG({
                    .textColor = kTitle, .fontSize = 0,
                }));
            }

            // Instructions
            CLAY_TEXT(CLAY_STRING("Choose a name for your character (max 12 chars):"),
                CLAY_TEXT_CONFIG({ .textColor = kLabel, .fontSize = 0 }));

            // Name input — orange border (always focused; only field in this modal)
            CLAY(CLAY_ID("JoinInput"), {
                .layout = {
                    .sizing   = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28) },
                    .padding  = { 8, 8, 5, 5 },
                    .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
                },
                .backgroundColor = inputHov ? Clay_Color{ 16, 8, 1, 255 } : kInputBg,
                .cornerRadius    = CLAY_CORNER_RADIUS(2),
                .border          = { .color = kActBdr, .width = CLAY_BORDER_ALL(1) },
            }) {
                CLAY_TEXT(cs(display.c_str()), CLAY_TEXT_CONFIG({
                    .textColor = kText, .fontSize = 0,
                }));
            }

            // Hint when empty
            if (!nameOk) {
                CLAY_TEXT(CLAY_STRING("Enter a name to continue."),
                    CLAY_TEXT_CONFIG({ .textColor = kGrey, .fontSize = 0 }));
            }

            // Confirm button — no individual border
            CLAY(CLAY_ID("JoinBtnConfirm"), {
                .layout = {
                    .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                },
                .backgroundColor = !nameOk    ? Clay_Color{ 35, 20, 7, 120 }
                                 : confirmHov  ? kBtnHov : kBtnBg,
                .cornerRadius    = CLAY_CORNER_RADIUS(2),
            }) {
                CLAY_TEXT(CLAY_STRING("Confirm"), CLAY_TEXT_CONFIG({
                    .textColor = nameOk ? kWhite : kGrey, .fontSize = 0,
                }));
            }
        }
    }
}

} // namespace ui
