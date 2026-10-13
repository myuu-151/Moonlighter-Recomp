#pragma once
// The third-party plugins translated scripts call, as far as the game's state goes:
//   - DarkTonic MasterAudio: PlaySound's UnityEngine.Random draws (the hand-written model,
//     game/MasterAudio.h, which the driver loads and gives the clocks); the sound itself is
//     presentation. A play gives a PlaySoundResult whose variation reports playing.
//   - CodeStage AntiCheat's obscured values: the plain values (the encryption only hides them
//     in memory; its keys are fixed unless RandomizeCryptoKey is called, which isn't bound).

#include <cstdint>

#include "bind/Core.h"

namespace game {
class MasterAudio;
}

namespace DarkTonic {
namespace MasterAudio {

class SoundGroupVariation : public cs::Object {
public:
    const char* cs_typename() const override { return "DarkTonic.MasterAudio.SoundGroupVariation"; }
    bool cs_traced() const override { return true; }
    void cs_trace(cs::Tracer& t) override { t.Mark(soundFinished_); }
    bool get_IsPlaying() { return playing_; }
    void Stop(bool stopEndDetection, bool skipLinked)
    {
        (void)stopEndDetection, (void)skipLinked;
        playing_ = false;
    }
    void Stop() { Stop(false, false); }
    // SoundFinished (the clip's end: not modelled, never raised).
    void add_SoundFinished(cs::Delegate<void()>* d) { soundFinished_ = cs::Delegate<void()>::Combine(soundFinished_, d); }
    void remove_SoundFinished(cs::Delegate<void()>* d) { soundFinished_ = cs::Delegate<void()>::Remove(soundFinished_, d); }

    bool playing_ = true;
    cs::Delegate<void()>* soundFinished_ = nullptr;
};

class PlaySoundResult : public cs::Object {
public:
    const char* cs_typename() const override { return "DarkTonic.MasterAudio.PlaySoundResult"; }
    bool cs_traced() const override { return true; }
    void cs_trace(cs::Tracer& t) override { t.Mark(variation_); }
    bool get_SoundPlayed() { return true; }
    bool get_SoundScheduled() { return false; }
    SoundGroupVariation* get_ActingVariation() { return variation_; }
    SoundGroupVariation* variation_ = nullptr;
};

class MasterAudio {
public:
    // The driver's model of the scene's sound groups (none: no sound plays, no draws).
    static game::MasterAudio* engine;
    static PlaySoundResult* PlaySound(cs::String* sType, float volumePercentage, cs::Nullable<float> pitch, float delaySoundTime,
                                      cs::String* variationName, cs::Nullable<double> timeToSchedulePlay, bool isChaining,
                                      bool isSingleSubscribedPlay);
    static bool PlaySoundAndForget(cs::String* sType, float volumePercentage, cs::Nullable<float> pitch, float delaySoundTime,
                                   cs::String* variationName, cs::Nullable<double> timeToSchedulePlay);
};

} // namespace MasterAudio
} // namespace DarkTonic

namespace CodeStage {
namespace AntiCheat {
namespace ObscuredTypes {

template <typename T>
struct Obscured {
    T value{};
    static Obscured op_Implicit(T v)
    {
        Obscured o;
        o.value = v;
        return o;
    }
    static T op_Implicit(Obscured o) { return o.value; }
    T GetDecrypted() const { return value; }
};
using ObscuredBool = Obscured<bool>;
using ObscuredInt = Obscured<int32_t>;
using ObscuredFloat = Obscured<float>;
template <typename T>
inline void cs_trace_value(cs::Tracer&, const Obscured<T>&) {}

} // namespace ObscuredTypes
} // namespace AntiCheat
} // namespace CodeStage
