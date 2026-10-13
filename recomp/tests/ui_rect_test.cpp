// RectTransform layout against the game (unity/RectTransform.h): the UI oracle's recordings
// (`-oracle-ui=<script>`, data/oracle/ui_<script>.jsonl; skipped if there's none) hold, frame
// by frame, every RectTransform under an active root canvas whenever anything of it changed,
// as float bits. Two passes, both bit for bit:
//
//  1. row by row: each recorded row's rect from its parent's recorded rect, its anchors,
//     sizeDelta and pivot; its localPosition from its anchoredPosition (or, for a position
//     written directly and kept, its anchoredPosition from its localPosition within the
//     setters' 10 ulps: Unity's rule, counted apart); its GetWorldCorners through the recorded
//     chain's local values (localToWorldMatrix, MultiplyPoint); Transform.position; each root
//     canvas's own RectTransform made from nothing by UpdateOverlayCanvas / UpdateCameraCanvas
//     (the screen, the scale factor, the camera); the CanvasScaler's scale factor;
//  2. replayed: the hierarchy built as unity::RectTransform objects, each frame's recorded
//     values put in through the setters (anchors, sizeDelta, pivot, anchoredPosition, or the
//     localPosition when the game wrote that), and every live object's rect, anchoredPosition,
//     localPosition, world corners and position read back after each frame.
//
// Usage: ui_rect_test <ui_*.jsonl>...   (ML_UI_DEBUG=1: the first mismatches in detail)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "unity/RectTransform.h"
#include "util/Json.h"

using namespace unity;

namespace {

float FromBits(int64_t v)
{
    uint32_t u = static_cast<uint32_t>(v);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

bool Same(float a, float b) { return Bits(a) == Bits(b); }

struct NodeInfo {
    int parent = 0;
    std::string name, path;
    bool rect = false;
};

struct CameraInfo {
    bool present = false;
    bool orthographic = false;
    float size = 0.0f, aspect = 0.0f;
    Rect pixelRect, viewport;
    Vector3f position;
    Quaternionf rotation;
    int pixelWidth = 0, pixelHeight = 0;
};

struct CanvasInfo {
    bool present = false;
    int renderMode = 0;
    float scaleFactor = 1.0f;
    bool root = false;
    bool hasCamera = false; // (its worldCamera set)
    float planeDistance = 0.0f;
    CameraInfo camera;
};

// The last recorded values of an object.
struct State {
    bool x = false; // a plain Transform above a root canvas
    Vector2 anchorMin, anchorMax, anchoredPosition, sizeDelta, pivot;
    Vector3f lp, ls;
    Quaternionf lr;
    Rect rect;
    Vector3f corners[4];
    bool hasWp = false;
    Vector3f wp;
    CanvasInfo canvas;
    int frame = -1;
};

struct Scaler {
    int mode = 0, match = 0;
    float matchWidthOrHeight = 0.0f;
    Vector2 refRes;
    float scaleFactor = 1.0f;
};

struct Counter {
    long checked = 0, bad = 0;
    void Add(bool ok)
    {
        ++checked;
        if (!ok) ++bad;
    }
};

Vector2 V2(const json::Value& a, size_t i) { return Vector2{FromBits(a[i].AsInt()), FromBits(a[i + 1].AsInt())}; }
Vector3f V3(const json::Value& a, size_t i) { return Vector3f{FromBits(a[i].AsInt()), FromBits(a[i + 1].AsInt()), FromBits(a[i + 2].AsInt())}; }
Quaternionf Q4(const json::Value& a, size_t i)
{
    return Quaternionf{FromBits(a[i].AsInt()), FromBits(a[i + 1].AsInt()), FromBits(a[i + 2].AsInt()), FromBits(a[i + 3].AsInt())};
}

bool SameV(Vector2 a, Vector2 b) { return Same(a.x, b.x) && Same(a.y, b.y); }
bool SameV(Vector3f a, Vector3f b) { return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z); }
bool SameQ(Quaternionf a, Quaternionf b) { return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z) && Same(a.w, b.w); }
bool SameR(const Rect& a, const Rect& b) { return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.width, b.width) && Same(a.height, b.height); }

class Recording {
public:
    std::unordered_map<int, NodeInfo> nodes;
    std::unordered_map<int, State> state;
    std::map<std::string, Scaler> scalers;
    int debugShown = 0;
    bool debug = std::getenv("ML_UI_DEBUG") != nullptr;

