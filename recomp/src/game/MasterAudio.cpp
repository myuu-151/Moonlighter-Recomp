#include "game/MasterAudio.h"

#include <algorithm>

#include "unity/Random.h"

namespace game {

namespace {

bool Bool(const json::Value& v, const char* key, bool fallback)
{
    const json::Value* f = v.Find(key);
    return f && f->kind == json::Value::Kind::Bool ? f->b : fallback;
}

int Int(const json::Value& v, const char* key, int fallback)
{
    const json::Value* f = v.Find(key);
    return f && !f->IsNull() ? static_cast<int>(f->AsInt()) : fallback;
}

float Float(const json::Value& v, const char* key, float fallback)
{
    const json::Value* f = v.Find(key);
    return f && !f->IsNull() ? static_cast<float>(f->AsDouble()) : fallback;
}

} // namespace

bool MasterAudio::Load(const json::Value& soundGroups, const std::vector<std::string>& creators)
{
    bool any = false;
    for (const std::string& creator : creators) {
        const json::Value* c = soundGroups.Find(creator);
        if (!c) continue;
        any = true;
        for (const auto& [name, gv] : c->obj) {
            if (byName_.count(name)) continue; // the first group of a name wins
            Group g;
            g.name = name;
            g.retriggerPercentage = Int(gv, "retriggerPercentage", 50);
            g.curVariationSequence = Int(gv, "curVariationSequence", 0);
            g.useNoRepeatRefill = Bool(gv, "useNoRepeatRefill", true);
            g.curVariationMode = Int(gv, "curVariationMode", 0);
            g.limitMode = Int(gv, "limitMode", 0);
            g.limitPerXFrames = Int(gv, "limitPerXFrames", 1);
            g.minimumTimeBetween = Float(gv, "minimumTimeBetween", 0.1f);
            g.limitPolyphony = Bool(gv, "limitPolyphony", false);
            g.voiceLimitCount = Int(gv, "voiceLimitCount", 1);
            g.linkedStartGroupSelectionType = Int(gv, "linkedStartGroupSelectionType", 0);
            if (const json::Value* kids = gv.Find("childSoundGroups"))
                for (const json::Value& k : kids->arr) g.childSoundGroups.push_back(k.s);
            for (const json::Value& vv : gv["variations"].arr) {
                Variation v;
                v.name = vv["name"].s;
                v.index = static_cast<int>(&vv - gv["variations"].arr.data());
                v.probabilityToPlay = Int(vv, "probabilityToPlay", 100);
                v.useRandomPitch = Bool(vv, "useRandomPitch", false);
                v.randomPitchMin = Float(vv, "randomPitchMin", 0.0f);
                v.randomPitchMax = Float(vv, "randomPitchMax", 0.0f);
                v.useRandomVolume = Bool(vv, "useRandomVolume", false);
                v.randomVolumeMin = Float(vv, "randomVolumeMin", 0.0f);
                v.randomVolumeMax = Float(vv, "randomVolumeMax", 0.0f);
                v.minCustomLoops = Int(vv, "minCustomLoops", 1);
                v.maxCustomLoops = Int(vv, "maxCustomLoops", 5);
                v.useIntroSilence = Bool(vv, "useIntroSilence", false);
                v.introSilenceMin = Float(vv, "introSilenceMin", 0.0f);
                v.introSilenceMax = Float(vv, "introSilenceMax", 0.0f);
                v.useRandomStartTime = Bool(vv, "useRandomStartTime", false);
                v.randomStartMinPercent = Float(vv, "randomStartMinPercent", 0.0f);
                v.randomStartMaxPercent = Float(vv, "randomStartMaxPercent", 0.0f);
                v.clipLength = Float(vv, "clipLength", 0.0f);
                // CreateGroup makes `weight` voices of each variation.
                const int weight = std::max(1, Int(vv, "weight", 1));
                for (int k = 0; k < weight; k++) g.sources.push_back(v);
            }
            g.variationCount = static_cast<int>(g.sources.size());
            // The pool before CreateGroups shuffles it: the voices in order.
            for (int k = 0; k < g.variationCount; k++) g.origin.push_back(k);
            g.pool = g.leftovers = g.origin;
            byName_[name] = groups_.size();
            groups_.push_back(std::move(g));
        }
    }
    return any;
}

void MasterAudio::Shuffle(std::vector<int>& list)
{
    // ArrayListUtil.SortIntArray.
    const int n = static_cast<int>(list.size());
    for (int i = 0; i < n; i++) {
        const int value = list[static_cast<size_t>(i)];
        const int index = unity::Random::Range(i, n);
        list[static_cast<size_t>(i)] = list[static_cast<size_t>(index)];
        list[static_cast<size_t>(index)] = value;
    }
}

void MasterAudio::CreateGroups()
{
    for (Group& g : groups_) {
        std::vector<int> list;
        for (int k = 0; k < g.variationCount; k++) list.push_back(k);
        if (g.curVariationSequence == 0) Shuffle(list);
        g.pool = list;
        g.origin = list;
        g.leftovers = list;
        g.playedOldestFirst.clear();
    }
}

MasterAudio::Group* MasterAudio::Find(const std::string& sType)
{
    auto it = byName_.find(sType);
    return it == byName_.end() ? nullptr : &groups_[it->second];
}

void MasterAudio::Refill(Group& g)
{
    // RefillSoundGroupPool.
    std::vector<int>& list = g.pool;
    std::vector<int>& list2 = g.playedOldestFirst;
    auto has = [&](int item) { return std::find(list2.begin(), list2.end(), item) != list2.end(); };
    for (int item : list)
        if (!has(item)) list2.push_back(item);
    if (list2.size() < g.origin.size())
        for (int item : g.origin)
            if (!has(item)) list2.push_back(item);
    list.clear();
    if (g.curVariationSequence == 0) {
        bool hasLast = false;
        int last = 0;
        if (g.UsesNoRepeat() && !list2.empty()) {
            hasLast = true;
            last = list2.back();
        }
        Shuffle(list2);
        if (hasLast && last == list2[0]) {
            const int item = list2[0];
            list2.erase(list2.begin());
            const int at = unity::Random::Range(1, static_cast<int>(list2.size()));
            list2.insert(list2.begin() + at, item);
        }
    }
    list.insert(list.end(), list2.begin(), list2.end());
    g.leftovers.insert(g.leftovers.end(), list2.begin(), list2.end());
    list2.clear();
}

void MasterAudio::LateUpdate()
{
    // PlaySoundAndWait: the intro silence, then ClipStartPosition.
    for (const UpdaterStart& u : waiting_) {
        float silence = 0.0f, start = 0.0f;
        if (u.introSilence) silence = unity::Random::Range(u.introSilenceMin, u.introSilenceMax);
        if (u.randomStartTime) start = unity::Random::Range(u.randomStartMinPercent, u.randomStartMaxPercent);
        if (u.played >= 0) {
            played_[static_cast<size_t>(u.played)].introSilence = silence;
            played_[static_cast<size_t>(u.played)].startPercent = start;
        }
    }
    waiting_.clear();
    if (onPlay)
        for (const Played& p : played_) onPlay(p);
    played_.clear();
}

bool MasterAudio::SourcePlaying(const Variation& v, double time, int frame)
{
    return v.startedAt >= 0.0 && frame != v.startedFrame && time < v.startedAt + v.clipLength;
}

bool MasterAudio::PlayIfAvailable(Group& g, Variation& v, double time, int frame)
{
    // A Normal group's variation still playing short of retriggerPercentage of its clip.
    if (g.curVariationMode == 0 && v.clipLength > 0.0f && SourcePlaying(v, time, frame)) {
        const double played = (time - v.startedAt) / static_cast<double>(v.clipLength) * 100.0;
        if (played < static_cast<double>(g.retriggerPercentage)) return false;
    }
    Played p;
    p.group = g.name;
    p.variation = v.index;
    if (v.useRandomVolume) {
        p.randomVolume = true;
        p.volume = unity::Random::Range(v.randomVolumeMin, v.randomVolumeMax);
    }
    // SoundGroupVariation.Play (no pitch given).
    if (v.useRandomPitch) {
        p.randomPitch = true;
        p.pitch = unity::Random::Range(v.randomPitchMin, v.randomPitchMax);
    }
    p.loops = v.minCustomLoops;
    if (v.maxCustomLoops != v.minCustomLoops) {
        // _maxLoops = Range(min, max + 1).
        p.loops = unity::Random::Range(v.minCustomLoops, v.maxCustomLoops + 1);
    }
    if (onPlay) played_.push_back(p);
    // Its updater's start: at MasterAudio's LateUpdate.
    if ((v.useIntroSilence && v.introSilenceMax > 0.0f) || v.useRandomStartTime) {
        UpdaterStart u;
        u.introSilence = v.useIntroSilence && v.introSilenceMax > 0.0f;
        u.introSilenceMin = v.introSilenceMin;
        u.introSilenceMax = v.introSilenceMax;
        u.randomStartTime = v.useRandomStartTime;
        u.randomStartMinPercent = v.randomStartMinPercent;
        u.randomStartMaxPercent = v.randomStartMaxPercent;
        u.played = onPlay ? static_cast<int>(played_.size()) - 1 : -1;
        waiting_.push_back(u);
    }
    v.startedAt = time;
    v.startedFrame = frame;
    return true;
}

bool MasterAudio::PlayAtVolume(Group& g, double time, int frame, bool triggeredAsChildGroup)
{
    if (g.curVariationMode == 0) {
        if (g.limitMode == 2 && g.minimumTimeBetween > 0.0f && time < g.lastTimePlayed + static_cast<double>(g.minimumTimeBetween)) return false;
        if (g.limitMode == 1 && frame - g.lastFramePlayed < g.limitPerXFrames) return false;
    }
    g.lastTimePlayed = time;
    g.lastFramePlayed = frame;
    if (g.sources.empty()) return false;
    auto playing = [&](const Variation& v) { return SourcePlaying(v, time, frame); };
    if (g.curVariationMode == 0 && g.limitPolyphony) {
        int n = 0;
        for (const Variation& v : g.sources)
            if (playing(v) && ++n >= g.voiceLimitCount) return false;
    }
    Variation* info = nullptr;
    const bool single = g.sources.size() == 1;
    int picked = -1;
    std::vector<int>* list2 = nullptr;
    if (single) {
        info = &g.sources[0];
    } else {
        if (g.pool.empty()) return false;
        picked = g.pool[0];
        info = &g.sources[static_cast<size_t>(picked)];
        list2 = &g.leftovers;
        auto it = std::find(list2->begin(), list2->end(), picked);
        if (it != list2->end()) list2->erase(it);
    }
    if (info->probabilityToPlay < 100 && unity::Random::Range(0, 100) >= info->probabilityToPlay) return false;
    bool played = false, refilled = false, last = false;
    do {
        last = false;
        if (PlayIfAvailable(g, *info, time, frame)) {
            played = true;
            if (!single) {
                // RemoveClipAndRefillIfEmpty: the pool's front goes (the pick), into the played list.
                g.pool.erase(g.pool.begin());
                g.playedOldestFirst.push_back(picked);
                if (g.pool.empty()) Refill(g);
            }
        } else {
            if (!list2) continue;
            if (list2->empty()) {
                if (refilled) continue;
                Refill(g);
                refilled = true;
                list2->clear();
                list2->insert(list2->end(), g.pool.begin(), g.pool.end());
            }
            info = &g.sources[static_cast<size_t>((*list2)[0])];
            list2->erase(list2->begin());
            if (refilled && list2->empty()) last = true;
        }
    } while (!played && list2 && (!list2->empty() || !refilled || last));
    if (!played) return false;
    if (!triggeredAsChildGroup) {
        if (g.linkedStartGroupSelectionType == 0) {
            for (const std::string& child : g.childSoundGroups)
                if (Group* c = Find(child)) PlayAtVolume(*c, time, frame, true);
        } else if (!g.childSoundGroups.empty()) {
            const int count = static_cast<int>(g.childSoundGroups.size());
            const int index = unity::Random::Range(0, count);
            if (Group* c = Find(g.childSoundGroups[static_cast<size_t>(index)])) PlayAtVolume(*c, time, frame, true);
        }
    }
    // (soundPlayedEventActive's custom event: not ported.)
    return true;
}

bool MasterAudio::PlaySound(const std::string& sType, double time, int frame)
{
    Group* g = Find(sType);
    return g ? PlayAtVolume(*g, time, frame, false) : false;
}

} // namespace game
