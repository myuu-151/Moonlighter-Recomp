#include "game/PrefabCatalog.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace game {

namespace {

struct Bundle {
    std::vector<std::string> paths, lower;
};

std::map<std::string, Bundle>& Bundles()
{
    static std::map<std::string, Bundle> bundles;
    return bundles;
}

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// LoadAssetContaining(bundle, filter): the first asset path containing the filter; the
// prefab's name is the file's name.
std::string FindIn(const char* bundle, const std::string& filter)
{
    auto it = Bundles().find(bundle);
    if (it == Bundles().end()) return "";
    std::string f = Lower(filter);
    for (size_t i = 0; i < it->second.lower.size(); i++) {
        if (it->second.lower[i].find(f) == std::string::npos) continue;
        const std::string& p = it->second.paths[i];
        size_t slash = p.rfind('/'), dot = p.rfind('.');
        return p.substr(slash + 1, dot - slash - 1);
    }
    return "";
}

} // namespace

void PrefabCatalog::Load(const json::Value& bundleAssets)
{
    Bundles().clear();
    for (const auto& [name, list] : bundleAssets.obj) {
        Bundle& b = Bundles()[name];
        for (const json::Value& p : list.arr) {
            b.paths.push_back(p.s);
            b.lower.push_back(Lower(p.s));
        }
    }
}

bool PrefabCatalog::Loaded() { return !Bundles().empty(); }

std::string PrefabCatalog::FindEnemy(const std::string& name, const std::vector<Culture>& loaded)
{
    static const std::pair<Culture, std::pair<const char*, const char*>> kBundles[] = {
        {Culture::Golem, {"enemies_golem", "Golem"}},
        {Culture::Forest, {"enemies_forest", "Forest"}},
        {Culture::Desert, {"enemies_desert", "Desert"}},
        {Culture::Tech, {"enemies_tesla", "Tesla"}},
    };
    for (const auto& [culture, bundle] : kBundles) {
        if (std::find(loaded.begin(), loaded.end(), culture) == loaded.end()) continue;
        std::string found = FindIn(bundle.first, std::string("Enemies/") + bundle.second + "/" + name + ".");
        if (!found.empty()) return found;
    }
    return "";
}

} // namespace game
