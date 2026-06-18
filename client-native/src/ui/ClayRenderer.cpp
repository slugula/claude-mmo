// ClayRenderer.cpp
// CLAY_IMPLEMENTATION must be defined in exactly one translation unit.
// This file owns it. All other files that include clay.h get declarations only.

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#define CLAY_IMPLEMENTATION
#include <clay.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "ui/ClayRenderer.hpp"
#include "ui/ClayHudPanel.hpp"
#include "ui/ClayContextMenu.hpp"
#include "ui/ClayClickFeedback.hpp"
#include "ui/ClayContextInfo.hpp"
#include "ui/ClayTooltip.hpp"
#include "ui/ClayChatLog.hpp"
#include "ui/ClayLoginModal.hpp"
#include "ui/ClayBankPanel.hpp"
#include "net/NetworkClient.hpp"
#include "world/SpriteCache.hpp"

#include <imgui.h>
#include <cfloat>   // FLT_MAX
#include <cmath>    // std::floor
#include <cstring>

namespace ui {

// ── Arena ─────────────────────────────────────────────────────────────────────
static uint8_t* s_clayMem  = nullptr;
static size_t   s_arenaSize = 0;

// ── Clay UI ownership tracking ────────────────────────────────────────────────
// Set after Clay_EndLayout to gate world hover/click suppression next frame.
static bool s_clayOwned     = false;
static bool s_minimapHovered = false;

bool clayIsPointerOverUI() { return s_clayOwned; }
bool clayMinimapHovered()  { return s_minimapHovered; }

void claySetDebugMode(bool enabled) { Clay_SetDebugModeEnabled(enabled); }

// Resolve a Clay fontId to a loaded ImGui font (0 = UI, 1 = large pixel font).
// Falls back to the current font if the id is out of range.
static ImFont* fontForId(uint16_t id) {
    ImGuiIO& io = ImGui::GetIO();
    if (id < io.Fonts->Fonts.Size && io.Fonts->Fonts[id]) return io.Fonts->Fonts[id];
    return ImGui::GetFont();
}

// ── Text measurement callback (called by Clay during layout) ──────────────────
static Clay_Dimensions measureText(Clay_StringSlice text,
                                   Clay_TextElementConfig* cfg,
                                   void* /*userData*/)
{
    ImFont* font = fontForId(cfg ? cfg->fontId : 0);
    if (!font) return { 0.f, 0.f };
    float size = (cfg && cfg->fontSize > 0) ? static_cast<float>(cfg->fontSize)
                                             : ImGui::GetFontSize();
    ImVec2 dim = font->CalcTextSizeA(size, FLT_MAX, -1.f,
                                     text.chars,
                                     text.chars + text.length);
    return { dim.x, dim.y };
}

// ── Error handler (swallow — don't crash on layout mistakes) ──────────────────
static void onClayError(Clay_ErrorData err) { (void)err; }

// ── UI scale ──────────────────────────────────────────────────────────────────
// Clay lays out in LOGICAL pixels (framebuffer / scale); the renderer multiplies
// every coordinate by the scale on the way out, so the HUD stays a consistent
// physical size regardless of display DPI.
static float s_uiScale = 1.0f;
void  claySetUiScale(float scale) { s_uiScale = (scale > 0.25f) ? scale : 1.0f; }
float clayUiScale() { return s_uiScale; }

// ── Init ──────────────────────────────────────────────────────────────────────
void clayInit(int w, int h)
{
    s_arenaSize = Clay_MinMemorySize();
    s_clayMem   = new uint8_t[s_arenaSize];

    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(
        s_arenaSize, s_clayMem);
    Clay_Initialize(arena,
                    { w / s_uiScale, h / s_uiScale },
                    { onClayError });
    Clay_SetMeasureTextFunction(measureText, nullptr);
}

void clayResize(int w, int h)
{
    Clay_SetLayoutDimensions({ w / s_uiScale, h / s_uiScale });
}

// ── Colour helper ─────────────────────────────────────────────────────────────
static ImU32 toImU32(Clay_Color c)
{
    return IM_COL32(static_cast<int>(c.r),
                    static_cast<int>(c.g),
                    static_cast<int>(c.b),
                    static_cast<int>(c.a));
}

// ── Render (internal) ─────────────────────────────────────────────────────────
static void clayRenderInternal(Clay_RenderCommandArray commands)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    for (int32_t i = 0; i < (int32_t)commands.length; ++i) {
        Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&commands, i);
        const Clay_BoundingBox& b = cmd->boundingBox;
        // Scale logical Clay coordinates up to physical pixels by the UI scale,
        // then snap edges to the pixel grid (avoids blurry half-pixel sprites).
        const float S = s_uiScale;
        ImVec2 p0 { std::floor(b.x * S),              std::floor(b.y * S)              };
        ImVec2 p1 { std::floor((b.x + b.width) * S),  std::floor((b.y + b.height) * S) };

        switch (cmd->commandType) {

        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            const auto& r = cmd->renderData.rectangle;
            float radius  = r.cornerRadius.topLeft * S;
            dl->AddRectFilled(p0, p1, toImU32(r.backgroundColor), radius);
            break;
        }

