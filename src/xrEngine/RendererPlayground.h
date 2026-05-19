#pragma once

#include "editor_base.h"

namespace xray::editor
{
// Lightweight ring buffer for GL error events captured via the CHK_GL
// macro (Apple-only — non-Apple builds either no-op or modal-fail). Single
// producer (CHK_GL on main thread), single consumer (playground draw on
// the same thread); no synchronization needed but the design is sound for
// later threading if it changes.
struct GLErrorEntry
{
    u32         frame{};
    unsigned    err{};
    const char* expr{};  // string literal from CHK_GL #expr — stable lifetime
    const char* file{};
    int         line{};
};

constexpr u32 GL_ERROR_RING_CAPACITY = 64;

struct GLErrorRing
{
    GLErrorEntry entries[GL_ERROR_RING_CAPACITY]{};
    u32          head{};    // write index, increments forever modulo capacity
    u32          total{};   // total pushes ever (useful for "overflowed N times" UI)

    void push(unsigned err, const char* expr, const char* file, int line, u32 frame) noexcept;
};

extern GLErrorRing g_glErrorRing;


// In-engine renderer playground / scene debugger.
//
// v0 ships a single Frame Stats tab. v1 adds RT picker, GL state inspector,
// event history. v2 adds shader hot-reload and pipeline stage toggles.
//
// Lifecycle: constructed once by ide::InitBackend() after the ImGui context
// exists. Registers itself with Device.editor() through the ide_tool ctor.
//
// Toggle: F11 (kRENDER_PLAYGROUND action). macOS users must first disable
// Mission Control's F11 binding (System Settings -> Keyboard Shortcuts ->
// Mission Control). This is a developer-only tool, so the global rebind
// is acceptable. The MasterGold Tools menu is
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
    void DrawGLStateTab();
    void DrawRTPickerTab();
    void DrawEventLogTab();
    void DrawPipelineTogglesTab();

    int m_lastTabIndex{ 0 };

    // RT picker UI state, persisted across frames so the user keeps their
    // selection while the underlying RT vector is rebuilt every frame.
    xr_string m_selectedRtName;
    float     m_rtPreviewScale{ 0.5f };
    bool      m_rtFlipY{ true }; // GL textures are bottom-up; default to flipping
};
} // namespace xray::editor
