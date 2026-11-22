/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Creature.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Playerbots.h"
#include "RaidInteractionPolicy.h"
#include "RaidTocStrategy.h"
#include "Spell.h"
#include "WorldSession.h"

namespace
{
using RaidInteractions::Colour;
constexpr uint32 MAP_TRIAL_OF_THE_CRUSADER = 649;
constexpr uint32 BOSS_TWINS = 3;
constexpr uint32 DATA_LIGHTBANE = 11;
constexpr uint32 DATA_DARKBANE = 12;
constexpr uint32 NPC_LIGHT_ESSENCE = 34568;
constexpr uint32 NPC_DARK_ESSENCE = 34567;
constexpr float ESSENCE_SEARCH_RANGE = 120.0f;
constexpr float ESSENCE_CLICK_RANGE = 4.0f;

Colour CastingVortex(Creature* twin)
{
    if (!twin || !twin->IsAlive())
        return Colour::None;
    // The cast is followed by a five-second periodic aura. Keep that colour
    // until the final pulse rather than switching back at cast completion.
    for (uint32 spell : RaidInteractions::LightVortex)
        if (twin->HasAura(spell))
            return Colour::Light;
    for (uint32 spell : RaidInteractions::DarkVortex)
        if (twin->HasAura(spell))
            return Colour::Dark;
    for (CurrentSpellTypes slot : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
        if (Spell* spell = twin->GetCurrentSpell(slot))
            if (Colour const colour = RaidInteractions::VortexColour(spell->GetSpellInfo()->Id); colour != Colour::None)
                return colour;
    return Colour::None;
}

Colour NeededEssence(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!sPlayerbotAIConfig.TwinValkyrInteractions || bot->GetMapId() != MAP_TRIAL_OF_THE_CRUSADER || !bot->IsAlive() ||
        IsSelfBot(bot) || !bot->GetGroup() || !bot->GetGroup()->isRaidGroup() || bot->IsBeingTeleported() ||
        bot->GetVehicle() || bot->GetTransport() || ai->HasStrategy("stay", BOT_STATE_COMBAT) ||
        ai->HasStrategy("passive", BOT_STATE_COMBAT))
        return Colour::None;
    InstanceScript* instance = bot->GetInstanceScript();
    if (!instance ||
        (instance->GetBossState(BOSS_TWINS) != NOT_STARTED && instance->GetBossState(BOSS_TWINS) != IN_PROGRESS))
        return Colour::None;
    uint32 const difficulty = bot->GetMap()->GetDifficulty();
    if (difficulty >= RaidInteractions::LightEssence.size())
        return Colour::None;
    Creature* light = bot->GetMap()->GetCreature(instance->GetGuidData(DATA_LIGHTBANE));
    Creature* dark = bot->GetMap()->GetCreature(instance->GetGuidData(DATA_DARKBANE));
    if ((!light || !light->IsAlive() || !bot->IsWithinDistInMap(light, ESSENCE_SEARCH_RANGE)) &&
        (!dark || !dark->IsAlive() || !bot->IsWithinDistInMap(dark, ESSENCE_SEARCH_RANGE)))
        return Colour::None;
    Colour const current = bot->HasAura(RaidInteractions::LightEssence[difficulty])  ? Colour::Light
                           : bot->HasAura(RaidInteractions::DarkEssence[difficulty]) ? Colour::Dark
                                                                                     : Colour::None;
    Colour const touch = bot->HasAura(RaidInteractions::LightTouch[difficulty])  ? Colour::Light
                         : bot->HasAura(RaidInteractions::DarkTouch[difficulty]) ? Colour::Dark
                                                                                 : Colour::None;
    Colour vortex = CastingVortex(light);
    if (vortex == Colour::None)
        vortex = CastingVortex(dark);
    Unit* target = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    Colour const wanted = RaidInteractions::WantedEssence(current, target ? target->GetEntry() : 0, vortex, touch);
    return wanted == current ? Colour::None : wanted;
}
}  // namespace

void RaidTocStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("toc twin essence", {NextAction("toc twin essence", ACTION_RAID + 8)}));
}

bool TocEssenceTrigger::IsActive() { return NeededEssence(botAI) != Colour::None; }

bool TocEssenceAction::isUseful() { return NeededEssence(botAI) != Colour::None; }

bool TocEssenceAction::Execute(Event /*event*/)
{
    Colour const wanted = NeededEssence(botAI);
    if (wanted == Colour::None)
        return false;
    Creature* essence =
        bot->FindNearestCreature(wanted == Colour::Light ? NPC_LIGHT_ESSENCE : NPC_DARK_ESSENCE, ESSENCE_SEARCH_RANGE);
    if (!essence || !essence->HasNpcFlag(UNIT_NPC_FLAG_GOSSIP) || !essence->InSamePhase(bot))
        return false;
    if (!bot->IsWithinDistInMap(essence, ESSENCE_CLICK_RANGE) || !bot->IsWithinLOSInMap(essence))
    {
        if (bot->isMoving() &&
            IsDuplicateMove(essence->GetPositionX(), essence->GetPositionY(), essence->GetPositionZ()))
            return true;
        return MoveTo(bot->GetMapId(), essence->GetPositionX(), essence->GetPositionY(), essence->GetPositionZ(), false,
                      false, false, false, MovementPriority::MOVEMENT_FORCED);
    }
    bot->InterruptNonMeleeSpells(false);
    bot->StopMoving();
    bot->SetFacingToObject(essence);
    WorldPacket hello(CMSG_GOSSIP_HELLO);
    hello << essence->GetGUID();
    bot->GetSession()->HandleGossipHelloOpcode(hello);
    return true;
}
