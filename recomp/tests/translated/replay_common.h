#pragma once
// What the translated replays (enemy_translated_test, tangle_translated_test) share: the
// recording's values as the hand port's replays read them (tests/enemy_replay.h, tangle_test.cpp),
// the tally of rows, the room's colliders from the recording's "collider" lines.
// (Included inside each test's anonymous namespace, after its headers.)

// ---- the recording's values (as tests/enemy_replay.h reads them) ---------------------------------------

float Bits(const Value& v)
{
    int32_t i = static_cast<int32_t>(v.AsInt());
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}
uint32_t U(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
bool Same(float a, float b) { return U(a) == U(b); }
bool Same(Vector2 a, Vector2 b) { return Same(a.x, b.x) && Same(a.y, b.y); }
Vector2 V2(const Value& v) { return {Bits(v[0]), Bits(v[1])}; }
Vector2 XY(UnityEngine::Vector3 v) { return {v.x, v.y}; }
unity::Random::State State(const Value& v)
{
    return {static_cast<uint32_t>(v[0].AsInt()), static_cast<uint32_t>(v[1].AsInt()), static_cast<uint32_t>(v[2].AsInt()),
            static_cast<uint32_t>(v[3].AsInt())};
}
bool SameState(const unity::Random::State& a, const unity::Random::State& b) { return a.s0 == b.s0 && a.s1 == b.s1 && a.s2 == b.s2 && a.s3 == b.s3; }
std::string Str(Vector2 v)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "(%.9g, %.9g)", v.x, v.y);
    return buf;
}
std::string ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
cs::String* S(const std::string& s) { return cs::String::FromUtf8(s); }
std::string Utf8(cs::String* s) { return s ? s->ToUtf8() : std::string(); }

struct Tally {
    int width = 44; // (the row name's column: the hand port's test's)
    std::map<std::string, std::pair<int, int>> rows;
    std::map<std::string, int> shownOf;
    std::vector<std::string> order, info;
    int shown = 0;
    bool debug = false;
    void Add(const std::string& what, bool ok, const std::string& detail = "")
    {
        if (!rows.count(what)) order.push_back(what);
        auto& r = rows[what];
        (ok ? r.first : r.second)++;
        if (!ok && (debug || (shown < 60 && shownOf[what] < 4))) {
            shown++;
            shownOf[what]++;
            std::printf("  %s: %s\n", what.c_str(), detail.c_str());
        }
    }
    void Info(const std::string& what, bool ok, const std::string& detail = "")
    {
        if (std::find(info.begin(), info.end(), what) == info.end()) info.push_back(what);
        Add(what, ok, detail);
    }
    bool Ok() const
    {
        for (const auto& [k, v] : rows)
            if (v.second && std::find(info.begin(), info.end(), k) == info.end()) return false;
        return true;
    }
    void Print() const
    {
        for (const std::string& k : order) {
            const auto& r = rows.at(k);
            const bool isInfo = std::find(info.begin(), info.end(), k) != info.end();
            std::printf("%-*s %6d match %6d differ%s\n", width, k.c_str(), r.first, r.second, isInfo ? "   (not failing)" : "");
        }
    }
};

unity::Collider2D ColliderFrom(const Value& v)
{
    unity::Collider2D c;
    c.path = v["path"].s;
    c.tag = v["tag"].s;
    c.layer = static_cast<int>(v["layer"].AsInt());
    c.enabled = v["enabled"].b;
    c.trigger = v["trigger"].b;
    c.position = V2(v["pos"]);
    c.scale = V2(v["scale"]);
    c.offset = V2(v["offset"]);
    if (const Value* s = v.Find("size")) c.size = V2(*s);
    c.hasWorldBox = c.enabled;
    c.worldMin = V2(v["min"]);
    c.worldMax = V2(v["max"]);
    // Its turn, which the oracle's rows don't carry: a dungeon room's ("Dungeon/<room>/<path>")
    // as the DungeonRoom prefab has it (game::DungeonRoomTurn: the turned door boundaries).
    const size_t room = c.path.rfind("Dungeon/", 0) == 0 ? c.path.find('/', 8) : std::string::npos;
    game::Turn turn;
    if (room != std::string::npos && game::DungeonRoomTurn(c.path.substr(room + 1), turn)) {
        c.rotZ = turn.z;
        c.rotW = turn.w;
        c.matrixScale = turn.scale;
    }
    return c;
}

// A hole region's outline back to the tiles it covers, for the queries, and its convex pieces for
// the physics world (tests/enemy_replay.h's).
unity::Collider2D HoleFrom(const Value& v)
{
    std::vector<std::vector<Vector2>> paths;
    for (const Value& path : v["paths"].arr) {
        paths.emplace_back();
        for (size_t k = 0; k + 1 < path.arr.size(); k += 2) paths.back().push_back({Bits(path[k]), Bits(path[k + 1])});
    }
    auto inside = [&](float x, float y) {
        bool in = false;
        for (const auto& poly : paths)
            for (size_t a = 0, b = poly.size() - 1; a < poly.size(); b = a++)
                if ((poly[a].y > y) != (poly[b].y > y) && x < (poly[b].x - poly[a].x) * (y - poly[a].y) / (poly[b].y - poly[a].y) + poly[a].x) in = !in;
        return in;
    };
    unity::Collider2D c = ColliderFrom(v);
    c.hasWorldBox = true;
    for (int j = 0; j < 11; j++)
        for (int i = 0; i < 20; i++) {
            if (!inside(24.0f * static_cast<float>(i) - 228.0f, 24.0f * static_cast<float>(j) - 120.0f)) continue;
            float x0 = 24.0f * static_cast<float>(i) - 240.0f, x1 = x0 + 24.0f, y0 = 24.0f * static_cast<float>(j) - 132.0f, y1 = y0 + 24.0f;
            if (i == 0) x0 -= 10.0f;
            if (i == 19) x1 += 10.0f;
            if (j == 0) y0 -= 10.0f;
            if (j == 10) y1 += 10.0f;
            c.parts.emplace_back(Vector2{c.position.x + x0, c.position.y + y0}, Vector2{c.position.x + x1, c.position.y + y1});
        }
    unity::SetPolygonColliderShapes(c, paths);
    return c;
}

// The hero's own triggers (his ActionAreas, his weapon's "Collider"): not in the replay's world.
bool HerosTrigger(const std::string& name)
{
    const std::string area = "ActionArea";
    return name == "Collider" || (name.size() >= area.size() && name.compare(name.size() - area.size(), area.size(), area) == 0);
}

std::string Leaf(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
