#pragma once

class CSoundRender_Environment;

class XR_NOVTABLE CSoundRender_Effects
{
public:
    virtual ~CSoundRender_Effects() = default;

    virtual bool initialized() = 0;

    virtual void set_listener(const CSoundRender_Environment& env) = 0;
    virtual void get_listener(CSoundRender_Environment& env) = 0;

    virtual void commit() = 0;

    // Disconnect the effect from its slot so the wet path goes silent.
    // Default no-op suits backends that don't keep state between commits
    // (e.g. EAX listener-style API). EFX overrides with the explicit
    // alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL) call.
    virtual void detach() {}
};
