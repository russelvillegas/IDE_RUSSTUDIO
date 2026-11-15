// RusStudioUI.cpp
// Complete Dear ImGui + SDL3 compiler-workbench UI layout.
//
// Expected dependencies:
//   SDL3
//   Dear ImGui
//   imgui_impl_sdl3.cpp
//   imgui_impl_sdlrenderer3.cpp
//
// Example build shape:
//   c++ -std=c++17 RusStudioUI.cpp \
//       imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp \
//       backends/imgui_impl_sdl3.cpp \
//       backends/imgui_impl_sdlrenderer3.cpp \
//       $(pkg-config --cflags --libs sdl3) \
//       -I/path/to/imgui -I/path/to/imgui/backends \
//       -o russtudio
//
// The UI is deliberately organized as a compiler pipeline:
//   Input Buffer -> Token Stream / IR -> Runtime State / Program Output

#include <SDL3/SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace russtudio {

// -----------------------------------------------------------------------------
// Theme
// -----------------------------------------------------------------------------

struct Colors {
    ImVec4 background      = ImVec4(0.965f, 0.965f, 0.970f, 1.0f);
    ImVec4 panel           = ImVec4(1.000f, 1.000f, 1.000f, 1.0f);
    ImVec4 panel_alt       = ImVec4(0.945f, 0.945f, 0.955f, 1.0f);
    ImVec4 border           = ImVec4(0.820f, 0.820f, 0.835f, 1.0f);
    ImVec4 text             = ImVec4(0.080f, 0.080f, 0.090f, 1.0f);
    ImVec4 muted            = ImVec4(0.420f, 0.420f, 0.450f, 1.0f);
    ImVec4 amber            = ImVec4(0.180f, 0.180f, 0.200f, 1.0f);
    ImVec4 cyan             = ImVec4(0.020f, 0.020f, 0.025f, 1.0f);
    ImVec4 green            = ImVec4(0.280f, 0.280f, 0.300f, 1.0f);
    ImVec4 red              = ImVec4(0.480f, 0.480f, 0.500f, 1.0f);
    ImVec4 orange           = ImVec4(0.360f, 0.360f, 0.380f, 1.0f);
};

static Colors g_colors;

static ImU32 ColorU32(const ImVec4& c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

static void SetupCompilerTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(10, 7);
    style.ItemSpacing = ImVec2(12, 10);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.CellPadding = ImVec2(10, 8);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.WindowRounding = 14.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 8.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = g_colors.text;
    c[ImGuiCol_TextDisabled]         = g_colors.muted;
    c[ImGuiCol_WindowBg]              = g_colors.background;
    c[ImGuiCol_ChildBg]               = g_colors.panel;
    c[ImGuiCol_PopupBg]               = g_colors.panel_alt;
    c[ImGuiCol_Border]                = g_colors.border;
    c[ImGuiCol_FrameBg]               = ImVec4(0.950f, 0.950f, 0.960f, 1.0f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.900f, 0.900f, 0.915f, 1.0f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.860f, 0.860f, 0.880f, 1.0f);
    c[ImGuiCol_Button]                = ImVec4(0.930f, 0.930f, 0.940f, 1.0f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.875f, 0.875f, 0.890f, 1.0f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.800f, 0.800f, 0.820f, 1.0f);
    c[ImGuiCol_Header]                = ImVec4(0.930f, 0.930f, 0.940f, 1.0f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.875f, 0.875f, 0.890f, 1.0f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.800f, 0.800f, 0.820f, 1.0f);
    c[ImGuiCol_Separator]             = g_colors.border;
    c[ImGuiCol_SeparatorHovered]      = g_colors.cyan;
    c[ImGuiCol_CheckMark]             = g_colors.green;
    c[ImGuiCol_SliderGrab]            = g_colors.cyan;
    c[ImGuiCol_SliderGrabActive]      = g_colors.green;
    c[ImGuiCol_Tab]                   = ImVec4(0.075f, 0.100f, 0.135f, 1.0f);
    c[ImGuiCol_TabHovered]            = ImVec4(0.120f, 0.175f, 0.225f, 1.0f);
    c[ImGuiCol_TabActive]             = ImVec4(0.100f, 0.145f, 0.190f, 1.0f);
}

