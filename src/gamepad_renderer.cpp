#include "gamepad_renderer.h"
#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace GamepadRenderer {

// ── layout coordinates (Xbox: stick upper-left, D-pad lower-left; Sony: D-pad upper-left, stick lower-left) ──

struct LayoutCoords {
	float dpadX, dpadY;
	float faceX, faceY;
	float leftStickX, leftStickY;
	float rightStickX, rightStickY;
	float lbTlX, lbTlY, lbBrX, lbBrY;
	float rbTlX, rbTlY, rbBrX, rbBrY;
	float ltTlX, ltTlY, ltBrX, ltBrY;
	float rtTlX, rtTlY, rtBrX, rtBrY;
	float backX, backY, guideX, guideY, startX, startY;
	const char* faceLabelA;
	const char* faceLabelB;
	const char* faceLabelX;
	const char* faceLabelY;
	const char* backLabel;   // Back / Share
	const char* guideLabel;  // Guide / PS
	const char* startLabel;  // Start / Options
};

static const LayoutCoords XBOX_LAYOUT = {
	160.f, 154.f,   // D-pad lower-left
	275.f, 85.f,   // face buttons
	125.f, 85.f,   // left stick upper-left
	252.f, 158.f,  // right stick
	83.f, 33.f, 151.f, 47.f,    // left bumper (smaller)
	249.f, 33.f, 317.f, 47.f,   // right bumper
	34.f, 108.f, 46.f, 248.f,   // left trigger: slim, tall, on left grip
	354.f, 108.f, 366.f, 248.f, // right trigger: mirror on right grip
	182.f, 88.f, 200.f, 100.f, 218.f, 88.f,
	"A", "B", "X", "Y",
	"Back", "Guide", "Start"
};

// PlayStation: A slot = south = Cross, B slot = east = Circle; Square (X), Triangle (Y) — ASCII so default font works
static const char* SONY_FACE_A = "X";   // Cross (south)
static const char* SONY_FACE_B = "O";   // Circle (east)
static const char* SONY_FACE_X = "S";   // Square
static const char* SONY_FACE_Y = "T";   // Triangle

static const LayoutCoords SONY_LAYOUT = {
	114.f, 92.5f,   // D-pad upper-left (Sony symmetrical)
	285.f, 96.f,   // face buttons
	160.f, 130.f,   // left stick lower-left (Sony symmetrical)
	240.f, 130.f,  // right stick
	83.f, 33.f, 151.f, 47.f,
	249.f, 33.f, 317.f, 47.f,
	34.f, 108.f, 46.f, 248.f,
	354.f, 108.f, 366.f, 248.f,
	182.f, 88.f, 200.f, 100.f, 218.f, 88.f,
	SONY_FACE_A, SONY_FACE_B, SONY_FACE_X, SONY_FACE_Y,
	"Share", "PS", "Options"
};

/**
 * @brief Selects the layout coordinates for the specified layout type.
 *
 * @param t Layout type indicating which coordinate set to use (e.g., LayoutType::Sony).
 * @return const LayoutCoords& Reference to the corresponding LayoutCoords constant: SONY_LAYOUT when t is LayoutType::Sony, otherwise XBOX_LAYOUT.
 */
static const LayoutCoords& GetLayoutCoords(LayoutType t) {
	return t == LayoutType::Sony ? SONY_LAYOUT : XBOX_LAYOUT;
}

/**
 * @brief Color used for the "A" face button.
 *
 * @return ImU32 Packed RGBA color equal to approximately RGB(96, 202, 56) with alpha 255.
 */

static ImU32 ColorA()  { return IM_COL32( 96, 202,  56, 255); }
static ImU32 ColorB()  { return IM_COL32(228,  56,  56, 255); }
static ImU32 ColorX()  { return IM_COL32( 56, 138, 224, 255); }
static ImU32 ColorY()  { return IM_COL32(244, 196,  48, 255); }

static ImU32 Lit()     { return IM_COL32(255, 255, 255, 255); }
static ImU32 Dim()     { return IM_COL32(100, 100, 100, 255); }
static ImU32 Body()    { return IM_COL32( 50,  50,  55, 255); }
static ImU32 Outline() { return IM_COL32(120, 120, 130, 255); }
static ImU32 BgDim()   { return IM_COL32( 35,  35,  38, 180); }

/**
 * @brief Draws the controller body and its left/right side panels using the provided layout.
 *
 * Renders filled, rounded rectangles with outlines to represent the main controller shell
 * and the two side panels. Positions, sizes, corner radii and stroke widths are derived
 * from the supplied Layout instance and the module's color helpers.
 *
 * @param dl ImGui draw list used for rendering.
 * @param L Layout providing coordinate transforms and scaling for placement and sizes.
 */

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

/**
 * @brief Renders a D-pad at the given logical center, showing directional presses.
 *
 * Draws a cross-shaped D-pad centered at (cx, cy) in layout coordinates and highlights
 * individual directions when the corresponding D-pad buttons in gs are pressed.
 *
 * @param dl ImGui draw list used to issue drawing commands.
 * @param L Layout helper providing coordinate transforms and scaling.
 * @param gs Current gamepad state used to query directional button presses.
 * @param cx X coordinate of the D-pad center in logical layout space.
 * @param cy Y coordinate of the D-pad center in logical layout space.
 */