    // pass 1
    Counter rows, rect, lpForward, worldCorners, worldPosition, rootCanvas, scaleFactor;
    long lpKept = 0, lpBad = 0, rootSkipped = 0, rootWorld = 0, scalerSkipped = 0, rectKept = 0, reparented = 0, chainIncomplete = 0;
    // pass 2
    Counter replayRect, replayAnchored, replayLocal, replayCorners, replayPosition;
    long replayFrames = 0;

    std::unordered_map<int, std::unique_ptr<TransformNode>> live;

    void Note(const char* what, int id, int frame)
    {
        if (!debug && debugShown >= 12) return;
        ++debugShown;
        auto it = nodes.find(id);
        std::printf("  frame %d %s: %s MISMATCH\n", frame, it != nodes.end() ? it->second.path.c_str() : "?", what);
    }

    Rect ParentRect(int id)
    {
        auto n = nodes.find(id);
        if (n == nodes.end() || n->second.parent == 0) return Rect{};
        auto p = nodes.find(n->second.parent);
        if (p == nodes.end() || !p->second.rect) return Rect{};
        auto s = state.find(n->second.parent);
        if (s == state.end()) return Rect{};
        return s->second.rect;
    }

    // localToWorldMatrix through the recorded local values of the object and what's above it.
    bool Matrix(int id, Matrix4x4f& out)
    {
        const State& own = state.at(id);
        out = LocalMatrix(own.lp, own.lr, own.ls);
        for (int p = nodes.at(id).parent; p != 0; p = nodes.at(p).parent) {
            auto s = state.find(p);
            if (s == state.end()) return false;
            out = ParentTimes(LocalMatrix(s->second.lp, s->second.lr, s->second.ls), out);
        }
        return true;
    }

    bool Position(int id, Vector3f& out)
    {
        out = state.at(id).lp;
        for (int p = nodes.at(id).parent; p != 0; p = nodes.at(p).parent) {
            auto s = state.find(p);
            if (s == state.end()) return false;
            out = TransformStep(out, s->second.lp, s->second.lr, s->second.ls);
        }
        return true;
    }

    void ReadElement(const json::Value& row, State& s)
    {
        const json::Value& rt = row["rt"];
        s.x = false;
        s.anchorMin = V2(rt, 0);
        s.anchorMax = V2(rt, 2);
        s.anchoredPosition = V2(rt, 4);
        s.sizeDelta = V2(rt, 6);
        s.pivot = V2(rt, 8);
        s.lp = V3(rt, 10);
        s.lr = Q4(rt, 13);
        s.ls = V3(rt, 17);
        s.rect = Rect{FromBits(rt[20].AsInt()), FromBits(rt[21].AsInt()), FromBits(rt[22].AsInt()), FromBits(rt[23].AsInt())};
        for (int i = 0; i < 4; ++i) s.corners[i] = V3(rt, 24 + 3 * static_cast<size_t>(i));
        s.hasWp = false;
        if (const json::Value* wp = row.Find("wp")) {
            s.hasWp = true;
            s.wp = V3(*wp, 0);
        }
        s.canvas = CanvasInfo{};
        if (const json::Value* cv = row.Find("cv")) {
            s.canvas.present = true;
            s.canvas.renderMode = static_cast<int>((*cv)[0].AsInt());
            s.canvas.scaleFactor = FromBits((*cv)[1].AsInt());
            s.canvas.root = (*cv)[6].b;
            s.canvas.planeDistance = FromBits((*cv)[12].AsInt());
            s.canvas.hasCamera = !(*cv)[13].IsNull();
            if (const json::Value* cam = row.Find("cam")) {
                CameraInfo& c = s.canvas.camera;
                c.present = true;
                c.orthographic = (*cam)[0].b;
                c.size = FromBits((*cam)[1].AsInt());
                c.aspect = FromBits((*cam)[2].AsInt());
                c.pixelRect = Rect{FromBits((*cam)[4].AsInt()), FromBits((*cam)[5].AsInt()), FromBits((*cam)[6].AsInt()), FromBits((*cam)[7].AsInt())};
                c.viewport = Rect{FromBits((*cam)[8].AsInt()), FromBits((*cam)[9].AsInt()), FromBits((*cam)[10].AsInt()), FromBits((*cam)[11].AsInt())};
                c.position = V3(*cam, 12);
                c.rotation = Q4(*cam, 15);
                c.pixelWidth = static_cast<int>((*cam)[21].AsInt());
                c.pixelHeight = static_cast<int>((*cam)[22].AsInt());
            }
        }
    }

