/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RAIDTOCSTRATEGY_H
#define PLAYERBOTS_RAIDTOCSTRATEGY_H

#include "MovementActions.h"
#include "NamedObjectContext.h"
#include "Strategy.h"
#include "Trigger.h"

class RaidTocStrategy : public Strategy
{
public:
    RaidTocStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "toc"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

class TocEssenceTrigger : public Trigger
{
public:
    TocEssenceTrigger(PlayerbotAI* botAI) : Trigger(botAI, "toc twin essence") {}
    bool IsActive() override;
};

class TocEssenceAction : public MovementAction
{
public:
    TocEssenceAction(PlayerbotAI* botAI) : MovementAction(botAI, "toc twin essence") {}
    bool isUseful() override;
    bool Execute(Event event) override;
};

class RaidTocActionContext : public NamedObjectContext<Action>
{
public:
    RaidTocActionContext() { creators["toc twin essence"] = &RaidTocActionContext::Essence; }

private:
    static Action* Essence(PlayerbotAI* botAI) { return new TocEssenceAction(botAI); }
};

class RaidTocTriggerContext : public NamedObjectContext<Trigger>
{
public:
    RaidTocTriggerContext() { creators["toc twin essence"] = &RaidTocTriggerContext::Essence; }

private:
    static Trigger* Essence(PlayerbotAI* botAI) { return new TocEssenceTrigger(botAI); }
};

#endif