// -----------------------------------------------------------------------------
// UI model
// -----------------------------------------------------------------------------

struct MorseToken {
    std::string morse;
    std::string opcode;
    std::string source;
    bool active = false;
    bool executed = false;
};

struct Diagnostic {
    std::string severity;
    std::string message;
    int line = 0;
};

struct AppState {
    std::string input_buffer;
    std::string current_tap = ".";
    std::string decoded_preview = "Awaiting telegraph input...";
    std::string status = "READY";
    std::string output = "Hello, RusStudio!\n";
    std::string filename = "untitled.rus";

    bool live = true;
    bool running = false;
    bool audio_enabled = true;
    bool show_memory = true;
    bool show_diagnostics = true;
    bool spacebar_down = false;
    bool compile_requested = false;
    bool reset_requested = false;

    float key_hold_ms = 137.0f;
    float pulse_phase = 0.0f;
    int pointer = 42;
    int cell_value = 0;
    int cycles = 184;
    int tokens_compiled = 8;
    int current_token = 2;

    std::vector<MorseToken> tokens = {
        {".",   "INC",          "line 01", false, true},
        {"..",  "MOVE_RIGHT",  "line 02", false, true},
        {".-",  "LOOP_BEGIN",  "line 03", true,  false},
        {"...", "OUTPUT",      "line 04", false, false},
        {"-",   "DEC",          "line 05", false, false},
        {"--",  "MOVE_LEFT",   "line 06", false, false},
        {"---", "INPUT",       "line 07", false, false},
        {".-",  "LOOP_END",    "line 08", false, false}
    };

    std::vector<Diagnostic> diagnostics = {
        {"INFO", "Compilation unit is valid", 0},
        {"INFO", "8 opcodes emitted for RUS-VM", 0}
    };
};

static AppState g_app;

// -----------------------------------------------------------------------------
// Small drawing helpers
// -----------------------------------------------------------------------------

static void TextColored(const ImVec4& color, const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    ImGui::TextColored(color, "%s", buffer);
}

static void SectionLabel(const char* title, const char* detail = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_Text, g_colors.muted);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (detail) {
        ImGui::SameLine();
        ImGui::TextColored(g_colors.muted, "// %s", detail);
    }
    ImGui::Separator();
}

static void PanelBegin(const char* id, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, g_colors.panel);
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);
    ImGui::PopStyleColor();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 18));
}

static void PanelEnd() {
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

static void StatusDot(const ImVec4& color) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    draw->AddCircleFilled(ImVec2(p.x + 5, p.y + 9), 4.0f, ColorU32(color));
    ImGui::Dummy(ImVec2(14, 18));
}

static void MonoText(const char* text, const ImVec4& color = ImVec4(0, 0, 0, -1)) {
    if (color.w >= 0.0f) ImGui::TextColored(color, "%s", text);
    else ImGui::TextUnformatted(text);
}

static void DrawDurationMeter() {
    const float width = std::max(60.0f, ImGui::GetContentRegionAvail().x);
    const float height = 10.0f;
    const float ratio = std::clamp(g_app.key_hold_ms / 450.0f, 0.0f, 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ColorU32(ImVec4(0.035f, 0.045f, 0.060f, 1.0f)));
    draw->AddRectFilled(p, ImVec2(p.x + width * ratio, p.y + height),
                        ColorU32(g_app.key_hold_ms < 200.0f ? g_colors.amber : g_colors.cyan));
    draw->AddLine(ImVec2(p.x + width * (200.0f / 450.0f), p.y - 3),
                  ImVec2(p.x + width * (200.0f / 450.0f), p.y + height + 3),
                  ColorU32(g_colors.text), 1.0f);
    draw->AddLine(ImVec2(p.x + width * (450.0f / 450.0f), p.y - 3),
                  ImVec2(p.x + width * (450.0f / 450.0f), p.y + height + 3),
                  ColorU32(g_colors.orange), 1.0f);
    ImGui::Dummy(ImVec2(width, height + 6));
}

