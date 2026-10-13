#pragma once
// DarkTonic MasterAudio, the part that touches the game: the UnityEngine.Random draws
// MasterAudio.PlaySound makes (the sound itself is presentation). The groups come from the
// DynamicSoundGroupCreator prefabs (data/extracted/sound_groups.json,
// tools/extract/sound_groups.py); a scene's creators make its groups in order (the first group
// of a name wins).
//
// PlaySound(sType) (MasterAudio.PlaySoundAtVolume, PlaySoundIfAvailable, SoundGroupVariation.Play):
//   - skipped, no draws: no such group; the group's limit (FrameBased: Time.frameCount within
//     limitPerXFrames of its last play; TimeBased: minimumTimeBetween, on realtimeSinceStartup,
//     here the clock the caller gives); limitPolyphony with voiceLimitCount playing;
//   - the variation: the only one, else the next of the group's pool (a shuffled list of
//     variation indices: CreateGroup shuffles it, RefillSoundGroupPool reshuffles it when it
//     runs out: ArrayListUtil.SortIntArray's Range(i, n) for each i, and with no-repeat (a
//     Randomized group of 3 or more) a Range(1, n) moving the last played away from the front);
//   - probabilityToPlay < 100: Range(0, 100), the play fails at or above it;
//   - a variation still playing under retriggerPercentage of its clip: not played (the next
//     of the pool is tried); played: useRandomVolume Range(min, max); then
//     SoundGroupVariation.Play: useRandomPitch Range(min, max); minCustomLoops != maxCustomLoops
//     Range(min, max + 1) (most variations: 1 and 5, so nearly every play draws once); the
//     updater's useIntroSilence and useRandomStartTime draws (made a frame later in the game:
//     here at once); played, its pool entry is used up (refilled when empty);
//   - linked groups: All plays each, OneAtRandom draws Range(0, count) and plays it.
// "Still playing" is the variation's clip length from its start on the caller's clock (the
// game's AudioSource time, which a pitch would change: not modelled). Its AudioSource only
// starts in MasterAudio's LateUpdate (SoundGroupVariation.Play enables its updater, whose
// ManualUpdate calls PlaySoundAndWait: PlayScheduled), so in the frame it was played it isn't
// playing yet: a second PlaySound of it in that frame plays it again, with its draws
// (enemy_BrokenGolemTurret.jsonl: both turrets' PlayShootSound in one animation pass, one draw
// each).
//
// Checked against the game for golem_dungeon_soldier_sword_crash (enemy_GolemSoldier.jsonl:
// one draw a play, the loops' Range(1, 6)); the pools, limits and retriggers aren't recorded yet.

#include <map>
#include <string>
#include <vector>

#include "util/Json.h"

namespace game {

class MasterAudio {
public:
    struct Variation {
        std::string name;
        int probabilityToPlay = 100;
        bool useRandomPitch = false, useRandomVolume = false;
        float randomPitchMin = 0.0f, randomPitchMax = 0.0f, randomVolumeMin = 0.0f, randomVolumeMax = 0.0f;
        int minCustomLoops = 1, maxCustomLoops = 5;
        bool useIntroSilence = false, useRandomStartTime = false;
        float introSilenceMin = 0.0f, introSilenceMax = 0.0f, randomStartMinPercent = 0.0f, randomStartMaxPercent = 0.0f;
        float clipLength = 0.0f;
        // State: when it last started (the caller's clock), -1: never; in which frame.
        double startedAt = -1.0;
        int startedFrame = -1000000;
    };
    struct Group {
        std::string name;
        int retriggerPercentage = 50;
        int curVariationSequence = 0; // 0 Randomized, 1 TopToBottom
        bool useNoRepeatRefill = true;
        int curVariationMode = 0;     // 0 Normal, 1 LoopedChain, 2 Dialog
        int limitMode = 0;            // 0 None, 1 FrameBased, 2 TimeBased
        int limitPerXFrames = 1;
        float minimumTimeBetween = 0.1f;
        bool limitPolyphony = false;
        int voiceLimitCount = 1;
        std::vector<std::string> childSoundGroups;
        int linkedStartGroupSelectionType = 0; // 0 All, 1 OneAtRandom
        int variationCount = 0;        // groupVariations.Count: the voices (weight copies included)
        std::vector<Variation> sources; // a variation per voice (weight copies)
        // State: the pool (_randomizer), what's left of it (_randomizerLeftovers), what played
        // since the last refill (_clipsPlayedBySoundTypeOldestFirst), the last play.
        std::vector<int> pool, leftovers, playedOldestFirst, origin;
        double lastTimePlayed = -1e9;
        int lastFramePlayed = -1000000;
        bool UsesNoRepeat() const { return curVariationSequence == 0 && variationCount >= 3 && useNoRepeatRefill; }
    };

    // The groups of `creators` ("Sounds - GolemDungeon", ...) from sound_groups.json, in order.
    // False if the file has none of them.
    bool Load(const json::Value& soundGroups, const std::vector<std::string>& creators);
    // CreateGroup's pool for each group, in the order they were loaded (a Randomized group's
    // shuffle draws, at the scene's load).
    void CreateGroups();

    // MasterAudio.PlaySound(sType) at `time` (realtimeSinceStartup's stand-in: the frame's time)
    // in frame `frame` (Time.frameCount): the draws. True if a variation played.
    bool PlaySound(const std::string& sType, double time, int frame);
    // The clocks the driver keeps (each frame: Time.realtimeSinceStartup's stand-in and
    // Time.frameCount), and PlaySound at them.
    double now = 0.0;
    int frameCount = 0;
    bool PlaySound(const std::string& sType) { return PlaySound(sType, now, frameCount); }

    Group* Find(const std::string& sType);

private:
    bool PlayAtVolume(Group& g, double time, int frame, bool triggeredAsChildGroup);
    bool PlayIfAvailable(Group& g, Variation& v, double time, int frame);
    // Its AudioSource playing at `time` in `frame` (AudioSource.isPlaying): started in an earlier
    // frame's LateUpdate, its clip not over.
    static bool SourcePlaying(const Variation& v, double time, int frame);
    void Refill(Group& g);
    void Shuffle(std::vector<int>& list);
    std::vector<Group> groups_;
    std::map<std::string, size_t> byName_;
};

} // namespace game
