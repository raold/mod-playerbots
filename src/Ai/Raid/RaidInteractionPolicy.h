/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RAIDINTERACTIONPOLICY_H
#define PLAYERBOTS_RAIDINTERACTIONPOLICY_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace RaidInteractions
{
enum class Colour
{
    None,
    Light,
    Dark
};

inline constexpr std::array<std::uint32_t, 4> LightEssence = {65686, 67222, 67223, 67224};
inline constexpr std::array<std::uint32_t, 4> DarkEssence = {65684, 67176, 67177, 67178};
inline constexpr std::array<std::uint32_t, 4> LightTouch = {65950, 67296, 67297, 67298};
inline constexpr std::array<std::uint32_t, 4> DarkTouch = {66001, 67281, 67282, 67283};
inline constexpr std::array<std::uint32_t, 4> LightVortex = {66046, 67206, 67207, 67208};
inline constexpr std::array<std::uint32_t, 4> DarkVortex = {66058, 67182, 67183, 67184};

inline Colour VortexColour(std::uint32_t spell)
{
    switch (spell)
    {
        case 66046:
        case 67206:
        case 67207:
        case 67208:
            return Colour::Light;
        case 66058:
        case 67182:
        case 67183:
        case 67184:
            return Colour::Dark;
        default:
            return Colour::None;
    }
}

inline Colour WantedEssence(Colour current, std::uint32_t targetEntry, Colour vortex, Colour touch)
{
    if (vortex != Colour::None)
        return vortex;
    if (touch != Colour::None)
        return touch;
    // Absorb the vortex's colour, but damage the opposite-colour twin.
    if (targetEntry == 34497)
        return Colour::Dark;
    if (targetEntry == 34496)
        return Colour::Light;
    return current == Colour::None ? Colour::Light : current;
}

struct Healer
{
    std::uint64_t guid;
    bool druid;
};

// Whether this healer is one of Valithria's reserved raid healers (the first one or two,
// druids first). A lone healer is reserved; the rest are dragon healers who may portal.
inline std::optional<bool> HealRaid(std::uint64_t guid, std::vector<Healer> healers)
{
    std::sort(healers.begin(), healers.end(), [](Healer const& left, Healer const& right)
              { return left.druid != right.druid ? left.druid : left.guid < right.guid; });
    auto const found =
        std::find_if(healers.begin(), healers.end(), [guid](Healer const& healer) { return healer.guid == guid; });
    if (found == healers.end())
        return std::nullopt;
    if (healers.size() == 1)
        return true;
    std::size_t const reserve = std::min(healers.size() - 1, healers.size() > 3 ? std::size_t{2} : std::size_t{1});
    return static_cast<std::size_t>(found - healers.begin()) < reserve;
}

// A reserved raid healer spends its casts on the dragon while everyone it could heal is at
// or above this health and it keeps this much mana for the raid; otherwise ordinary party
// healing takes over.
inline constexpr float RaidHealerDragonFloorPct = 80.0f;
inline constexpr float RaidHealerDragonManaPct = 40.0f;

// lowestPct: the lowest health % among the group members this healer could heal (100 when
// nobody is hurt). Dragon healers always heal her; raid healers only while the raid is fine.
inline bool HealsDragon(bool raidHealer, float lowestPct, float manaPct)
{
    return !raidHealer || (lowestPct >= RaidHealerDragonFloorPct && manaPct >= RaidHealerDragonManaPct);
}

// What a Valithria healer is doing, in the order the strategy's triggers outrank each other
// (zombie kite, portal, dream cloud, dragon heal; low health or a rot worm stops the heal).
struct ValithriaDutyInputs
{
    bool alive = true;
    bool dream = false;
    bool kited = false;
    bool wormed = false;
    bool portal = false;
    bool lowHealth = false;
    bool dragon = false;
};

inline char const* ValithriaDuty(ValithriaDutyInputs const& in)
{
    if (!in.alive)
        return "dead";
    if (in.dream)
        return "dream";
    if (in.kited)
        return "kite";
    if (in.portal && !in.wormed)
        return "portal";
    if (in.lowHealth)
        return "low_hp";
    if (in.wormed)
        return "worm";
    return in.dragon ? "dragon" : "raid";
}

// The newest executed action in an engine trace ("|A:<name> - OK|..."), or empty.
inline std::string_view LastExecutedAction(std::string_view trace)
{
    constexpr std::string_view head = "|A:";
    constexpr std::string_view tail = " - OK";
    std::size_t end = trace.size();
    while (end > 0)
    {
        std::size_t const start = trace.rfind(head, end - 1);
        if (start == std::string_view::npos)
            break;
        std::size_t const stop = std::min(trace.find('|', start + 1), end);
        std::string_view const entry = trace.substr(start + head.size(), stop - start - head.size());
        if (entry.size() > tail.size() && entry.substr(entry.size() - tail.size()) == tail)
            return entry.substr(0, entry.size() - tail.size());
        end = start;
    }
    return {};
}

inline bool CanUsePortal(std::optional<bool> raidHealer) { return raidHealer.has_value() && !*raidHealer; }

inline bool AffordableHeal(std::int32_t cost, std::uint32_t mana)
{
    return cost >= 0 && static_cast<std::uint32_t>(cost) <= mana;
}

template <class Ready>
std::uint32_t FirstReadyHeal(std::vector<std::uint32_t> const& choices, Ready ready)
{
    for (std::uint32_t spell : choices)
        if (spell && ready(spell))
            return spell;
    return 0;
}
}  // namespace RaidInteractions

#endif
