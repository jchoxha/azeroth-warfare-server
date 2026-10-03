/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_PACKETS_H
#define FUSION_PACKETS_H

#include "fusion/Common.h"
#include "fusion/Movement.h"

#include <string>
#include <vector>

// The fusion's own world packets, beside 1.12's (whose table ends at 827). The client side lives
// in the benilla fork (benilla-protocol, fusion.rs); both sides are tested against the same byte
// vectors (src/fusion/vectors/packets.txt), so they can't drift apart. Little-endian throughout.
namespace Fusion::Packets
{
    constexpr uint16_t CMSG_FUSION_SHOTS = 828;
    constexpr uint16_t SMSG_FUSION_EVENTS = 829;
    constexpr uint16_t CMSG_FUSION_STATE = 830;
    constexpr uint16_t kNumMsgTypes = 831;

    constexpr uint8_t kVersion = 1;
    // One batch carries at most this many shots (the client sends 20 batches a second).
    constexpr uint8_t kMaxShotsPerBatch = 16;
    constexpr uint8_t kMaxEventsPerPacket = 32;

    // ── CMSG_FUSION_SHOTS ───────────────────────────────────────────────────────────────────
    enum ShotFlags : uint8_t
    {
        SHOT_ADS = 0x01,
        SHOT_CLAIMS_HEAD = 0x02,
        SHOT_AIRBORNE = 0x04,
    };

    struct Shot
    {
        uint32_t timeMs = 0;        // the shooter's view time on the server clock
        uint16_t weaponId = 0;
        uint8_t flags = 0;
        Stance stance = Stance::Stand;
        uint64_t targetGuid = 0;    // the unit the client's ray hit, 0 = none
        Vec3 origin;
        Vec3 direction;
    };

    struct ShotBatch
    {
        std::vector<Shot> shots;
    };

    // ── CMSG_FUSION_STATE ───────────────────────────────────────────────────────────────────
    enum StateFlags : uint8_t
    {
        STATE_AIMING = 0x01,
        STATE_FFA_ON = 0x02,    // request the free-for-all flag on
        STATE_FFA_OFF = 0x04,   // request it off (takes 5 minutes out of combat)
        STATE_RELOADING = 0x08,
        STATE_REARM = 0x10,     // buy special rounds; only while resting (a city or an inn)
    };

    struct State
    {
        Stance stance = Stance::Stand;
        Gait gait = Gait::Run;
        uint8_t flags = 0;
        uint16_t heldWeaponId = 0;
        uint8_t loadedAmmo = 0;     // AmmoType
    };

    // ── SMSG_FUSION_EVENTS ──────────────────────────────────────────────────────────────────
    enum class EventKind : uint8_t
    {
        Hit = 0,            // you hit guid for amount
        Kill = 1,           // you killed guid
        Hurt = 2,           // guid hit you for amount
        StreakEarned = 3,   // extra = reward id
        Ammo = 4,           // amount = magazine, extra = reserve (a resync)
        Revived = 5,
    };

    enum EventFlags : uint8_t
    {
        EVENT_HEADSHOT = 0x01,
        EVENT_FATAL = 0x02,
        EVENT_ASSIST = 0x04,
    };

    struct Event
    {
        EventKind kind = EventKind::Hit;
        uint8_t flags = 0;
        uint64_t guid = 0;
        uint32_t amount = 0;
        uint16_t extra = 0;
    };

    struct Events
    {
        std::vector<Event> events;
    };

    // Encode a message's body (the opcode travels in the world packet header).
    std::vector<uint8_t> Encode(ShotBatch const& m);
    std::vector<uint8_t> Encode(State const& m);
    std::vector<uint8_t> Encode(Events const& m);

    // Decode a body; false on a wrong version, a bad count or a short buffer.
    bool Decode(std::vector<uint8_t> const& body, ShotBatch& out);
    bool Decode(std::vector<uint8_t> const& body, State& out);
    bool Decode(std::vector<uint8_t> const& body, Events& out);

    std::string ToHex(std::vector<uint8_t> const& bytes);
    std::vector<uint8_t> FromHex(std::string const& hex);
}

#endif