    // ---- pass 1: the row against what's recorded around it ----
    void CheckRow(int id, int frame, int screenW, int screenH)
    {
        const State& s = state.at(id);
        if (s.x) return; // a plain Transform: what's below it uses its values
        rows.Add(true);
        rect_layout::AnchorSpan span = rect_layout::Anchors(ParentRect(id), s.anchorMin, s.anchorMax);
        Rect r = rect_layout::RectFor(span, s.sizeDelta, s.pivot);
        if (SameR(r, s.rect)) {
            rect.Add(true);
        } else if (r.x == s.rect.x && r.y == s.rect.y && r.width == s.rect.width && r.height == s.rect.height) {
            // UpdatePosAndRect stores a rect only if it compares unequal as floats: one that
            // differs from the stored one in the signs of zeros alone leaves it.
            ++rectKept;
        } else {
            rect.Add(false);
            Note("rect", id, frame);
        }

        Vector2 lp = rect_layout::LocalPositionFor(span, s.anchoredPosition, s.pivot);
        if (Same(lp.x, s.lp.x) && Same(lp.y, s.lp.y)) {
            lpForward.Add(true);
        } else {
            Vector2 back = rect_layout::AnchoredPositionFor(span, Vector2{s.lp.x, s.lp.y}, s.pivot);
            if (CompareVector2fRobust(s.anchoredPosition, back, 10)) {
                ++lpKept;
            } else {
                ++lpBad;
                Note("localPosition", id, frame);
            }
        }

        Matrix4x4f m;
        if (!Matrix(id, m)) {
            ++chainIncomplete; // an ancestor not recorded active: a hole in the recording
            Note("chain (an ancestor not recorded)", id, frame);
        } else {
            Vector3f local[4];
            LocalCorners(s.rect, local);
            bool ok = true;
            for (int i = 0; i < 4; ++i) ok = ok && SameV(MultiplyPoint(m, local[i]), s.corners[i]);
            worldCorners.Add(ok);
            if (!ok) Note("world corners", id, frame);
        }
        if (s.hasWp) {
            Vector3f p;
            if (Position(id, p)) {
                bool ok = SameV(p, s.wp);
                worldPosition.Add(ok);
                if (!ok) Note("position", id, frame);
            }
        }

        if (s.canvas.present && s.canvas.root) CheckRootCanvas(id, frame, screenW, screenH);
    }

