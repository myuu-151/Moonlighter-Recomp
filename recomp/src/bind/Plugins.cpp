#include "bind/Plugins.h"

#include "game/MasterAudio.h"

namespace DarkTonic {
namespace MasterAudio {

game::MasterAudio* MasterAudio::engine = nullptr;

PlaySoundResult* MasterAudio::PlaySound(cs::String* sType, float volumePercentage, cs::Nullable<float> pitch, float delaySoundTime,
                                        cs::String* variationName, cs::Nullable<double> timeToSchedulePlay, bool isChaining,
                                        bool isSingleSubscribedPlay)
{
    (void)volumePercentage, (void)pitch, (void)delaySoundTime, (void)timeToSchedulePlay, (void)isChaining, (void)isSingleSubscribedPlay;
    if (variationName) cs::NotTranslated("MasterAudio.PlaySound of a named variation");
    if (!engine || !sType) return nullptr;
    if (!engine->PlaySound(sType->ToUtf8())) return nullptr;
    auto* r = cs::gcnew<PlaySoundResult>();
    r->variation_ = cs::gcnew<SoundGroupVariation>();
    return r;
}

bool MasterAudio::PlaySoundAndForget(cs::String* sType, float volumePercentage, cs::Nullable<float> pitch, float delaySoundTime,
                                     cs::String* variationName, cs::Nullable<double> timeToSchedulePlay)
{
    return PlaySound(sType, volumePercentage, pitch, delaySoundTime, variationName, timeToSchedulePlay, false, false) != nullptr;
}

} // namespace MasterAudio
} // namespace DarkTonic
