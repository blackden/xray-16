#pragma once

#include "xrEngine/editor_base.h"

namespace xray::editor
{
// ALife Inspector — diagnostic overlay for the simulation layer.
//
// Hotkey: F12 (kALIFE_INSPECTOR). Gated at runtime by the `dev_tools`
// cvar — default 0 in MasterGold, 1 elsewhere, so shipped builds don't
// expose this to end-users. QA enables in master via console: `dev_tools 1`.
// On macOS F12 is the hardware Volume Up key by default; either enable
// "Use F1, F2... as standard function keys" in System Settings or press
// Fn+F12. Same toggle UX as F11 playground; the tool registers itself
// via the ide_tool ctor and the engine's ToggleNamedTool dispatch routes
// hotkey events to it by tool_name().
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