    void CheckRootCanvas(int id, int frame, int screenW, int screenH)
    {
        const State& s = state.at(id);
        const NodeInfo& n = nodes.at(id);
        // The CanvasScaler's factor, if the canvas has one (uiStart's, by path).
        auto sc = scalers.find(n.path);
        if (sc != scalers.end() && s.canvas.renderMode != 2) {
            float want = 0.0f;
            bool known = true;
            if (sc->second.mode == 1)
                want = CanvasScalerScaleWithScreenSize(static_cast<float>(screenW), static_cast<float>(screenH), sc->second.refRes,
                                                       sc->second.match, sc->second.matchWidthOrHeight);
            else if (sc->second.mode == 0)
                want = sc->second.scaleFactor;
            else
                known = false;
            if (known) {
                // Canvas::SetScaleFactor: at least 1e-4.
                if (want < 1e-4f) want = 1e-4f;
                bool ok = Same(want, s.canvas.scaleFactor);
                scaleFactor.Add(ok);
                if (!ok) Note("CanvasScaler scale factor", id, frame);
            } else {
                ++scalerSkipped;
            }
        }
        // The root canvas's RectTransform made from nothing.
        RectTransform rt;
        rt.Load(Vector2{}, Vector2{}, Vector2{}, Vector2{}, Vector2{0.5f, 0.5f}, Vector3f{}, Quaternionf{}, Vector3f{1.0f, 1.0f, 1.0f});
        int mode = s.canvas.renderMode;
        if (mode == 2) {
            ++rootWorld; // WorldSpace: the RectTransform as authored, nothing made
            return;
        }
        // Canvas::GetRenderMode: a ScreenSpaceCamera canvas without a camera is an overlay.
        if (mode == 1 && !s.canvas.hasCamera) mode = 0;
        if (mode == 1 && !(s.canvas.camera.present && s.canvas.camera.orthographic)) mode = -1;
        if (mode == 0) {
            rt.UpdateOverlayCanvas(screenW, screenH, s.canvas.scaleFactor);
        } else if (mode == 1) {
            const CameraInfo& c = s.canvas.camera;
            RectTransform::OrthoCamera cam;
            cam.orthographicSize = c.size;
            cam.pixelRect = c.pixelRect;
            cam.pixelWidth = static_cast<float>(c.pixelWidth);
            cam.pixelHeight = static_cast<float>(c.pixelHeight);
            cam.position = c.position;
            cam.rotation = c.rotation;
            rt.UpdateCameraCanvas(cam, s.canvas.scaleFactor, s.canvas.planeDistance, true);
        } else {
            ++rootSkipped;
            return;
        }
        Vector3f corners[4];
        rt.GetWorldCorners(corners);
        bool ok = SameV(rt.anchorMin(), s.anchorMin) && SameV(rt.anchorMax(), s.anchorMax) && SameV(rt.pivot(), s.pivot) &&
                  SameV(rt.sizeDelta(), s.sizeDelta) && SameV(rt.anchoredPosition(), s.anchoredPosition) && SameV(rt.localPosition(), s.lp) &&
                  SameQ(rt.localRotation(), s.lr) && SameV(rt.localScale(), s.ls) && SameR(rt.rect(), s.rect);
        for (int i = 0; i < 4; ++i) ok = ok && SameV(corners[i], s.corners[i]);
        rootCanvas.Add(ok);
        if (!ok) {
            Note(mode == 0 ? "root canvas (overlay)" : "root canvas (camera)", id, frame);
            if (debug)
                std::printf("    port: size (%.9g, %.9g) anchored (%.9g, %.9g) local (%.9g, %.9g, %.9g) scale %.9g | game: size (%.9g, %.9g) anchored "
                            "(%.9g, %.9g) local (%.9g, %.9g, %.9g) scale %.9g\n",
                            rt.sizeDelta().x, rt.sizeDelta().y, rt.anchoredPosition().x, rt.anchoredPosition().y, rt.localPosition().x,
                            rt.localPosition().y, rt.localPosition().z, rt.localScale().x, s.sizeDelta.x, s.sizeDelta.y, s.anchoredPosition.x,
                            s.anchoredPosition.y, s.lp.x, s.lp.y, s.lp.z, s.ls.x);
        }
    }

    // ---- pass 2: the hierarchy as RectTransforms, the recorded values put in ----
    TransformNode* Live(int id)
    {
        auto it = live.find(id);
        if (it != live.end()) return it->second.get();
        const NodeInfo& n = nodes.at(id);
        std::unique_ptr<TransformNode> node;
        if (n.rect) node = std::make_unique<RectTransform>();
        else node = std::make_unique<TransformNode>();
        TransformNode* raw = node.get();
        live[id] = std::move(node);
        if (n.parent != 0 && nodes.count(n.parent)) raw->SetParent(Live(n.parent));
        return raw;
    }

    std::set<int> loaded;

    void Apply(int id, const State& s)
    {
        TransformNode* node = Live(id);
        if (s.x) {
            if (!SameV(node->localPosition(), s.lp)) node->SetLocalPosition(s.lp);
            if (!SameQ(node->localRotation(), s.lr)) node->SetLocalRotation(s.lr);
            if (!SameV(node->localScale(), s.ls)) node->SetLocalScale(s.ls);
            return;
        }
        RectTransform* rt = node->AsRect();
        if (!loaded.count(id)) {
            // First seen (made, or activated): its values as they are.
            rt->Load(s.anchorMin, s.anchorMax, s.anchoredPosition, s.sizeDelta, s.pivot, s.lp, s.lr, s.ls);
            loaded.insert(id);
            return;
        }
        if (!SameQ(rt->localRotation(), s.lr)) rt->SetLocalRotation(s.lr);
        if (!SameV(rt->localScale(), s.ls)) rt->SetLocalScale(s.ls);
        if (!SameV(rt->anchorMin(), s.anchorMin)) rt->SetAnchorMin(s.anchorMin);
        if (!SameV(rt->anchorMax(), s.anchorMax)) rt->SetAnchorMax(s.anchorMax);
        if (!SameV(rt->pivot(), s.pivot)) rt->SetPivot(s.pivot);
        if (!SameV(rt->sizeDelta(), s.sizeDelta)) rt->SetSizeDelta(s.sizeDelta);
        // The position as the game wrote it: through anchoredPosition when the local position
        // is what that makes of it, else the local position itself.
        rect_layout::AnchorSpan span = rect_layout::Anchors(ParentRect(id), s.anchorMin, s.anchorMax);
        Vector2 lp = rect_layout::LocalPositionFor(span, s.anchoredPosition, s.pivot);
        if (Same(lp.x, s.lp.x) && Same(lp.y, s.lp.y)) {
            if (!Same(rt->localPosition().z, s.lp.z)) rt->SetLocalPosition(Vector3f{rt->localPosition().x, rt->localPosition().y, s.lp.z});
            if (!SameV(rt->anchoredPosition(), s.anchoredPosition)) rt->SetAnchoredPosition(s.anchoredPosition);
        } else if (!SameV(rt->localPosition(), s.lp)) {
            rt->SetLocalPosition(s.lp);
        }
    }

