#include "game/Constants.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "util/Json.h"

namespace game {
namespace Constants {

namespace {
json::Value gValues;
}

void LoadFromText(const std::string& jsonText) { gValues = json::Parse(jsonText); }

bool LoadFromFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    LoadFromText(ss.str());
    return true;
}

int GetInt(const std::string& key)
{
    const json::Value* v = gValues.Find(key);
    if (!v) return 0;
    if (v->IsDouble()) return static_cast<int>(static_cast<int64_t>(v->d));
    if (!v->IsInt()) return 0;
    return static_cast<int>(v->i);
}

float GetFloat(const std::string& key)
{
    const json::Value* v = gValues.Find(key);
    if (!v) return 0.0f;
    if (v->IsInt()) return static_cast<float>(static_cast<double>(v->i));
    if (!v->IsDouble()) return 0.0f;
    return static_cast<float>(v->d);
}

bool GetBool(const std::string& key)
{
    const json::Value* v = gValues.Find(key);
    if (!v || v->kind != json::Value::Kind::Bool) return false;
    return v->b;
}

std::vector<float> GetFloatArray(const std::string& key)
{
    std::vector<float> out;
    const json::Value* v = gValues.Find(key);
    if (!v || !v->IsArray()) return out;
    for (const json::Value& x : v->arr) {
        double d = 0.0;
        if (x.IsDouble()) d = x.d;
        else if (x.IsInt()) d = static_cast<double>(x.i);
        else if (x.IsString()) d = std::strtod(x.s.c_str(), nullptr);
        out.push_back(static_cast<float>(d));
    }
    return out;
}

std::vector<std::string> GetStringArray(const std::string& key)
{
    std::vector<std::string> out;
    const json::Value* v = gValues.Find(key);
    if (!v || !v->IsArray()) return out;
    for (const json::Value& x : v->arr) {
        if (!x.IsString()) return {};
        out.push_back(x.s);
    }
    return out;
}

std::vector<std::vector<float>> GetFloat2DArray(const std::string& key)
{
    std::vector<std::vector<float>> out;
    const json::Value* v = gValues.Find(key);
    if (!v || !v->IsArray()) return out;
    for (const json::Value& row : v->arr) {
        if (!row.IsArray()) return {};
        for (const json::Value& x : row.arr)
            if (!x.IsDouble()) return {};
    }
    for (const json::Value& row : v->arr) {
        out.emplace_back();
        for (const json::Value& x : row.arr) out.back().push_back(static_cast<float>(x.d));
    }
    return out;
}

} // namespace Constants
} // namespace game
