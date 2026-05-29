#include "stdafx.h"

#include "SoundRender_EffectsA_EFX.h"

#ifdef XR_HAS_EFX
#   include "SoundRender_CoreA.h"
#   include "SoundRender_Environment.h"
#   include "Sound.h" // psSoundEFXPreset
#   if __has_include(<efx-presets.h>)
#       include <efx-presets.h>
#   endif

#define LOAD_PROC(x, type) \
    do \
    { \
        ((x) = (type)alGetProcAddress(#x)); \
        if (!(x)) \
            return; \
    } while (false)

CSoundRender_EffectsA_EFX::CSoundRender_EffectsA_EFX()
{
    LOAD_PROC(alGenEffects, LPALGENEFFECTS);
    LOAD_PROC(alDeleteEffects, LPALDELETEEFFECTS);
    LOAD_PROC(alIsEffect, LPALISEFFECT);
    LOAD_PROC(alEffecti, LPALEFFECTI);
    LOAD_PROC(alEffectf, LPALEFFECTF);
    LOAD_PROC(alGenAuxiliaryEffectSlots, LPALGENAUXILIARYEFFECTSLOTS);
    LOAD_PROC(alDeleteAuxiliaryEffectSlots, LPALDELETEAUXILIARYEFFECTSLOTS);
    LOAD_PROC(alAuxiliaryEffectSloti, LPALAUXILIARYEFFECTSLOTI);
    LOAD_PROC(alIsAuxiliaryEffectSlot, LPALISAUXILIARYEFFECTSLOT);

    alGenEffects(1, &effect);

    // AL_EFFECT_EAXREVERB is the richer reverb model (more parameters than
    // AL_EFFECT_REVERB) and is what makes vanilla CoP environments sound
    // recognizably reverberant. Parameters that the engine env-data doesn't
    // carry (DECAY_LFRATIO, ECHO_*, MODULATION_*, *REFERENCE, DECAY_HFLIMIT)
    // are seeded here with their AL_EAXREVERB_DEFAULT_* values and never
    // touched again; set_listener() drives only the params backed by env data.
    alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
    alEffectf(effect, AL_EAXREVERB_DENSITY, AL_EAXREVERB_DEFAULT_DENSITY);
    alEffectf(effect, AL_EAXREVERB_DIFFUSION, AL_EAXREVERB_DEFAULT_DIFFUSION);
    alEffectf(effect, AL_EAXREVERB_GAIN, AL_EAXREVERB_DEFAULT_GAIN);
    alEffectf(effect, AL_EAXREVERB_GAINHF, AL_EAXREVERB_DEFAULT_GAINHF);
    alEffectf(effect, AL_EAXREVERB_GAINLF, AL_EAXREVERB_DEFAULT_GAINLF);
    alEffectf(effect, AL_EAXREVERB_DECAY_TIME, AL_EAXREVERB_DEFAULT_DECAY_TIME);
    alEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, AL_EAXREVERB_DEFAULT_DECAY_HFRATIO);
    alEffectf(effect, AL_EAXREVERB_DECAY_LFRATIO, AL_EAXREVERB_DEFAULT_DECAY_LFRATIO);
    alEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, AL_EAXREVERB_DEFAULT_REFLECTIONS_GAIN);
    alEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, AL_EAXREVERB_DEFAULT_REFLECTIONS_DELAY);
    alEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, AL_EAXREVERB_DEFAULT_LATE_REVERB_GAIN);
    alEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, AL_EAXREVERB_DEFAULT_LATE_REVERB_DELAY);
    alEffectf(effect, AL_EAXREVERB_ECHO_TIME, AL_EAXREVERB_DEFAULT_ECHO_TIME);
    alEffectf(effect, AL_EAXREVERB_ECHO_DEPTH, AL_EAXREVERB_DEFAULT_ECHO_DEPTH);
    alEffectf(effect, AL_EAXREVERB_MODULATION_TIME, AL_EAXREVERB_DEFAULT_MODULATION_TIME);
    alEffectf(effect, AL_EAXREVERB_MODULATION_DEPTH, AL_EAXREVERB_DEFAULT_MODULATION_DEPTH);
    alEffectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, AL_EAXREVERB_DEFAULT_AIR_ABSORPTION_GAINHF);
    alEffectf(effect, AL_EAXREVERB_HFREFERENCE, AL_EAXREVERB_DEFAULT_HFREFERENCE);
    alEffectf(effect, AL_EAXREVERB_LFREFERENCE, AL_EAXREVERB_DEFAULT_LFREFERENCE);
    alEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, AL_EAXREVERB_DEFAULT_ROOM_ROLLOFF_FACTOR);
    alEffecti(effect, AL_EAXREVERB_DECAY_HFLIMIT, AL_EAXREVERB_DEFAULT_DECAY_HFLIMIT);

    if (const ALenum err = alGetError(); err == AL_NO_ERROR)
        m_is_supported = true;
    else
    {
        Log("SOUND: OpenAL: Failed to init EFX:", alGetString(err));
        if (alIsEffect(effect))
            alDeleteEffects(1, &effect);
    }

    alGenAuxiliaryEffectSlots(1, &slot);
    if (const ALenum err = alGetError(); err != AL_NO_ERROR)
        Log("! SOUND: OpenAL: failed to generate auxiliary slot:", alGetString(err));

    // AUXILIARY_SEND_AUTO=AL_FALSE: engine drives reverb gain explicitly per
    // environment zone via set_listener(); openal-soft's auto-attenuation
    // would double-dip on top of the engine-computed level.
    alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_AUXILIARY_SEND_AUTO, AL_FALSE);

    Log("* SOUND: EFX extension:", m_is_supported ? "present" : "absent");
}

