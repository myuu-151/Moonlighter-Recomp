#include "bind/UnityEngine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace UnityEngine {

namespace {
std::string gTextRoot = "data/extracted";

void TraceLive(cs::Tracer& t)
{
    for (Object* o : Object::Live()) t.Mark(o);
}
const bool gLiveRoots = cs::RegisterRoots(&TraceLive);

bool LogOn()
{
    static const bool on = std::getenv("ML_UNITY_LOG") != nullptr;
    return on;
}
void Print(const char* level, cs::Object* message)
{
    if (!LogOn()) return;
    cs::String* s = cs::ToStringOrEmpty(message);
    std::fprintf(stderr, "[unity %s] %s\n", level, s->ToUtf8().c_str());
}
} // namespace

std::vector<Object*>& Object::Live()
{
    static auto* live = new std::vector<Object*>(); // never destroyed: objects outlive it at exit
    return *live;
}

Object::Object()
{
    (void)gLiveRoots;
    Live().push_back(this);
}

Object::~Object()
{
    auto& l = Live();
    auto it = std::find(l.begin(), l.end(), this);
    if (it != l.end()) l.erase(it);
}

void Object::Destroy(Object* o)
{
    if (o) o->destroyed_ = true;
}

// Destroy(obj, t): a delayed destroy needs the scene's scheduler (not bound yet): reaching it is an error.
void Object::Destroy(Object* o, float delay)
{
    (void)o, (void)delay;
    cs::NotTranslated("UnityEngine.Object.Destroy(obj, t) (needs the scene's delayed calls)");
}

void Resources::SetTextRoot(const std::string& dir) { gTextRoot = dir; }

TextAsset* Resources::LoadText(cs::String* path)
{
    std::string p = cs::nc(path)->ToUtf8();
    for (const char* ext : {".json", ".txt", ".bytes", ""}) {
        std::ifstream f(gTextRoot + "/" + p + ext, std::ios::binary);
        if (!f) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        std::string bytes = ss.str();
        // TextAsset.text decodes UTF-8 and drops a byte order mark
        if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
            static_cast<unsigned char>(bytes[2]) == 0xBF)
            bytes = bytes.substr(3);
        auto* t = cs::gcnew<TextAsset>();
        t->text_ = cs::String::FromUtf8(bytes);
        t->name_ = path;
        return t;
    }
    return nullptr; // Resources.Load returns null for a missing asset
}

void Debug::Log(cs::Object* message) { Print("log", message); }
void Debug::LogWarning(cs::Object* message) { Print("warning", message); }
void Debug::LogError(cs::Object* message) { Print("error", message); }
void Debug::LogFormat(cs::String* format, cs::Array<cs::Object*>* args) { Print("log", cs::String::Format(format, args)); }
void Debug::LogErrorFormat(cs::String* format, cs::Array<cs::Object*>* args) { Print("error", cs::String::Format(format, args)); }

float Time::realtimeSinceStartup = 0.0f;
float Time::time = 0.0f;
float Time::deltaTime = 0.02f;
float Time::fixedDeltaTime = 0.02f;
int32_t Time::frameCount = 0;

} // namespace UnityEngine
