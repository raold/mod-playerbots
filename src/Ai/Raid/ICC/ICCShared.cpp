/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ICCShared.h"

#include <algorithm>
#include <list>
#include <map>
#include <string>

#include "Engine.h"
#include "ICCActions.h"
#include "ICCScripts.h"
#include "ICCTriggers.h"
#include "InstanceScript.h"
#include "LastSpellCastValue.h"
#include "Playerbots.h"
#include "RaidInteractionPolicy.h"
#include "StringFormat.h"
#include "ThreatManager.h"
#include "Timer.h"

namespace
{
    std::vector<Creature*> IccGetCreaturesByEntry(WorldObject* searcher, uint32 entry, float range)
    {
        std::list<Creature*> raw;
        searcher->GetCreatureListWithEntryInGrid(raw, entry, range);

        std::vector<Creature*> out;
        out.reserve(raw.size());
        for (Creature* c : raw)
            if (c && c->IsAlive())
                out.push_back(c);

        return out;
    }
}

std::vector<Creature*> IccGetCreaturesByEntries(WorldObject* searcher, std::initializer_list<uint32> entries,
                                                float range)
{
    std::vector<Creature*> out;
    for (uint32 entry : entries)
    {
        auto part = IccGetCreaturesByEntry(searcher, entry, range);
        out.insert(out.end(), part.begin(), part.end());
    }
    std::sort(out.begin(), out.end(), [](Creature const* a, Creature const* b) { return a->GetGUID() < b->GetGUID(); });
    out.erase(std::unique(out.begin(), out.end()), out.end());

    return out;
}

bool IccCastClassTaunt(Player* bot, PlayerbotAI* botAI, Unit* target)
{
    if (!target || !target->IsAlive())
        return false;

    uint32 const now = getMSTime();
    uint32& last = IcecrownHelpers::IccState(bot->GetInstanceId()).lastTauntMs[bot->GetGUID()];
    if (last != 0 && getMSTimeDiff(last, now) < 500)
        return false;
    last = now;

    bool const threatReset = botAI->HasCheat(BotCheatMask::raid);

    if (threatReset && target->GetVictim() != bot)
    {
        ThreatManager& mgr = target->GetThreatMgr();
        mgr.AddThreat(bot, 1000000.0f, nullptr, true, true);
        mgr.FixateTarget(bot);
    }

    switch (bot->getClass())
    {
        case CLASS_PALADIN:
            if (threatReset)
                bot->RemoveSpellCooldown(SPELL_TAUNT_PALADIN, true);
            if (botAI->CastSpell("hand of reckoning", target))
                return true;
            break;
        case CLASS_DEATH_KNIGHT:
            if (threatReset)
                bot->RemoveSpellCooldown(SPELL_TAUNT_DK, true);
            if (botAI->CastSpell("dark command", target))
                return true;
            break;
        case CLASS_DRUID:
            if (threatReset)
                bot->RemoveSpellCooldown(SPELL_TAUNT_DRUID, true);
            if (botAI->CastSpell("growl", target))
                return true;
            break;
        case CLASS_WARRIOR:
            if (threatReset)
                bot->RemoveSpellCooldown(SPELL_TAUNT_WARRIOR, true);
            if (botAI->CastSpell("taunt", target))
                return true;
            break;
        default:
            break;
    }

    if (botAI->CastSpell("shoot", target) || botAI->CastSpell("throw", target))
        return true;

    return false;
}

