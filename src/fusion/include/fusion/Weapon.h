/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_WEAPON_H
#define FUSION_WEAPON_H

#include "fusion/Common.h"

#include <string>
#include <vector>

namespace Fusion
{
    // MW2's weapon classes, which set movement speed, the class bonuses and which perks apply.
    enum class WeaponClass : uint8_t
    {
        AssaultRifle,
        Smg,
        Lmg,
        Shotgun,
        Sniper,
        Pistol,
        MachinePistol,
        Launcher,
        RiotShield,
        Melee,
    };

    // Where a hit landed; each weapon carries MW2's own multiplier for each.
    enum class HitLocation : uint8_t
    {
        Head,
        Neck,
        TorsoUpper,
        TorsoLower,
        Arm,
        Leg,
        Count,
    };

    // One weapon's behaviour as MW2 defines it, exported from the player's MW2 install
    // (weapons.json). Ranges are converted from MW2 inches to yards at export.
    struct WeaponProfile
    {
        uint16_t id = 0;
        std::string name;               // "m4_mp"
        WeaponClass weaponClass = WeaponClass::AssaultRifle;

        float damage = 30.f;            // CoD damage at or inside maxDamageRange
        float minDamage = 20.f;         // CoD damage at or beyond minDamageRange
        float maxDamageRange = 42.f;    // yards
        float minDamageRange = 83.f;    // yards
        uint8_t pellets = 1;            // shotguns fire several

        float fireTime = 0.08f;         // seconds between shots
        uint16_t magSize = 30;
        uint16_t maxReserve = 120;
        float reloadTime = 2.2f;
        float reloadEmptyTime = 2.8f;
        float moveSpeedScale = 0.95f;   // MW2's own, 1.0 = unencumbered
        float adsMoveSpeedScale = 0.5f;

        float locationMultiplier[size_t(HitLocation::Count)] = {1.4f, 1.f, 1.f, 1.f, 1.f, 1.f};

        uint8_t mw2UnlockRank = 1;      // MW2's own unlock rank, 1 = default
        bool starter = false;           // one per class at level 1

        float LocationMultiplier(HitLocation loc) const { return locationMultiplier[size_t(loc)]; }
    };

    // MW2's damage falloff: full damage inside maxDamageRange, minDamage beyond minDamageRange,
    // linear between.
    inline float CodDamageAtRange(WeaponProfile const& w, float distance)
    {
        if (distance <= w.maxDamageRange)
            return w.damage;
        if (distance >= w.minDamageRange || w.minDamageRange <= w.maxDamageRange)
            return w.minDamage;
        float t = (distance - w.maxDamageRange) / (w.minDamageRange - w.maxDamageRange);
        return Lerp(w.damage, w.minDamage, t);
    }

    // The level a weapon unlocks at: MW2's rank scaled from MW2's cap (70) to WoW's (60).
    inline uint8_t UnlockLevel(uint8_t mw2Rank)
    {
        int level = int(std::ceil(float(mw2Rank) * 60.f / 70.f));
        return uint8_t(std::max(1, std::min(60, level)));
    }

    // The weapon roster: the exported profiles, looked up by id or name.
    class WeaponTable
    {
    public:
        void Add(WeaponProfile profile) { m_weapons.push_back(std::move(profile)); }
        WeaponProfile const* Find(uint16_t id) const;
        WeaponProfile const* Find(std::string const& name) const;
        std::vector<WeaponProfile> const& All() const { return m_weapons; }
        size_t Size() const { return m_weapons.size(); }

        // Parse weapons.json as the export tool writes it. Returns false and fills error on a
        // malformed file; unknown keys are ignored so the format can grow.
        bool LoadJson(std::string const& text, std::string& error);

    private:
        std::vector<WeaponProfile> m_weapons;
    };
}

#endif
