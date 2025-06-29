#pragma once

class IKinematics;

struct SBoneProtections
{
    struct BoneProtection
    {
        float koeff{ 1.0f };
        float armor{ 0.0f };
        bool BonePassBullet{};
    };

    mutable xr_map<s16, BoneProtection> m_bones_koeff;

    float m_fHitFracNpc{ 0.1f };
    float m_fHitFracActor{ 0.1f };

    [[nodiscard]] float getBoneProtection(s16 bone_id) const;
    [[nodiscard]] float getBoneArmor(s16 bone_id) const;
    [[nodiscard]] bool  getBonePassBullet(s16 bone_id) const;

    void reload(const shared_str& outfit_section, IKinematics* kinematics);
    void add(const shared_str& outfit_section, IKinematics* kinematics);
};
