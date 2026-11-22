/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ICCVALUECONTEXT_H
#define PLAYERBOTS_ICCVALUECONTEXT_H

#include <optional>

#include "NamedObjectContext.h"
#include "ObjectGuid.h"
#include "Value.h"

struct IccValithriaHealing
{
    std::optional<bool> RaidHealer;
    float LowestNearbyHealthPct = 100.0f;
    ObjectGuid Dragon;
    ObjectGuid Worm;
    ObjectGuid Zombie;
};

class IccValithriaHealingValue : public CalculatedValue<IccValithriaHealing>
{
public:
    IccValithriaHealingValue(PlayerbotAI* botAI)
        : CalculatedValue<IccValithriaHealing>(botAI, "icc valithria healing", CACHE_INTERVAL_MS)
    {
    }

    IccValithriaHealing Get() override;

protected:
    IccValithriaHealing Calculate() override;

private:
    static constexpr uint32 CACHE_INTERVAL_MS = 250;
    ObjectGuid _group;
    uint32 _mapId = 0;
    uint32 _instanceId = 0;
    uint32 _phaseMask = 0;
};

class RaidIccValueContext : public NamedObjectContext<UntypedValue>
{
public:
    RaidIccValueContext() { creators["icc valithria healing"] = &RaidIccValueContext::ValithriaHealing; }

private:
    static UntypedValue* ValithriaHealing(PlayerbotAI* botAI) { return new IccValithriaHealingValue(botAI); }
};

#endif
