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
        if (ImGui::BeginTabItem("GL State"))
        {
            m_lastTabIndex = 1;
            DrawGLStateTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("RT Picker"))
        {
            m_lastTabIndex = 2;
            DrawRTPickerTab();
            ImGui::EndTabItem();
        }
        // v2 tabs (Event Log, Hot Reload, Pipeline Toggles) добавляются
        // здесь по мере появления.
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

void RendererPlayground::DrawGLStateTab()
{
    const auto state = GEnv.Render ? GEnv.Render->GetPlaygroundGLState() : IRender::PlaygroundGLState{};

    ImGui::TextUnformatted("Current GL bindings");
    ImGui::Separator();
    ImGui::Text("VAO          %u", state.vao);
    ImGui::Text("Program      %u", state.program);
    ImGui::Text("Draw FBO     %u", state.drawFbo);
    ImGui::Text("Read FBO     %u", state.readFbo);

    ImGui::Spacing();
    ImGui::TextUnformatted("Backend draw counters (previous frame)");
    ImGui::Separator();
    ImGui::Text("Draw calls   %u", state.drawCalls);
    ImGui::Text("Vertices     %u", state.verts);
    ImGui::Text("Polygons     %u", state.polys);

    ImGui::Spacing();
    ImGui::TextDisabled("Counters reset every frame in CBackend::OnFrameBegin.");
    ImGui::TextDisabled("VAO/Program/FBO are sampled via glGetIntegerv at panel-draw time.");
}

void RendererPlayground::DrawRTPickerTab()
{
    static xr_vector<IRender::PlaygroundRenderTarget> entries;
    entries.clear();
    if (GEnv.Render)
        GEnv.Render->EnumerateRenderTargets(entries);

    if (entries.empty())
    {
        ImGui::TextDisabled("No render targets registered yet.");
        return;
    }

    ImGui::Text("%zu render targets", entries.size());
    ImGui::Separator();

    ImGui::SliderFloat("Preview scale", &m_rtPreviewScale, 0.05f, 1.0f, "%.2f");
    ImGui::SameLine();
    ImGui::Checkbox("Flip Y", &m_rtFlipY);

    ImGui::BeginChild("##rt_list", ImVec2(260, 0), true);
    for (const auto& e : entries)
    {
        bool selected = (m_selectedRtName == e.name);
        if (ImGui::Selectable(e.name, selected))
            m_selectedRtName = e.name;
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##rt_preview", ImVec2(0, 0), true);

    const IRender::PlaygroundRenderTarget* sel = nullptr;
    for (const auto& e : entries)
        if (m_selectedRtName == e.name)
        {
            sel = &e;
            break;
        }

    if (!sel)
    {
        ImGui::TextDisabled("Pick a render target on the left.");
    }
    else
    {
        ImGui::Text("Name:    %s", sel->name);
        ImGui::Text("Size:    %ux%u", sel->width, sel->height);
        ImGui::Text("Color id: %u%s", sel->colorId, sel->hasColor ? "" : " (none)");
        ImGui::Text("Depth id: %u%s", sel->depthId, sel->hasDepth ? "" : " (none)");

        const ImVec2 size(sel->width * m_rtPreviewScale, sel->height * m_rtPreviewScale);
        const ImVec2 uv0 = m_rtFlipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
        const ImVec2 uv1 = m_rtFlipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);

        if (sel->hasColor)
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("Color");
            ImGui::Image(static_cast<ImTextureID>(sel->colorId),
                size, uv0, uv1);
        }
        if (sel->hasDepth)
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("Depth (red channel = depth value, may 0x502 on Apple GL)");
            ImGui::Image(static_cast<ImTextureID>(sel->depthId),
                size, uv0, uv1);
        }
    }

    ImGui::EndChild();
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