        case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            const auto& bd = cmd->renderData.border;
            // Guard: only draw if at least one side has a non-zero width.
            const uint16_t maxW = std::max({bd.width.top, bd.width.bottom,
                                            bd.width.left, bd.width.right});
            if (maxW == 0) break;
            ImU32 col = toImU32(bd.color);
            float r   = bd.cornerRadius.topLeft * S;
            float t   = static_cast<float>(maxW) * S;
            if (r > 0.f) {
                // Rounded border: draw as a single outlined rect.
                dl->AddRect(p0, p1, col, r, 0, t);
            } else {
                if (bd.width.top)
                    dl->AddLine({p0.x, p0.y}, {p1.x, p0.y}, col, bd.width.top * S);
                if (bd.width.bottom)
                    dl->AddLine({p0.x, p1.y}, {p1.x, p1.y}, col, bd.width.bottom * S);
                if (bd.width.left)
                    dl->AddLine({p0.x, p0.y}, {p0.x, p1.y}, col, bd.width.left * S);
                if (bd.width.right)
                    dl->AddLine({p1.x, p0.y}, {p1.x, p1.y}, col, bd.width.right * S);
            }
            break;
        }

        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            const auto& t = cmd->renderData.text;
            ImFont* font  = fontForId(t.fontId);
            float size    = ((t.fontSize > 0) ? static_cast<float>(t.fontSize)
                                              : ImGui::GetFontSize()) * S;
            dl->AddText(font, size, p0, toImU32(t.textColor),
                        t.stringContents.chars,
                        t.stringContents.chars + t.stringContents.length);
            break;
        }

        case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
            // imageData carries a GLuint cast to void* via uintptr_t.
            GLuint tex = static_cast<GLuint>(
                reinterpret_cast<uintptr_t>(cmd->renderData.image.imageData));
            dl->AddImage((ImTextureID)(uintptr_t)tex, p0, p1);
            break;
        }

        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            dl->PushClipRect(p0, p1, /*intersect_with_current=*/true);
            break;

        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            dl->PopClipRect();
            break;

        default:
            break;
        }
    }
}

// ── Frame ─────────────────────────────────────────────────────────────────────
void clayFrame(const shared::PlayerState* player,
               net::NetworkClient* netc,
               const SpriteCache*  sprites,
               UiHoverState*       hover,
               float dt,
               float mx, float my,
               float screenW, float screenH,
               bool mouseDown,
               bool leftClicked,
               bool rightClicked,
               const char* contextVerb,
               const char* contextSubject,
               float wheelDelta,
               bool showLoginModal,
               bool showJoinModal,
               bool bankOpen,
               unsigned int minimapTex)
{
    // Clay works in logical pixels: convert the physical-pixel mouse + screen
    // dimensions down by the UI scale.
    const float invS = 1.0f / s_uiScale;
    mx *= invS; my *= invS; screenW *= invS; screenH *= invS;
    Clay_SetPointerState({ mx, my }, mouseDown);
    // wheelDelta from ImGui io.MouseWheel (positive = scroll up).
    // Clay multiplies scrollDelta by 10 internally; pass *3 for ~2 lines/notch.
    Clay_UpdateScrollContainers(false, { 0.f, wheelDelta * 3.f }, dt);
    Clay_BeginLayout();

    clayHudBuildLayout(player, sprites, mx, my, minimapTex, bankOpen);
    buildChatLog(screenW, screenH, player, netc);
    buildContextMenu();
    buildClickFeedback(dt);
    buildContextInfo(contextVerb, contextSubject);
    buildTooltip(mx, my, screenW, screenH);

    // Bank panel (rendered above HUD, below modals)
    buildBankPanel(screenW, screenH, player, netc, sprites,
                   bankOpen, leftClicked, rightClicked, mouseDown, mx, my, hover);

    // Modals render last (highest z-index — cover all other UI)
    if (showJoinModal)  buildJoinModal(screenW, screenH, leftClicked);
    if (showLoginModal) buildLoginModal(screenW, screenH, leftClicked, netc);

    Clay_RenderCommandArray cmds = Clay_EndLayout(dt);

    // Track which major containers the pointer was over this frame.
    // Used next frame to suppress world hover/click events when Clay owns the mouse.
    s_minimapHovered = Clay_PointerOver(CLAY_ID("MinimapPanel"));
    s_clayOwned = Clay_PointerOver(CLAY_ID("HudPanel"))
               || Clay_PointerOver(CLAY_ID("ChatBox"))
               || Clay_PointerOver(CLAY_ID("BkPanel"))
               || Clay_PointerOver(CLAY_ID("LoginOverlay"))
               || Clay_PointerOver(CLAY_ID("JoinOverlay"))
               || Clay_PointerOver(CLAY_ID("ContextMenu"))
               || s_minimapHovered;
    // Qty badge floating elements use CLAY_ATTACH_TO_PARENT so they are
    // positioned relative to their slot, but Clay_PointerOver on the parent
    // panel may not cover them when they visually overflow.  Check each badge
    // explicitly so hovering over a stack count still blocks world picking.
    if (!s_clayOwned) {
        // Inventory: 4×7 = 28 slots
        for (int i = 0; i < 28 && !s_clayOwned; ++i)
            s_clayOwned = Clay_PointerOver(CLAY_IDI("InvQty", i));
        // Equipment: up to 12 slots
        for (int i = 0; i < 12 && !s_clayOwned; ++i)
            s_clayOwned = Clay_PointerOver(CLAY_IDI("EqQty", i));
        // Bank grids: bank up to 400, bank-inv 28
        for (int i = 0; i < 400 && !s_clayOwned; ++i)
            s_clayOwned = Clay_PointerOver(CLAY_IDI("BkBankQty", i));
        for (int i = 0; i < 28 && !s_clayOwned; ++i)
            s_clayOwned = Clay_PointerOver(CLAY_IDI("BkInvQty", i));
    }

    // Input handling (after layout so PointerOver uses this frame's bounds)
    clayHudHandleInput(player, netc, hover, leftClicked, rightClicked, mouseDown, mx, my);
    handleContextMenuInput(leftClicked, mx, my);

    clayRenderInternal(cmds);
}

} // namespace ui
