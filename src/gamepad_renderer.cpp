#include "gamepad_renderer.h"
#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace GamepadRenderer {

// ── palette ──────────────────────────────────────────────────

static ImU32 ColorA()  { return IM_COL32( 96, 202,  56, 255); }
static ImU32 ColorB()  { return IM_COL32(228,  56,  56, 255); }
static ImU32 ColorX()  { return IM_COL32( 56, 138, 224, 255); }
static ImU32 ColorY()  { return IM_COL32(244, 196,  48, 255); }

static ImU32 Lit()     { return IM_COL32(255, 255, 255, 255); }
static ImU32 Dim()     { return IM_COL32(100, 100, 100, 255); }
static ImU32 Body()    { return IM_COL32( 50,  50,  55, 255); }
static ImU32 Outline() { return IM_COL32(120, 120, 130, 255); }
static ImU32 BgDim()   { return IM_COL32( 35,  35,  38, 180); }

// ── small drawing helpers ────────────────────────────────────

static void DrawBody(ImDrawList* dl, const Layout& L) {
    ImVec2 tl = L.P(40, 30);
    ImVec2 br = L.P(360, 230);
    dl->AddRectFilled(tl, br, Body(), L.S(30));
    dl->AddRect(tl, br, Outline(), L.S(30), 0, L.S(2));

    ImVec2 gltl = L.P(30, 100);
    ImVec2 glbr = L.P(100, 260);
    dl->AddRectFilled(gltl, glbr, Body(), L.S(20));
    dl->AddRect(gltl, glbr, Outline(), L.S(20), 0, L.S(2));

    ImVec2 grtl = L.P(300, 100);
    ImVec2 grbr = L.P(370, 260);
    dl->AddRectFilled(grtl, grbr, Body(), L.S(20));
    dl->AddRect(grtl, grbr, Outline(), L.S(20), 0, L.S(2));
}

static void DrawDPad(ImDrawList* dl, const Layout& L, const GamepadState& gs) {
    ImVec2 center = L.P(120, 155);
    float arm = L.S(18);
    float w   = L.S(12);
    float rnd = L.S(3);

    auto bar = [&](ImVec2 a, ImVec2 b, bool pressed) {
        ImVec2 half(w * 0.5f, w * 0.5f);
        dl->AddRectFilled(
            ImVec2(a.x - half.x, a.y - half.y),
            ImVec2(b.x + half.x, b.y + half.y),
            pressed ? Lit() : Dim(), rnd);
    };

    bar(ImVec2(center.x, center.y - arm), ImVec2(center.x, center.y + arm), false);
    bar(ImVec2(center.x - arm, center.y), ImVec2(center.x + arm, center.y), false);

    float ha = arm * 0.55f;
    if (gs.IsPressed(Button::DPadUp))
        dl->AddRectFilled(
            ImVec2(center.x - w * 0.5f, center.y - arm),
            ImVec2(center.x + w * 0.5f, center.y - ha + w * 0.5f), Lit(), rnd);
    if (gs.IsPressed(Button::DPadDown))
        dl->AddRectFilled(
            ImVec2(center.x - w * 0.5f, center.y + ha - w * 0.5f),
            ImVec2(center.x + w * 0.5f, center.y + arm), Lit(), rnd);
    if (gs.IsPressed(Button::DPadLeft))
        dl->AddRectFilled(
            ImVec2(center.x - arm, center.y - w * 0.5f),
            ImVec2(center.x - ha + w * 0.5f, center.y + w * 0.5f), Lit(), rnd);
    if (gs.IsPressed(Button::DPadRight))
        dl->AddRectFilled(
            ImVec2(center.x + ha - w * 0.5f, center.y - w * 0.5f),
            ImVec2(center.x + arm, center.y + w * 0.5f), Lit(), rnd);
}

static void DrawFaceButtons(ImDrawList* dl, const Layout& L, const GamepadState& gs) {
    ImVec2 center = L.P(290, 110);
    float spread = L.S(18);
    float r      = L.S(11);

    struct FaceBtn { float dx; float dy; Button btn; ImU32 col; const char* label; };
    std::array btns = {
        FaceBtn{  0,  spread, Button::A, ColorA(), "A" },
        FaceBtn{  spread,  0, Button::B, ColorB(), "B" },
        FaceBtn{ -spread,  0, Button::X, ColorX(), "X" },
        FaceBtn{  0, -spread, Button::Y, ColorY(), "Y" },
    };

    for (auto& fb : btns) {
        ImVec2 pos(center.x + fb.dx, center.y + fb.dy);
        bool pressed = gs.IsPressed(fb.btn);
        dl->AddCircleFilled(pos, r, pressed ? fb.col : IM_COL32(60, 60, 65, 255));
        dl->AddCircle(pos, r, fb.col, 0, L.S(2));

        ImVec2 ts = ImGui::CalcTextSize(fb.label);
        dl->AddText(ImVec2(pos.x - ts.x * 0.5f, pos.y - ts.y * 0.5f),
                    pressed ? IM_COL32(255, 255, 255, 255) : fb.col, fb.label);
    }
}

