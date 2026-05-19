#include "stdafx.h"

#include "RendererPlayground.h"
#include "Render.h"

#include <imgui.h>

namespace xray::editor
{
GLErrorRing g_glErrorRing;

void GLErrorRing::push(unsigned err, const char* expr, const char* file, int line, u32 frame) noexcept
{
    const u32 idx = head % GL_ERROR_RING_CAPACITY;
    entries[idx] = GLErrorEntry{ frame, err, expr, file, line };
    ++head;
    ++total;
}

// CHK_GL-side hook. Pointer set by ide::InitBackend; macro path is no-op
// when the playground hasn't initialised yet.
static void GLErrorSinkImpl(unsigned err, const char* expr, const char* file, int line)
{
    g_glErrorRing.push(err, expr, file, line, Device.dwFrame);
}

extern "C" xr_gl_error_sink_fn RendererPlayground_GetGLErrorSink()
{
    return &GLErrorSinkImpl;
}

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
        if (ImGui::BeginTabItem("Event Log"))
        {
            m_lastTabIndex = 3;
            DrawEventLogTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Pipeline Toggles"))
        {
            m_lastTabIndex = 4;
            DrawPipelineTogglesTab();
            ImGui::EndTabItem();
        }
#if defined(XR_PLATFORM_APPLE)
        if (ImGui::BeginTabItem("Hot Reload"))
        {
            m_lastTabIndex = 5;
            DrawHotReloadTab();
            ImGui::EndTabItem();
        }
#endif
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

void RendererPlayground::DrawEventLogTab()
{
    const u32 total = g_glErrorRing.total;
    const u32 count = total < GL_ERROR_RING_CAPACITY ? total : GL_ERROR_RING_CAPACITY;

    ImGui::Text("GL errors captured: %u total, last %u shown", total, count);
    if (total > GL_ERROR_RING_CAPACITY)
        ImGui::Text("(ring buffer wrapped %u times)", total / GL_ERROR_RING_CAPACITY);

    if (ImGui::Button("Clear"))
    {
        g_glErrorRing.head  = 0;
        g_glErrorRing.total = 0;
    }
    ImGui::Separator();

    if (count == 0)
    {
        ImGui::TextDisabled("No GL errors yet. CHK_GL pushes into this ring on every");
        ImGui::TextDisabled("Apple-side macro invocation that observes glGetError != 0.");
        return;
    }

    if (ImGui::BeginTable("##glerrors", 4,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn("frame");
        ImGui::TableSetupColumn("err");
        ImGui::TableSetupColumn("expr");
        ImGui::TableSetupColumn("at");
        ImGui::TableHeadersRow();

        // Walk oldest -> newest. head is the next-write slot; oldest is
        // head - count (mod capacity).
        const u32 start = (g_glErrorRing.head + GL_ERROR_RING_CAPACITY - count) % GL_ERROR_RING_CAPACITY;
        for (u32 i = 0; i < count; ++i)
        {
            const u32 idx = (start + i) % GL_ERROR_RING_CAPACITY;
            const GLErrorEntry& e = g_glErrorRing.entries[idx];

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u", e.frame);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("0x%X", e.err);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(e.expr ? e.expr : "<null>");
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s:%d", e.file ? e.file : "<null>", e.line);
        }
        ImGui::EndTable();
    }
}

void RendererPlayground::DrawPipelineTogglesTab()
{
    IRender::DebugRenderToggles* t = GEnv.Render ? GEnv.Render->GetDebugToggles() : nullptr;
    if (!t)
    {
        ImGui::TextDisabled("Renderer not available or backend doesn't expose toggles.");
        return;
    }

    ImGui::TextUnformatted("Gate pipeline stages off to localise visual bugs.");
    ImGui::TextDisabled("These are not persisted across runs — they reset on engine restart.");
    ImGui::Separator();

    ImGui::Checkbox("Sun shadows (DEFER_SUN block)", &t->shadows);
    ImGui::TextDisabled("  off = no shadow cascades and no direct sun contribution.");

    ImGui::Checkbox("Occlusion queries", &t->occq);
    ImGui::TextDisabled("  off = light visibility tests skipped; all lights flow as 'normal',");
    ImGui::TextDisabled("  possible perf hit on busy scenes.");

    ImGui::Checkbox("Details / grass", &t->details);
    ImGui::TextDisabled("  off = no grass / detail-object renderer pass.");

    ImGui::Checkbox("Wallmarks (decals)", &t->wallmarks);
    ImGui::TextDisabled("  off = no decal pass; bullet holes / blood splats stop appearing");
    ImGui::TextDisabled("  on geometry until re-enabled.");

    ImGui::Checkbox("Lights (point + spot, OCCQ + non-OCCQ)", &t->lights);
    ImGui::TextDisabled("  off = no dynamic light contribution; scene illuminated only by");
    ImGui::TextDisabled("  sun (if shadows toggle is on) and emissive surfaces.");

    ImGui::Spacing();
    if (ImGui::Button("Reset all to on"))
    {
        t->shadows = t->occq = t->details = t->wallmarks = t->lights = true;
    }
}

#if defined(XR_PLATFORM_APPLE)
// Defined in RendererPlayground_HotReload.mm
extern "C" bool     RendererPlayground_HotReload_Start(const char* watchPath);
extern "C" void     RendererPlayground_HotReload_Poll(void);
extern "C" unsigned RendererPlayground_HotReload_Total(void);
extern "C" unsigned RendererPlayground_HotReload_Capacity(void);
extern "C" void     RendererPlayground_HotReload_Entry(unsigned i, const char** outPath, unsigned long long* outTs);
extern "C" void     RendererPlayground_HotReload_Clear(void);
#endif

void RendererPlayground::DrawHotReloadTab()
{
#if defined(XR_PLATFORM_APPLE)
    // Lazy start so we only pay the FSEvents cost when the user opens the
    // tab. Watch path is resolved from $game_shaders$ once.
    if (!m_hotReloadStarted)
    {
        string_path resolved;
        FS.update_path(resolved, "$game_shaders$", "");
        convert_path_separators(resolved);
        if (RendererPlayground_HotReload_Start(resolved))
        {
            m_hotReloadStarted = true;
            Msg("* Playground: hot-reload watcher started on '%s'", resolved);
        }
        else
        {
            ImGui::TextDisabled("Failed to start FSEvents watcher on $game_shaders$.");
            return;
        }
    }

    // Pull pending events into the ring before drawing.
    RendererPlayground_HotReload_Poll();

    const unsigned total = RendererPlayground_HotReload_Total();
    const unsigned cap   = RendererPlayground_HotReload_Capacity();
    const unsigned count = total < cap ? total : cap;

    ImGui::TextWrapped("Watching $game_shaders$ recursively. File changes appear here "
                       "as the FSEvents framework reports them. Live in-engine shader "
                       "swap is not implemented yet — use 'Clear shader disk cache' "
                       "and restart to pick up changes.");
    ImGui::Spacing();

    ImGui::Text("Changes captured: %u total, last %u shown", total, count);
    if (ImGui::Button("Clear list"))
        RendererPlayground_HotReload_Clear();
    ImGui::SameLine();
    if (ImGui::Button("Clear shader disk cache"))
    {
        string_path cacheRoot;
        FS.update_path(cacheRoot, "$app_data_root$", "shaders_cache_oxr");
        convert_path_separators(cacheRoot);
        // Best-effort: ask the engine to drop the cache directory. We
        // don't recursively unlink ourselves to avoid touching engine
        // FS internals; the user can rm -rf the path shown below.
        ImGui::OpenPopup("##shadercache_path");
        m_shaderCachePath = cacheRoot;
    }

    if (ImGui::BeginPopup("##shadercache_path"))
    {
        ImGui::TextUnformatted("Shader cache lives at:");
        ImGui::TextWrapped("%s", m_shaderCachePath.c_str());
        ImGui::TextDisabled("Quit the engine, rm -rf this directory, restart.");
        if (ImGui::Button("Close"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Separator();

    if (count == 0)
    {
        ImGui::TextDisabled("No changes since the watcher started. Edit a .s / .ps / .vs");
        ImGui::TextDisabled("file in gamedata/shaders/ and save to see entries land here.");
        return;
    }

    if (ImGui::BeginTable("##hotreload", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn("when (epoch s)");
        ImGui::TableSetupColumn("path");
        ImGui::TableHeadersRow();
        for (unsigned i = 0; i < count; ++i)
        {
            const char* path = "";
            unsigned long long ts = 0;
            RendererPlayground_HotReload_Entry(i, &path, &ts);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%llu", ts);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(path);
        }
        ImGui::EndTable();
    }
#else
    ImGui::TextDisabled("Hot reload is macOS-only for now (FSEvents-based watcher).");
#endif
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