std::optional<bool> IccTryClassCC(Player* bot, PlayerbotAI* botAI, Unit* target)
{
    if (!target || !target->IsAlive())
        return std::nullopt;

    char const* spell = nullptr;
    switch (bot->getClass())
    {
        case CLASS_MAGE:         spell = "Frost Nova"; break;
        case CLASS_DRUID:        spell = "Entangling Roots"; break;
        case CLASS_PALADIN:      spell = "Hammer of Justice"; break;
        case CLASS_WARRIOR:      spell = "Hamstring"; break;
        case CLASS_HUNTER:       spell = "Concussive Shot"; break;
        case CLASS_ROGUE:        spell = "Kidney Shot"; break;
        case CLASS_SHAMAN:       spell = "Frost Shock"; break;
        case CLASS_DEATH_KNIGHT: spell = "Chains of Ice"; break;
        case CLASS_PRIEST:       spell = "Psychic Scream"; break;
        case CLASS_WARLOCK:      spell = "Fear"; break;
        default:                 return std::nullopt;
    }

    if (botAI->HasAura(spell, target))
        return std::nullopt;

    uint32 const now = getMSTime();
    uint32& last = IcecrownHelpers::IccState(bot->GetInstanceId()).lastCcMs[bot->GetGUID()];
    if (last != 0 && getMSTimeDiff(last, now) < 500)
        return std::nullopt;
    last = now;

    return botAI->CastSpell(spell, target);
}

void IccEnsureIconOn(Player* bot, PlayerbotAI* botAI, int8 icon, Unit* target)
{
    if (!target)
        return;

    Group* group = bot->GetGroup();
    if (!group)
        return;

    Unit* current = botAI->GetUnit(group->GetTargetIcon(icon));
    if (!current || !current->IsAlive() || current != target)
        group->SetTargetIcon(icon, bot->GetGUID(), target->GetGUID());
}

void IccApplyHeroicBuffToMember(Player* member, bool applyPainSupp, bool applyNoThreat)
{
    uint32 const now = getMSTime();
    uint32& last = IcecrownHelpers::IccState(member->GetInstanceId()).lastBuffMs[member->GetGUID()];
    if (last != 0 && getMSTimeDiff(last, now) < 500)
        return;
    last = now;

    if (!member->HasAura(SPELL_EXPERIENCED))
        member->AddAura(SPELL_EXPERIENCED, member);

    if (!member->HasAura(SPELL_AGEIS_OF_DALARAN))
        member->AddAura(SPELL_AGEIS_OF_DALARAN, member);

    if (applyPainSupp && !member->HasAura(SPELL_PAIN_SUPPRESION))
        member->AddAura(SPELL_PAIN_SUPPRESION, member);

    if (applyNoThreat && !PlayerbotAI::IsTank(member) && !member->HasAura(SPELL_NO_THREAT))
        member->AddAura(SPELL_NO_THREAT, member);
}

void IccStripExperienced(Player* bot)
{
    if (bot->HasAura(SPELL_EXPERIENCED))
        bot->RemoveAura(SPELL_EXPERIENCED);
}

bool IccBossCastingBlisteringCold(Unit* boss)
{
    return boss && (boss->FindCurrentSpellBySpellId(SPELL_BLISTERING_COLD1) ||
                    boss->FindCurrentSpellBySpellId(SPELL_BLISTERING_COLD2) ||
                    boss->FindCurrentSpellBySpellId(SPELL_BLISTERING_COLD3) ||
                    boss->FindCurrentSpellBySpellId(SPELL_BLISTERING_COLD4));
}

bool IccBossCastingRemorselessWinter(Unit* boss)
{
    if (!boss || !boss->HasUnitState(UNIT_STATE_CASTING))
        return false;

    return boss->FindCurrentSpellBySpellId(SPELL_REMORSELESS_WINTER1) ||
           boss->FindCurrentSpellBySpellId(SPELL_REMORSELESS_WINTER2) ||
           boss->FindCurrentSpellBySpellId(SPELL_REMORSELESS_WINTER3) ||
           boss->FindCurrentSpellBySpellId(SPELL_REMORSELESS_WINTER4) ||
           boss->FindCurrentSpellBySpellId(SPELL_REMORSELESS_WINTER5) ||
           boss->FindCurrentSpellBySpellId(SPELL_REMORSELESS_WINTER6) ||
           boss->FindCurrentSpellBySpellId(SPELL_REMORSELESS_WINTER7) ||
           boss->FindCurrentSpellBySpellId(SPELL_REMORSELESS_WINTER8);
}

