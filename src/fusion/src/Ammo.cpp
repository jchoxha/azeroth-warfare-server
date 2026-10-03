/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Ammo.h"

namespace Fusion
{
    void GunAmmo::Reset(WeaponProfile const& w)
    {
        m_magSize = w.magSize;
        m_maxReserve = w.maxReserve;
        m_mag = w.magSize;
        m_reserve = w.maxReserve;
        m_trickle = 0.f;
        m_loaded = AmmoType::Basic;
        m_special.fill(0);
    }

    AmmoType GunAmmo::NextRound() const
    {
        if (m_loaded != AmmoType::Basic && m_special[size_t(m_loaded)] > 0)
            return m_loaded;
        return AmmoType::Basic;
    }

    bool GunAmmo::Fire()
    {
        if (m_mag == 0)
            return false;
        --m_mag;
        AmmoType round = NextRound();
        if (round != AmmoType::Basic)
            --m_special[size_t(round)];
        return true;
    }

    uint32_t GunAmmo::Reload()
    {
        uint32_t want = m_magSize - std::min<uint32_t>(m_mag, m_magSize);
        uint32_t moved = std::min(want, m_reserve);
        m_mag += moved;
        m_reserve -= moved;
        return moved;
    }

    void GunAmmo::FieldRearm(float secondsOutOfCombat, float dt)
    {
        if (secondsOutOfCombat < kFieldRearmDelay)
        {
            m_trickle = 0.f;
            return;
        }
        uint32_t kit = uint32_t(float(m_maxReserve) * kFieldKitShare);
        if (m_reserve >= kit)
            return;
        m_trickle += float(m_maxReserve) * kFieldRearmPerSecond * dt;
        uint32_t add = uint32_t(m_trickle);
        m_trickle -= float(add);
        m_reserve = std::min(kit, m_reserve + add);
    }

    void GunAmmo::FullResupply()
    {
        m_mag = m_magSize;
        m_reserve = m_maxReserve;
    }

    void GunAmmo::QuartermasterRearm(uint32_t level)
    {
        for (size_t t = 1; t < size_t(AmmoType::Count); ++t)
        {
            if (level >= AmmoUnlockLevel(AmmoType(t)))
                m_special[t] = SpecialCap();
        }
    }

    void GunAmmo::AddSpecial(AmmoType t, uint32_t rounds)
    {
        if (t == AmmoType::Basic || t == AmmoType::Count)
            return;
        m_special[size_t(t)] = std::min(SpecialCap(), m_special[size_t(t)] + rounds);
    }

    uint32_t QuartermasterFee(uint32_t level)
    {
        // 1.12 quest rewards run from about 1 silver at level 10 to a few gold at 60.
        float l = float(std::max<uint32_t>(level, 1));
        return uint32_t(20.f * l + 1.f * l * l);   // 3 silver at 10, 48 silver at 60
    }
}
