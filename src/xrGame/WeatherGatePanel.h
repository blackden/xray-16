#pragma once

#include "xrEngine/editor_base.h"

namespace xray::editor
{
// WeatherGatePanel — diagnostic overlay for the rain (and, downstream,
// other weather particle) indoor-suppression gate.
//
// Surfaces three streams of state per frame:
//   - LightTrack hemi_smooth read from the actor's CROS_impl, which is
//     known to be light-polluted (see notes/engine-map.md and memory
//     [[project-hemi-cube-light-pollution]]). The cellar repro should
//     show this >0 even when the camera is under solid cover.
//   - CEffect_Rain::m_hemi_factor — the 5-raycast camera-centric gate
//     that supersedes the polluted hemi_cube. Falls toward 0 under a
//     roof; what the *shader* uses to fade streak intensity.
//   - CEffect_Rain Born() per-frame counters: attempts vs rejected, and
//     the last spawn point's world position + whether its 5m upward
//     raycast was blocked. This is the gate that's supposed to drop
//     individual streaks above an opaque ceiling but is currently
//     suspected of letting some through (visible cellar rain repro
//     2026-05-20).
//
// Hotkey: opens via ToggleNamedTool("Weather Gate"), routed by the
// engine's IDE dispatch. No dedicated F-key for now; surface through
// the existing F6 playground menu or a console binding.
class WeatherGatePanel final : public ide_tool
{
public:
    WeatherGatePanel();
    ~WeatherGatePanel() override = default;

    void   on_tool_frame() override;
    pcstr  tool_name() const override { return "Weather Gate"; }
};
} // namespace xray::editor
