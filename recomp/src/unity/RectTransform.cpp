#include "unity/RectTransform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "unity/Mathf.h"

namespace unity {

using mono::D;
using mono::F;

namespace {

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

float FromBits(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// The engine's "has it changed" tests compare as floats (-0 equals 0): a value that differs
// only in the signs of zeros isn't written (a canvas keeps its (0, 0, 0, 1) rotation under a
// camera turned (-0, -0, -0, 1); a rect of no size keeps (0, 0, 0, 0)).
bool Equal(Vector2 a, Vector2 b) { return a.x == b.x && a.y == b.y; }
bool Equal(Vector3f a, Vector3f b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool Equal(Quaternionf a, Quaternionf b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
bool Equal(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; }

// The float's bits as an integer that orders like the float (CompareFloatRobust's).
int64_t Ordered(float f)
{
    uint32_t u = Bits(f);
    if (u & 0x80000000u) return static_cast<int64_t>(static_cast<int32_t>(0x80000000u - u));
    return static_cast<int64_t>(u);
}

} // namespace

bool CompareFloatRobust(float a, float b, int maxUlps)
{
    bool negA = (Bits(a) & 0x80000000u) != 0;
    bool negB = (Bits(b) & 0x80000000u) != 0;
    if (negA != negB) return a == b; // (+0 and -0)
    int64_t d = Ordered(a) - Ordered(b);
    return (d < 0 ? -d : d) <= maxUlps;
}

bool CompareVector2fRobust(Vector2 a, Vector2 b, int maxUlps)
{
    return CompareFloatRobust(a.x, b.x, maxUlps) && CompareFloatRobust(a.y, b.y, maxUlps);
}

namespace rect_layout {

// CalculateRectInParentSpace: pw * anchor + px, per axis.
AnchorSpan Anchors(const Rect& parent, Vector2 anchorMin, Vector2 anchorMax)
{
    AnchorSpan a;
    float t = parent.width * anchorMin.x;
    a.minX = t + parent.x;
    t = parent.height * anchorMin.y;
    a.minY = t + parent.y;
    t = parent.width * anchorMax.x;
    a.maxX = t + parent.x;
    t = parent.height * anchorMax.y;
    a.maxY = t + parent.y;
    return a;
}

// UpdatePosAndRect's rect.
Rect RectFor(const AnchorSpan& a, Vector2 sizeDelta, Vector2 pivot)
{
    float spanX = a.maxX - a.minX;
    float spanY = a.maxY - a.minY;
    float w = spanX + sizeDelta.x;
    float h = spanY + sizeDelta.y;
    float px = w * pivot.x;
    float py = h * pivot.y;
    return Rect{-px, -py, w, h};
}

// CalculateLocalPosition2.
Vector2 LocalPositionFor(const AnchorSpan& a, Vector2 anchoredPosition, Vector2 pivot)
{
    float baseX = a.minX + anchoredPosition.x;
    float baseY = a.minY + anchoredPosition.y;
    float spanX = a.maxX - a.minX;
    float spanY = a.maxY - a.minY;
    float offX = spanX * pivot.x;
    float offY = spanY * pivot.y;
    return Vector2{baseX + offX, baseY + offY};
}

// UpdateRectTransform's reverse.
Vector2 AnchoredPositionFor(const AnchorSpan& a, Vector2 localPosition, Vector2 pivot)
{
    float spanX = a.maxX - a.minX;
    float spanY = a.maxY - a.minY;
    float offX = spanX * pivot.x;
    float offY = spanY * pivot.y;
    float refX = offX + a.minX;
    float refY = offY + a.minY;
    return Vector2{localPosition.x - refX, localPosition.y - refY};
}

} // namespace rect_layout

// CalculateGlobalMatrix's own level: each product (±2a)·b, the sums in this order, the
// columns times the scale's components.
Matrix4x4f LocalMatrix(Vector3f t, Quaternionf q, Vector3f s)
{
    const float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    Matrix4x4f m;
    float a, b;
    // column 0
    a = (-y2) * q.y;
    b = (-z2) * q.z;
    m.c[0][0] = (a + b) + 1.0f;
    a = y2 * q.x;
    b = z2 * q.w;
    m.c[0][1] = (a + b) + 0.0f;
    a = (-y2) * q.w;
    b = z2 * q.x;
    m.c[0][2] = (a + b) + 0.0f;
    // column 1
    a = (-z2) * q.w;
    b = x2 * q.y;
    m.c[1][0] = a + b;
    a = (-z2) * q.z;
    b = (-x2) * q.x;
    m.c[1][1] = (a + b) + 1.0f;
    a = z2 * q.y;
    b = x2 * q.w;
    m.c[1][2] = a + b;
    // column 2
    a = x2 * q.z;
    b = y2 * q.w;
    m.c[2][0] = a + b;
    a = (-x2) * q.w;
    b = y2 * q.z;
    m.c[2][1] = a + b;
    a = (-x2) * q.x;
    b = (-y2) * q.y;
    m.c[2][2] = (a + b) + 1.0f;
    const float scale[3] = {s.x, s.y, s.z};
    for (int k = 0; k < 3; ++k) {
        for (int r = 0; r < 3; ++r) m.c[k][r] = m.c[k][r] * scale[k];
        m.c[k][3] = 0.0f;
    }
    m.c[3][0] = t.x;
    m.c[3][1] = t.y;
    m.c[3][2] = t.z;
    m.c[3][3] = 1.0f;
    return m;
}

// One level up: P's columns times m's: Pc0 * m.x + (Pc1 * m.y + Pc2 * m.z) (+ P's translation
// for the last column); the bottom row (0, 0, 0, 1).
Matrix4x4f ParentTimes(const Matrix4x4f& p, const Matrix4x4f& m)
{
    Matrix4x4f out;
    for (int k = 0; k < 4; ++k) {
        for (int r = 0; r < 3; ++r) {
            float a = p.c[0][r] * m.c[k][0];
            float b = p.c[1][r] * m.c[k][1];
            float c = p.c[2][r] * m.c[k][2];
            float v = a + (b + c);
            if (k == 3) v = v + p.c[3][r];
            out.c[k][r] = v;
        }
        out.c[k][3] = k == 3 ? 1.0f : 0.0f;
    }
    return out;
}

// TransformPoint's step: v = S p; ((m1 v.y + m2 v.z) + (m0 v.x + v)) + T, with m0..m2 the
// columns of (R - I).
Vector3f TransformStep(Vector3f p, Vector3f t, Quaternionf q, Vector3f s)
{
    const float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    float m0[3], m1[3], m2[3];
    m0[0] = ((-y2) * q.y) - (z2 * q.z);
    m0[1] = (y2 * q.x) + (z2 * q.w);
    m0[2] = (z2 * q.x) - (y2 * q.w);
    m1[0] = (x2 * q.y) - (z2 * q.w);
    m1[1] = ((-z2) * q.z) - (x2 * q.x);
    m1[2] = (z2 * q.y) + (x2 * q.w);
    m2[0] = (x2 * q.z) + (y2 * q.w);
    m2[1] = (y2 * q.z) - (x2 * q.w);
    m2[2] = ((-x2) * q.x) - (y2 * q.y);
    const float v[3] = {s.x * p.x, s.y * p.y, s.z * p.z};
    const float tt[3] = {t.x, t.y, t.z};
    float out[3];
    for (int r = 0; r < 3; ++r) {
        float a = m1[r] * v[1];
        float b = m2[r] * v[2];
        float c = m0[r] * v[0];
        float left = a + b;
        float right = c + v[r];
        out[r] = (left + right) + tt[r];
    }
    return Vector3f{out[0], out[1], out[2]};
}

// Matrix4x4.MultiplyPoint: each row's sum in doubles, rounded to float when stored; divided by
// w (1 / w rounded, then the product rounded).
Vector3f MultiplyPoint(const Matrix4x4f& m, Vector3f p)
{
    auto row = [&](int r) {
        return F(((D(m.c[0][r]) * D(p.x) + D(m.c[1][r]) * D(p.y)) + D(m.c[2][r]) * D(p.z)) + D(m.c[3][r]));
    };
    Vector3f out{row(0), row(1), row(2)};
    float w = row(3);
    float inv = F(1.0 / D(w));
    out.x = F(D(out.x) * D(inv));
    out.y = F(D(out.y) * D(inv));
    out.z = F(D(out.z) * D(inv));
    return out;
}

void LocalCorners(const Rect& r, Vector3f out[4])
{
    float x = r.x;
    float y = r.y;
    float xMax = F(D(r.width) + D(r.x));
    float yMax = F(D(r.height) + D(r.y));
    out[0] = Vector3f{x, y, 0.0f};
    out[1] = Vector3f{x, yMax, 0.0f};
    out[2] = Vector3f{xMax, yMax, 0.0f};
    out[3] = Vector3f{xMax, y, 0.0f};
}

float CanvasScalerScaleWithScreenSize(float screenWidth, float screenHeight, Vector2 referenceResolution, int screenMatchMode,
                                      float matchWidthOrHeight)
{
    switch (screenMatchMode) {
    case 0: { // MatchWidthOrHeight: 2 to the lerp of the log2 ratios (Mathf.Log(f, 2): Math.Log(f) / Math.Log(2))
        float logWidth = F(std::log(D(F(D(screenWidth) / D(referenceResolution.x)))) / std::log(2.0));
        float logHeight = F(std::log(D(F(D(screenHeight) / D(referenceResolution.y)))) / std::log(2.0));
        float logWeighted = Mathf::Lerp(logWidth, logHeight, matchWidthOrHeight);
        return F(std::pow(2.0, D(logWeighted)));
    }
    case 1: // Expand
        return Mathf::Min(F(D(screenWidth) / D(referenceResolution.x)), F(D(screenHeight) / D(referenceResolution.y)));
    case 2: // Shrink
        return Mathf::Max(F(D(screenWidth) / D(referenceResolution.x)), F(D(screenHeight) / D(referenceResolution.y)));
    default:
        return 0.0f;
    }
}

// ---- TransformNode ----

TransformNode::~TransformNode()
{
    if (parent_ != nullptr) {
        auto& siblings = parent_->children_;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }
    for (TransformNode* child : children_) child->parent_ = nullptr;
}

void TransformNode::SetParent(TransformNode* parent)
{
    if (parent_ != nullptr) {
        auto& siblings = parent_->children_;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }
    parent_ = parent;
    if (parent_ != nullptr) parent_->children_.push_back(this);
    MarkTransformChanged();
    std::vector<TransformNode*> stack{this};
    while (!stack.empty()) {
        TransformNode* n = stack.back();
        stack.pop_back();
        n->OnParentChanged();
        for (TransformNode* c : n->children_) stack.push_back(c);
    }
}

void TransformNode::MarkTransformChanged()
{
    std::vector<TransformNode*> stack{this};
    while (!stack.empty()) {
        TransformNode* n = stack.back();
        stack.pop_back();
        if (n->AsRect() != nullptr) n->transformChanged_ = true;
        for (TransformNode* c : n->children_) stack.push_back(c);
    }
}

void TransformNode::SetLocalPosition(Vector3f v)
{
    localPosition_ = v;
    MarkTransformChanged();
}

void TransformNode::SetLocalRotation(Quaternionf q)
{
    localRotation_ = q;
    MarkTransformChanged();
}

void TransformNode::SetLocalScale(Vector3f s)
{
    localScale_ = s;
    MarkTransformChanged();
}

Matrix4x4f TransformNode::localToWorldMatrix() const
{
    Matrix4x4f m = LocalMatrix(localPosition_, localRotation_, localScale_);
    for (const TransformNode* p = parent_; p != nullptr; p = p->parent_)
        m = ParentTimes(LocalMatrix(p->localPosition_, p->localRotation_, p->localScale_), m);
    return m;
}

Vector3f TransformNode::position() const
{
    Vector3f p = localPosition_;
    for (const TransformNode* a = parent_; a != nullptr; a = a->parent_)
        p = TransformStep(p, a->localPosition_, a->localRotation_, a->localScale_);
    return p;
}

Vector3f TransformNode::TransformPoint(Vector3f p) const
{
    for (const TransformNode* n = this; n != nullptr; n = n->parent_)
        p = TransformStep(p, n->localPosition_, n->localRotation_, n->localScale_);
    return p;
}

// ---- RectTransform ----

int RectTransform::enterCounter_ = 0;

void RectTransform::Load(Vector2 anchorMin, Vector2 anchorMax, Vector2 anchoredPosition, Vector2 sizeDelta, Vector2 pivot,
                         Vector3f localPosition, Quaternionf localRotation, Vector3f localScale)
{
    anchorMin_ = anchorMin;
    anchorMax_ = anchorMax;
    anchoredPosition_ = anchoredPosition;
    sizeDelta_ = sizeDelta;
    pivot_ = pivot;
    localPosition_ = localPosition;
    localRotation_ = localRotation;
    localScale_ = localScale;
    rectDirty_ = true;
    transformChanged_ = false;
}

Rect RectTransform::ParentRect()
{
    RectTransform* p = parent_ != nullptr ? parent_->AsRect() : nullptr;
    if (p == nullptr) return Rect{};
    if (p->rectDirty_) p->UpdatePosAndRect();
    return p->rect_;
}

bool RectTransform::UpdatePosAndRect()
{
    rect_layout::AnchorSpan span = rect_layout::Anchors(ParentRect(), anchorMin_, anchorMax_);
    Vector2 lp = rect_layout::LocalPositionFor(span, anchoredPosition_, pivot_);
    if (lp.x != localPosition_.x || lp.y != localPosition_.y)
        SetLocalPositionQuiet(Vector3f{lp.x, lp.y, localPosition_.z});
    Rect r = rect_layout::RectFor(span, sizeDelta_, pivot_);
    // Stored only if it compares unequal as floats: a rect of no size, (-0, -0, 0, 0) from a
    // pivot's products, keeps the (0, 0, 0, 0) it had.
    bool changed = !Equal(r, rect_);
    if (changed) rect_ = r;
    rectDirty_ = false;
    return changed;
}

void RectTransform::UpdatePosAndRectRecursive(bool sendMessage)
{
    if (UpdatePosAndRect()) {
        for (TransformNode* child : children_)
            if (RectTransform* r = child->AsRect()) r->UpdatePosAndRectRecursive(sendMessage);
    }
    if (sendMessage && onDimensionsChange) onDimensionsChange(*this);
}

void RectTransform::UpdateAnchorPositionIfTransformChanged(bool robust)
{
    if (enterCounter_ > 1 || !transformChanged_) return;
    RectTransform* top = this;
    for (TransformNode* p = parent_; p != nullptr; p = p->parent()) {
        RectTransform* r = p->AsRect();
        if (r == nullptr || !r->transformChanged_) break;
        top = r;
    }
    // Depth first, parents before their children.
    std::vector<TransformNode*> stack{top};
    while (!stack.empty()) {
        TransformNode* n = stack.back();
        stack.pop_back();
        if (RectTransform* r = n->AsRect()) {
            if (r->transformChanged_) {
                r->transformChanged_ = false;
                r->UpdateRectTransform(robust);
            }
        }
        for (auto it = n->children().rbegin(); it != n->children().rend(); ++it) stack.push_back(*it);
    }
}

void RectTransform::UpdateRectTransform(bool robust)
{
    rect_layout::AnchorSpan span = rect_layout::Anchors(ParentRect(), anchorMin_, anchorMax_);
    Vector2 anch = rect_layout::AnchoredPositionFor(span, Vector2{localPosition_.x, localPosition_.y}, pivot_);
    if (!robust) {
        anchoredPosition_ = anch;
        return;
    }
    if (CompareVector2fRobust(anchoredPosition_, anch, 10)) return;
    anchoredPosition_ = anch;
    UpdatePosAndRectRecursive(true);
}

Vector2 RectTransform::anchoredPosition()
{
    UpdateAnchorPositionIfTransformChanged(true);
    return anchoredPosition_;
}

Rect RectTransform::rect()
{
    UpdateAnchorPositionIfTransformChanged(true);
    if (rectDirty_) UpdatePosAndRectRecursive(false);
    return rect_;
}

void RectTransform::RobustSet(Vector2& field, Vector2 v)
{
    ++enterCounter_;
    UpdateAnchorPositionIfTransformChanged(true);
    if (!CompareVector2fRobust(field, v, 10)) {
        field = v;
        UpdatePosAndRectRecursive(true);
    }
    --enterCounter_;
}

void RectTransform::SetAnchorMin(Vector2 v) { RobustSet(anchorMin_, v); }
void RectTransform::SetAnchorMax(Vector2 v) { RobustSet(anchorMax_, v); }
void RectTransform::SetAnchoredPosition(Vector2 v) { RobustSet(anchoredPosition_, v); }
void RectTransform::SetSizeDelta(Vector2 v) { RobustSet(sizeDelta_, v); }
void RectTransform::SetPivot(Vector2 v) { RobustSet(pivot_, v); }

void RectTransform::GetLocalCorners(Vector3f out[4]) { LocalCorners(rect(), out); }

void RectTransform::GetWorldCorners(Vector3f out[4])
{
    GetLocalCorners(out);
    Matrix4x4f m = localToWorldMatrix();
    for (int i = 0; i < 4; ++i) out[i] = MultiplyPoint(m, out[i]);
}

void RectTransform::UpdateOverlayCanvas(int screenWidth, int screenHeight, float scaleFactor)
{
    SetAnchorMin(Vector2{0.0f, 0.0f});
    SetAnchorMax(Vector2{0.0f, 0.0f});
    SetPivot(Vector2{0.5f, 0.5f});
    // CanvasManager::GetDisplaySize: the screen's ints as floats.
    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);
    Vector2 size{w / scaleFactor, h / scaleFactor};
    if (!Equal(size, sizeDelta_)) SetSizeDelta(size);
    Vector3f centre{w * 0.5f, h * 0.5f, 0.0f};
    float dx = anchoredPosition_.x - centre.x;
    float dy = anchoredPosition_.y - centre.y;
    float dx2 = dx * dx;
    float dy2 = dy * dy;
    if (dx2 + dy2 > FromBits(0x322BCC76u)) SetAnchoredPosition(Vector2{centre.x, centre.y});
    // SetGlobalMatrixLossy(centre, identity, scale): a root's local values.
    if (!Equal(localPosition_, centre)) SetLocalPosition(centre);
    if (!Equal(localRotation_, Quaternionf{})) SetLocalRotation(Quaternionf{});
    Vector3f scale{scaleFactor, scaleFactor, scaleFactor};
    if (!Equal(localScale_, scale)) SetLocalScale(scale);
}

void RectTransform::UpdateCameraCanvas(const OrthoCamera& camera, float scaleFactor, float planeDistance, bool align)
{
    SetAnchorMin(Vector2{0.0f, 0.0f});
    SetAnchorMax(Vector2{0.0f, 0.0f});
    SetPivot(Vector2{0.5f, 0.5f});
    const Rect& r = camera.pixelRect;
    float aspect = camera.pixelWidth / camera.pixelHeight;
    // GetFrustumPlaneSizeAt, orthographic.
    float frustumH = camera.orthographicSize + camera.orthographicSize;
    float frustumW = frustumH * aspect;
    float k;
    Vector2 size;
    if (std::fabs(r.height) > 0.0f) {
        k = frustumH / r.height;
        size = k == 0.0f ? Vector2{0.0f, 0.0f} : Vector2{frustumW / k, frustumH / k};
    } else {
        k = 1.0f;
        size = Vector2{r.width, r.height};
    }
    Vector2 sd{size.x / scaleFactor, size.y / scaleFactor};
    if (!Equal(sd, sizeDelta_)) SetSizeDelta(sd);
    float s = k * scaleFactor;
    Vector3f scale{s, s, s};
    if (!Equal(localScale_, scale)) localScale_ = scale; // SetLocalScaleWithoutNotification
    if (align) AlignWithCamera(camera, planeDistance);
}

void RectTransform::AlignWithCamera(const OrthoCamera& camera, float planeDistance)
{
    const Quaternionf& q = camera.rotation;
    // The offset planeDistance along the camera's forward (aim (0, 0, 1)), turned.
    const float ox = planeDistance * 0.0f, oy = planeDistance * 0.0f, oz = planeDistance * 1.0f;
    const float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    const float xx = q.x * x2, yy = q.y * y2, zz = q.z * z2;
    const float xy = q.x * y2, xz = q.x * z2, yz = q.y * z2;
    const float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    float rx = (((1.0f - (zz + yy)) * ox) + ((xy - wz) * oy)) + ((wy + xz) * oz);
    float ry = (((1.0f - (zz + xx)) * oy) + ((wz + xy) * ox)) + ((yz - wx) * oz);
    float rz = (((wx + yz) * oy) + ((xz - wy) * ox)) + ((1.0f - (yy + xx)) * oz);
    Vector3f target{camera.position.x + rx, camera.position.y + ry, camera.position.z + rz};
    Vector3f pos = position();
    float dx = target.x - pos.x, dy = target.y - pos.y, dz = target.z - pos.z;
    float d2 = (dx * dx + dy * dy) + dz * dz;
    const float eps = 1e-5f;
    bool changed = false;
    if (d2 > eps * eps) {
        // (A root canvas: the world position is the local one.)
        SetLocalPosition(target);
        changed = true;
    }
    if (!Equal(localRotation_, q)) {
        SetLocalRotation(q);
        changed = true;
    }
    if (changed) UpdateAnchorPositionIfTransformChanged(false);
}

} // namespace unity
