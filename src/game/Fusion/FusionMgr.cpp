/*
 * Azeroth Warfare: where the fusion rules meet the game. GPL-2.0-or-later, see FusionMgr.h.
 */

#include "FusionMgr.h"

#include "Config/Config.h"
#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"
#include "Util.h"
#include "Formulas.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "fusion/Creatures.h"
#include "fusion/Detection.h"

#include <fstream>
#include <sstream>

namespace
{
    Fusion::Vec3 V(float x, float y, float z) { return {x, y, z}; }

    // The eye above the feet, by stance: the shot's origin must sit near it.
    float EyeHeight(Fusion::Stance s) { return 1.6f * Fusion::StanceHeightScale(s); }

    Fusion::ArmorClass ArmorFor(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
                return Fusion::ArmorClass::Plate;
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
                return Fusion::ArmorClass::Mail;
            case CLASS_ROGUE:
            case CLASS_DRUID:
                return Fusion::ArmorClass::Leather;
            default:
                return Fusion::ArmorClass::Cloth;
        }
    }

    Fusion::CreatureRank RankOf(Creature const* c)
    {
        uint32 rank = c->GetCreatureInfo() ? c->GetCreatureInfo()->rank : 0;
        // A dungeon's elite boss: an elite in an instance with a boss-sized health pool.
        if (rank == CREATURE_ELITE_ELITE && c->GetMap()->IsDungeon() && c->IsWorldBoss())
            return Fusion::CreatureRank::DungeonBoss;
        return rank <= 4 ? Fusion::CreatureRank(rank) : Fusion::CreatureRank::Normal;
    }

    Fusion::PvpPlayer PvpOf(Player const* p, FusionPlayerData const* d)
    {
        Fusion::PvpPlayer out;
        out.guid = p->GetObjectGuid().GetRawValue();
        out.team = p->GetTeam() == HORDE ? Fusion::Team::Horde : Fusion::Team::Alliance;
        out.level = p->GetLevel();
        out.groupId = p->GetGroup() ? p->GetGroup()->GetId() : 0;
        out.ffaFlag = d ? d->ffa.On() : p->IsFFAPvP();
        out.inSanctuary = false;    // the game's own sanctuary checks run in IsValidAttackTarget
        return out;
    }

    Fusion::AttackerStats StatsOf(Player const* p)
    {
        Fusion::AttackerStats a;
        a.level = p->GetLevel();
        a.attackPower = std::max(p->GetTotalAttackPowerValue(BASE_ATTACK), p->GetTotalAttackPowerValue(RANGED_ATTACK));
        if (Item const* ranged = p->GetWeaponForAttack(RANGED_ATTACK))
            a.rangedItemLevel = float(ranged->GetProto()->ItemLevel);
        if (Item const* main = p->GetWeaponForAttack(BASE_ATTACK))
            a.mainHandItemLevel = float(main->GetProto()->ItemLevel);
        a.critChance = p->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE) / 100.f;
        return a;
    }

    Fusion::TargetTraits TraitsOf(Unit const* u)
    {
        Fusion::TargetTraits t;
        t.isPlayer = u->IsPlayer();
        t.creatureType = Fusion::CreatureType(u->GetCreatureType());
        if (Player const* p = u->ToPlayer())
            t.armor = ArmorFor(p->GetClass());
        if (Creature const* c = u->ToCreature())
            t.slowImmune = c->IsWorldBoss();
        return t;
    }
}

uint32 FusionPlayerData::ToServerTime(uint32 clientMs, uint32 nowMs)
{
    int32 const offset = int32(nowMs - clientMs);
    if (!hasClockOffset || offset < clockOffset)
    {
        clockOffset = offset;
        hasClockOffset = true;
    }
    return clientMs + uint32(clockOffset);
}

Fusion::GunAmmo& FusionPlayerData::AmmoFor(Fusion::WeaponProfile const& w)
{
    auto it = ammo.find(w.id);
    if (it == ammo.end())
    {
        it = ammo.emplace(w.id, Fusion::GunAmmo{}).first;
        it->second.Reset(w);
    }
    return it->second;
}

