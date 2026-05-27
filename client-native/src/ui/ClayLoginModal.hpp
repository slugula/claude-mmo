#pragma once
// ClayLoginModal — OSRS-style login and player-join modals rendered with Clay.
// Text input uses manual ImGui keystroke capture (no ImGui InputText widgets).
// Call buildLoginModal / buildJoinModal between Clay_BeginLayout / Clay_EndLayout.
// Read loginFormState() / joinFormState() AFTER clayFrame to dispatch network calls.

#include <string>

namespace net { class NetworkClient; }

namespace ui {

// ── Login modal state ─────────────────────────────────────────────────────────
struct LoginFormState {
    bool submitted    = false;  // true on the frame the user presses Connect/Create
    bool registerMode = false;
    char host[256]    = {};
    int  port         = 8080;
    char username[32] = {};
    char password[64] = {};
};
const LoginFormState& loginFormState();

// Call between Clay_BeginLayout / Clay_EndLayout.
// Shows a full-screen overlay + centred login dialog.
// keyboardActive: pass false when another modal is open and should own the keyboard.
void buildLoginModal(float screenW, float screenH,
                     bool leftClicked,
                     net::NetworkClient* netc);

// Zero the password buffer in both the UI state and the shared LoginFormState.
// Call from App.cpp immediately after reading loginFormState().password and
// dispatching it to loginAndConnect / registerAndConnect.
void loginClearPassword();

// ── Join modal state ──────────────────────────────────────────────────────────
struct JoinFormState {
    bool submitted = false;  // true on the frame user presses Confirm
    char name[13]  = {};     // max 12 chars + null
};
const JoinFormState& joinFormState();

// Call between Clay_BeginLayout / Clay_EndLayout.
// Shows a centred name-picker dialog (no overlay — assumes login overlay covers bg).
void buildJoinModal(float screenW, float screenH,
                    bool leftClicked);

} // namespace ui
