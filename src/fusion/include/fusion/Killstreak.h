/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#ifndef FUSION_KILLSTREAK_H
#define FUSION_KILLSTREAK_H

#include "fusion/Common.h"

#include <map>
#include <optional>
#include <vector>

namespace Fusion
{
    // What a kill was worth toward a streak (design doc, section 7 and the streak rules).
    enum class KillKind : uint8_t
    {
        Player,
        Creature,
        EliteCreature,
        GrayCreature,   // far below the killer: worth nothing
    };

    uint32_t StreakValue(KillKind k);

    struct StreakReward
    {
        uint32_t id = 0;
        uint32_t kills = 0;     // the streak that earns it
        bool aerial = false;    // counts against the airspace limit
    };

    // A player's streak: kills since the last death, earned rewards waiting to be called in.
    class Streak
    {
    public:
        // The player's chosen rewards (three, from the Armory), and whether Hardline is on.
        void SetLoadout(std::vector<StreakReward> rewards, bool hardline);

        // Count a kill; returns the rewards that kill just earned.
        std::vector<StreakReward> AddKill(KillKind k);
        // Death, or logging out, ends the streak; earned-but-unused rewards are kept until used.
        void Reset() { m_count = 0; }
        uint32_t Count() const { return m_count; }
        std::vector<StreakReward> const& Earned() const { return m_earned; }
        // Spend an earned reward; false if it isn't held.
        bool Use(uint32_t rewardId);

    private:
        std::vector<StreakReward> m_rewards;
        std::vector<StreakReward> m_earned;
        uint32_t m_count = 0;
        bool m_hardline = false;
    };

    // The airspace limit: at most a few aerial streaks over any 200-yard area at once, as MW2
    // allows one helicopter at a time; extras wait ("Airspace too crowded").
    class Airspace
    {
    public:
        static constexpr float kCellSize = 200.f;
        static constexpr uint32_t kMaxPerCell = 2;

        struct Cell
        {
            int32_t x = 0, y = 0;
            uint32_t map = 0;
            bool operator<(Cell const& o) const
            {
                return map != o.map ? map < o.map : (x != o.x ? x < o.x : y < o.y);
            }
        };

        static Cell CellAt(uint32_t map, float x, float y);

        // Try to launch; on success returns a ticket to release when the streak ends.
        std::optional<uint64_t> TryLaunch(uint32_t map, float x, float y);
        void Release(uint64_t ticket);
        uint32_t ActiveIn(uint32_t map, float x, float y) const;

    private:
        std::map<Cell, std::vector<uint64_t>> m_active;
        std::map<uint64_t, Cell> m_tickets;
        uint64_t m_next = 1;
    };
}

#endif