FusionMgr& FusionMgr::Instance()
{
    static FusionMgr instance;
    return instance;
}

void FusionMgr::Load()
{
    m_enabled = sConfig.GetBoolDefault("Fusion.Enable", false);
    m_pvpLowLevelProtection = sConfig.GetBoolDefault("Fusion.PvP.LowLevelProtection", true);
    m_coefficients.pve = sConfig.GetFloatDefault("Fusion.Damage.PvE", 1.0f);
    m_coefficients.pvp = sConfig.GetFloatDefault("Fusion.Damage.PvP", 0.4f);
    if (!m_enabled)
        return;

    std::string path = sConfig.GetStringDefault("Fusion.WeaponsFile", "");
    if (path.empty())
        path = sWorld.GetDataPath() + "fusion/weapons.json";
    std::ifstream in(path);
    if (!in)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: no weapon table at %s; guns are disabled until it is exported (tools/fusion/export_weapons).", path.c_str());
        return;
    }
    std::stringstream text;
    text << in.rdbuf();
    std::string error;
    Fusion::WeaponTable table;
    if (!table.LoadJson(text.str(), error))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Fusion: %s: %s", path.c_str(), error.c_str());
        return;
    }
    m_weapons = std::move(table);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Fusion: enabled, %u weapons from %s.", uint32(m_weapons.Size()), path.c_str());
}

void FusionMgr::SendEvents(Player* player, Fusion::Packets::Events const& events) const
{
    if (events.events.empty())
        return;
    std::vector<uint8_t> body = Fusion::Packets::Encode(events);
    WorldPacket data(SMSG_FUSION_EVENTS, body.size());
    data.append(body.data(), body.size());
    player->GetSession()->SendPacket(&data);
}

float FusionMgr::MoveSpeedMultiplier(Player const* player) const
{
    if (!m_enabled || !player->HasFusion() || player->IsMounted())
        return 1.f;
    FusionPlayerData const& d = player->GetFusionConst();
    Fusion::MovementState m;
    m.stance = d.stance;
    // Walking and backpedalling are WoW's own speeds already; only sprint is the fusion's.
    m.gait = d.sprinting ? Fusion::Gait::Sprint : Fusion::Gait::Run;
    m.aiming = d.stateFlags & Fusion::Packets::STATE_AIMING;
    return Fusion::SpeedMultiplier(m, m_weapons.Find(d.heldWeaponId));
}

