/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Movement.h"

namespace Fusion
{
    float StanceHeightScale(Stance s)
    {
        switch (s)
        {
            case Stance::Stand:  return 1.f;
            case Stance::Crouch:
            case Stance::Slide:  return 0.65f;
            case Stance::Prone:  return 0.30f;
        }
        return 1.f;
    }

    float SpeedMultiplier(MovementState const& m, WeaponProfile const* held)
    {
        float stance = 1.f;
        switch (m.stance)
        {
            case Stance::Stand:  stance = 1.f; break;
            case Stance::Crouch: stance = 0.6f; break;
            case Stance::Prone:  stance = 0.2f; break;
            case Stance::Slide:  stance = 1.5f; break;
        }

        float gait = 1.f;
        switch (m.gait)
        {
            case Gait::Still:     gait = 0.f; break;
            case Gait::Walk:      gait = 0.5f; break;
            case Gait::Run:       gait = 1.f; break;
            case Gait::Backpedal: gait = 0.85f; break;
            case Gait::Sprint:
                // No sprinting while aiming, crouched or prone: fall back to a run.
                gait = (m.aiming || m.stance != Stance::Stand) ? 1.f : 1.3f;
                break;
        }

        float weapon = held ? held->moveSpeedScale : 1.f;
        if (m.aiming && held)
            weapon *= held->adsMoveSpeedScale;
        float perk = m.lightweight ? 1.07f : 1.f;
        return stance * gait * weapon * perk;
    }

    bool SprintStamina::Update(float dt, bool sprinting, bool marathon)
    {
        if (marathon)
        {
            current = max;
            return true;
        }
        if (sprinting)
        {
            current = std::max(0.f, current - dt);
            return current > 0.f;
        }
        current = std::min(max, current + regenPerSecond * dt);
        return true;
    }
}
