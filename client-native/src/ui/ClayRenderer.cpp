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
#include "net/NetworkClient.hpp"
#include "world/SpriteCache.hpp"

#include <imgui.h>
#include <cfloat>   // FLT_MAX
#include <cstring>

namespace ui {

// ── Arena ─────────────────────────────────────────────────────────────────────
static uint8_t* s_clayMem  = nullptr;
static size_t   s_arenaSize = 0;

// ── Text measurement callback (called by Clay during layout) ──────────────────
static Clay_Dimensions measureText(Clay_StringSlice text,
                                   Clay_TextElementConfig* cfg,
                                   void* /*userData*/)
{
    ImFont* font = ImGui::GetFont();
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

// ── Init ──────────────────────────────────────────────────────────────────────
void clayInit(int w, int h)
{
    s_arenaSize = Clay_MinMemorySize();
    s_clayMem   = new uint8_t[s_arenaSize];

    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(
        s_arenaSize, s_clayMem);
    Clay_Initialize(arena,
                    { static_cast<float>(w), static_cast<float>(h) },
                    { onClayError });
    Clay_SetMeasureTextFunction(measureText, nullptr);
}

void clayResize(int w, int h)
{
    Clay_SetLayoutDimensions({ static_cast<float>(w), static_cast<float>(h) });
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
        ImVec2 p0 { b.x,           b.y            };
        ImVec2 p1 { b.x + b.width, b.y + b.height };

        switch (cmd->commandType) {

        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            const auto& r = cmd->renderData.rectangle;
            float radius  = r.cornerRadius.topLeft;
            dl->AddRectFilled(p0, p1, toImU32(r.backgroundColor), radius);
            break;
        }

        case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            const auto& bd = cmd->renderData.border;
            ImU32 col      = toImU32(bd.color);
            float r        = bd.cornerRadius.topLeft;
            if (r > 0.f) {
                // Rounded border: draw as outlined rect
                float t = static_cast<float>(
                    bd.width.top ? bd.width.top :
                    bd.width.left ? bd.width.left : 1);
                dl->AddRect(p0, p1, col, r, 0, t);
            } else {
                if (bd.width.top)
                    dl->AddLine({p0.x, p0.y}, {p1.x, p0.y},
                                col, static_cast<float>(bd.width.top));
                if (bd.width.bottom)
                    dl->AddLine({p0.x, p1.y}, {p1.x, p1.y},
                                col, static_cast<float>(bd.width.bottom));
                if (bd.width.left)
                    dl->AddLine({p0.x, p0.y}, {p0.x, p1.y},
                                col, static_cast<float>(bd.width.left));
                if (bd.width.right)
                    dl->AddLine({p1.x, p0.y}, {p1.x, p1.y},
                                col, static_cast<float>(bd.width.right));
            }
            break;
        }

        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            const auto& t = cmd->renderData.text;
            ImFont* font  = ImGui::GetFont();
            float size    = (t.fontSize > 0) ? static_cast<float>(t.fontSize)
                                             : ImGui::GetFontSize();
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
               bool mouseDown,
               bool leftClicked,
               bool rightClicked)
{
    Clay_SetPointerState({ mx, my }, mouseDown);
    Clay_UpdateScrollContainers(false, { 0.f, 0.f }, dt);
    Clay_BeginLayout();
    clayHudBuildLayout(player, sprites);
    Clay_RenderCommandArray cmds = Clay_EndLayout(dt);
    clayHudHandleInput(player, netc, hover, leftClicked, rightClicked);
    clayRenderInternal(cmds);
}

} // namespace ui