static void DrawDPad(ImDrawList* dl, const Layout& L, const GamepadState& gs, float cx, float cy) {
    ImVec2 center = L.P(cx, cy);
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

/**
 * @brief Render the four face buttons (A/B/X/Y) with colors, labels, and pressed-state visuals.
 *
 * Draws four circular face buttons positioned around the given center using the supplied Layout for
 * scaling and positioning. Each button is filled with its face color when pressed (or a dim fill when
 * not), has a colored outline, and displays the provided label centered on the button. Button press
 * state is read from the provided GamepadState.
 *
 * @param dl ImGui draw list used to issue the rendering commands.
 * @param L Layout providing coordinate transforms and scaling.
 * @param gs Current gamepad state used to determine which face buttons are pressed.
 * @param cx X coordinate (logical) of the face-button group center.
 * @param cy Y coordinate (logical) of the face-button group center.
 * @param labelA Label text to render on the A button.
 * @param labelB Label text to render on the B button.
 * @param labelX Label text to render on the X button.
 * @param labelY Label text to render on the Y button.
 */
static void DrawFaceButtons(ImDrawList* dl, const Layout& L, const GamepadState& gs,
                            float cx, float cy,
                            const char* labelA, const char* labelB,
                            const char* labelX, const char* labelY) {
    ImVec2 center = L.P(cx, cy);
    float spread = L.S(18);
    float r      = L.S(11);

    struct FaceBtn { float dx; float dy; Button btn; ImU32 col; const char* label; };
    std::array btns = {
        FaceBtn{  0,  spread, Button::A, ColorA(), labelA },
        FaceBtn{  spread,  0, Button::B, ColorB(), labelB },
        FaceBtn{ -spread,  0, Button::X, ColorX(), labelX },
        FaceBtn{  0, -spread, Button::Y, ColorY(), labelY },
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
    float w = br.x - tl.x;
    float h = br.y - tl.y;
    float rnd = std::min(L.S(4.f), 0.5f * std::min(w, h));
    dl->AddRectFilled(tl, br, IM_COL32(35, 35, 38, 255), rnd);
    dl->AddRect(tl, br, Outline(), rnd, 0, L.S(1.5f));

    if (value > 0.01f) {
        float fillH = h * value;
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
 * @brief Render a framed controller panel visualizing a gamepad's layout and current state.
 *
 * Renders a header containing the player slot, optional display name, and backend name plus a
 * connection-status pill, and draws a scaled representation of the controller (body, D‑pad,
 * face buttons, thumbsticks, bumpers, triggers, and small buttons) inside the panel. If the
 * gamepad is not connected, displays a centered "No controller detected" message instead of
 * the controls. If a body texture is provided, it is used for the controller background;
 * otherwise the body is drawn procedurally.
 *
 * @param dl ImGui draw list used for low-level drawing operations.
 * @param panelPos Top-left corner of the panel in screen coordinates.
 * @param panelSize Width and height of the panel in screen coordinates.
 * @param gs Current gamepad state (buttons, sticks, triggers, connection).
 * @param slotIndex Zero-based player slot index used in the header label.
 * @param backendName Null-terminated string identifying the input backend (shown in header).
 * @param displayName Optional null-terminated display name for the controller; when non-empty
 *                    it is included in the header as "Player N - DisplayName  [backend]".
 * @param bodyTexture Optional ImGui texture ID to render as the controller body; when null,
 *                    the procedural body is drawn instead.
 * @param textureSizeLogical Logical size (width, height) of the provided bodyTexture in the
 *                           same coordinate space used by the renderer.
 * @param layoutType LayoutType selecting the coordinate/label set (e.g., Xbox or Sony) used to
 *                   position and label controls within the rendered controller.
 */

void DrawGamepad(ImDrawList* dl, ImVec2 panelPos, ImVec2 panelSize,
                 const GamepadState& gs, int slotIndex, const char* backendName,
                 const char* displayName,
                 ImTextureID bodyTexture,
                 ImVec2 textureSizeLogical,
                 LayoutType layoutType) {
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

    if (bodyTexture != GamepadRenderer::kNoBodyTexture) {
        ImVec2 tl = L.P(0.f, 0.f);
        ImVec2 br = L.P(textureSizeLogical.x, textureSizeLogical.y);
        dl->AddImage(bodyTexture, tl, br, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f));
    } else {
        DrawBody(dl, L);
    }

    const LayoutCoords& c = GetLayoutCoords(layoutType);
    DrawDPad(dl, L, gs, c.dpadX, c.dpadY);
    DrawFaceButtons(dl, L, gs, c.faceX, c.faceY,
                   c.faceLabelA, c.faceLabelB, c.faceLabelX, c.faceLabelY);

    DrawThumbstick(dl, L, L.P(c.leftStickX, c.leftStickY), gs.leftStickX, gs.leftStickY,
                   gs.IsPressed(Button::LeftThumb));
    DrawThumbstick(dl, L, L.P(c.rightStickX, c.rightStickY), gs.rightStickX, gs.rightStickY,
                   gs.IsPressed(Button::RightThumb));

    DrawBumper(dl, L, L.P(c.lbTlX, c.lbTlY), L.P(c.lbBrX, c.lbBrY), gs.IsPressed(Button::LeftBumper));
    DrawBumper(dl, L, L.P(c.rbTlX, c.rbTlY), L.P(c.rbBrX, c.rbBrY), gs.IsPressed(Button::RightBumper));

    DrawTrigger(dl, L, L.P(c.ltTlX, c.ltTlY), L.P(c.ltBrX, c.ltBrY), gs.leftTrigger);
    DrawTrigger(dl, L, L.P(c.rtTlX, c.rtTlY), L.P(c.rtBrX, c.rtBrY), gs.rightTrigger);

    DrawSmallButton(dl, L, L.P(c.backX, c.backY), L.S(28), L.S(12), c.backLabel,
                    gs.IsPressed(Button::Back));
    DrawSmallButton(dl, L, L.P(c.guideX, c.guideY), L.S(22), L.S(12), c.guideLabel,
                    gs.IsPressed(Button::Guide));
    DrawSmallButton(dl, L, L.P(c.startX, c.startY), L.S(28), L.S(12), c.startLabel,
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
