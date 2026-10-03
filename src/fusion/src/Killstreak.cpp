/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Killstreak.h"

namespace Fusion
{
    uint32_t StreakValue(KillKind k)
    {
        switch (k)
        {
            case KillKind::Player:        return 1;
            case KillKind::Creature:      return 1;
            case KillKind::EliteCreature: return 2;
            case KillKind::GrayCreature:  return 0;
        }
        return 0;
    }

    void Streak::SetLoadout(std::vector<StreakReward> rewards, bool hardline)
    {
        m_rewards = std::move(rewards);
        m_hardline = hardline;
    }

    std::vector<StreakReward> Streak::AddKill(KillKind k)
    {
        std::vector<StreakReward> earned;
        uint32_t value = StreakValue(k);
        if (value == 0)
            return earned;
        uint32_t before = m_count;
        m_count += value;
        for (auto const& r : m_rewards)
        {
            uint32_t need = r.kills;
            if (m_hardline && need > 1)
                --need;
            // An elite worth two can cross a threshold without landing on it.
            if (before < need && m_count >= need)
            {
                earned.push_back(r);
                m_earned.push_back(r);
            }
        }
        return earned;
    }

    bool Streak::Use(uint32_t rewardId)
    {
        for (auto it = m_earned.begin(); it != m_earned.end(); ++it)
        {
            if (it->id == rewardId)
            {
                m_earned.erase(it);
                return true;
            }
        }
        return false;
    }

    Airspace::Cell Airspace::CellAt(uint32_t map, float x, float y)
    {
        return Cell{int32_t(std::floor(x / kCellSize)), int32_t(std::floor(y / kCellSize)), map};
    }

    std::optional<uint64_t> Airspace::TryLaunch(uint32_t map, float x, float y)
    {
        Cell c = CellAt(map, x, y);
        auto& list = m_active[c];
        if (list.size() >= kMaxPerCell)
            return std::nullopt;
        uint64_t ticket = m_next++;
        list.push_back(ticket);
        m_tickets[ticket] = c;
        return ticket;
    }

    void Airspace::Release(uint64_t ticket)
    {
        auto t = m_tickets.find(ticket);
        if (t == m_tickets.end())
            return;
        auto& list = m_active[t->second];
        for (auto it = list.begin(); it != list.end(); ++it)
        {
            if (*it == ticket)
            {
                list.erase(it);
                break;
            }
        }
        m_tickets.erase(t);
    }

    uint32_t Airspace::ActiveIn(uint32_t map, float x, float y) const
    {
        auto it = m_active.find(CellAt(map, x, y));
        return it == m_active.end() ? 0 : uint32_t(it->second.size());
    }
}
