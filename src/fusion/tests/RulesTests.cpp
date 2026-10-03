/*
 * Azeroth Warfare: the fusion rules' tests. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"

#include "fusion/Ammo.h"
#include "fusion/Creatures.h"
#include "fusion/Detection.h"
#include "fusion/Killstreak.h"
#include "fusion/Movement.h"
#include "fusion/Progression.h"
#include "fusion/Pvp.h"

using namespace Fusion;

// ── Detection ───────────────────────────────────────────────────────────────────────────────

TEST(wow_aggro_radius_by_level_gap)
{
    CHECK_NEAR(WowAggroRadius(10, 10), 20.f, 1e-6);
    CHECK_NEAR(WowAggroRadius(8, 10), 18.f, 1e-6);
    CHECK_NEAR(WowAggroRadius(1, 60), 5.f, 1e-6);
    CHECK_NEAR(WowAggroRadius(60, 1), 45.f, 1e-6);
}

TEST(level_10_crawls_past_level_8_murlocs)
{
    // The design doc's example: prone and moving, in front of the murloc: about 4.6 yards.
    Observer murloc{8, CreatureType::Humanoid, {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
    Intruder player{10, {4.f, 0.f, 0.f}, Stance::Prone, Gait::Walk, false};
    float r = DetectionRadius(murloc, player);
    CHECK_NEAR(r, 18.f * 0.3f * 0.85f, 1e-4);
    CHECK(r > 4.f && r < 5.f);
}

TEST(stance_motion_facing_and_ninja_shrink_detection)
{
    Observer o{20, CreatureType::Humanoid, {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
    Intruder front{20, {5.f, 0.f, 0.f}, Stance::Stand, Gait::Run, false};
    Intruder behind = front;
    behind.position = {-5.f, 0.f, 0.f};
    CHECK_NEAR(DetectionRadius(o, behind), DetectionRadius(o, front) * 0.5f, 1e-4);
    Intruder sprint = front;
    sprint.gait = Gait::Sprint;
    CHECK(DetectionRadius(o, sprint) > DetectionRadius(o, front));
    Intruder ninja = front;
    ninja.ninja = true;
    CHECK_NEAR(DetectionRadius(o, ninja), DetectionRadius(o, front) * 0.7f, 1e-4);
    // Beasts smell what's behind them.
    Observer wolf = o;
    wolf.type = CreatureType::Beast;
    CHECK_NEAR(DetectionRadius(wolf, behind), DetectionRadius(wolf, front) * 0.8f, 1e-4);
}

TEST(alert_meter_fills_over_a_second_and_drains)
{
    AlertMeter m;
    CHECK(!m.Update(0.5f, true));
    CHECK(m.Alerted());
    CHECK(m.Update(0.6f, true));        // crosses full: hostile now
    CHECK(!m.Update(0.1f, true));       // fires once
    m.Update(5.f, false);
    CHECK(!m.Alerted());
}

// ── PvP ─────────────────────────────────────────────────────────────────────────────────────

namespace
{
    PvpPlayer P(uint64_t guid, Team team, uint32_t level = 30)
    {
        PvpPlayer p;
        p.guid = guid;
        p.team = team;
        p.level = level;
        return p;
    }
}

TEST(factions_fight_and_a_faction_needs_both_flags)
{
    PvpPlayer a = P(1, Team::Alliance), h = P(2, Team::Horde), a2 = P(3, Team::Alliance);
    CHECK(CanAttack(a, h));
    CHECK(!CanAttack(a, a2));
    a.ffaFlag = true;
    CHECK(!CanAttack(a, a2));
    a2.ffaFlag = true;
    CHECK(CanAttack(a, a2));
    a.groupId = a2.groupId = 7;
    CHECK(!CanAttack(a, a2));
    CHECK(!CanAttack(a, a));
}

TEST(sanctuaries_and_low_level_protection)
{
    PvpPlayer a = P(1, Team::Alliance, 60), h = P(2, Team::Horde, 45);
    CHECK(!CanAttack(a, h));            // 15 levels below
    CHECK(CanAttack(a, h, true));       // unless they started it
    CHECK(CanAttack(h, a));             // up is always allowed
    h.level = 55;
    CHECK(CanAttack(a, h));
    h.inSanctuary = true;
    CHECK(!CanAttack(a, h));
}

TEST(collision_only_between_enemies)
{
    PvpPlayer a = P(1, Team::Alliance), h = P(2, Team::Horde), a2 = P(3, Team::Alliance);
    CHECK(BodiesCollide(a, h));
    CHECK(!BodiesCollide(a, a2));
    a.ffaFlag = a2.ffaFlag = true;
    CHECK(BodiesCollide(a, a2));
    a.groupId = a2.groupId = 4;
    CHECK(!BodiesCollide(a, a2));
    h.inSanctuary = true;
    CHECK(!BodiesCollide(a, h));
}

TEST(ffa_flag_turns_off_after_five_quiet_minutes)
{
    FfaFlag f;
    f.RequestOn();
    CHECK(f.On());
    f.RequestOff();
    f.Update(200.f, false);
    CHECK(f.On());
    f.Update(10.f, true);               // combat restarts the countdown
    f.Update(250.f, false);
    CHECK(f.On());
    f.Update(60.f, false);
    CHECK(!f.On());
}

// ── Movement ────────────────────────────────────────────────────────────────────────────────

TEST(speeds_follow_stance_gait_and_weapon_weight)
{
    WeaponProfile lmg;
    lmg.moveSpeedScale = 0.875f;
    lmg.adsMoveSpeedScale = 0.4f;
    MovementState m;
    m.gait = Gait::Sprint;
    CHECK_NEAR(SpeedMultiplier(m, &lmg), 1.3f * 0.875f, 1e-5);
    m.aiming = true;                    // no sprinting while aiming
    CHECK_NEAR(SpeedMultiplier(m, &lmg), 0.875f * 0.4f, 1e-5);
    MovementState prone;
    prone.stance = Stance::Prone;
    CHECK_NEAR(SpeedMultiplier(prone, nullptr), 0.2f, 1e-6);
    MovementState back;
    back.gait = Gait::Backpedal;
    CHECK_NEAR(SpeedMultiplier(back, nullptr), 0.85f, 1e-6);
    MovementState light;
    light.lightweight = true;
    CHECK_NEAR(SpeedMultiplier(light, nullptr), 1.07f, 1e-6);
    CHECK_NEAR(StanceHeightScale(Stance::Crouch), 0.65f, 1e-6);
}

TEST(sprint_stamina_and_marathon)
{
    SprintStamina s;
    CHECK(s.Update(3.f, true, false));
    CHECK(!s.Update(2.f, true, false));
    s.Update(10.f, false, false);
    CHECK_NEAR(s.current, s.max, 1e-6);
    SprintStamina m;
    CHECK(m.Update(100.f, true, true));
}

// ── Killstreaks ─────────────────────────────────────────────────────────────────────────────

namespace
{
    std::vector<StreakReward> Ladder()
    {
        return {{1, 3, false}, {2, 5, true}, {3, 7, true}};
    }
}

TEST(streak_rewards_at_their_kill_counts)
{
    Streak s;
    s.SetLoadout(Ladder(), false);
    CHECK(s.AddKill(KillKind::Player).empty());
    CHECK(s.AddKill(KillKind::GrayCreature).empty());
    CHECK(s.Count() == 1);
    s.AddKill(KillKind::Creature);
    auto r = s.AddKill(KillKind::Creature);
    CHECK(r.size() == 1 && r[0].id == 1);
    // An elite counts two and can jump past 5 to 6 without missing the reward.
    s.AddKill(KillKind::Creature);
    r = s.AddKill(KillKind::EliteCreature);
    CHECK(s.Count() == 6);
    CHECK(r.size() == 1 && r[0].id == 2);
    s.Reset();
    CHECK(s.Count() == 0);
    CHECK(s.Earned().size() == 2);      // earned rewards outlive the death
    CHECK(s.Use(1));
    CHECK(!s.Use(1));
}

TEST(hardline_takes_one_kill_off)
{
    Streak s;
    s.SetLoadout(Ladder(), true);
    s.AddKill(KillKind::Player);
    auto r = s.AddKill(KillKind::Player);
    CHECK(r.size() == 1 && r[0].id == 1);
}

TEST(airspace_allows_two_per_area)
{
    Airspace a;
    auto t1 = a.TryLaunch(0, 10.f, 10.f);
    auto t2 = a.TryLaunch(0, 150.f, 50.f);
    CHECK(t1 && t2);
    CHECK(!a.TryLaunch(0, 100.f, 100.f));
    CHECK(a.TryLaunch(0, 450.f, 10.f).has_value());     // the next area over
    CHECK(a.TryLaunch(1, 10.f, 10.f).has_value());      // another map
    a.Release(*t1);
    CHECK(a.ActiveIn(0, 10.f, 10.f) == 1);
    CHECK(a.TryLaunch(0, 20.f, 20.f).has_value());
}

// ── Ammo ────────────────────────────────────────────────────────────────────────────────────

TEST(magazine_reserve_and_specials)
{
    WeaponProfile w;
    w.magSize = 30;
    w.maxReserve = 90;
    GunAmmo g;
    g.Reset(w);
    for (int i = 0; i < 30; ++i)
        CHECK(g.Fire());
    CHECK(!g.Fire());
    CHECK(g.Reload() == 30);
    CHECK(g.Reserve() == 60);

    g.LoadSpecial(AmmoType::Incendiary);
    CHECK(g.NextRound() == AmmoType::Basic);       // none carried yet
    g.QuartermasterRearm(9);
    CHECK(g.Special(AmmoType::Incendiary) == 0);   // not unlocked below 10
    g.QuartermasterRearm(10);
    CHECK(g.Special(AmmoType::Incendiary) == 120); // four magazines
    CHECK(g.Special(AmmoType::Explosive) == 0);    // level 20
    CHECK(g.NextRound() == AmmoType::Incendiary);
    g.Fire();
    CHECK(g.Special(AmmoType::Incendiary) == 119);
}

TEST(field_rearm_trickles_to_half_after_ten_quiet_seconds)
{
    WeaponProfile w;
    w.magSize = 30;
    w.maxReserve = 100;
    GunAmmo g;
    g.Reset(w);
    for (int i = 0; i < 4; ++i)
    {
        while (g.Fire()) {}
        g.Reload();
    }
    CHECK(g.Reserve() == 0);
    g.FieldRearm(5.f, 1.f);
    CHECK(g.Reserve() == 0);
    for (int i = 0; i < 100; ++i)
        g.FieldRearm(20.f, 1.f);
    CHECK(g.Reserve() == 50);
    g.FullResupply();
    CHECK(g.Reserve() == 100 && g.Mag() == 30);
}

TEST(quartermaster_fee_is_small)
{
    CHECK(QuartermasterFee(10) == 300);     // 3 silver
    CHECK(QuartermasterFee(60) == 4800);    // 48 silver
}

// ── Progression and health ──────────────────────────────────────────────────────────────────

TEST(xp_rates)
{
    CHECK(ScaleKillXp(100, false) == 40);
    CHECK(ScaleKillXp(100, true) == 80);
    CHECK(ScaleKillXp(1, false) == 1);
    CHECK(ScaleQuestXp(1000, false) == 1000);
    CHECK(UseRequirement(25) == 250);
}

TEST(regen_waits_and_wounds_stop_it)
{
    HealthRegen r;
    r.OnDamaged(false);
    CHECK(r.Update(4.f, 0.5f) == 0.f);
    CHECK(r.Update(1.5f, 0.5f) > 0.f);
    r.OnDamaged(true);
    CHECK(r.Update(6.f, 0.5f) == 0.f);      // wounded: past the delay but no regen
    CHECK(r.Update(3.f, 0.5f) > 0.f);
    CHECK_NEAR(ReviveTime(ReviverKind::Healer), 2.f, 1e-6);
    CHECK_NEAR(ReviveTime(ReviverKind::Anyone), 6.f, 1e-6);
}

// ── Creatures ───────────────────────────────────────────────────────────────────────────────

TEST(creature_profiles)
{
    CHECK(ClassifyCreature({CreatureType::Humanoid, false, true, false, false}) == Profile::Soldier);
    CHECK(ClassifyCreature({CreatureType::Humanoid, true, false, false, false}) == Profile::Caster);
    CHECK(ClassifyCreature({CreatureType::Beast}) == Profile::Rusher);
    CHECK(ClassifyCreature({CreatureType::Beast, false, false, true, false}) == Profile::Flyer);
    CHECK(ClassifyCreature({CreatureType::Undead}) == Profile::Horde);
    CHECK(ClassifyCreature({CreatureType::Giant}) == Profile::Juggernaut);
    CHECK(ClassifyCreature({CreatureType::Mechanical}) == Profile::Machine);
    CHECK(ClassifyCreature({CreatureType::Critter}) == Profile::Passive);
}

TEST(rushers_close_faster_from_far)
{
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Rusher, 5.f), 1.f, 1e-6);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Rusher, 40.f), 1.6f, 1e-5);
    CHECK_NEAR(ChaseSpeedMultiplier(Profile::Soldier, 40.f), 1.f, 1e-6);
}

TEST(unreachable_targets_trigger_the_ranged_answer)
{
    ReachTracker t;
    t.Update(2.f, false);
    CHECK(!t.Unreachable());
    t.Update(1.5f, false);
    CHECK(t.Unreachable());
    CHECK_NEAR(t.DamageTakenMultiplier(), 0.5f, 1e-6);
    t.Update(0.1f, true);
    CHECK(!t.Unreachable());
}
