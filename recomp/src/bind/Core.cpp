#include "bind/Core.h"

#include "bind/Scene.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace UnityEngine {

namespace {
std::string gTextRoot = "data/extracted";

// The native side's: every object not destroyed (a destroyed one only through what references it).
void TraceLive(cs::Tracer& t)
{
    for (Object* o : Object::Live())
        if (!o->destroyed_ && o->rooted_) t.Mark(o);
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

size_t& Object::DestroyedFreed()
{
    static size_t n = 0;
    return n;
}

Object::Object(const Object& o) : cs::Object(o), destroyed_(false), name_(o.name_), rooted_(o.rooted_)
{
    Live().push_back(this);
}

Object* Object::cs_Clone()
{
    cs::NotTranslated((std::string("Instantiate of a ") + cs_typename() + " (bind/Engine.cpp)").c_str());
}

Object::~Object()
{
    if (destroyed_) DestroyedFreed()++;
    auto& l = Live();
    auto it = std::find(l.begin(), l.end(), this);
    if (it != l.end()) l.erase(it);
}

cs::String* Object::ToString()
{
    std::string type = cs_typename();
    size_t dot = type.rfind('.');
    if (dot != std::string::npos) type = type.substr(dot + 1);
    return cs::String::Concat(get_name(), cs::String::FromAscii((" (" + type + ")").c_str()));
}

// Destroy(obj): at the frame's end (the scene's ProcessDestroys; the object compares unequal to
// null until then). Without a scene (the generation's tests): at once.
void Object::Destroy(Object* o)
{
    if (!o) return;
    if (bind::Scene::current) bind::Scene::current->MarkDestroy(o);
    else o->destroyed_ = true;
}

// Destroy(obj, t): when the delayed calls' pass reaches it (the scene's), destroyed then.
void Object::Destroy(Object* o, float delay)
{
    if (!o) return;
    bind::Scene* s = bind::Scene::current;
    if (!s) cs::NotTranslated("UnityEngine.Object.Destroy(obj, t) without a scene");
    s->calls.Destroy(o, delay, [s, o] { s->DestroyNow(o); });
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
