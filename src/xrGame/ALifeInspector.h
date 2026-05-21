#pragma once

#include "xrEngine/editor_base.h"

namespace xray::editor
{
// ALife Inspector — diagnostic overlay for the simulation layer.
//
// Hotkey: F7 (kALIFE_INSPECTOR), paired with F6 for the Renderer
// Playground. F11/F12 were the obvious defaults but macOS reserves both
// (F11 = Mission Control "Show Desktop"; F12 = hardware Volume Up media
// key, intercepted before SDL ever sees it). F6/F7 reach SDL cleanly.
// Both keys are gated at runtime by the `dev_tools` cvar — default 0 in
// MasterGold, 1 elsewhere — so shipped builds don't expose this to end-
// users. QA enables in master via console: `dev_tools 1`. The tool
// registers itself via the ide_tool ctor; engine's ToggleNamedTool
// dispatch routes hotkey events by tool_name().
//
// v0 of this tool ships only counts (total objects, online vs offline,
// breakdown by faction-relevant classes) and the current level name.
// Future expansion lands here: per-object detail, smart-terrain task
// state, faction inventory drift, dialog memory of NPCs.
class ALifeInspector final : public ide_tool
{
public:
    ALifeInspector();
    ~ALifeInspector() override = default;

    void   on_tool_frame() override;
    pcstr  tool_name() const override { return "ALife Inspector"; }
};
} // namespace xray::editor