bool IccBossHasRemorselessWinter(Unit* boss)
{
    if (!boss)
        return false;

    if (boss->HasAura(SPELL_REMORSELESS_WINTER1) || boss->HasAura(SPELL_REMORSELESS_WINTER2) ||
        boss->HasAura(SPELL_REMORSELESS_WINTER3) || boss->HasAura(SPELL_REMORSELESS_WINTER4) ||
        boss->HasAura(SPELL_REMORSELESS_WINTER5) || boss->HasAura(SPELL_REMORSELESS_WINTER6) ||
        boss->HasAura(SPELL_REMORSELESS_WINTER7) || boss->HasAura(SPELL_REMORSELESS_WINTER8))
        return true;

    return IccBossCastingRemorselessWinter(boss);
}

bool IccAnyGroupMemberHasAura(Player* bot, uint32 spellId)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member->IsAlive() && member->HasAura(spellId))
            return true;
    }

    return false;
}

bool IccAnyBloodPrincePresent(PlayerbotAI* botAI)
{
    auto* context = botAI->GetAiObjectContext();
    return AI_VALUE2(Unit*, "find target", "prince valanar") ||
           AI_VALUE2(Unit*, "find target", "prince taldaram") ||
           AI_VALUE2(Unit*, "find target", "prince keleseth");
}

namespace
{
std::vector<RaidInteractions::Healer> ValithriaHealers(Player* bot)
{
    std::vector<RaidInteractions::Healer> healers;
    Group* group = bot->GetGroup();
    if (!group)
        return healers;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld() || !member->IsAlive() || member->GetMap() != bot->GetMap() ||
            IsSelfBot(member) || !PlayerbotAI::IsHeal(member))
            continue;
        // Dream healers remain in the allocation while in the other phase.
        healers.push_back({member->GetGUID().GetRawValue(), member->getClass() == CLASS_DRUID});
    }
    return healers;
}

// Lowest health % of the living group members in this healer's phase within the radius the stock
// "party member to heal" search covers (twice the heal distance); 100 when nobody there is hurt.
float ValithriaLowestNearby(Player* bot)
{
    float lowest = 100.0f;
    Group* group = bot->GetGroup();
    if (!group)
        return lowest;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld() || !member->IsAlive() || member->GetMap() != bot->GetMap() ||
            !member->InSamePhase(bot) || !bot->IsWithinDistInMap(member, sPlayerbotAIConfig.HealDistance * 2.0f))
            continue;
        lowest = std::min(lowest, member->GetHealthPct());
    }
    return lowest;
}

bool ValithriaChasedBy(Player* bot, uint32 entry)
{
    Creature* add = bot->FindNearestCreature(entry, 100.0f);
    return add && add->GetVictim() == bot;
}
}  // namespace

std::optional<bool> IccValithriaShouldHealRaid(Player* bot)
{
    return RaidInteractions::HealRaid(bot->GetGUID().GetRawValue(), ValithriaHealers(bot));
}

std::optional<bool> IccValithriaHealsDragon(Player* bot)
{
    auto const raidHealer = IccValithriaShouldHealRaid(bot);
    if (!raidHealer.has_value())
        return std::nullopt;
    if (!*raidHealer)
        return true;
    return RaidInteractions::HealsDragon(true, ValithriaLowestNearby(bot), bot->GetPowerPct(POWER_MANA));
}

bool IccValithriaOnDragon(Player* bot)
{
    if (!bot->IsAlive() || !PlayerbotAI::IsHeal(bot) || bot->HasAura(SPELL_DREAM_STATE) || bot->HealthBelowPct(50) ||
        !bot->FindNearestCreature(NPC_VALITHRIA_DREAMWALKER, 100.0f))
        return false;
    if (ValithriaChasedBy(bot, NPC_ROT_WORM) || ValithriaChasedBy(bot, NPC_BLISTERING_ZOMBIE))
        return false;
    return IccValithriaHealsDragon(bot).value_or(false);
}