// -----------------------------------------------------------------------------
// Header and footer
// -----------------------------------------------------------------------------

static void DrawHeader() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, g_colors.panel_alt);
    ImGui::BeginChild("##header", ImVec2(0, 72), false);
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(24, 13));
    ImGui::TextColored(g_colors.text, "RUSSTUDIO");
    ImGui::SameLine();
    ImGui::TextColored(g_colors.muted, " 0.1.0  /  MORSE COMPILER");

    ImGui::SetCursorPos(ImVec2(24, 41));
    ImGui::TextColored(g_colors.muted, "TARGET:");
    ImGui::SameLine(0, 5);
    ImGui::TextColored(g_colors.cyan, "RUS-VM");
    ImGui::SameLine(0, 18);
    ImGui::TextColored(g_colors.muted, "MODE:");
    ImGui::SameLine(0, 5);
    ImGui::TextColored(g_colors.amber, "TELEGRAPH");

    ImGui::SameLine(ImGui::GetWindowWidth() - 420.0f);
    StatusDot(g_app.live ? g_colors.green : g_colors.red);
    ImGui::SameLine(0, 0);
    ImGui::TextColored(g_app.live ? g_colors.green : g_colors.red, "%s", g_app.live ? "LIVE" : "OFFLINE");
    ImGui::SameLine(0, 18);
    if (ImGui::Button("BUILD", ImVec2(74, 28))) g_app.compile_requested = true;
    ImGui::SameLine();
    if (ImGui::Button(g_app.running ? "STOP" : "RUN", ImVec2(74, 28))) g_app.running = !g_app.running;
    ImGui::SameLine();
    if (ImGui::Button("RESET", ImVec2(74, 28))) g_app.reset_requested = true;

    ImGui::EndChild();
    ImGui::Separator();
}

static void DrawFooter() {
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, g_colors.panel_alt);
    ImGui::BeginChild("##footer", ImVec2(0, 31), false);
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(20, 9));
    TextColored(g_app.running ? g_colors.green : g_colors.cyan, "%s", g_app.running ? "RUNNING" : g_app.status.c_str());
    ImGui::SameLine(0, 24);
    ImGui::TextColored(g_colors.muted, "KEYER:");
    ImGui::SameLine(0, 5);
    ImGui::TextColored(g_colors.amber, "650Hz");
    ImGui::SameLine(0, 24);
    ImGui::TextColored(g_colors.muted, "TAPE:");
    ImGui::SameLine(0, 5);
    ImGui::TextColored(g_colors.cyan, "30,000 CELLS");
    ImGui::SameLine(0, 24);
    ImGui::TextColored(g_colors.muted, "DOT:");
    ImGui::SameLine(0, 5);
    ImGui::TextColored(g_colors.text, "< 200ms");
    ImGui::SameLine(0, 24);
    ImGui::TextColored(g_colors.muted, "COMMAND PAUSE:");
    ImGui::SameLine(0, 5);
    ImGui::TextColored(g_colors.text, "> 450ms");

    ImGui::EndChild();
}

// -----------------------------------------------------------------------------
// Three-pane compiler workbench
// -----------------------------------------------------------------------------

