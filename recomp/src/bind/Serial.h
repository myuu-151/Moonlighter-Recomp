#pragma once
// Unity's serialization as translated classes meet it: the values a prefab, an asset or a JSON
// file (FullSerializer's) gives a script's fields, read as each field's type by the generated
// cs_SetField (SerialValue, bind/Core.h); lists, arrays and [Serializable] classes element by
// element and field by field (SerialRead below); and what a deserialized object never has: a
// null string, list, array or [Serializable] object in a serialized field (the generated
// cs_SerialInit makes them empty, as Unity's serializer does when it loads an object).

#include <functional>
#include <type_traits>

#include "bind/Core.h"
#include "bind/Scene.h"
#include "util/Json.h"

namespace UnityEngine {

// A JSON value (a prefab field's raw value, a FullSerializer file's) as a serialized value:
// numbers to floats or ints, {x, y(, z)} vectors, {r, g, b, a} colours, a string path naming
// an object below `root` (a prefab's reference to its own object; "" the root).
class JsonSerialValue : public SerialValue {
public:
    JsonSerialValue(const json::Value& v, GameObject* root) : v_(v), root_(root) {}
    bool AsBool() const override;
    int32_t AsInt() const override;
    float AsFloat() const override;
    cs::String* AsString() const override;
    unity::Vector2 AsVector2() const override;
    Vector3 AsVector3() const override;
    Color AsColor() const override;
    GameObject* AsGameObject() const override;
    // A reference to another prefab (an asset: a projectile's), by its name: the driver's (none: null).
    static std::function<GameObject*(const std::string& name)> assets;
    int32_t Length() const override { return v_.IsArray() ? static_cast<int32_t>(v_.arr.size()) : -1; }
    void Element(int32_t i, const std::function<void(const SerialValue&)>& f) const override;
    void Fields(const std::function<void(const char*, const SerialValue&)>& f) const override;

private:
    float Num(const char* key) const;
    const json::Value& v_;
    GameObject* root_;
};

// A field's value as its type T (as the generated code names it).
template <typename T>
T SerialRead(const SerialValue& v);

// A [Serializable] class's object: its constructor, then the fields the value has.
template <typename C>
C* SerialObject(const SerialValue& v)
{
    auto* o = cs::New<C>();
    v.Fields([o](const char* name, const SerialValue& fv) { o->cs_SetField(name, fv); });
    return o;
}

template <typename T>
cs::List<T>* SerialList(const SerialValue& v)
{
    auto* l = cs::New<cs::List<T>>();
    const int32_t n = v.Length();
    for (int32_t i = 0; i < n; i++) v.Element(i, [l](const SerialValue& e) { l->Add(SerialRead<T>(e)); });
    return l;
}

template <typename T>
cs::Array<T>* SerialArray(const SerialValue& v)
{
    const int32_t n = v.Length();
    auto* a = cs::Array<T>::New(n < 0 ? 0 : n);
    for (int32_t i = 0; i < n; i++) v.Element(i, [a, i](const SerialValue& e) { a->at(i) = SerialRead<T>(e); });
    return a;
}

template <typename T>
T SerialRead(const SerialValue& v)
{
    if constexpr (std::is_same<T, bool>::value) return v.AsBool();
    else if constexpr (std::is_same<T, int32_t>::value) return v.AsInt();
    else if constexpr (std::is_same<T, float>::value) return v.AsFloat();
    else if constexpr (std::is_same<T, double>::value) return static_cast<double>(v.AsFloat());
    else if constexpr (std::is_same<T, cs::String*>::value) return v.AsString();
    else if constexpr (std::is_same<T, unity::Vector2>::value) return v.AsVector2();
    else if constexpr (std::is_same<T, Vector3>::value) return v.AsVector3();
    else if constexpr (std::is_same<T, Color>::value) return v.AsColor();
    else if constexpr (std::is_enum<T>::value) return static_cast<T>(v.AsInt());
    else if constexpr (std::is_same<T, GameObject*>::value) return v.AsGameObject();
    else if constexpr (std::is_pointer<T>::value && std::is_base_of<Component, typename std::remove_pointer<T>::type>::value) {
        GameObject* g = v.AsGameObject();
        return g ? g->GetComponent<T>() : nullptr;
    } else if constexpr (std::is_pointer<T>::value && std::is_base_of<Object, typename std::remove_pointer<T>::type>::value)
        return nullptr; // (an asset: a sprite, a material; not in the extracted values)
    else if constexpr (std::is_pointer<T>::value) return SerialObject<typename std::remove_pointer<T>::type>(v);
    else {
        static_assert(std::is_pointer<T>::value, "a serialized value of this type");
        return T{};
    }
}

} // namespace UnityEngine
