/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include <iostream>
#include <string_view>

#include "../src/Ai/Raid/RaidInteractionPolicy.h"

namespace
{
int checks = 0;
int failures = 0;
void Check(bool success, int line)
{
    ++checks;
    if (!success)
    {
        ++failures;
        std::cerr << "Failed at line " << line << '\n';
    }
}
}  // namespace

#define CHECK(expression) Check(static_cast<bool>(expression), __LINE__)

int main()
{
    using namespace RaidInteractions;
    for (std::size_t difficulty = 0; difficulty < 4; ++difficulty)
    {
        CHECK(VortexColour(LightVortex[difficulty]) == Colour::Light);
        CHECK(VortexColour(DarkVortex[difficulty]) == Colour::Dark);
        CHECK(LightEssence[difficulty] != DarkEssence[difficulty]);
        CHECK(LightTouch[difficulty] != DarkTouch[difficulty]);
    }
    CHECK(VortexColour(0) == Colour::None);
    CHECK(VortexColour(66048) == Colour::None);  // A pulse is not a new casting window.
    for (Colour current : {Colour::None, Colour::Light, Colour::Dark})
        for (Colour touch : {Colour::None, Colour::Light, Colour::Dark})
            for (std::uint32_t target : {0u, 34496u, 34497u})
            {
                CHECK(WantedEssence(current, target, Colour::Light, touch) == Colour::Light);
                CHECK(WantedEssence(current, target, Colour::Dark, touch) == Colour::Dark);
            }
    CHECK(WantedEssence(Colour::None, 34497, Colour::None, Colour::None) == Colour::Dark);
    CHECK(WantedEssence(Colour::None, 34496, Colour::None, Colour::None) == Colour::Light);
    CHECK(WantedEssence(Colour::None, 0, Colour::None, Colour::None) == Colour::Light);
    CHECK(WantedEssence(Colour::Dark, 0, Colour::None, Colour::None) == Colour::Dark);
    CHECK(WantedEssence(Colour::Dark, 34497, Colour::None, Colour::Light) == Colour::Light);
    CHECK(WantedEssence(Colour::Light, 34496, Colour::None, Colour::Dark) == Colour::Dark);
    CHECK(!HealRaid(101, {}).has_value());
    CHECK(!CanUsePortal(101, {}));
    CHECK(HealRaid(101, {{101, true}}) == true);  // A lone healer is the raid's, and tops the dragon when it can.
    CHECK(!CanUsePortal(101, {{101, true}}));
    CHECK(HealRaid(101, {{101, true}, {102, false}}) == true);
    CHECK(HealRaid(102, {{101, true}, {102, false}}) == false);

    // Every roster/class mix retains a dragon healer and a surface healer;
    // input traversal order and the portal phase do not change assignments.
    for (std::uint64_t count = 1; count <= 5; ++count)
        for (std::uint32_t druids = 0; druids < (1u << count); ++druids)
        {
            std::vector<Healer> roster;
            for (std::uint64_t index = 0; index < count; ++index)
                roster.push_back({101 + index, (druids & (1u << index)) != 0});
            std::size_t surface = 0;
            std::size_t portal = 0;
            for (Healer const& healer : roster)
            {
                auto const assignment = HealRaid(healer.guid, roster);
                CHECK(assignment.has_value());
                surface += *assignment;
                portal += CanUsePortal(healer.guid, roster);
                auto reversed = roster;
                std::reverse(reversed.begin(), reversed.end());
                CHECK(HealRaid(healer.guid, reversed) == assignment);
            }
            CHECK(surface == (count > 3 ? 2u : 1u));
            CHECK(count == 1 || surface < count);
            CHECK(count == 1 ? portal == 0 : portal + surface == count);
            CHECK(!HealRaid(1, roster).has_value());
            CHECK(!CanUsePortal(1, roster));
        }

    // Dragon healers always heal her; raid healers only while nobody they could heal is hurt
    // and they keep mana for the raid.
    for (float lowest : {0.0f, 25.0f, 79.9f, 80.0f, 100.0f})
        for (float mana : {0.0f, 39.9f, 40.0f, 100.0f})
        {
            CHECK(HealsDragon(false, lowest, mana));
            CHECK(HealsDragon(true, lowest, mana) ==
                  (lowest >= RaidHealerDragonFloorPct && mana >= RaidHealerDragonManaPct));
        }
    // 25-man: six healers, one druid. The druid and the lowest guid heal the raid; on a quiet
    // raid every healer works on the dragon; with someone hurt, or the raid healers short of mana,
    // only the four dragon healers do.
    {
        std::vector<Healer> const six = {{106, false}, {105, false}, {104, true},
                                         {103, false}, {102, false}, {101, false}};
        std::size_t quiet = 0;
        std::size_t hurt = 0;
        std::size_t dry = 0;
        for (Healer const& healer : six)
        {
            bool const raid = *HealRaid(healer.guid, six);
            CHECK(raid == (healer.guid == 104 || healer.guid == 101));
            quiet += HealsDragon(raid, 100.0f, 100.0f);
            hurt += HealsDragon(raid, 50.0f, 100.0f);
            dry += HealsDragon(raid, 100.0f, 20.0f);
        }
        CHECK(quiet == 6);
        CHECK(hurt == 4);
        CHECK(dry == 4);
    }

    // The reported duty follows the strategy's trigger order.
    auto duty = [](auto set)
    {
        ValithriaDutyInputs in;
        set(in);
        return std::string_view(ValithriaDuty(in));
    };
    CHECK(duty([](ValithriaDutyInputs&) {}) == "raid");
    CHECK(duty([](ValithriaDutyInputs& in) { in.dragon = true; }) == "dragon");
    CHECK(duty(
              [](ValithriaDutyInputs& in)
              {
                  in.alive = false;
                  in.dream = true;
              }) == "dead");
    CHECK(duty(
              [](ValithriaDutyInputs& in)
              {
                  in.dream = true;
                  in.kited = true;
                  in.portal = true;
              }) == "dream");
    CHECK(duty(
              [](ValithriaDutyInputs& in)
              {
                  in.kited = true;
                  in.portal = true;
                  in.dragon = true;
              }) == "kite");
    CHECK(duty(
              [](ValithriaDutyInputs& in)
              {
                  in.portal = true;
                  in.lowHealth = true;
                  in.dragon = true;
              }) == "portal");
    CHECK(duty(
              [](ValithriaDutyInputs& in)
              {
                  in.wormed = true;
                  in.portal = true;
                  in.dragon = true;
              }) == "worm");
    CHECK(duty(
              [](ValithriaDutyInputs& in)
              {
                  in.lowHealth = true;
                  in.dragon = true;
              }) == "low_hp");

    // The engine trace's newest executed action, skipping failed and useless entries.
    CHECK(LastExecutedAction("").empty());
    CHECK(LastExecutedAction("|--- AI Tick ---|T:icc valithria heal").empty());
    CHECK(LastExecutedAction("|A:icc valithria heal - OK") == "icc valithria heal");
    CHECK(LastExecutedAction("|A:icc valithria heal - OK|A:holy light on party - FAILED|T:x") == "icc valithria heal");
    CHECK(LastExecutedAction("|A:a - OK|--- AI Tick ---|A:reach party member to heal - OK|A:b - USELESS") ==
          "reach party member to heal");
    CHECK(LastExecutedAction("A:not prefixed - OK").empty());
    CHECK(LastExecutedAction("|A: - OK").empty());

    // An absent talent, unknown rank or cooldown cannot swallow a usable heal.
    for (std::uint32_t readiness = 0; readiness < 8; ++readiness)
    {
        std::vector<std::uint32_t> inspected;
        auto const chosen = FirstReadyHeal({0, 1001, 1002, 1003},
                                           [&](std::uint32_t spell)
                                           {
                                               inspected.push_back(spell);
                                               return (readiness & (1u << (spell - 1001))) != 0;
                                           });
        std::uint32_t const expected = readiness & 1 ? 1001 : readiness & 2 ? 1002 : readiness & 4 ? 1003 : 0;
        CHECK(chosen == expected);
        CHECK(inspected.size() == (expected ? expected - 1000 : 3));
    }
    CHECK(AffordableHeal(0, 0));
    CHECK(AffordableHeal(40, 40));
    CHECK(!AffordableHeal(41, 40));
    CHECK(!AffordableHeal(-1, 40));
    CHECK(FirstReadyHeal({1001, 1002},
                         [](std::uint32_t spell) { return AffordableHeal(spell == 1001 ? 130 : 40, 50); }) == 1002);
    std::cout << checks - failures << " checks passed, " << failures << " failed\n";
    return failures ? 1 : 0;
}
