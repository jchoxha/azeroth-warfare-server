/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_MOVEMENT_H
#define FUSION_MOVEMENT_H

#include "fusion/Common.h"
#include "fusion/Weapon.h"

namespace Fusion
{
    // CoD stances (design doc, section 15). The server tracks them and grants the speeds below,
    // so the client never moves faster than it was granted.
    enum class Stance : uint8_t
    {
        Stand,
        Crouch,
        Prone,
        Slide,      // a short burst out of a sprint, at crouch height
    };

    enum class Gait : uint8_t
    {
        Still,
        Walk,
        Run,
        Sprint,
        Backpedal,
    };

    struct MovementState
    {
        Stance stance = Stance::Stand;
        Gait gait = Gait::Run;
        bool aiming = false;            // aiming down sights
        bool marathon = false;          // the perk: no sprint limit
        bool lightweight = false;       // the perk: +7%
    };

    // The share of standing height a stance leaves of the hitbox and the camera.
    float StanceHeightScale(Stance s);

    // The speed multiplier over the character's base run speed. Sprint can't be combined with
    // aiming or a crouch; the weapon's own MW2 weight applies to every gait.
    float SpeedMultiplier(MovementState const& m, WeaponProfile const* held);

    // Sprint stamina: seconds of sprint available, and how fast it comes back.
    struct SprintStamina
    {
        float max = 4.f;
        float current = 4.f;
        float regenPerSecond = 1.f;

        // Advance by dt; returns whether the character may keep sprinting.
        bool Update(float dt, bool sprinting, bool marathon);
    };

    // Seconds after a sprint before the gun can be aimed or fired (MW2's sprint-out time).
    constexpr float kSprintOutTime = 0.25f;
    // Spread multiplier while airborne.
    constexpr float kAirborneSpreadMultiplier = 3.f;
}

#endif
