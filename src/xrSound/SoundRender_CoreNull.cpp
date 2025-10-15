#include "stdafx.h"

#include "SoundRender_CoreNull.h"

CSoundRender_CoreNull::CSoundRender_CoreNull(CSoundManager& parent)
    : CSoundRender_Core(parent)
{
    bPresent = false;
    bReady = false;
}

void CSoundRender_CoreNull::_initialize_devices_list()
{
    // No hardware enumeration; keep bPresent = false so higher-level code
    // recognises that audio output is unavailable.
    supports_float_pcm = false;
}

void CSoundRender_CoreNull::_initialize()
{
    // Keep timers stopped so that update() exits early via bReady == false.
    bPresent = false;
    bReady = false;
}

void CSoundRender_CoreNull::_clear()
{
    // Ensure base containers are reset for consistency with real backends.
    CSoundRender_Core::_clear();
}

void CSoundRender_CoreNull::set_master_volume(float /*f*/)
{
    // Intentionally no-op.
}

void CSoundRender_CoreNull::update_listener(const Fvector& P, const Fvector& D, const Fvector& N, const Fvector& R,
    float dt)
{
    // Maintain listener state for subsystems that rely on it (e.g. scripted queries).
    CSoundRender_Core::update_listener(P, D, N, R, dt);
}
