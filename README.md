# RusStudio

RusStudio is a compiler-workbench interface for a Morse-code esoteric language. The Spacebar or the on-screen telegraph control becomes a telegraph key, Morse sequences are decoded into RUS-VM opcodes, and the runtime state is presented in an Apple-inspired desktop workbench.

## Included files

- `RusStudioUI.cpp` — SDL3 + Dear ImGui desktop UI source.
- `russtudio-preview-apple.html` — interactive browser preview with workspace navigation, Morse tapping, Web Audio sidetone, haptic simulation, and light/dark appearance modes.
- `russtudio-preview-workspaces-readable.png` — reference screenshot of the desktop layout.

## Preview

Open `russtudio-preview-apple.html` directly in a browser, or serve the folder with a local HTTP server:

```bash
python3 -m http.server 4173
```

Then visit `http://localhost:4173/russtudio-preview-apple.html`.

## Desktop UI build dependencies

The C++ UI expects:

- SDL3
- Dear ImGui
- `imgui_impl_sdl3.cpp`
- `imgui_impl_sdlrenderer3.cpp`

Example build command:

```bash
c++ -std=c++17 RusStudioUI.cpp \
  imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp \
  backends/imgui_impl_sdl3.cpp \
  backends/imgui_impl_sdlrenderer3.cpp \
  $(pkg-config --cflags --libs sdl3) \
  -I/path/to/imgui \
  -I/path/to/imgui/backends \
  -o russtudio
```

## Current scope

The included C++ file contains the complete compiler-workbench UI shell, SDL3 lifecycle, Dear ImGui theme, input event handling, token stream, runtime state, diagnostics, VM tape visualization, and mock execution state. Connect `TelegraphKeyer` and `RusStudioVM` to replace the demo state with the production compiler engine.

The browser preview is independently interactive and is intended for design review and UI prototyping.