static void DrawThumbstick(ImDrawList* dl, const Layout& L,
                           ImVec2 center, float stickX, float stickY,
                           bool pressed) {
    float outerR = L.S(20);
    float innerR = L.S(13);
    float maxDeflect = L.S(10);

    dl->AddCircleFilled(center, outerR, IM_COL32(35, 35, 38, 255));
    dl->AddCircle(center, outerR, Outline(), 0, L.S(2));

    ImVec2 knob(center.x + stickX * maxDeflect,
                center.y - stickY * maxDeflect);
    dl->AddCircleFilled(knob, innerR, pressed ? Lit() : IM_COL32(80, 80, 85, 255));
    dl->AddCircle(knob, innerR, Outline(), 0, L.S(1.5f));
}

static void DrawBumper(ImDrawList* dl, const Layout& L,
                       ImVec2 tl, ImVec2 br, bool pressed) {
    float rnd = L.S(5);
    dl->AddRectFilled(tl, br, pressed ? Lit() : Dim(), rnd);
    dl->AddRect(tl, br, Outline(), rnd, 0, L.S(1.5f));
}

static void DrawTrigger(ImDrawList* dl, const Layout& L,
                        ImVec2 tl, ImVec2 br, float value) {
    float rnd = L.S(4);
    dl->AddRectFilled(tl, br, IM_COL32(35, 35, 38, 255), rnd);
    dl->AddRect(tl, br, Outline(), rnd, 0, L.S(1.5f));

    if (value > 0.01f) {
        float fillH = (br.y - tl.y) * value;
        ImVec2 ftl(tl.x, br.y - fillH);
        dl->AddRectFilled(ftl, br, IM_COL32(100, 200, 255, 220), rnd);
    }
}

static void DrawSmallButton(ImDrawList* dl, const Layout& L,
                            ImVec2 center, float w, float h,
                            const char* label, bool pressed) {
    ImVec2 tl(center.x - w * 0.5f, center.y - h * 0.5f);
    ImVec2 br(center.x + w * 0.5f, center.y + h * 0.5f);
    float rnd = L.S(4);
    dl->AddRectFilled(tl, br, pressed ? Lit() : Dim(), rnd);
    dl->AddRect(tl, br, Outline(), rnd, 0, L.S(1));

    ImVec2 ts = ImGui::CalcTextSize(label);
    ImU32 tc = pressed ? IM_COL32(0, 0, 0, 255) : IM_COL32(180, 180, 180, 255);
    dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), tc, label);
}

/**
 * @brief Render a controller panel that visualizes a gamepad's layout and state.
 *
 * Renders a framed panel at the given position/size containing a header (built from the player
 * slot, optional display name, and backend name), a connection-status pill, and a scaled
 * representation of the controller showing body, D-pad, face buttons, thumbsticks, bumpers,
 * triggers, and small buttons. When the gamepad is not connected, displays a centered
 * "No controller detected" message instead of the controls.
 *
 * @param dl ImGui draw list to use for low-level drawing operations.
 * @param panelPos Top-left corner of the panel in screen coordinates.
 * @param panelSize Width and height of the panel in screen coordinates.
 * @param gs Current gamepad state (buttons, sticks, triggers, connection).
 * @param slotIndex Zero-based player slot index used in the header label.
 * @param backendName Null-terminated string identifying the input backend (shown in header).
 * @param displayName Optional null-terminated display name for the controller; when non-empty
 *                    it is included in the header as "Player N - DisplayName  [backend]".
 */

