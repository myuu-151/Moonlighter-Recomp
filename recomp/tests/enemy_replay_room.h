#pragma once
// The room's DisableEnemies in an enemy replay (tests/enemy_replay.h; game/EnemyRoom.h): the hero
// leaving the room is his doing, fed from the recording as his hits are. The recorded enemies, in
// their ids' order (the oracle takes them from roomEnemies in order), are disabled where the game
// did it:
//   - "disableEnemies" events (EnemyOracleRoom.cs: DungeonRoom.DisableEnemies on the recorded
//     room, with its fixed step, -1 for the frame's part, and "updates", how many of the frame's
//     recorded enemy Updates ran before it);
//   - a recording from before that event (enemy_BrokenGolemTurret.jsonl: the hero falls into
//     the hole over the room's secret room at frame 208): the frame where every enemy still out
//     goes from active to inactive at once, which its row shows exactly (its Animator enabled but
//     not isActiveAndEnabled: "on" with the state hash 0 and no DistanceX/Y, which the oracle
//     writes only for an active and enabled Animator; the frame before had them). Its fixed step
//     isn't recorded: the frame's part, after the frame's recorded Updates (an enemy made inactive
//     runs no Update; frame 208's DestroyNow of the turret's shot in flight, step -1).
// (Included inside enemy_replay.h's anonymous namespace: its headers are included there first.)
// An inactive enemy's Animator answers GetCurrentAnimatorStateInfo with zeros (the rows of an
// inactive turret: state 0, t 0): InfoOf.

// Included by enemy_replay.h inside its anonymous namespace (game/EnemyRoom.h and the rest
// included at its top).

struct RoomExitReplay {
    bool explicitEvents = false; // the recording has "disableEnemies" events
    int inferred = 0;            // DisableEnemies placed from the rows (no event)
    // This frame's: -2 none; else the fixed step (-1: the frame's part) and the Updates before it.
    int step = -2;
    size_t updatesBefore = 0;

    template <typename Frames>
    void Scan(Frames& frames)
    {
        for (const json::Value& f : frames)
            for (const json::Value& ev : f["events"].arr)
                if (ev["k"].s == "disableEnemies") explicitEvents = true;
    }

    static const json::Value* RowOf(const json::Value* frame, int id)
    {
        if (!frame) return nullptr;
        const json::Value* list = frame->Find("enemies");
        if (!list) list = frame->Find("slimes");
        if (!list) return nullptr;
        for (const json::Value& v : list->arr)
            if (v["s"].AsInt() == id) return &v;
        return nullptr;
    }

    // The row shows the enemy's object inactive (its Animator enabled, not active and enabled)
    // where the previous frame's showed it active.
    static bool WentInactive(const json::Value* row, const json::Value* before)
    {
        if (!row || !before || (*row)["gone"].b) return false;
        const json::Value& an = (*row)["anim"];
        const json::Value& was = (*before)["anim"];
        return an["on"].b && an["state"].AsInt() == 0 && !an.Find("dx") && was.Find("dx") != nullptr;
    }

    // `enemies`: (id, enemy) in the ids' order.
    void Plan(const json::Value& f, const json::Value* previous, const std::vector<std::pair<int, game::Enemy*>>& enemies)
    {
        step = -2;
        updatesBefore = 0;
        if (explicitEvents) {
            for (const json::Value& ev : f["events"].arr)
                if (ev["k"].s == "disableEnemies") {
                    step = static_cast<int>(ev["step"].AsInt());
                    updatesBefore = static_cast<size_t>(ev["updates"].AsInt());
                }
            return;
        }
        int out = 0, gone = 0;
        for (const auto& [id, e] : enemies) {
            if (!e || e->destroyed || !e->active || !e->inRoomEnemies) continue;
            out++;
            gone += WentInactive(RowOf(&f, id), RowOf(previous, id));
        }
        if (out == 0 || gone != out) return;
        step = -1;
        updatesBefore = f["updates"].size();
        inferred++;
    }

    static void Run(const std::vector<std::pair<int, game::Enemy*>>& enemies)
    {
        std::vector<game::Enemy*> roomEnemies;
        for (const auto& [id, e] : enemies) roomEnemies.push_back(e);
        game::DisableEnemies(roomEnemies);
    }

    // Animator.GetCurrentAnimatorStateInfo(0): zeros while the object is inactive.
    static unity::AnimatorStateInfo InfoOf(const game::Enemy& e)
    {
        if (!e.active) return unity::AnimatorStateInfo{};
        return e.enemyAnimator->GetCurrentAnimatorStateInfo();
    }
};