    void CompareLive(int frame)
    {
        ++replayFrames;
        for (auto& [id, s] : state) {
            if (s.x) continue;
            auto it = live.find(id);
            if (it == live.end()) continue;
            RectTransform* rt = it->second->AsRect();
            if (rt == nullptr) continue;
            bool ok = SameR(rt->rect(), s.rect);
            replayRect.Add(ok);
            if (!ok) Note("replayed rect", id, frame);
            ok = SameV(rt->anchoredPosition(), s.anchoredPosition);
            replayAnchored.Add(ok);
            if (!ok) Note("replayed anchoredPosition", id, frame);
            ok = SameV(rt->localPosition(), s.lp);
            replayLocal.Add(ok);
            if (!ok) Note("replayed localPosition", id, frame);
            Vector3f c[4];
            rt->GetWorldCorners(c);
            ok = true;
            for (int i = 0; i < 4; ++i) ok = ok && SameV(c[i], s.corners[i]);
            replayCorners.Add(ok);
            if (!ok) Note("replayed world corners", id, frame);
            if (s.hasWp) {
                ok = SameV(rt->position(), s.wp);
                replayPosition.Add(ok);
                if (!ok) Note("replayed position", id, frame);
            }
        }
    }

    void Off(int id)
    {
        state.erase(id);
        loaded.erase(id);
    }

    bool Run(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::string line;
        int frame = -1, screenW = 0, screenH = 0;
        std::vector<int> frameRows;
        auto endFrame = [&]() {
            if (frame < 0) return;
            for (int id : frameRows) CheckRow(id, frame, screenW, screenH);
            for (int id : frameRows) Apply(id, state.at(id));
            CompareLive(frame);
            frameRows.clear();
        };
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            json::Value row = json::Parse(line);
            const std::string& ev = row["ev"].s;
            if (ev == "uiStart") {
                for (const json::Value& sc : row["scalers"].arr) {
                    Scaler s;
                    s.mode = static_cast<int>(sc["mode"].AsInt());
                    s.match = static_cast<int>(sc["match"].AsInt());
                    s.matchWidthOrHeight = FromBits(sc["matchWidthOrHeight"].AsInt());
                    s.refRes = V2(sc["refRes"], 0);
                    s.scaleFactor = FromBits(sc["scaleFactor"].AsInt());
                    scalers[sc["path"].s] = s;
                }
            } else if (ev == "frame") {
                endFrame();
                frame = static_cast<int>(row["f"].AsInt());
                screenW = static_cast<int>(row["sw"].AsInt());
                screenH = static_cast<int>(row["sh"].AsInt());
            } else if (ev == "node") {
                NodeInfo n;
                n.parent = static_cast<int>(row["parent"].AsInt());
                n.name = row["name"].s;
                n.path = row["path"].s;
                n.rect = row["rect"].b;
                int id = static_cast<int>(row["id"].AsInt());
                nodes[id] = n;
                if (const json::Value* re = row.Find("reparented"); re != nullptr && re->b) {
                    // SetParent (keeping its world position: InverseTransformPosition, not
                    // modelled here): the replay takes its next values as recorded.
                    ++reparented;
                    auto it = live.find(id);
                    if (it != live.end()) {
                        it->second->SetParent(n.parent != 0 && nodes.count(n.parent) ? Live(n.parent) : nullptr);
                        loaded.erase(id);
                    }
                }
            } else if (ev == "x") {
                int id = static_cast<int>(row["id"].AsInt());
                State& s = state[id];
                const json::Value& t = row["t"];
                s.x = true;
                s.lp = V3(t, 0);
                s.lr = Q4(t, 3);
                s.ls = V3(t, 7);
                s.frame = frame;
                frameRows.push_back(id);
            } else if (ev == "e") {
                int id = static_cast<int>(row["id"].AsInt());
                ReadElement(row, state[id]);
                state[id].frame = frame;
                frameRows.push_back(id);
            } else if (ev == "off") {
                for (const json::Value& id : row["ids"].arr) Off(static_cast<int>(id.AsInt()));
            }
        }
        endFrame();
        // Element rows only (pass 1 counts the "x" rows of plain Transforms out).
        return true;
    }
};

} // namespace

