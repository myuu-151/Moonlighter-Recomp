#pragma once
// The UnityEngine API as translated game code calls it (tools/translate/bindings.tsv), on top
// of the hand-written Unity layer (recomp/src/unity): value types with Unity's exact float
// semantics, UnityEngine.Object with its overloaded == null for destroyed objects, the
// ScriptableObject / Component / MonoBehaviour hierarchy, Resources, Debug, Time.
//
// Grows with the translated code: what the game uses that isn't here yet is listed in
// progress/unity_api_gaps.tsv (made by tools/translate survey).

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include "cs/Runtime.h"
#include "unity/Mathf.h"
#include "unity/Random.h"
#include "unity/Vector2.h"

namespace UnityEngine {

// ---- values --------------------------------------------------------------------------------------

struct Vector3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    constexpr Vector3() = default;
    constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

// Vector2 == Vector2: `num * num + num2 * num2 < 9.99999944E-11f`, the sum in double (Mono).
inline bool Vector2_op_Equality(unity::Vector2 a, unity::Vector2 b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return static_cast<double>(dx) * dx + static_cast<double>(dy) * dy < static_cast<double>(9.99999944e-11f);
}
inline unity::Vector2 Vector3_to_Vector2(Vector3 v) { return unity::Vector2(v.x, v.y); }
inline Vector3 Vector2_to_Vector3(unity::Vector2 v) { return Vector3(v.x, v.y, 0.0f); }

struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    void __ctor(float r_, float g_, float b_, float a_)
    {
        r = r_;
        g = g_;
        b = b_;
        a = a_;
    }
    void __ctor(float r_, float g_, float b_) { __ctor(r_, g_, b_, 1.0f); }
};

// ---- objects -------------------------------------------------------------------------------------

class GameObject;
class Sprite;
class Object;

// Instantiate's remap: the original tree's objects (GameObjects, components) to their copies.
struct Remap {
    std::vector<std::pair<const Object*, Object*>> map;
    Object* Find(const Object* o) const
    {
        for (const auto& [from, to] : map)
            if (from == o) return to;
        return nullptr;
    }
    template <typename T>
    void Field(T*& p) const
    {
        if constexpr (std::is_base_of<Object, T>::value) {
            if (!p) return;
            if (Object* to = Find(p)) p = dynamic_cast<T*>(to);
        } else {
            (void)p; // (a type the facade doesn't have: not an object of the tree)
        }
    }
};

// A serialized value a prefab or an asset gives a field (bind/Prefabs.cpp reads them from
// prefabs.json); the generated cs_SetField reads it as the field's type.
class SerialValue {
public:
    virtual ~SerialValue() = default;
    virtual bool AsBool() const = 0;
    virtual int32_t AsInt() const = 0;
    virtual float AsFloat() const = 0;
    virtual cs::String* AsString() const = 0;
    virtual unity::Vector2 AsVector2() const = 0;
    virtual struct Vector3 AsVector3() const = 0;
    virtual struct Color AsColor() const = 0;
    // A reference to an object of the prefab (nullptr: none, or not one this value can name).
    virtual GameObject* AsGameObject() const { return nullptr; }
    virtual Sprite* AsSprite() const { return nullptr; }
    // A list's or an array's elements (-1: not one), an object's fields (bind/Serial.h).
    virtual int32_t Length() const { return -1; }
    virtual void Element(int32_t i, const std::function<void(const SerialValue&)>& f) const { (void)i, (void)f; }
    virtual void Fields(const std::function<void(const char*, const SerialValue&)>& f) const { (void)f; }
};

// UnityEngine.Object: a managed object with a native counterpart. Destroy ends the native side:
// the managed object lives on, but compares equal to null (op_Equality, op_Implicit).
class Object : public cs::Object {
public:
    Object();
    Object(const Object& o); // (a clone: Instantiate)
    ~Object() override;
    const char* cs_typename() const override { return "UnityEngine.Object"; }
    // A copy of this object for Instantiate (generated for translated classes; the facade's
    // components have theirs): the fields as they are, not yet on an object (bind/Engine.cpp).
    virtual Object* cs_Clone();
    // A copy's references into the copied tree made its own (generated for translated classes).
    virtual void cs_Remap(const Remap& r) { (void)r; }
    bool cs_traced() const override { return true; }
    void cs_trace(cs::Tracer& t) override { t.Mark(name_); }
    // A serialized field by its C# name (generated for translated classes); false: no such field here.
    virtual bool cs_SetField(const char* name, const SerialValue& value)
    {
        (void)name, (void)value;
        return false;
    }
    // The serialized fields a loaded object never has null (strings, lists, arrays, [Serializable]
    // objects) made empty, as Unity's serializer leaves them (generated; bind/Serial.h).
    virtual void cs_SerialInit() {}
    // Unity's Object.ToString: "<name> (<type>)".
    cs::String* ToString() override;
    int32_t GetInstanceID() { return GetHashCode(); }

