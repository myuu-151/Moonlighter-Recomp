#include "bind/Serial.h"

#include "bind/Prefabs.h"

namespace UnityEngine {

bool JsonSerialValue::AsBool() const
{
    if (v_.kind == json::Value::Kind::Bool) return v_.b;
    return (v_.IsInt() || v_.IsDouble()) && v_.AsDouble() != 0.0;
}

int32_t JsonSerialValue::AsInt() const { return v_.IsInt() || v_.IsDouble() ? static_cast<int32_t>(v_.AsInt()) : 0; }

// (A float field from a JSON number: FullSerializer's double, converted; Unity's own values are floats already.)
float JsonSerialValue::AsFloat() const { return v_.IsInt() || v_.IsDouble() ? static_cast<float>(v_.AsDouble()) : 0.0f; }

cs::String* JsonSerialValue::AsString() const { return v_.IsString() ? cs::String::FromUtf8(v_.s) : nullptr; }

float JsonSerialValue::Num(const char* key) const
{
    const json::Value* x = v_.Find(key);
    return x && (x->IsInt() || x->IsDouble()) ? static_cast<float>(x->AsDouble()) : 0.0f;
}

unity::Vector2 JsonSerialValue::AsVector2() const { return {Num("x"), Num("y")}; }
Vector3 JsonSerialValue::AsVector3() const { return {Num("x"), Num("y"), Num("z")}; }
Color JsonSerialValue::AsColor() const { return {Num("r"), Num("g"), Num("b"), Num("a")}; }

GameObject* JsonSerialValue::AsGameObject() const { return v_.IsString() && root_ ? bind::FindBelow(root_, v_.s) : nullptr; }

void JsonSerialValue::Element(int32_t i, const std::function<void(const SerialValue&)>& f) const
{
    if (!v_.IsArray() || i < 0 || static_cast<size_t>(i) >= v_.arr.size()) return;
    JsonSerialValue e(v_.arr[static_cast<size_t>(i)], root_);
    f(e);
}

void JsonSerialValue::Fields(const std::function<void(const char*, const SerialValue&)>& f) const
{
    if (!v_.IsObject()) return;
    for (const auto& [name, value] : v_.obj) {
        JsonSerialValue fv(value, root_);
        f(name.c_str(), fv);
    }
}

} // namespace UnityEngine