void FusionMgr::HandleState(Player* player, Fusion::Packets::State const& state)
{
    FusionPlayerData& d = player->GetFusion();
    bool const speedChanged = d.stance != state.stance || d.heldWeaponId != state.heldWeaponId
        || ((d.stateFlags ^ state.flags) & Fusion::Packets::STATE_AIMING);
    d.stance = state.stance;
    d.gait = state.gait;
    d.stateFlags = state.flags;
    d.heldWeaponId = state.heldWeaponId;
    if (state.loadedAmmo < uint8(Fusion::AmmoType::Count))
    {
        auto const type = Fusion::AmmoType(state.loadedAmmo);
        // Only rounds the character has unlocked.
        if (player->GetLevel() >= Fusion::AmmoUnlockLevel(type))
        {
            d.loadedAmmo = type;
            if (Fusion::WeaponProfile const* w = m_weapons.Find(state.heldWeaponId))
                d.AmmoFor(*w).LoadSpecial(type);
        }
    }

    if (speedChanged)
    {
        player->UpdateSpeed(MOVE_RUN, true);
        player->UpdateSpeed(MOVE_RUN_BACK, true);
    }

    if (state.flags & Fusion::Packets::STATE_FFA_ON)
        d.ffa.RequestOn();
    else if (state.flags & Fusion::Packets::STATE_FFA_OFF)
        d.ffa.RequestOff();
    if (d.ffa.On() != player->IsFFAPvP())
        player->SetFFAPvP(d.ffa.On());

    // The quartermaster (design doc, section 21): resting in a capital or an inn, one click tops
    // every special round the character has unlocked up for every gun, for a level-scaled fee.
    if ((state.flags & Fusion::Packets::STATE_REARM) && player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
    {
        uint32 const fee = Fusion::QuartermasterFee(player->GetLevel());
        if (player->GetMoney() >= fee)
        {
            player->ModifyMoney(-int32(fee));
            if (Fusion::WeaponProfile const* w = m_weapons.Find(state.heldWeaponId))
                d.AmmoFor(*w);
            for (auto& kv : d.ammo)
            {
                kv.second.FullResupply();
                kv.second.QuartermasterRearm(player->GetLevel());
            }
        }
    }

    if (state.flags & Fusion::Packets::STATE_RELOADING)
    {
        if (Fusion::WeaponProfile const* w = m_weapons.Find(state.heldWeaponId))
        {
            Fusion::GunAmmo& ammo = d.AmmoFor(*w);
            ammo.Reload();
            Fusion::Packets::Events ev;
            ev.events.push_back({Fusion::Packets::EventKind::Ammo, 0, 0, ammo.Mag(), uint16(std::min<uint32>(ammo.Reserve(), 0xFFFF))});
            SendEvents(player, ev);
        }
    }
}

void FusionMgr::UpdatePlayer(Player* player, uint32 diffMs)
{
    FusionPlayerData& d = player->GetFusion();
    float const dt = diffMs / 1000.f;
    uint32 const now = WorldTimer::getMSTime();

    Fusion::BodyPose pose;
    pose.feet = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    pose.orientation = player->GetOrientation();
    pose.stance = d.stance;
    d.poses.Record(now, pose);

    if (!player->IsAlive())
        d.streak.Reset();

    // Sprint lasts as long as the stamina does.
    bool const wantsSprint = d.gait == Fusion::Gait::Sprint && d.stance == Fusion::Stance::Stand;
    bool const sprinting = d.stamina.Update(dt, wantsSprint, false) && wantsSprint;
    if (sprinting != d.sprinting)
    {
        d.sprinting = sprinting;
        player->UpdateSpeed(MOVE_RUN, true);
    }   // death ends the streak; earned rewards stay until used

    bool const inCombat = player->IsInCombat();
    d.outOfCombatSeconds = inCombat ? 0.f : d.outOfCombatSeconds + dt;

    bool const wasOn = d.ffa.On();
    d.ffa.Update(dt, inCombat);
    if (wasOn && !d.ffa.On())
        player->SetFFAPvP(false);

    if (!inCombat)
        for (auto& kv : d.ammo)
            kv.second.FieldRearm(d.outOfCombatSeconds, dt);
}

float FusionMgr::AggroRadiusMultiplier(Creature const* creature, Unit const* target) const
{
    if (!m_enabled)
        return 1.f;
    Player const* player = target->ToPlayer();
    if (!player || !player->HasFusion())
        return 1.f;
    FusionPlayerData const& d = player->GetFusionConst();

    Fusion::Observer o;
    o.level = creature->GetLevel();
    o.type = Fusion::CreatureType(creature->GetCreatureType());
    o.position = V(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ());
    o.facing = V(std::cos(creature->GetOrientation()), std::sin(creature->GetOrientation()), 0.f);

    Fusion::Intruder p;
    p.level = player->GetLevel();
    p.position = V(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    p.stance = d.stance;
    p.gait = d.gait;

    float const base = Fusion::WowAggroRadius(o.level, p.level);
    return base > 0.f ? Fusion::DetectionRadius(o, p) / base : 1.f;
}

float FusionMgr::CreatureChaseMultiplier(Creature const* creature) const
{
    if (!m_enabled || creature->IsPet())
        return 1.f;
    Unit const* victim = creature->GetVictim();
    if (!victim || !victim->IsCharmerOrOwnerPlayerOrPlayerItself())
        return 1.f;
    Fusion::CreatureTraits t;
    t.type = Fusion::CreatureType(creature->GetCreatureType());
    t.caster = creature->GetMaxPower(POWER_MANA) > 0;
    t.flying = creature->CanFly();
    t.large = creature->GetObjectBoundingRadius() > 1.2f;
    return Fusion::ChaseSpeedMultiplier(Fusion::ClassifyCreature(t), creature->GetDistance(victim));
}

void FusionMgr::OnKill(Player* killer, Unit* victim)
{
    Fusion::KillKind kind = Fusion::KillKind::Creature;
    if (victim->IsPlayer())
        kind = Fusion::KillKind::Player;
    else if (Creature* c = victim->ToCreature())
    {
        if (MaNGOS::XP::GetColorCode(killer->GetLevel(), c->GetLevel()) == MaNGOS::XP::GRAY)
            kind = Fusion::KillKind::GrayCreature;
        else if (c->IsElite())
            kind = Fusion::KillKind::EliteCreature;
    }
    std::vector<Fusion::StreakReward> earned = killer->GetFusion().streak.AddKill(kind);
    Fusion::Packets::Events ev;
    for (auto const& r : earned)
        ev.events.push_back({Fusion::Packets::EventKind::StreakEarned, 0, 0, r.kills, uint16(r.id)});
    SendEvents(killer, ev);
}

void FusionMgr::OnPlayerDeath(Player* victim)
{
    if (victim->HasFusion())
        victim->GetFusion().streak.Reset();
}

void FusionMgr::HandleShots(Player* shooter, Fusion::Packets::ShotBatch const& batch)
{
    if (!m_enabled || !shooter->IsAlive())
        return;
    FusionPlayerData& d = shooter->GetFusion();
    uint32 const now = WorldTimer::getMSTime();
    Fusion::Packets::Events shooterEvents;

    // A shotgun sends one shot per pellet with the same time: one round, one fire-rate check.
    uint32 pelletTime = 0;
    uint32 pelletsLeft = 0;

    for (Fusion::Packets::Shot const& shot : batch.shots)
    {
        Fusion::WeaponProfile const* w = m_weapons.Find(shot.weaponId);
        if (!w)
            continue;
        if (!w->starter && shooter->GetLevel() < Fusion::UnlockLevel(w->mw2UnlockRank))
            continue;

        uint32 const shotTime = d.ToServerTime(shot.timeMs, now);
        bool const extraPellet = pelletsLeft > 0 && shotTime == pelletTime;
        Fusion::GunAmmo& ammo = d.AmmoFor(*w);
        Fusion::AmmoType const round = ammo.NextRound();
        if (extraPellet)
            --pelletsLeft;
        else
        {
            if (d.hasLastShot && int32(shotTime - d.lastShotMs) < int32(w->fireTime * 1000.f * 0.8f))
                continue;   // faster than the gun fires
            if (!ammo.Fire())
                continue;
            d.lastShotMs = shotTime;
            d.hasLastShot = true;
            pelletTime = shotTime;
            pelletsLeft = w->pellets > 1 ? w->pellets - 1u : 0u;
        }

        if (!shot.targetGuid)
            continue;
        Unit* target = ObjectAccessor::GetUnit(*shooter, ObjectGuid(shot.targetGuid));
        if (!target || !target->IsAlive() || target == shooter || !shooter->IsValidAttackTarget(target))
            continue;

        Player* targetPlayer = target->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (targetPlayer && targetPlayer != shooter)
        {
            Fusion::PvpPlayer const a = PvpOf(shooter, &d);
            Fusion::PvpPlayer b = PvpOf(targetPlayer, targetPlayer->HasFusion() ? &targetPlayer->GetFusionConst() : nullptr);
            bool const victimStarted = targetPlayer->GetVictim() == shooter || !m_pvpLowLevelProtection;
            if (!Fusion::CanAttack(a, b, victimStarted))
                continue;
        }

        // The referee: the server's own ray against the target where the shooter saw it.
        Fusion::PoseHistory single;
        Fusion::PoseHistory const* history = &single;
        Fusion::HitboxShape shape;
        if (Player* tp = target->ToPlayer(); tp && tp->HasFusion())
            history = &tp->GetFusionConst().poses;
        else
        {
            Fusion::BodyPose pose;
            pose.feet = V(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
            pose.orientation = target->GetOrientation();
            single.Record(shotTime, pose);  // creatures: no rewind, their current place
            if (!target->IsPlayer())
            {
                shape.height = std::max(1.0f, target->GetCollisionHeight());
                shape.radius = std::max(0.45f, target->GetObjectBoundingRadius());
                shape.headRadius = std::max(0.16f, shape.radius * 0.35f);
            }
        }

        Fusion::ShotClaim claim;
        claim.shooterTimeMs = shotTime;
        claim.origin = V(shot.origin.x, shot.origin.y, shot.origin.z);
        claim.direction = V(shot.direction.x, shot.direction.y, shot.direction.z);
        claim.claimsHead = shot.flags & Fusion::Packets::SHOT_CLAIMS_HEAD;

        Fusion::ShotContext ctx;
        ctx.weapon = w;
        ctx.shooterPosition = V(shooter->GetPositionX(), shooter->GetPositionY(), shooter->GetPositionZ() + EyeHeight(d.stance));
        ctx.targetShape = shape;
        ctx.serverTimeMs = now;
        Map const* map = shooter->GetMap();
        ctx.lineOfSight = [map](Fusion::Vec3 a, Fusion::Vec3 b) {
            return map->isInLineOfSight(a.x, a.y, a.z, b.x, b.y, b.z, true, true);
        };

        Fusion::ShotResult const r = Fusion::ValidateShot(claim, ctx, *history);
        if (r.verdict != Fusion::ShotVerdict::Hit)
            continue;

        Fusion::HitInput in;
        in.weapon = w;
        in.distance = r.distance;
        in.location = r.location;
        in.ammo = round;
        in.attacker = StatsOf(shooter);
        in.critRoll = r.location != Fusion::HitLocation::Head && roll_chance_f(in.attacker.critChance * 100.f);
        Fusion::TargetTraits const traits = TraitsOf(target);
        Fusion::CombatContext const context = targetPlayer ? Fusion::CombatContext::PvP : Fusion::CombatContext::PvE;
        float const cod = Fusion::CodDamage(in, target->GetLevel(), traits, context, m_par, m_coefficients);

        float health;
        if (Player* tp = target->ToPlayer())
        {
            Fusion::PlayerDefense def{tp->GetLevel(), float(tp->GetMaxHealth()), float(tp->GetArmor())};
            health = Fusion::ToPlayerHealth(cod, Fusion::CodHealth(def, m_par), float(tp->GetMaxHealth()));
        }
        else if (Creature* c = target->ToCreature())
            health = Fusion::ToCreatureHealth(cod, float(c->GetMaxHealth()), RankOf(c));
        else
            health = cod;

        uint32 const damage = std::max<uint32>(1, uint32(health + 0.5f));
        bool const head = r.location == Fusion::HitLocation::Head || in.critRoll;
        shooter->DealDamage(target, damage, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
        bool const fatal = !target->IsAlive();

        uint8 flags = (head ? Fusion::Packets::EVENT_HEADSHOT : 0) | (fatal ? Fusion::Packets::EVENT_FATAL : 0);
        shooterEvents.events.push_back({Fusion::Packets::EventKind::Hit, flags, shot.targetGuid, damage, w->id});
        if (fatal)
        {
            shooterEvents.events.push_back({Fusion::Packets::EventKind::Kill, flags, shot.targetGuid, 0, w->id});
            OnKill(shooter, target);
        }
        if (Player* tp = target->ToPlayer())
        {
            Fusion::Packets::Events hurt;
            hurt.events.push_back({Fusion::Packets::EventKind::Hurt, flags, shooter->GetObjectGuid().GetRawValue(), damage, w->id});
            SendEvents(tp, hurt);
            if (fatal)
                OnPlayerDeath(tp);
        }
        if (shooterEvents.events.size() + 2 > Fusion::Packets::kMaxEventsPerPacket)
        {
            SendEvents(shooter, shooterEvents);
            shooterEvents.events.clear();
        }
    }
    SendEvents(shooter, shooterEvents);
}