    // exists: not null and not destroyed
    static bool op_Implicit(Object* o) { return o && !o->destroyed_; }
    // Object.CompareBaseObjects: a null reference equals a destroyed object (and null); two
    // references compare by instance, destroyed or not (two different destroyed objects differ).
    static bool op_Equality(Object* a, Object* b)
    {
        if (!a && !b) return true;
        if (!b) return a->destroyed_;
        if (!a) return b->destroyed_;
        return a == b;
    }
    // Object.Equals(object): another UnityEngine.Object (or null) by CompareBaseObjects, anything
    // else unequal.
    bool Equals(cs::Object* other) override
    {
        auto* o = dynamic_cast<Object*>(other);
        if (!o && other) return false;
        return op_Equality(this, o);
    }
    virtual cs::String* get_name() { return name_ ? name_ : cs::String::get_Empty(); }
    virtual void set_name(cs::String* n) { name_ = n; }
    static void Destroy(Object* o);
    static void Destroy(Object* o, float delay);
    static void DestroyImmediate(Object* o) { Destroy(o); }
    void __ctor() {}

    bool destroyed_ = false;
    cs::String* name_ = nullptr;
    // Held by its native side while it isn't destroyed (a scene's object, a component, an asset): a
    // root of the collector until Destroy. Off for what the engine leaves to its owner (a renderer's
    // own material instance: collected with the renderer, as UnloadUnusedAssets would).
    bool rooted_ = true;

    // Every UnityEngine.Object not yet collected (Resources.FindObjectsOfTypeAll); those not
    // destroyed (and rooted_) are roots of the collector. A destroyed one is collected once nothing
    // references it; until then it lives on, == null.
    static std::vector<Object*>& Live();
    // How many destroyed objects the collector has freed (the _gc replays' evidence).
    static size_t& DestroyedFreed();
};

class ScriptableObject : public Object {
public:
    const char* cs_typename() const override { return "UnityEngine.ScriptableObject"; }
    // CreateInstance<T>(): the object and its constructor (Unity's Awake/OnEnable messages: not yet).
    // Type arguments arrive as translated code names C# types: T is a class pointer.
    template <typename T>
    static T CreateInstance()
    {
        return cs::New<typename std::remove_pointer<T>::type>();
    }
};

class TextAsset : public Object {
public:
    const char* cs_typename() const override { return "UnityEngine.TextAsset"; }
    void cs_trace(cs::Tracer& t) override
    {
        Object::cs_trace(t);
        t.Mark(text_);
    }
    cs::String* get_text() { return text_; }
    cs::String* text_ = nullptr;
};

struct Resources {
    // Where Resources.Load<TextAsset>(path) finds <path>.json / .txt / .bytes (the extracted text assets).
    static void SetTextRoot(const std::string& dir);
    static TextAsset* LoadText(cs::String* path);

    // Type arguments arrive as translated code names C# types: T is a class pointer.
    template <typename T>
    static T Load(cs::String* path)
    {
        if constexpr (std::is_same<T, TextAsset*>::value) return LoadText(path);
        else {
            (void)path;
            cs::NotTranslated("UnityEngine.Resources.Load<T> for this T");
        }
    }
    // Resources.Load(path): untyped (a TextAsset when the path names one).
    static Object* Load(cs::String* path) { return LoadText(path); }
    template <typename T>
    static cs::Array<T>* FindObjectsOfTypeAll()
    {
        std::vector<T> found;
        for (Object* o : Object::Live())
            if (auto t = dynamic_cast<T>(o))
                if (!o->destroyed_) found.push_back(t);
        return cs::Array<T>::FromVector(std::move(found));
    }
};

struct Debug {
    // Drawing is the editor's: nothing.
    static void DrawLine(Vector3, Vector3) {}
    static void DrawLine(Vector3, Vector3, Color) {}
    static void DrawLine(Vector3, Vector3, Color, float) {}
    static void DrawRay(Vector3, Vector3) {}
    static void DrawRay(Vector3, Vector3, Color) {}
    // Logging is off unless ML_UNITY_LOG is set (the arguments are evaluated either way, as in C#).
    static void Log(cs::Object* message);
    static void LogWarning(cs::Object* message);
    static void LogError(cs::Object* message);
    static void Log(cs::Object* message, Object* context) { (void)context, Log(message); }
    static void LogWarning(cs::Object* message, Object* context) { (void)context, LogWarning(message); }
    static void LogError(cs::Object* message, Object* context) { (void)context, LogError(message); }
    static void LogFormat(cs::String* format, cs::Array<cs::Object*>* args);
    static void LogErrorFormat(cs::String* format, cs::Array<cs::Object*>* args);
};

