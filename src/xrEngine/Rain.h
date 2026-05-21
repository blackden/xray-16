// Rain.h: interface for the CRain class.
//
//////////////////////////////////////////////////////////////////////

#ifndef RainH
#define RainH
#pragma once

#include "xrCDB/xr_collide_defs.h"

#include "Include/xrRender/FactoryPtr.h"
#include "Include/xrRender/RainRender.h"

// refs
class ENGINE_API IRender_DetailModel;

namespace xray::render
{
namespace render_r4
{
class dxRainRender;
}
namespace render_gl
{
class dxRainRender;
}
} // namespace xray::render

class ENGINE_API CEffect_Rain
{
    friend class xray::render::render_r4::dxRainRender;
    friend class xray::render::render_gl::dxRainRender;

private:
    struct Item
    {
        Fvector P;
        Fvector Phit;
        Fvector D;
        float fSpeed;
        u32 dwTime_Life;
        u32 dwTime_Hit;
        u32 uv_set;
        void invalidate() { dwTime_Life = 0; }
    };
    struct Particle
    {
        Particle *next, *prev;
        Fmatrix mXForm;
        Fsphere bounds;
        float time;
    };
    enum States
    {
        stIdle = 0,
        stWorking
    };

private:
    // Visualization (rain) and (drops)
    FactoryPtr<IRainRender> m_pRender;

    // Data and logic
    xr_vector<Item> items;
    States state;

    // Particles
    xr_vector<Particle> particle_pool;
    Particle* particle_active;
    Particle* particle_idle;

    // Sounds
    ref_sound snd_Ambient;

    // Sky-visibility smoothing for indoor rain suppression.
    // Falls toward 0 when the camera enters a covered volume; render
    // sites multiply rain_density by smoothstep(this) so streaks and
    // wet-surface contribution fade out under roofs.
    float m_hemi_factor{0.f};

    // Diagnostic counters (reset per OnFrame). Read by WeatherGatePanel
    // to confirm whether the per-spawn indoor gate in Born() actually
    // rejects rain streaks above a cellar ceiling — the suspect bug
    // when the camera-centric m_hemi_factor reports 0 but rain still
    // visibly falls through the roof.
    mutable u32 m_dbg_born_attempts{0};
    mutable u32 m_dbg_born_rejected{0};
    Fvector     m_dbg_last_spawn{0.f, 0.f, 0.f};
    bool        m_dbg_last_rejected{false};

    // Utilities
    void p_create();
    void p_destroy();

    void p_remove(Particle* P, Particle*& LST);
    void p_insert(Particle* P, Particle*& LST);
    int p_size(Particle* LST);
    Particle* p_allocate();
    void p_free(Particle* P);

    // Some methods. Born returns false if the spawn point is under a
    // covered region (per-particle indoor gate, see Rain.cpp).
    bool Born(Item& dest, float radius);
    void Hit(Fvector& pos);
    bool RayPick(const Fvector& s, const Fvector& d, float& range, collide::rq_target tgt);
    void RenewItem(Item& dest, float height, bool bHit);

public:
    CEffect_Rain();
    ~CEffect_Rain();

    void Render();
    void OnFrame();

    float get_hemi_factor() const { return m_hemi_factor; }

    // Diagnostic accessors for WeatherGatePanel.
    u32     dbg_born_attempts() const { return m_dbg_born_attempts; }
    u32     dbg_born_rejected() const { return m_dbg_born_rejected; }
    Fvector dbg_last_spawn()    const { return m_dbg_last_spawn; }
    bool    dbg_last_rejected() const { return m_dbg_last_rejected; }
};

#endif // RainH