static void DrawInputBuffer(float width, float height) {
    PanelBegin("##input_buffer", ImVec2(width, height));
    SectionLabel("INPUT BUFFER", "RAW TELEGRAPH EVENTS");

    ImGui::TextColored(g_colors.muted, "SPACEBAR KEYER");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 48.0f);
    ImGui::TextColored(g_app.audio_enabled ? g_colors.green : g_colors.muted,
                       "%s", g_app.audio_enabled ? "AUDIO" : "MUTED");

    ImGui::Spacing();
    ImGui::BeginGroup();
    ImGui::TextColored(g_colors.muted, "CURRENT TAP");
    ImGui::TextColored(g_app.key_hold_ms < 200.0f ? g_colors.amber : g_colors.cyan,
                       "  %s", g_app.current_tap.c_str());
    ImGui::EndGroup();
    ImGui::SameLine(0, 36);
    ImGui::BeginGroup();
    ImGui::TextColored(g_colors.muted, "DURATION");
    ImGui::TextColored(g_colors.text, "%03.0f ms", g_app.key_hold_ms);
    ImGui::EndGroup();

    ImGui::Spacing();
    DrawDurationMeter();
    ImGui::TextColored(g_colors.muted, "DOT");
    ImGui::SameLine(0, 70);
    ImGui::TextColored(g_colors.muted, "DASH");
    ImGui::SameLine(0, 70);
    ImGui::TextColored(g_colors.muted, "PAUSE");

    ImGui::Spacing();
    SectionLabel("ACTIVE SEQUENCE");
    ImGui::BeginChild("##morse_sequence", ImVec2(0, 76), ImGuiChildFlags_Borders);
    ImGui::SetCursorPos(ImVec2(12, 16));
    ImGui::TextColored(g_colors.amber, ".  .  –  .");
    ImGui::SetCursorPos(ImVec2(12, 43));
    ImGui::TextColored(g_colors.muted, "MORSE TOKEN:");
    ImGui::SameLine();
    ImGui::TextColored(g_colors.cyan, " .-..");
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextColored(g_colors.muted, "DECODER PREVIEW");
    ImGui::TextColored(g_colors.cyan, "  .-..  →  OUTPUT");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(g_colors.muted, "KEYBOARD SHORTCUTS");
    ImGui::BulletText("Hold Spacebar  /  emit signal");
    ImGui::BulletText("Release Spacebar  /  commit tap");
    ImGui::BulletText("Wait 450ms  /  finish opcode");

    ImGui::Spacing();
    ImGui::Checkbox("Live input", &g_app.live);
    ImGui::SameLine();
    ImGui::Checkbox("650Hz sidetone", &g_app.audio_enabled);
    PanelEnd();
}

