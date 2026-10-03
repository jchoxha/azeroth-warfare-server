/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_AMMO_H
#define FUSION_AMMO_H

#include "fusion/Common.h"
#include "fusion/Damage.h"
#include "fusion/Weapon.h"

#include <array>

namespace Fusion
{
    // One gun's ammunition (design doc, section 21): MW2's magazine and reserve of basic rounds,
    // which are free, plus a carried reserve of the special type loaded into it.
    class GunAmmo
    {
    public:
        // How many magazines of a special type the character can carry for each gun.
        static constexpr uint32_t kSpecialMagazinesCarried = 4;
        // The share of the reserve the field rearm restores, and how fast.
        static constexpr float kFieldKitShare = 0.5f;
        static constexpr float kFieldRearmDelay = 10.f;     // seconds out of combat
        static constexpr float kFieldRearmPerSecond = 0.1f; // share of max reserve per second

        void Reset(WeaponProfile const& w);

        // Load a special type (from the Armory); basic rounds when it runs out.
        void LoadSpecial(AmmoType t) { m_loaded = t; }
        AmmoType Loaded() const { return m_loaded; }
        // What the next round is: the loaded special while any is carried, else basic.
        AmmoType NextRound() const;

        // Fire one round; false when the magazine is empty.
        bool Fire();
        // Reload from the reserve; returns rounds moved.
        uint32_t Reload();

        // Out-of-combat trickle back up to the field kit (half the max reserve).
        void FieldRearm(float secondsOutOfCombat, float dt);
        // A full kit: respawn or a camp.
        void FullResupply();

        uint32_t Mag() const { return m_mag; }
        uint32_t Reserve() const { return m_reserve; }
        uint32_t Special(AmmoType t) const { return m_special[size_t(t)]; }
        uint32_t SpecialCap() const { return m_magSize * kSpecialMagazinesCarried; }
        // Top every special type the character has unlocked back up to the cap (a quartermaster).
        void QuartermasterRearm(uint32_t level);
        void AddSpecial(AmmoType t, uint32_t rounds);

    private:
        uint16_t m_magSize = 0;
        uint16_t m_maxReserve = 0;
        uint32_t m_mag = 0;
        uint32_t m_reserve = 0;
        float m_trickle = 0.f;
        AmmoType m_loaded = AmmoType::Basic;
        std::array<uint32_t, size_t(AmmoType::Count)> m_special{};
    };

    // The quartermaster's fee for a full special rearm, in copper: about a quest reward or two at
    // the character's level, so it stays a small, one-stop cost (design doc, section 21).
    uint32_t QuartermasterFee(uint32_t level);
}

#endif