void IccValithriaNoteHeal(Player* bot, char const* outcome, bool cast)
{
    auto& note = IcecrownHelpers::IccState(bot->GetMap()->GetInstanceId()).vtHealNotes[bot->GetGUID()];
    note.outcome = outcome;
    if (cast)
        ++note.casts;
}

bool IccValithriaCanUsePortal(Player* bot)
{
    return RaidInteractions::CanUsePortal(bot->GetGUID().GetRawValue(), ValithriaHealers(bot));
}

namespace
{
constexpr uint32 ICC_BOSS_VALITHRIA = 10;  // DATA_VALITHRIA_DREAMWALKER in the core's icecrown_citadel.h
constexpr uint32 VALITHRIA_WATCH_MS = 5000;

char const* ValithriaHealerClass(uint8 playerClass)
{
    switch (playerClass)
    {
        case CLASS_DRUID:
            return "druid";
        case CLASS_SHAMAN:
            return "shaman";
        case CLASS_PRIEST:
            return "priest";
        case CLASS_PALADIN:
            return "paladin";
        default:
            return "other";
    }
}

char const* ValithriaStrategyFlag(PlayerbotAI* ai)
{
    bool const combat = ai->HasStrategy("icc", BOT_STATE_COMBAT);
    bool const nonCombat = ai->HasStrategy("icc", BOT_STATE_NON_COMBAT);
    return combat ? (nonCombat ? "1" : "c") : (nonCombat ? "n" : "0");
}

// "<spell>><target>@<age>s": target "valithria", "p<guid>" for a player, "c<entry>" for a creature.
std::string ValithriaLastCast(PlayerbotAI* ai, Creature* valithria)
{
    LastSpellCast const& last = ai->GetAiObjectContext()->GetValue<LastSpellCast&>("last spell cast")->Get();
    if (!last.id)
        return "-";
    std::string target = "-";
    if (last.target == valithria->GetGUID())
        target = "valithria";
    else if (last.target.IsPlayer())
        target = "p" + std::to_string(last.target.GetCounter());
    else if (!last.target.IsEmpty())
        target = "c" + std::to_string(last.target.GetEntry());
    return std::to_string(last.id) + ">" + target + "@" + std::to_string(time(nullptr) - last.timer) + "s";
}

bool ValithriaChases(std::list<Creature*> const& adds, Player* member)
{
    return std::any_of(adds.begin(), adds.end(),
                       [member](Creature* add) { return add && add->IsAlive() && add->GetVictim() == member; });
}
}  // namespace

