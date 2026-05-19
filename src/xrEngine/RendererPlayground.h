#pragma once

#include "editor_base.h"

namespace xray::editor
{
// In-engine renderer playground / scene debugger.
//
// v0 ships a single Frame Stats tab. v1 adds RT picker, GL state inspector,
// event history. v2 adds shader hot-reload and pipeline stage toggles.
//
// Lifecycle: constructed once by ide::InitBackend() after the ImGui context
// exists. Registers itself with Device.editor() through the ide_tool ctor.
//
// Toggle: F11 (kRENDER_PLAYGROUND action). The MasterGold Tools menu is
// gated #ifndef MASTER_GOLD, so the hotkey is the only entry point in the
// shipping build. Toggling open also ensures ide is at least in light
// visible_state so on_tool_frame fires.
class ENGINE_API RendererPlayground final : public ide_tool
{
public:
    RendererPlayground();
    ~RendererPlayground() override = default;

    void on_tool_frame() override;
    pcstr tool_name() const override { return "Renderer Playground"; }

    void save_settings(ImGuiTextBuffer* buffer) const override;
    void apply_setting(pcstr line) override;
    size_t estimate_settings_size() const override { return 64; }

private:
    void DrawFrameStatsTab();

    int m_lastTabIndex{ 0 };
};
} // namespace xray::editor
