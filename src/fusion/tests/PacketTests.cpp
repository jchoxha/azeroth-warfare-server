/*
 * Azeroth Warfare: the fusion rules' tests. GPL-2.0-or-later, see Common.h.
 */

#include "Test.h"

#include "fusion/Packets.h"

#include <fstream>
#include <map>
#include <sstream>

using namespace Fusion;
using namespace Fusion::Packets;

namespace
{
    // vectors/packets.txt: "name hex" per line, '#' comments. The benilla fork's tests read the
    // same file, so both ends agree on every byte.
    std::map<std::string, std::string> const& Vectors()
    {
        static std::map<std::string, std::string> vectors = [] {
            std::map<std::string, std::string> v;
            std::ifstream in(std::string(FUSION_VECTORS_DIR) + "/packets.txt");
            std::string line;
            while (std::getline(in, line))
            {
                if (line.empty() || line[0] == '#')
                    continue;
                std::istringstream s(line);
                std::string name, hex;
                s >> name >> hex;
                v[name] = hex;
            }
            return v;
        }();
        return vectors;
    }

    std::string Vector(char const* name)
    {
        auto it = Vectors().find(name);
        return it == Vectors().end() ? std::string("<missing>") : it->second;
    }
}

static ShotBatch SampleShots()
{
    ShotBatch b;
    Shot s;
    s.timeMs = 123456;
    s.weaponId = 7;
    s.flags = SHOT_ADS | SHOT_CLAIMS_HEAD;
    s.stance = Stance::Crouch;
    s.targetGuid = 0xF1A2B3;
    s.origin = {10.5f, -20.25f, 30.f};
    s.direction = {0.f, 1.f, 0.f};
    b.shots.push_back(s);
    return b;
}

static State SampleState()
{
    State st;
    st.stance = Stance::Prone;
    st.gait = Gait::Walk;
    st.flags = STATE_AIMING | STATE_FFA_ON;
    st.heldWeaponId = 42;
    st.loadedAmmo = 5;
    return st;
}

static Events SampleEvents()
{
    Events ev;
    ev.events.push_back({EventKind::Hit, EVENT_HEADSHOT, 0x1234, 45, 7});
    ev.events.push_back({EventKind::Kill, EVENT_FATAL, 0x1234, 0, 7});
    return ev;
}

TEST(opcodes_follow_the_1_12_table)
{
    CHECK(CMSG_FUSION_SHOTS == 828);
    CHECK(kNumMsgTypes == 831);
}

TEST(packets_match_the_shared_vectors)
{
    CHECK(ToHex(Encode(SampleShots())) == Vector("shots_one"));
    CHECK(ToHex(Encode(ShotBatch{})) == Vector("shots_empty"));
    CHECK(ToHex(Encode(SampleState())) == Vector("state"));
    CHECK(ToHex(Encode(SampleEvents())) == Vector("events_hit_kill"));
}

TEST(packets_round_trip)
{
    ShotBatch b;
    CHECK(Decode(FromHex(Vector("shots_one")), b));
    CHECK(b.shots.size() == 1);
    if (b.shots.size() != 1)
        return;
    CHECK(b.shots[0].timeMs == 123456 && b.shots[0].weaponId == 7 && b.shots[0].stance == Stance::Crouch);
    CHECK(b.shots[0].targetGuid == 0xF1A2B3);
    CHECK_NEAR(b.shots[0].origin.y, -20.25, 1e-6);

    State st;
    CHECK(Decode(FromHex(Vector("state")), st));
    CHECK(st.stance == Stance::Prone && st.gait == Gait::Walk && st.heldWeaponId == 42 && st.loadedAmmo == 5);

    Events ev;
    CHECK(Decode(FromHex(Vector("events_hit_kill")), ev));
    CHECK(ev.events.size() == 2 && ev.events.back().kind == EventKind::Kill && ev.events.front().amount == 45);
}

TEST(packets_reject_malformed_bodies)
{
    std::vector<uint8_t> body = Encode(SampleShots());
    ShotBatch b;
    std::vector<uint8_t> shortBody(body.begin(), body.end() - 1);
    CHECK(!Decode(shortBody, b));
    std::vector<uint8_t> longBody = body;
    longBody.push_back(0);
    CHECK(!Decode(longBody, b));
    std::vector<uint8_t> wrongVersion = body;
    wrongVersion[0] = 2;
    CHECK(!Decode(wrongVersion, b));
    std::vector<uint8_t> badStance = body;
    badStance[2 + 7] = 9;
    CHECK(!Decode(badStance, b));
    std::vector<uint8_t> tooMany = {kVersion, uint8_t(kMaxShotsPerBatch + 1)};
    CHECK(!Decode(tooMany, b));
    // A NaN direction.
    std::vector<uint8_t> nan = body;
    nan[2 + 28] = 0x00; nan[2 + 29] = 0x00; nan[2 + 30] = 0xc0; nan[2 + 31] = 0x7f;
    CHECK(!Decode(nan, b));

    State st;
    std::vector<uint8_t> badGait = Encode(SampleState());
    badGait[2] = 9;
    CHECK(!Decode(badGait, st));
}
