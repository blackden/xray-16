#include "StdAfx.h"

#include "WeatherGatePanel.h"

#include "GamePersistent.h"
#include "Level.h"

#include "xrEngine/Environment.h"
#include "xrEngine/Rain.h"
#include "xrEngine/IRenderable.h"
#include "xrEngine/Render.h"

#include <imgui.h>

namespace xray::editor
{
namespace
{
// Rolling buffer for the camera-centric hemi factor — at 60Hz, 240 samples
// is 4 seconds, enough to see the smoothing settle when crossing a doorway.
constexpr int kSparkLen = 240;

float g_hemi_factor_hist[kSparkLen] = {};
int   g_hemi_factor_head = 0;

float g_hemi_smooth_hist[kSparkLen] = {};
int   g_hemi_smooth_head = 0;
} // namespace

WeatherGatePanel::WeatherGatePanel() = default;

void WeatherGatePanel::on_tool_frame()
{
    bool& opened = get_open_state();
    if (!opened)
        return;

    if (!ImGui::Begin("Weather Gate", &opened, get_default_window_flags()))
    {
        ImGui::End();
        return;
    }

    if (!g_pGamePersistent)
    {
        ImGui::TextDisabled("g_pGamePersistent not initialized.");
        ImGui::End();
        return;
    }

    CEnvironment&   env  = g_pGamePersistent->Environment();
    CEffect_Rain*   rain = env.eff_Rain;

    // --- Camera-centric gate (Rain::m_hemi_factor) -------------------------
    float hemi_factor = rain ? rain->get_hemi_factor() : -1.f;
    g_hemi_factor_hist[g_hemi_factor_head] = hemi_factor;
    g_hemi_factor_head = (g_hemi_factor_head + 1) % kSparkLen;

    ImGui::Text("Rain m_hemi_factor (5-ray camera gate):  %.3f", hemi_factor);
    ImGui::PlotLines("##rainhemi", g_hemi_factor_hist, kSparkLen, g_hemi_factor_head,
        nullptr, 0.f, 1.f, ImVec2(0.f, 40.f));

    // --- LightTrack hemi (known light-polluted) ---------------------------
    float lt_hemi = -1.f;
    if (g_pGameLevel)
    {
        if (IGameObject* E = g_pGameLevel->CurrentViewEntity())
        {
            if (IRenderable* R = E->dcast_Renderable())
            {
                if (IRender_ObjectSpecific* ros = R->renderable_ROS())
                    lt_hemi = ros->get_luminocity_hemi();
            }
        }
    }
    g_hemi_smooth_hist[g_hemi_smooth_head] = lt_hemi;
    g_hemi_smooth_head = (g_hemi_smooth_head + 1) % kSparkLen;

    ImGui::Text("LightTrack get_luminocity_hemi (POLLUTED): %.3f", lt_hemi);
    ImGui::PlotLines("##lthemi", g_hemi_smooth_hist, kSparkLen, g_hemi_smooth_head,
        nullptr, 0.f, 1.f, ImVec2(0.f, 40.f));
    ImGui::TextDisabled("Light-polluted by point sources — diverges from m_hemi_factor under lit cover.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Per-spawn indoor gate (Rain::Born)");

    if (!rain)
    {
        ImGui::TextDisabled("eff_Rain == nullptr (no active rain effect).");
    }
    else
    {
        u32 att = rain->dbg_born_attempts();
        u32 rej = rain->dbg_born_rejected();
        u32 acc = att - rej;
        float pct = att ? (100.f * float(rej) / float(att)) : 0.f;
        ImGui::Text("This frame: %u attempts, %u accepted, %u rejected (%.1f%%)", att, acc, rej, pct);

        Fvector lp = rain->dbg_last_spawn();
        ImGui::Text("Last spawn: (%.1f, %.1f, %.1f)  blocked = %s",
            lp.x, lp.y, lp.z, rain->dbg_last_rejected() ? "true" : "false");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Repro: under a roof, expect");
    ImGui::TextDisabled(" - m_hemi_factor -> 0 (5 rays blocked by ceiling)");
    ImGui::TextDisabled(" - LightTrack hemi stays >0 if any lamp lights you");
    ImGui::TextDisabled(" - rejected/attempts -> high (>90%%) when ceiling is solid");
    ImGui::TextDisabled("Streaks still visible? per-spawn gate is being bypassed.");

    ImGui::End();
}
} // namespace xray::editor
