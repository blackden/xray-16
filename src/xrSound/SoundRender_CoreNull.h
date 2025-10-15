#pragma once

#include "SoundRender_Core.h"

class CSoundRender_CoreNull final : public CSoundRender_Core
{
public:
    explicit CSoundRender_CoreNull(CSoundManager& parent);

    void _initialize_devices_list() override;
    void _initialize() override;
    void _clear() override;

    void set_master_volume(float /*f*/) override;
    void update_listener(const Fvector& P, const Fvector& D, const Fvector& N, const Fvector& R, float dt) override;
};