int main(int argc, char** argv)
{
    bool ok = true;
    int ran = 0;
    for (int a = 1; a < argc; ++a) {
        std::string path = argv[a];
        std::ifstream probe(path);
        if (!probe) {
            std::printf("%s: no recording (the UI oracle's -oracle-ui): skipped\n", path.c_str());
            continue;
        }
        probe.close();
        ++ran;
        Recording r;
        try {
            r.Run(path);
        } catch (const std::exception& e) {
            std::printf("%s: %s\n", path.c_str(), e.what());
            return 1;
        }
        std::printf("%s: %zu objects\n", path.c_str(), r.nodes.size());
        std::printf("row by row (each recorded row against what's recorded around it)\n");
        std::printf("  rect from the parent's                     %7ld match %5ld differ   (and %ld whose zeros' signs the stored rect kept)\n",
                    r.rect.checked - r.rect.bad, r.rect.bad, r.rectKept);
        std::printf("  localPosition from anchoredPosition        %7ld match %5ld differ   (and %ld written directly, kept within 10 ulps)\n",
                    r.lpForward.checked, r.lpBad, r.lpKept);
        std::printf("  world corners (localToWorldMatrix)         %7ld match %5ld differ   (%ld rows whose chain isn't all recorded)\n",
                    r.worldCorners.checked - r.worldCorners.bad, r.worldCorners.bad, r.chainIncomplete);
        std::printf("  Transform.position                         %7ld match %5ld differ\n", r.worldPosition.checked - r.worldPosition.bad,
                    r.worldPosition.bad);
        std::printf("  root canvases' RectTransforms from nothing %7ld match %5ld differ   (%ld world space: as authored; %ld without their camera's data)\n",
                    r.rootCanvas.checked - r.rootCanvas.bad, r.rootCanvas.bad, r.rootWorld, r.rootSkipped);
        std::printf("  CanvasScaler scale factor                  %7ld match %5ld differ   (%ld modes not modelled)\n",
                    r.scaleFactor.checked - r.scaleFactor.bad, r.scaleFactor.bad, r.scalerSkipped);
        std::printf("replayed through unity::RectTransform (%ld frames, every live object after each; %ld reparented, taken as recorded)\n",
                    r.replayFrames, r.reparented);
        std::printf("  rect                                       %7ld match %5ld differ\n", r.replayRect.checked - r.replayRect.bad, r.replayRect.bad);
        std::printf("  anchoredPosition                           %7ld match %5ld differ\n", r.replayAnchored.checked - r.replayAnchored.bad,
                    r.replayAnchored.bad);
        std::printf("  localPosition                              %7ld match %5ld differ\n", r.replayLocal.checked - r.replayLocal.bad,
                    r.replayLocal.bad);
        std::printf("  GetWorldCorners                            %7ld match %5ld differ\n", r.replayCorners.checked - r.replayCorners.bad,
                    r.replayCorners.bad);
        std::printf("  position                                   %7ld match %5ld differ\n", r.replayPosition.checked - r.replayPosition.bad,
                    r.replayPosition.bad);
        bool fileOk = !r.rect.bad && !r.lpBad && !r.chainIncomplete && !r.worldCorners.bad && !r.worldPosition.bad && !r.rootCanvas.bad && !r.scaleFactor.bad &&
                      !r.replayRect.bad && !r.replayAnchored.bad && !r.replayLocal.bad && !r.replayCorners.bad && !r.replayPosition.bad &&
                      r.rect.checked > 0;
        ok = ok && fileOk;
    }
    if (ran == 0) {
        std::printf("no UI recordings: skipped\n");
        return 0;
    }
    std::printf("\n%s\n", ok ? "ALL MATCH" : "MISMATCHES");
    return ok ? 0 : 1;
}