void IccValithriaWatch(Creature* valithria)
{
    InstanceScript* instance = valithria->GetInstanceScript();
    Map* map = valithria->GetMap();
    if (!instance || !map)
        return;
    uint32 const instanceId = map->GetInstanceId();
    IcecrownHelpers::IccInstanceState& state = IcecrownHelpers::IccState(instanceId);
    IcecrownHelpers::ValithriaWatch& watch = state.vtWatch;
    EncounterState const encounter = instance->GetBossState(ICC_BOSS_VALITHRIA);
    if (encounter != IN_PROGRESS || !valithria->IsAlive())
    {
        if (watch.engaged)
            LOG_INFO("playerbots", "valithria_watch instance={} end={} hp={:.1f}", instanceId,
                     encounter == DONE ? "done" : "reset", valithria->GetHealthPct());
        watch = IcecrownHelpers::ValithriaWatch{};
        state.vtHealNotes.clear();
        return;
    }

    uint32 const now = getMSTime();
    if (watch.engaged && getMSTimeDiff(watch.lastMs, now) < VALITHRIA_WATCH_MS)
        return;
    watch.engaged = true;
    watch.lastMs = now;

    std::list<Creature*> zombies;
    std::list<Creature*> worms;
    valithria->GetCreatureListWithEntryInGrid(zombies, NPC_BLISTERING_ZOMBIE, 100.0f);
    valithria->GetCreatureListWithEntryInGrid(worms, NPC_ROT_WORM, 100.0f);
    bool const portalUp = !IccGetCreaturesByEntries(valithria,
                                                    {NPC_DREAM_PORTAL, NPC_DREAM_PORTAL_PRE_EFFECT,
                                                     NPC_NIGHTMARE_PORTAL, NPC_NIGHTMARE_PORTAL_PRE_EFFECT},
                                                    100.0f)
                               .empty();

    std::vector<Player*> healers;
    for (auto const& ref : map->GetPlayers())
    {
        Player* member = ref.GetSource();
        if (member && member->IsInWorld() && GET_PLAYERBOT_AI(member) && PlayerbotAI::IsHeal(member) &&
            member->GetDistance2d(valithria) <= 100.0f)
            healers.push_back(member);
    }
    std::sort(healers.begin(), healers.end(),
              [](Player const* left, Player const* right) { return left->GetGUID() < right->GetGUID(); });

    std::map<std::string, uint32> duties;
    uint32 casts = 0;
    std::vector<std::string> lines;
    for (Player* member : healers)
    {
        PlayerbotAI* ai = GET_PLAYERBOT_AI(member);
        bool const alive = member->IsAlive();
        auto const raidHealer = alive ? IccValithriaShouldHealRaid(member) : std::nullopt;
        Aura* twisted = ai->GetAura("Twisted Nightmares", member, false, true);
        RaidInteractions::ValithriaDutyInputs in;
        in.alive = alive;
        in.dream = member->HasAura(SPELL_DREAM_STATE);
        in.kited = ValithriaChases(zombies, member);
        in.wormed = ValithriaChases(worms, member);
        in.portal =
            portalUp && alive && (!twisted || twisted->GetStackAmount() < 25) && IccValithriaCanUsePortal(member);
        in.lowHealth = member->HealthBelowPct(50);
        in.dragon = alive && IccValithriaHealsDragon(member).value_or(false);
        char const* duty = RaidInteractions::ValithriaDuty(in);
        ++duties[duty];

        IcecrownHelpers::ValithriaHealNote& note = state.vtHealNotes[member->GetGUID()];
        casts += note.casts;
        Engine* engine = ai->GetCurrentEngine();
        std::string const trace = engine ? engine->GetLastAction() : std::string();
        std::string const did(RaidInteractions::LastExecutedAction(trace));
        lines.push_back(Acore::StringFormat(
            "valithria_healer instance={} guid={} class={} alloc={} now={} icc={} hp={:.0f} mana={:.0f} "
            "dist={:.1f} los={} casts={} heal={} last={} did=\"{}\"",
            instanceId, member->GetGUID().GetCounter(), ValithriaHealerClass(member->getClass()),
            !raidHealer.has_value() ? "-" : (*raidHealer ? "raid" : "boss"), duty, ValithriaStrategyFlag(ai),
            member->GetHealthPct(), member->GetPowerPct(POWER_MANA), member->GetDistance(valithria),
            member->IsWithinLOSInMap(valithria) ? 1 : 0, note.casts, note.outcome, ValithriaLastCast(ai, valithria),
            did.empty() ? "-" : did));
        note.casts = 0;
    }

    std::string summary;
    for (auto const& [duty, count] : duties)
        summary += " " + duty + "=" + std::to_string(count);
    LOG_INFO("playerbots", "valithria_watch instance={} diff={} hp={:.1f} healers={}{} casts={}", instanceId,
             uint32(map->GetDifficulty()), valithria->GetHealthPct(), healers.size(), summary, casts);
    for (std::string const& line : lines)
        LOG_INFO("playerbots", "{}", line);
}