static void DrawTokenStream(float width, float height) {
    PanelBegin("##token_stream", ImVec2(width, height));
    SectionLabel("TOKEN STREAM", "INTERMEDIATE REPRESENTATION");

    ImGui::TextColored(g_colors.muted, "LEXER");
    ImGui::SameLine();
    ImGui::TextColored(g_colors.green, "  %d TOKENS EMITTED", g_app.tokens_compiled);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 74.0f);
    ImGui::TextColored(g_colors.green, "VALID");

    ImGui::Spacing();
    ImGui::BeginChild("##tokens", ImVec2(0, height * 0.48f), ImGuiChildFlags_Borders);
    ImGui::Columns(4, "##token_columns", true);
    ImGui::TextColored(g_colors.muted, "MORSE"); ImGui::NextColumn();
    ImGui::TextColored(g_colors.muted, "OPCODE"); ImGui::NextColumn();
    ImGui::TextColored(g_colors.muted, "SOURCE"); ImGui::NextColumn();
    ImGui::TextColored(g_colors.muted, "STATE"); ImGui::NextColumn();
    ImGui::Separator();

    for (size_t i = 0; i < g_app.tokens.size(); ++i) {
        const MorseToken& token = g_app.tokens[i];
        ImVec4 row_color = token.active ? g_colors.cyan : (token.executed ? g_colors.green : g_colors.text);
        ImGui::TextColored(row_color, "%s", token.morse.c_str()); ImGui::NextColumn();
        ImGui::TextColored(row_color, "%s", token.opcode.c_str()); ImGui::NextColumn();
        ImGui::TextColored(g_colors.muted, "%s", token.source.c_str()); ImGui::NextColumn();
        if (token.active) ImGui::TextColored(g_colors.cyan, "▶ ACTIVE");
        else if (token.executed) ImGui::TextColored(g_colors.green, "✓ DONE");
        else ImGui::TextColored(g_colors.muted, "QUEUED");
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::Spacing();
    SectionLabel("COMPILER DIAGNOSTICS");
    ImGui::BeginChild("##diagnostics", ImVec2(0, height * 0.25f), ImGuiChildFlags_Borders);
    for (const auto& diagnostic : g_app.diagnostics) {
        const ImVec4& color = diagnostic.severity == "ERROR" ? g_colors.red :
                              diagnostic.severity == "WARN" ? g_colors.orange : g_colors.green;
        ImGui::TextColored(color, "%s", diagnostic.severity.c_str());
        ImGui::SameLine(0, 14);
        ImGui::TextColored(g_colors.muted, "%s", diagnostic.message.c_str());
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextColored(g_colors.muted, "PIPELINE");
    ImGui::SameLine();
    ImGui::TextColored(g_colors.amber, "RAW");
    ImGui::SameLine(); ImGui::TextColored(g_colors.muted, " → ");
    ImGui::SameLine(); ImGui::TextColored(g_colors.cyan, "LEX");
    ImGui::SameLine(); ImGui::TextColored(g_colors.muted, " → ");
    ImGui::SameLine(); ImGui::TextColored(g_colors.cyan, "DECODE");
    ImGui::SameLine(); ImGui::TextColored(g_colors.muted, " → ");
    ImGui::SameLine(); ImGui::TextColored(g_colors.green, "EMIT");
    ImGui::SameLine(); ImGui::TextColored(g_colors.muted, " → ");
    ImGui::SameLine(); ImGui::TextColored(g_colors.green, "EXECUTE");
    PanelEnd();
}

static void DrawRuntimePane(float width, float height) {
    PanelBegin("##runtime", ImVec2(width, height));
    SectionLabel("RUNTIME", "RUS-VM EXECUTION");

    ImGui::TextColored(g_colors.muted, "PROGRAM OUTPUT");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 48.0f);
    ImGui::TextColored(g_colors.green, "STDOUT");

    ImGui::BeginChild("##console", ImVec2(0, height * 0.31f), ImGuiChildFlags_Borders);
    ImGui::PushStyleColor(ImGuiCol_Text, g_colors.green);
    ImGui::TextUnformatted("> rus-vm --execute untitled.rus");
    ImGui::TextUnformatted("> status: running");
    ImGui::Spacing();
    ImGui::TextUnformatted(g_app.output.c_str());
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::Spacing();
    SectionLabel("VM STATE");
    ImGui::BeginChild("##vm_state", ImVec2(0, height * 0.23f), ImGuiChildFlags_Borders);
    ImGui::Columns(2, "##vm_columns", false);
    ImGui::TextColored(g_colors.muted, "POINTER"); ImGui::NextColumn();
    ImGui::TextColored(g_colors.cyan, "%04d", g_app.pointer); ImGui::NextColumn();
    ImGui::TextColored(g_colors.muted, "CURRENT CELL"); ImGui::NextColumn();
    ImGui::TextColored(g_colors.amber, "%03d", g_app.cell_value); ImGui::NextColumn();
    ImGui::TextColored(g_colors.muted, "CYCLES"); ImGui::NextColumn();
    ImGui::TextColored(g_colors.text, "%06d", g_app.cycles); ImGui::NextColumn();
    ImGui::TextColored(g_colors.muted, "CURRENT IR"); ImGui::NextColumn();
    ImGui::TextColored(g_colors.cyan, "OUTPUT"); ImGui::NextColumn();
    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::Spacing();
    SectionLabel("MEMORY TAPE", "30,000 BYTE CELLS");
    ImGui::BeginChild("##tape", ImVec2(0, height * 0.24f), ImGuiChildFlags_Borders);
    const int start = std::max(0, g_app.pointer - 4);
    for (int i = start; i < start + 9; ++i) {
        bool current = i == g_app.pointer;
        ImGui::PushStyleColor(ImGuiCol_Button, current ? g_colors.cyan : g_colors.panel_alt);
        ImGui::PushStyleColor(ImGuiCol_Text, current ? g_colors.background : g_colors.text);
        ImGui::Button((std::to_string(i) + "\n" + std::to_string(i == g_app.pointer ? g_app.cell_value : 0)).c_str(), ImVec2(48, 42));
        ImGui::PopStyleColor(2);
        if (i < start + 8) ImGui::SameLine();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Checkbox("Show memory tape", &g_app.show_memory);
    ImGui::SameLine();
    ImGui::Checkbox("Show diagnostics", &g_app.show_diagnostics);
    PanelEnd();
}

static void DrawWorkbench() {
    ImVec2 content = ImGui::GetContentRegionAvail();
    const float gap = 16.0f;
    const float left_width = content.x * 0.27f;
    const float center_width = content.x * 0.39f;
    const float right_width = content.x - left_width - center_width - gap * 2.0f;

    DrawInputBuffer(left_width, content.y);
    ImGui::SameLine(0, gap);
    DrawTokenStream(center_width, content.y);
    ImGui::SameLine(0, gap);
    DrawRuntimePane(right_width, content.y);
}

static void ProcessDemoState(float dt) {
    g_app.pulse_phase += dt;
    if (g_app.running) {
        g_app.cycles += 1;
        if (g_app.cycles % 30 == 0) {
            g_app.pointer = (g_app.pointer + 1) % 30000;
            g_app.cell_value = (g_app.cell_value + 1) % 256;
        }
    }

    if (g_app.compile_requested) {
        g_app.status = "COMPILED";
        g_app.diagnostics = {
            {"INFO", "Lexed 8 Morse tokens", 0},
            {"INFO", "Emitted 8 RUS-VM opcodes", 0},
            {"INFO", "Compilation completed successfully", 0}
        };
        g_app.compile_requested = false;
    }

    if (g_app.reset_requested) {
        g_app.running = false;
        g_app.status = "READY";
        g_app.pointer = 42;
        g_app.cell_value = 0;
        g_app.cycles = 184;
        g_app.reset_requested = false;
    }
}

} // namespace russtudio

