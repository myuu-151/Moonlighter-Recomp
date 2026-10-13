#pragma once
// RectTransform layout as Unity 2019.2 does it, float for float: the native part read from the
// game's UnityPlayer.dll (RectTransform, Canvas and Transform; public symbols from Unity's symbol
// server, tools/unity_dll), the managed part from UnityEngine.CoreModule and UnityEngine.UI.
// Native math is float32 (SSE, no fused multiply-add, no double), in the engine's association;
// managed math is the game's Mono's (doubles, rounded where stored: unity/Mathf.h's D and F).
//
// The pieces, checked against the game by ui_rect (tests/ui_rect_test.cpp) on the UI oracle's
// recordings (`-oracle-ui=<script>`: every RectTransform under an active root canvas, frame by
// frame, as float bits):
//
//  - a rect from the parent's (CalculateRectInParentSpace, UpdatePosAndRect): the anchors placed
//    in the parent's rect, size = anchor span + sizeDelta, origin = -(size * pivot);
//  - localPosition from anchoredPosition (CalculateLocalPosition2) and back
//    (UpdateRectTransform): the anchor reference is the anchors' span at the pivot;
//  - the setters (anchorMin, anchorMax, anchoredPosition, sizeDelta, pivot): a value within 10
//    ulps of the old one (CompareVector2fRobust) is ignored; otherwise stored, and the rect and
//    local position redone, into the children only while a rect changes
//    (UpdatePosAndRectRecursive);
//  - a localPosition written directly (Transform.localPosition, a tween's DOLocalMove) and
//    anything above it moved: the anchoredPosition worked out from it again when next read
//    (UpdateAnchorPositionIfTransformChanged), kept if within 10 ulps (then the localPosition
//    stays as written), else stored and the localPosition redone from it;
//  - a root canvas's own RectTransform (Canvas::UpdateCanvasRectTransform): ScreenSpaceOverlay
//    takes the screen's size over the scale factor, sits at the screen's centre, scaled by the
//    scale factor; ScreenSpaceCamera (orthographic) fills the camera's view at planeDistance;
//  - Transform.localToWorldMatrix (CalculateGlobalMatrix), Transform.position
//    (CalculateGlobalPosition), Transform.TransformPoint, and RectTransform.GetWorldCorners
//    (managed: the local corners through localToWorldMatrix.MultiplyPoint, in doubles);
//  - CanvasScaler's scale factor (UnityEngine.UI, managed: until that assembly is translated).

#include <functional>
#include <vector>

#include "unity/Rect.h"
#include "unity/Vector2.h"

namespace unity {

struct Vector3f {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Quaternionf {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

// UnityEngine.Matrix4x4: c[column][row] (m03 = c[3][0], the translation's x).
struct Matrix4x4f {
    float c[4][4] = {};
};

// CompareVector2fRobust (UnityPlayer: 10 ulps for the RectTransform setters): per component,
// floats of different signs are equal only if a == b (+0 and -0); else their bits as ordered
// integers may differ by at most maxUlps.
bool CompareFloatRobust(float a, float b, int maxUlps);
bool CompareVector2fRobust(Vector2 a, Vector2 b, int maxUlps = 10);

namespace rect_layout {

// The anchors placed in the parent's rect (zero for a parent that's no RectTransform).
struct AnchorSpan {
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
};
AnchorSpan Anchors(const Rect& parent, Vector2 anchorMin, Vector2 anchorMax);
// The rect: (-(w * pivot.x), -(h * pivot.y), w, h), w = (span) + sizeDelta.
Rect RectFor(const AnchorSpan& a, Vector2 sizeDelta, Vector2 pivot);
// localPosition.xy from anchoredPosition: (min + anchored) + span * pivot.
Vector2 LocalPositionFor(const AnchorSpan& a, Vector2 anchoredPosition, Vector2 pivot);
// anchoredPosition from localPosition.xy: local - (span * pivot + min).
Vector2 AnchoredPositionFor(const AnchorSpan& a, Vector2 localPosition, Vector2 pivot);

} // namespace rect_layout

// One level of Transform::CalculateGlobalMatrix: the rotation and scale's columns and the
// translation.
Matrix4x4f LocalMatrix(Vector3f t, Quaternionf q, Vector3f s);
// parent's local matrix times m (the next level up of CalculateGlobalMatrix).
Matrix4x4f ParentTimes(const Matrix4x4f& parent, const Matrix4x4f& m);
// One level of TransformPoint / CalculateGlobalPosition: the point scaled, turned and moved.
Vector3f TransformStep(Vector3f p, Vector3f t, Quaternionf q, Vector3f s);
// Matrix4x4.MultiplyPoint (managed).
Vector3f MultiplyPoint(const Matrix4x4f& m, Vector3f p);
// RectTransform.GetLocalCorners (managed): (x, y), (x, yMax), (xMax, yMax), (xMax, y), z 0.
void LocalCorners(const Rect& r, Vector3f out[4]);

// CanvasScaler (UnityEngine.UI, managed): ScaleWithScreenSize's factor for the screen, the
// reference resolution, the match mode (0 match width or height, 1 expand, 2 shrink) and match.
float CanvasScalerScaleWithScreenSize(float screenWidth, float screenHeight, Vector2 referenceResolution, int screenMatchMode,
                                      float matchWidthOrHeight);

class RectTransform;

// A Transform in a hierarchy (a plain one, or the base of a RectTransform).
class TransformNode {
public:
    TransformNode() = default;
    TransformNode(const TransformNode&) = delete;
    TransformNode& operator=(const TransformNode&) = delete;
    virtual ~TransformNode();