struct Time {
    static float realtimeSinceStartup;
    static float time;
    static float deltaTime;
    static float fixedDeltaTime;
    static int32_t frameCount;
    static float get_realtimeSinceStartup() { return realtimeSinceStartup; }
    static float get_time() { return time; }
    static float get_deltaTime() { return deltaTime; }
    static float get_fixedDeltaTime() { return fixedDeltaTime; }
    static int32_t get_frameCount() { return frameCount; }
};

// UnityEngine.Mathf with Unity 2019.2's managed code on Mono (unity/Mathf.h).
struct Mathf {
    static float Min(float a, float b) { return unity::Mathf::Min(a, b); }
    static float Max(float a, float b) { return unity::Mathf::Max(a, b); }
    static int32_t Min(int32_t a, int32_t b) { return a < b ? a : b; }
    static int32_t Max(int32_t a, int32_t b) { return a > b ? a : b; }
    static float Abs(float f) { return unity::Mathf::Abs(f); }
    static int32_t Abs(int32_t v) { return cs::Math::Abs(v); }
    static float Sign(float f) { return unity::Mathf::Sign(f); }
    static float Clamp(float v, float a, float b) { return unity::Mathf::Clamp(v, a, b); }
    static int32_t Clamp(int32_t v, int32_t a, int32_t b) { return v < a ? a : (v > b ? b : v); }
    static float Clamp01(float v) { return unity::Mathf::Clamp01(v); }
    static float Lerp(float a, float b, float t) { return unity::Mathf::Lerp(a, b, t); }
    static float Floor(float f) { return unity::Mathf::Floor(f); }
    static int32_t FloorToInt(float f) { return cs::conv_i4(std::floor(static_cast<double>(f))); }
    static float Ceil(float f) { return static_cast<float>(std::ceil(static_cast<double>(f))); }
    static int32_t CeilToInt(float f) { return cs::conv_i4(std::ceil(static_cast<double>(f))); }
    static float Round(float f) { return static_cast<float>(std::nearbyint(static_cast<double>(f))); }
    static int32_t RoundToInt(float f) { return unity::Mathf::RoundToInt(f); }
    static float Sqrt(float f) { return static_cast<float>(std::sqrt(static_cast<double>(f))); }
    static float Pow(float f, float p) { return unity::Mathf::Pow(f, p); }
    static float Atan2(float y, float x) { return unity::Mathf::Atan2(y, x); }
    static float Sin(float f) { return static_cast<float>(std::sin(static_cast<double>(f))); }
    static float Cos(float f) { return static_cast<float>(std::cos(static_cast<double>(f))); }
    static float Repeat(float t, float length) { return unity::Mathf::Repeat(t, length); }
    static float DeltaAngle(float a, float b) { return unity::Mathf::DeltaAngle(a, b); }
    static float MoveTowards(float c, float t, float d) { return unity::Mathf::MoveTowards(c, t, d); }
    static float MoveTowardsAngle(float c, float t, float d) { return unity::Mathf::MoveTowardsAngle(c, t, d); }
    // (shielded from engines' PI macro, as cs/Prim.h's Math::PI: the GameCube build includes
    // Octave's Maths.h, which defines PI)
#pragma push_macro("PI")
#undef PI
    static constexpr float PI = 3.14159274f;
#pragma pop_macro("PI")
    static constexpr float Infinity = std::numeric_limits<float>::infinity();
    static constexpr float Epsilon = unity::Mathf::Epsilon;
};

} // namespace UnityEngine

namespace unity {
// Vector2 for EqualityComparer / boxing (Unity 2019.2's Vector2.Equals(Vector2): x == other.x &&
// y == other.y, so NaN components differ; GetHashCode: x.GetHashCode() ^ (y.GetHashCode() << 2)).
inline bool cs_box_equals(const Vector2& a, const Vector2& b) { return a.x == b.x && a.y == b.y; }
inline int32_t cs_box_hash(const Vector2& v) { return cs::HashOf(v.x) ^ static_cast<int32_t>(static_cast<uint32_t>(cs::HashOf(v.y)) << 2); }
inline const char* cs_box_typename(const Vector2*) { return "UnityEngine.Vector2"; }
} // namespace unity
