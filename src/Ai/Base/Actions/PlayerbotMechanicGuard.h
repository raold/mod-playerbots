/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTMECHANICGUARD_H
#define PLAYERBOTS_PLAYERBOTMECHANICGUARD_H

#include <atomic>

class Player;

// Lets another module move the bots it chooses out of a boss mechanic when the boss starts casting it, and keep
// them out while it lasts. Nothing changes until a module installs a guard (once, before maps update). The calls
// run on the bot's map thread for every bot in combat (MechanicSpot) or about to move (MechanicRefuses), so a guard
// answers Active() from an atomic and false at once for a bot it does not handle.
class PlayerbotMechanicGuard
{
public:
    virtual ~PlayerbotMechanicGuard() = default;

    // True while the module has a mechanic in force anywhere; every caller asks this first.
    virtual bool Active() const = 0;

    // A lookup, no geometry: binds this bot for the trigger and to retire autonomous follow on its next update.
    virtual bool Binds(Player* bot) = 0;

    // Where the bot must move now for a mechanic in force: out of its area, onto its stack point or away from the
    // others. Returns true when it must move. A question only: the move is reported by MechanicMoved.
    virtual bool MechanicSpot(Player* bot, float& x, float& y, float& z) = 0;

    // The mechanic move's MoveTo to that spot was issued.
    virtual void MechanicMoved(Player* bot) = 0;

    // True when a move to (x, y, z) would take the bot back into a mechanic it keeps out of now. Forced moves (a
    // stock raid strategy's own, or this guard's) are never asked.
    virtual bool MechanicRefuses(Player* bot, float x, float y, float z) = 0;

    static PlayerbotMechanicGuard* Get() { return Slot().load(std::memory_order_acquire); }
    // Publish before map workers start; destroy/replace only after they stop.
    // The atomic publishes the pointer, but does not own the guard's lifetime.
    static void Set(PlayerbotMechanicGuard* guard) { Slot().store(guard, std::memory_order_release); }

private:
    static std::atomic<PlayerbotMechanicGuard*>& Slot()
    {
        static std::atomic<PlayerbotMechanicGuard*> slot{nullptr};
        return slot;
    }
};

#endif