    TransformNode* parent() const { return parent_; }
    const std::vector<TransformNode*>& children() const { return children_; }
    // Appends it as the last child (no world position kept: the local values stay). Its rect
    // and its anchoredPosition are worked out again when next used.
    void SetParent(TransformNode* parent);

    virtual RectTransform* AsRect() { return nullptr; }
    virtual const RectTransform* AsRect() const { return nullptr; }

    Vector3f localPosition() const { return localPosition_; }
    Quaternionf localRotation() const { return localRotation_; }
    Vector3f localScale() const { return localScale_; }
    // Transform.localPosition / localRotation / localScale: the RectTransforms here and below
    // see the change when next read (UpdateAnchorPositionIfTransformChanged).
    void SetLocalPosition(Vector3f v);
    void SetLocalRotation(Quaternionf q);
    void SetLocalScale(Vector3f s);

    Matrix4x4f localToWorldMatrix() const;
    Vector3f position() const;
    Vector3f TransformPoint(Vector3f p) const;

protected:
    // HandleParentHierarchyChanges: a RectTransform's rect is made again from the new parent's.
    virtual void OnParentChanged() {}
    // SetLocalPositionIgnoringSpecificSystems: nothing marked (the RectTransform's own write).
    void SetLocalPositionQuiet(Vector3f v) { localPosition_ = v; }
    // The global-T change: every RectTransform here and below.
    void MarkTransformChanged();

    TransformNode* parent_ = nullptr;
    std::vector<TransformNode*> children_;
    Vector3f localPosition_;
    Quaternionf localRotation_;
    Vector3f localScale_{1.0f, 1.0f, 1.0f};
    bool transformChanged_ = false; // (RectTransforms only)
};

class RectTransform : public TransformNode {
public:
    RectTransform* AsRect() override { return this; }
    const RectTransform* AsRect() const override { return this; }

    // The serialized values as a scene or prefab holds them (Transfer): the rect is made when
    // first needed.
    void Load(Vector2 anchorMin, Vector2 anchorMax, Vector2 anchoredPosition, Vector2 sizeDelta, Vector2 pivot,
              Vector3f localPosition, Quaternionf localRotation, Vector3f localScale);

    Vector2 anchorMin() const { return anchorMin_; }
    Vector2 anchorMax() const { return anchorMax_; }
    Vector2 sizeDelta() const { return sizeDelta_; }
    Vector2 pivot() const { return pivot_; }
    Vector2 anchoredPosition(); // brings it up to date with the local position first
    Rect rect();

    void SetAnchorMin(Vector2 v);
    void SetAnchorMax(Vector2 v);
    void SetAnchoredPosition(Vector2 v);
    void SetSizeDelta(Vector2 v);
    void SetPivot(Vector2 v);

    void GetLocalCorners(Vector3f out[4]);
    void GetWorldCorners(Vector3f out[4]);

    // OnRectTransformDimensionsChange, sent where UpdatePosAndRectRecursive sends it (a setter's
    // update, after the children's); the layout system's hook.
    std::function<void(RectTransform&)> onDimensionsChange;

    // Canvas::UpdateCanvasRectTransform for a ScreenSpaceOverlay root canvas: screen in pixels
    // (Screen.width / height), the canvas's scaleFactor.
    void UpdateOverlayCanvas(int screenWidth, int screenHeight, float scaleFactor);

    // The same for a ScreenSpaceCamera root canvas on an orthographic camera.
    struct OrthoCamera {
        float orthographicSize = 0.0f;
        Rect pixelRect;          // Camera::GetCameraRect
        float pixelWidth = 0.0f; // the aspect's (ResetAspect: pixelWidth / pixelHeight)
        float pixelHeight = 0.0f;
        Vector3f position;
        Quaternionf rotation;
    };
    // align: AlignCanvasRectTransformWithCamera too (every frame's PlayerUpdateCanvases does).
    void UpdateCameraCanvas(const OrthoCamera& camera, float scaleFactor, float planeDistance, bool align);
    // AlignCanvasRectTransformWithCamera: the canvas planeDistance in front of the camera, turned
    // as it is.
    void AlignWithCamera(const OrthoCamera& camera, float planeDistance);

private:
    void OnParentChanged() override { rectDirty_ = true; }
    void RobustSet(Vector2& field, Vector2 v);
    Rect ParentRect();
    bool UpdatePosAndRect();
    void UpdatePosAndRectRecursive(bool sendMessage);
    void UpdateAnchorPositionIfTransformChanged(bool robust);
    void UpdateRectTransform(bool robust);

    Vector2 anchorMin_{0.5f, 0.5f};
    Vector2 anchorMax_{0.5f, 0.5f};
    Vector2 anchoredPosition_;
    Vector2 sizeDelta_{100.0f, 100.0f};
    Vector2 pivot_{0.5f, 0.5f};
    Rect rect_;
    bool rectDirty_ = true;

    static int enterCounter_; // s_EnterCounter
};

} // namespace unity