#undef LOAD_PROC

CSoundRender_EffectsA_EFX::~CSoundRender_EffectsA_EFX()
{
    if (m_is_supported)
    {
        alDeleteEffects(1, &effect);
        if (alIsAuxiliaryEffectSlot(slot))
            alDeleteAuxiliaryEffectSlots(1, &slot);
    }
}

bool CSoundRender_EffectsA_EFX::initialized()
{
    return m_is_supported;
}

void CSoundRender_EffectsA_EFX::set_listener(const CSoundRender_Environment& env)
{
    // Debug preset selector. 0 (default) = drive AL_EAXREVERB_* from level
    // env data (vanilla CoP behaviour). 1+ = ignore env data, apply a fixed
    // preset from <efx-presets.h>. Switch via `snd_efx_preset N` in console.
#   if __has_include(<efx-presets.h>)
    static const EFXEAXREVERBPROPERTIES presets[] = {
        EFX_REVERB_PRESET_BATHROOM,       // 1 — tile slap
        EFX_REVERB_PRESET_AUDITORIUM,     // 2 — large hall
        EFX_REVERB_PRESET_HANGAR,         // 3 — metal hangar
        EFX_REVERB_PRESET_STONECORRIDOR,  // 4 — stone corridor
        EFX_REVERB_PRESET_CAVE,           // 5 — cavern
    };
    constexpr int presets_count = (int)(sizeof(presets) / sizeof(presets[0]));

    if (psSoundEFXPreset >= 1 && psSoundEFXPreset <= presets_count)
    {
        const EFXEAXREVERBPROPERTIES& p = presets[psSoundEFXPreset - 1];
        A_CHK(alEffectf(effect, AL_EAXREVERB_DENSITY, p.flDensity));
        A_CHK(alEffectf(effect, AL_EAXREVERB_DIFFUSION, p.flDiffusion));
        A_CHK(alEffectf(effect, AL_EAXREVERB_GAIN, p.flGain));
        A_CHK(alEffectf(effect, AL_EAXREVERB_GAINHF, p.flGainHF));
        A_CHK(alEffectf(effect, AL_EAXREVERB_GAINLF, p.flGainLF));
        A_CHK(alEffectf(effect, AL_EAXREVERB_DECAY_TIME, p.flDecayTime));
        A_CHK(alEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, p.flDecayHFRatio));
        A_CHK(alEffectf(effect, AL_EAXREVERB_DECAY_LFRATIO, p.flDecayLFRatio));
        A_CHK(alEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, p.flReflectionsGain));
        A_CHK(alEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, p.flReflectionsDelay));
        A_CHK(alEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, p.flLateReverbGain));
        A_CHK(alEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, p.flLateReverbDelay));
        A_CHK(alEffectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, p.flAirAbsorptionGainHF));
        A_CHK(alEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, p.flRoomRolloffFactor));
        return;
    }
#   endif

    // X-Ray env data stores levels in millibels (EAX 2.0 convention);
    // AL_EAXREVERB_* expects linear gain. mB → gain via 10^(mB/2000).
    auto mB_to_gain = [](float mb) -> float { return powf(10.0f, mb / 2000.0f); };

    // http://openal.org/pipermail/openal/2014-March/000083.html
    float density = powf(env.EnvironmentSize, 3.0f) / 16.0f;
    if (density > 1.0f)
        density = 1.0f;
    A_CHK(alEffectf(effect, AL_EAXREVERB_DENSITY, density));
    A_CHK(alEffectf(effect, AL_EAXREVERB_DIFFUSION, env.EnvironmentDiffusion));
    A_CHK(alEffectf(effect, AL_EAXREVERB_GAIN, mB_to_gain(env.Room)));
    A_CHK(alEffectf(effect, AL_EAXREVERB_GAINHF, mB_to_gain(env.RoomHF)));
    A_CHK(alEffectf(effect, AL_EAXREVERB_DECAY_TIME, env.DecayTime));
    A_CHK(alEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, env.DecayHFRatio));
    A_CHK(alEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, mB_to_gain(env.Reflections)));
    A_CHK(alEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, env.ReflectionsDelay));
    A_CHK(alEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, env.ReverbDelay));
    A_CHK(alEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, mB_to_gain(env.Reverb)));
    A_CHK(alEffectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, mB_to_gain(env.AirAbsorptionHF)));
    A_CHK(alEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, env.RoomRolloffFactor));
}

void CSoundRender_EffectsA_EFX::detach()
{
    // Disconnect the effect from the slot. Without this, toggling snd_efx
    // off leaves the last-committed effect playing because set_listener +
    // commit just stop refreshing — the slot keeps applying the stale
    // effect. Calling this makes the wet path go silent immediately.
    if (m_is_supported && alIsAuxiliaryEffectSlot(slot))
        A_CHK(alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL));
}

void CSoundRender_EffectsA_EFX::get_listener(CSoundRender_Environment& /*env*/)
{
    VERIFY(!"Not implemented.");
}

void CSoundRender_EffectsA_EFX::commit()
{
    // Tell the effect slot to use the loaded effect object. This effectively
    // copies the effect properties — modifying or deleting `effect` afterward
    // does not affect the slot until the next commit.
    A_CHK(alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, effect));
}
#endif // XR_HAS_EFX