// -----------------------------------------------------------------------------
// SDL3 application entry point
// -----------------------------------------------------------------------------

int main(int, char**) {
    using namespace russtudio;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "RusStudio - Morse Compiler",
        1440,
        860,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;

    SetupCompilerTheme();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    bool quit = false;
    Uint64 previous_counter = SDL_GetPerformanceCounter();

    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_SPACE && !event.key.repeat) {
                g_app.spacebar_down = true;
                g_app.current_tap = ".";
                g_app.key_hold_ms = 80.0f;
            }

            if (event.type == SDL_EVENT_KEY_UP && event.key.key == SDLK_SPACE) {
                g_app.spacebar_down = false;
                g_app.key_hold_ms = g_app.key_hold_ms < 200.0f ? 137.0f : 265.0f;
            }
        }

        Uint64 current_counter = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(current_counter - previous_counter) /
                   static_cast<float>(SDL_GetPerformanceFrequency());
        previous_counter = current_counter;
        ProcessDemoState(dt);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("RusStudio Compiler Workbench", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus);
        ImGui::PopStyleVar(2);

        DrawHeader();
        ImGui::SetCursorPos(ImVec2(20, 88));
        ImGui::BeginGroup();
        DrawWorkbench();
        ImGui::EndGroup();
        DrawFooter();

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 247, 247, 249, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
