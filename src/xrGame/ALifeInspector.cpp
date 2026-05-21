#include "StdAfx.h"

#include "ALifeInspector.h"

#include "ai_space.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "Level.h"
#include "xrServerEntities/xrServer_Objects_ALife.h"
#include "xrServerEntities/xrServer_Objects_ALife_Monsters.h"
#include "xrServerEntities/xrServer_Objects_ALife_Items.h"

#include <imgui.h>

namespace xray::editor
{
ALifeInspector::ALifeInspector() = default;

void ALifeInspector::on_tool_frame()
{
    bool& opened = get_open_state();
    if (!opened)
        return;

    if (!ImGui::Begin("ALife Inspector", &opened, get_default_window_flags()))
    {
        ImGui::End();
        return;
    }

    const CALifeSimulator* alife = g_ai_space ? g_ai_space->get_alife() : nullptr;
    if (!alife)
    {
        ImGui::TextDisabled("No ALife session active — load a savegame or start a new game.");
        ImGui::End();
        return;
    }

    const char* levelName = "<no level>";
    if (g_pGameLevel)
    {
        const shared_str& nm = g_pGameLevel->name();
        if (nm.c_str() && nm.c_str()[0])
            levelName = nm.c_str();
    }
    ImGui::Text("Current level: %s", levelName);
    ImGui::Spacing();

    // Walk the registry once, categorising by the runtime class we care
    // about. dynamic_cast against the most specific bases gives us a stable
    // breakdown without poking at clsid tables.
    const auto& objects = alife->objects().objects();

    u32 total = 0, online = 0, offline = 0;
    u32 stalkers = 0, monsters = 0, items = 0, anomalies = 0, other = 0;

    for (const auto& kv : objects)
    {
        CSE_ALifeDynamicObject* obj = kv.second;
        if (!obj)
            continue;
        ++total;
        if (obj->m_bOnline)
            ++online;
        else
            ++offline;

        // Classification — dynamic_cast in declared-specificity order so
        // a stalker isn't double-counted as a monster.
        if (dynamic_cast<CSE_ALifeHumanAbstract*>(obj))
            ++stalkers;
        else if (dynamic_cast<CSE_ALifeMonsterAbstract*>(obj))
            ++monsters;
        else if (dynamic_cast<CSE_ALifeInventoryItem*>(obj))
            ++items;
        else if (dynamic_cast<CSE_ALifeAnomalousZone*>(obj))
            ++anomalies;
        else
            ++other;
    }

    ImGui::Text("Objects: %u total  (online %u / offline %u)", total, online, offline);
    ImGui::Spacing();

    if (ImGui::BeginTable("##alife_breakdown", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("class");
        ImGui::TableSetupColumn("count");
        ImGui::TableHeadersRow();

        auto row = [](const char* name, u32 n)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", n);
        };

        row("Humans (stalkers, NPCs)", stalkers);
        row("Monsters", monsters);
        row("Inventory items", items);
        row("Anomalous zones", anomalies);
        row("Other (props / triggers)", other);

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("v0 — counts only. Per-object detail, smart-terrain task state,");
    ImGui::TextDisabled("faction inventory drift, NPC dialog memory land in follow-ups.");

    ImGui::End();
}
} // namespace xray::editor
