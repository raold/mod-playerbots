/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTHAZARDFILTER_H
#define PLAYERBOTS_PLAYERBOTHAZARDFILTER_H

#include <atomic>

class Player;

// Lets another module keep the bots it chooses off ground hazards. Nothing changes until a module installs a
// filter (once, before maps update). Both calls then run on the bot's map thread, for every bot that follows
// or avoids aoe, so a filter answers false at once for a bot it does not handle.
class PlayerbotHazardFilter
{
public:
    virtual ~PlayerbotHazardFilter() = default;

    // Where to walk for a follow spot: a safe spot nearby when it lies in a hazard, or a waypoint round a hazard
    // the straight walk to it would cross. Returns true when it changed the spot.
    virtual bool SafeFollowSpot(Player* bot, float& x, float& y, float& z) = 0;

    // The bot stands in a hazard: where to step out to. Consulted after the stock avoid aoe cases.
    virtual bool HazardExit(Player* bot, float& x, float& y, float& z) = 0;

    static PlayerbotHazardFilter* Get() { return Slot().load(std::memory_order_acquire); }
    // Publish before map workers start; destroy/replace only after they stop.
    // The atomic publishes the pointer, but does not own the filter's lifetime.
    static void Set(PlayerbotHazardFilter* filter) { Slot().store(filter, std::memory_order_release); }

private:
    static std::atomic<PlayerbotHazardFilter*>& Slot()
    {
        static std::atomic<PlayerbotHazardFilter*> slot{nullptr};
        return slot;
    }
};

#endif
