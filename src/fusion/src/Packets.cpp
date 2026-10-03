/*
 * Azeroth Warfare: the fusion rules. GPL-2.0-or-later, see Common.h.
 */

#include "fusion/Packets.h"

#include <cstring>

namespace Fusion::Packets
{
    namespace
    {
        struct Writer
        {
            std::vector<uint8_t> out;

            void U8(uint8_t v) { out.push_back(v); }
            void U16(uint16_t v)
            {
                for (int i = 0; i < 2; ++i)
                    out.push_back(uint8_t(v >> (8 * i)));
            }
            void U32(uint32_t v)
            {
                for (int i = 0; i < 4; ++i)
                    out.push_back(uint8_t(v >> (8 * i)));
            }
            void U64(uint64_t v)
            {
                for (int i = 0; i < 8; ++i)
                    out.push_back(uint8_t(v >> (8 * i)));
            }
            void F32(float f)
            {
                uint32_t bits;
                std::memcpy(&bits, &f, 4);
                U32(bits);
            }
            void V3(Vec3 v)
            {
                F32(v.x);
                F32(v.y);
                F32(v.z);
            }
        };

        struct Reader
        {
            std::vector<uint8_t> const& in;
            size_t pos = 0;
            bool ok = true;

            bool Need(size_t n)
            {
                if (pos + n > in.size())
                    ok = false;
                return ok;
            }
            uint8_t U8()
            {
                if (!Need(1))
                    return 0;
                return in[pos++];
            }
            uint16_t U16()
            {
                if (!Need(2))
                    return 0;
                uint16_t v = uint16_t(in[pos] | (in[pos + 1] << 8));
                pos += 2;
                return v;
            }
            uint32_t U32()
            {
                if (!Need(4))
                    return 0;
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= uint32_t(in[pos + i]) << (8 * i);
                pos += 4;
                return v;
            }
            uint64_t U64()
            {
                if (!Need(8))
                    return 0;
                uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v |= uint64_t(in[pos + i]) << (8 * i);
                pos += 8;
                return v;
            }
            float F32()
            {
                uint32_t bits = U32();
                float f;
                std::memcpy(&f, &bits, 4);
                return f;
            }
            Vec3 V3()
            {
                float x = F32();
                float y = F32();
                float z = F32();
                return {x, y, z};
            }
            bool Done() const { return ok && pos == in.size(); }
        };

        bool FiniteVec(Vec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    }

    std::vector<uint8_t> Encode(ShotBatch const& m)
    {
        Writer w;
        w.U8(kVersion);
        size_t n = std::min<size_t>(m.shots.size(), kMaxShotsPerBatch);
        w.U8(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Shot const& s = m.shots[i];
            w.U32(s.timeMs);
            w.U16(s.weaponId);
            w.U8(s.flags);
            w.U8(uint8_t(s.stance));
            w.U64(s.targetGuid);
            w.V3(s.origin);
            w.V3(s.direction);
        }
        return w.out;
    }

    std::vector<uint8_t> Encode(State const& m)
    {
        Writer w;
        w.U8(kVersion);
        w.U8(uint8_t(m.stance));
        w.U8(uint8_t(m.gait));
        w.U8(m.flags);
        w.U16(m.heldWeaponId);
        w.U8(m.loadedAmmo);
        return w.out;
    }

    std::vector<uint8_t> Encode(Events const& m)
    {
        Writer w;
        w.U8(kVersion);
        size_t n = std::min<size_t>(m.events.size(), kMaxEventsPerPacket);
        w.U8(uint8_t(n));
        for (size_t i = 0; i < n; ++i)
        {
            Event const& e = m.events[i];
            w.U8(uint8_t(e.kind));
            w.U8(e.flags);
            w.U64(e.guid);
            w.U32(e.amount);
            w.U16(e.extra);
        }
        return w.out;
    }

    bool Decode(std::vector<uint8_t> const& body, ShotBatch& out)
    {
        Reader r{body};
        out.shots.clear();
        if (r.U8() != kVersion)
            return false;
        uint8_t n = r.U8();
        if (!r.ok || n > kMaxShotsPerBatch)
            return false;
        for (uint8_t i = 0; i < n; ++i)
        {
            Shot s;
            s.timeMs = r.U32();
            s.weaponId = r.U16();
            s.flags = r.U8();
            uint8_t stance = r.U8();
            s.targetGuid = r.U64();
            s.origin = r.V3();
            s.direction = r.V3();
            if (!r.ok || stance > uint8_t(Stance::Slide) || !FiniteVec(s.origin) || !FiniteVec(s.direction))
                return false;
            s.stance = Stance(stance);
            out.shots.push_back(s);
        }
        return r.Done();
    }

    bool Decode(std::vector<uint8_t> const& body, State& out)
    {
        Reader r{body};
        if (r.U8() != kVersion)
            return false;
        uint8_t stance = r.U8();
        uint8_t gait = r.U8();
        out.flags = r.U8();
        out.heldWeaponId = r.U16();
        out.loadedAmmo = r.U8();
        if (!r.Done() || stance > uint8_t(Stance::Slide) || gait > uint8_t(Gait::Backpedal))
            return false;
        out.stance = Stance(stance);
        out.gait = Gait(gait);
        return true;
    }

    bool Decode(std::vector<uint8_t> const& body, Events& out)
    {
        Reader r{body};
        out.events.clear();
        if (r.U8() != kVersion)
            return false;
        uint8_t n = r.U8();
        if (!r.ok || n > kMaxEventsPerPacket)
            return false;
        for (uint8_t i = 0; i < n; ++i)
        {
            Event e;
            uint8_t kind = r.U8();
            e.flags = r.U8();
            e.guid = r.U64();
            e.amount = r.U32();
            e.extra = r.U16();
            if (!r.ok || kind > uint8_t(EventKind::Revived))
                return false;
            e.kind = EventKind(kind);
            out.events.push_back(e);
        }
        return r.Done();
    }

    std::string ToHex(std::vector<uint8_t> const& bytes)
    {
        static char const digits[] = "0123456789abcdef";
        std::string s;
        s.reserve(bytes.size() * 2);
        for (uint8_t b : bytes)
        {
            s.push_back(digits[b >> 4]);
            s.push_back(digits[b & 15]);
        }
        return s;
    }

    std::vector<uint8_t> FromHex(std::string const& hex)
    {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        std::vector<uint8_t> out;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            int hi = nib(hex[i]), lo = nib(hex[i + 1]);
            if (hi < 0 || lo < 0)
                return {};
            out.push_back(uint8_t(hi << 4 | lo));
        }
        return out;
    }
}
