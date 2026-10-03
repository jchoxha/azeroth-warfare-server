/*
 * Azeroth Warfare: where the fusion rules (src/fusion) meet the game. The rules are plain data
 * in, data out; this manager feeds them the game's state at its decision points: shots, the
 * client's stance and gait, creature aggro, and per-player ammo, streaks and the FFA flag.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef MANGOS_FUSION_MGR_H
#define MANGOS_FUSION_MGR_H

#include "Common.h"
#include "fusion/Ammo.h"
#include "fusion/Damage.h"
#include "fusion/HitValidation.h"
#include "fusion/Killstreak.h"
#include "fusion/Movement.h"
#include "fusion/Packets.h"
#include "fusion/Pvp.h"
#include "fusion/Weapon.h"

#include <unordered_map>

class Creature;
class Player;
class Unit;

// What the fusion keeps for each player; owned by the Player (Player::GetFusion).
struct FusionPlayerData
{
    Fusion::Stance stance = Fusion::Stance::Stand;
    Fusion::Gait gait = Fusion::Gait::Run;
    uint8 stateFlags = 0;
    uint16 heldWeaponId = 0;
    Fusion::AmmoType loadedAmmo = Fusion::AmmoType::Basic;

    std::unordered_map<uint16, Fusion::GunAmmo> ammo;  // per gun, made on first use
    uint32 lastShotMs = 0;                              // server clock
    bool hasLastShot = false;

    Fusion::SprintStamina stamina;
    bool sprinting = false;                             // asked to sprint and has the stamina

    Fusion::FfaFlag ffa;
    Fusion::Streak streak;
    Fusion::PoseHistory poses;                          // for lag-compensated hits on this player
    float outOfCombatSeconds = 0.f;

    // The client stamps shots with its own millisecond clock; the smallest (server - client)
    // difference seen is the offset with the least latency in it.
    bool hasClockOffset = false;
    int32 clockOffset = 0;
    uint32 ToServerTime(uint32 clientMs, uint32 nowMs);

    Fusion::GunAmmo& AmmoFor(Fusion::WeaponProfile const& w);
};

class FusionMgr
{
public:
    static FusionMgr& Instance();

    // Read the config (Fusion.*) and the weapon table (<DataDir>/fusion/weapons.json).
    void Load();
    bool IsEnabled() const { return m_enabled; }

    Fusion::WeaponTable const& Weapons() const { return m_weapons; }
    Fusion::ParTable const& Par() const { return m_par; }

    // The packet handlers.
    void HandleShots(Player* shooter, Fusion::Packets::ShotBatch const& batch);
    void HandleState(Player* player, Fusion::Packets::State const& state);

    // Every player update: pose history, the FFA countdown and the field rearm.
    void UpdatePlayer(Player* player, uint32 diffMs);

    // The share of a creature's WoW aggro radius a player's stance and motion leave (design doc
    // section 15); 1 for anything but a fusion player.
    float AggroRadiusMultiplier(Creature const* creature, Unit const* target) const;

    // The share of the run speed the player's stance, sprint, aim and gun leave (design doc,
    // section 15); 1 when mounted or not a fusion player. Unit::UpdateSpeed applies it.
    float MoveSpeedMultiplier(Player const* player) const;

    // A unit died to a player's gunfire, or a player died: streak bookkeeping.
    void OnKill(Player* killer, Unit* victim);
    void OnPlayerDeath(Player* victim);

    void SendEvents(Player* player, Fusion::Packets::Events const& events) const;

private:
    bool m_enabled = false;
    bool m_pvpLowLevelProtection = true;
    Fusion::Coefficients m_coefficients;
    Fusion::WeaponTable m_weapons;
    Fusion::ParTable m_par;
};

#define sFusionMgr FusionMgr::Instance()

#endif