void DrawGamepad(ImDrawList* dl, ImVec2 panelPos, ImVec2 panelSize,
                 const GamepadState& gs, int slotIndex, const char* backendName,
                 const char* displayName,
                 ImTextureID bodyTexture,
                 ImVec2 textureSizeLogical) {
    dl->AddRectFilled(panelPos,
        ImVec2(panelPos.x + panelSize.x, panelPos.y + panelSize.y),
        BgDim(), 8.0f);

    std::string header;
    if (displayName && displayName[0] != '\0')
        header = std::format("Player {} - {}  [{}]", slotIndex + 1, displayName, backendName);
    else
        header = std::format("Player {}  [{}]", slotIndex + 1, backendName);
    ImVec2 hts = ImGui::CalcTextSize(header.c_str());
    float headerH = hts.y + 10.0f;

    const char* status = gs.connected ? "Connected" : "Not Connected";
    ImVec2 sts = ImGui::CalcTextSize(status);
    const float pillLeft = panelPos.x + panelSize.x - sts.x - 18 - 4;
    const float headerClipRight = pillLeft;
    dl->PushClipRect(ImVec2(panelPos.x + 10, panelPos.y),
                     ImVec2(headerClipRight, panelPos.y + headerH), true);
    dl->AddText(ImVec2(panelPos.x + 10, panelPos.y + 5),
                gs.connected ? IM_COL32(220, 220, 220, 255)
                             : IM_COL32(100, 100, 100, 255),
                header.c_str());
    dl->PopClipRect();

    {
        float px = panelPos.x + panelSize.x - sts.x - 18;
        float py = panelPos.y + 5;
        ImU32 pillCol = gs.connected ? IM_COL32(50, 180, 80, 200)
                                     : IM_COL32(180, 50, 50, 200);
        dl->AddRectFilled(ImVec2(px - 4, py - 1), ImVec2(px + sts.x + 4, py + sts.y + 1),
                          pillCol, 4.0f);
        dl->AddText(ImVec2(px, py), IM_COL32(255, 255, 255, 255), status);
    }

    float padW = panelSize.x - 20.0f;
    float padH = panelSize.y - headerH - 10.0f;
    ImVec2 padOrigin(panelPos.x + 10.0f, panelPos.y + headerH + 2.0f);

    float scaleX = padW / 400.0f;
    float scaleY = padH / 280.0f;
    float scale  = std::min(scaleX, scaleY);

    Layout L;
    L.origin = ImVec2(padOrigin.x + (padW - 400.0f * scale) * 0.5f,
                      padOrigin.y + (padH - 280.0f * scale) * 0.5f);
    L.sx = scale;
    L.sy = scale;

    if (!gs.connected) {
        ImVec2 center(padOrigin.x + padW * 0.5f, padOrigin.y + padH * 0.5f);
        const char* msg = "No controller detected";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
                    IM_COL32(80, 80, 80, 255), msg);
        return;
    }

    if (bodyTexture != nullptr) {
        ImVec2 tl = L.P(0.f, 0.f);
        ImVec2 br = L.P(textureSizeLogical.x, textureSizeLogical.y);
        dl->AddImage(bodyTexture, tl, br, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f));
    } else {
        DrawBody(dl, L);
    }
    DrawDPad(dl, L, gs);
    DrawFaceButtons(dl, L, gs);

    DrawThumbstick(dl, L, L.P(170, 170), gs.leftStickX, gs.leftStickY,
                   gs.IsPressed(Button::LeftThumb));
    DrawThumbstick(dl, L, L.P(240, 170), gs.rightStickX, gs.rightStickY,
                   gs.IsPressed(Button::RightThumb));

    DrawBumper(dl, L, L.P(70, 30), L.P(160, 48), gs.IsPressed(Button::LeftBumper));
    DrawBumper(dl, L, L.P(240, 30), L.P(330, 48), gs.IsPressed(Button::RightBumper));

    DrawTrigger(dl, L, L.P(50, 2), L.P(80, 28), gs.leftTrigger);
    DrawTrigger(dl, L, L.P(320, 2), L.P(350, 28), gs.rightTrigger);

    DrawSmallButton(dl, L, L.P(175, 100), L.S(28), L.S(12), "Back",
                    gs.IsPressed(Button::Back));
    DrawSmallButton(dl, L, L.P(200, 118), L.S(22), L.S(12), "Guide",
                    gs.IsPressed(Button::Guide));
    DrawSmallButton(dl, L, L.P(225, 100), L.S(28), L.S(12), "Start",
                    gs.IsPressed(Button::Start));

    {
        auto buf = std::format(
            "LX:{: .2f}  LY:{: .2f}  RX:{: .2f}  RY:{: .2f}  LT:{:.2f}  RT:{:.2f}",
            gs.leftStickX, gs.leftStickY,
            gs.rightStickX, gs.rightStickY,
            gs.leftTrigger, gs.rightTrigger);
        ImVec2 ts = ImGui::CalcTextSize(buf.c_str());
        float tx = padOrigin.x + (padW - ts.x) * 0.5f;
        float ty = padOrigin.y + padH - ts.y - 2.0f;
        dl->AddText(ImVec2(tx, ty), IM_COL32(160, 160, 170, 255), buf.c_str());
    }
}

} // namespace GamepadRenderer
