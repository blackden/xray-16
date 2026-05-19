#include "stdafx.h"

#include "RendererPlayground.h"
#include "Render.h"

#include <imgui.h>

namespace xray::editor
{
RendererPlayground::RendererPlayground() = default;

void RendererPlayground::on_tool_frame()
{
    bool& opened = get_open_state();
    if (!opened)
        return;

    if (!ImGui::Begin("Renderer Playground", &opened, get_default_window_flags()))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##playground_tabs", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("Frame Stats"))
        {
            m_lastTabIndex = 0;
            DrawFrameStatsTab();
            ImGui::EndTabItem();
        }
        // v1 tabs (GL State, RT Picker, Event Log) и v2 (Hot Reload, Pipeline
        // Toggles) добавляются здесь по мере появления.
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void RendererPlayground::DrawFrameStatsTab()
{
    const auto& stats = Device.GetStats();

    ImGui::TextUnformatted("Per-frame counters");
    ImGui::Separator();

    ImGui::Text("FPS       %6.1f", stats.fFPS);
    ImGui::Text("RenderFPS %6.1f", stats.fRFPS);
    ImGui::Text("TPS       %6.1f", stats.fTPS);

    ImGui::Spacing();
    ImGui::TextUnformatted("Render cache stats");
    ImGui::Separator();

    // Не зовём DumpStatistics — он ребутит BasicStats.FrameStart/End таймеры.
    // Полигоны достаём через стабильный getter; остальные счётчики появятся
    // в v1 через новый IRender::GetCacheStatRender query.
    const u32 polys = GEnv.Render ? GEnv.Render->GetCacheStatPolys() : 0;
    ImGui::Text("Polys     %u", polys);

    ImGui::Spacing();
    ImGui::TextDisabled("v1 will add: call/vert/poly breakdown, GL state, RT picker, event log.");
}

void RendererPlayground::save_settings(ImGuiTextBuffer* buffer) const
{
    if (!buffer)
        return;
    buffer->appendf("Open=%d\n", is_open() ? 1 : 0);
    buffer->appendf("LastTab=%d\n", m_lastTabIndex);
}

void RendererPlayground::apply_setting(pcstr line)
{
    if (!line)
        return;
    int v = 0;
    if (sscanf(line, "Open=%d", &v) == 1)
    {
        get_open_state() = (v != 0);
    }
    else if (sscanf(line, "LastTab=%d", &v) == 1)
    {
        m_lastTabIndex = v;
    }
}
} // namespace xray::editor
