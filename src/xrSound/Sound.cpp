#include "stdafx.h"

#include "SoundRender_CoreA.h"
#include "SoundRender_CoreNull.h"

XRSOUND_API u32 snd_device_id = u32(-1);

ISoundScene* DefaultSoundScene{};

void CSoundManager::CreateDevicesList()
{
    ZoneScoped;

    static const bool noSound = strstr(Core.Params, "-nosound");

    if (noSound)
        SoundRender = xr_new<CSoundRender_CoreNull>(*this);
    else
        SoundRender = xr_new<CSoundRender_CoreA>(*this);

    SoundRender->_initialize_devices_list();

    if (!SoundRender->bPresent)
        soundDevices.emplace_back(nullptr, -1);

    GEnv.Sound = SoundRender;
}

void CSoundManager::Create()
{
    ZoneScoped;

    if (SoundRender->bPresent)
    {
        env_load();
        SoundRender->_initialize();
    }
}

void CSoundManager::Destroy()
{
    ZoneScoped;

    GEnv.Sound = nullptr;

    SoundRender->_clear();
    xr_delete(SoundRender);

    env_unload();

    for (auto& token : soundDevices)
    {
        pstr tokenName = const_cast<pstr>(token.name);
        xr_free(tokenName);
    }
    soundDevices.clear();
}

bool CSoundManager::IsSoundEnabled() const
{
    return SoundRender && SoundRender->bPresent;
}

void CSoundManager::env_load()
{
    string_path fn;
    if (FS.exist(fn, "$game_data$", SNDENV_FILENAME))
    {
        soundEnvironment = xr_new<SoundEnvironment_LIB>();
        soundEnvironment->Load(fn);
    }
}

void CSoundManager::env_unload()
{
    if (soundEnvironment)
        soundEnvironment->Unload();
    xr_delete(soundEnvironment);
}

SoundEnvironment_LIB* CSoundManager::get_env_library() const
{
    return soundEnvironment;
}

void CSoundManager::refresh_env_library()
{
    env_unload();
    env_load();
    SoundRender->env_apply();
}
