using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;
using InControl;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace MoonlighterOracle
{
    /// `-oracle-ui=<script>`: drives the game's UI with a scripted controller (the same hooks as
    /// the walk: InControl's actions, through PlayerAction.Update and the left stick's
    /// UpdateWithAxes) and records the UI every frame into ui_<script>.jsonl, once the canvases
    /// have been laid out for the frame (Canvas.willRenderCanvases, after the layout and graphic
    /// rebuilds of CanvasUpdateRegistry):
    ///
    ///   {"ev":"uiStart", ...}                    the script, the screen, the CanvasScaler settings
    ///   {"ev":"frame","f",...}                   per frame: clocks, screen, the input given, the
    ///                                            EventSystem's selected object, the open panel and
    ///                                            popup (GUIManager's), whether a press was refused
    ///   {"ev":"node","id","parent","name","path","rect","comps"}
    ///                                            an object the first time it's seen (id: its
    ///                                            Transform's instance id; parent 0 at the scene's root),
    ///                                            and again ("reparented") when its parent changed
    ///   {"ev":"x","f","id","t":[...]}            a plain Transform above a root canvas (its local
    ///                                            position, rotation, scale), when it changes
    ///   {"ev":"e","f","id","rt":[...], ...}      an active object under an active root canvas,
    ///                                            when anything recorded of it changed (below)
    ///   {"ev":"off","f","ids":[...]}             objects recorded before that aren't active any more
    ///
    /// An "e" row: rt = anchorMin, anchorMax, anchoredPosition, sizeDelta, pivot (2 each),
    /// localPosition (3), localRotation (4), localScale (3), rect (x, y, width, height),
    /// GetWorldCorners (4 x 3): 36 floats as their bits; "wp": Transform.position. Then what it
    /// has of: "cv" (a Canvas: render mode, scale factor, reference pixels per unit, sorting
    /// layer and order, override sorting, root, enabled, pixel rect, plane distance, world camera;
    /// "cam" for a root canvas with a camera: orthographic, size, aspect, field of view, pixel
    /// rect, viewport rect, position, rotation, clip planes, pixel size), "cg" (a CanvasGroup: alpha,
    /// interactable, blocks raycasts, ignore parent groups), "g" (a Graphic: its type, enabled,
    /// colour, the CanvasRenderer's alpha and inherited alpha, raycast target, culled), "img"
    /// (an Image: sprite, overrideSprite, type, fill amount, method, origin, clockwise, preserve
    /// aspect, fill centre), "raw" (a RawImage: texture, uv rect), "txt" (a Text: text, font,
    /// size, style, alignment, line spacing, rich text, best fit min/max, overflows, preferred
    /// width and height), "sel" (a Selectable: interactable, navigation mode), "mask"
    /// (a Mask's showMaskGraphic, or "rect2d" for a RectMask2D).
    ///
    /// Saving and Steam stats stay blocked (Harvest.InstallSafety) and the news popup is
    /// answered with "Later" (Disable), never "Read More". The scripts:
    ///   title     the main menu: its doors opened, the selection moved down and up through the
    ///             buttons (none pressed), the doors closed
    ///   dungeon   the first save slot (Continue, as the harvest does it), the Golem dungeon's
    ///             first floor (as the walk takes it): the HUD idle and with the hero walking,
    ///             then the bag opened, its slots walked through, and closed; the minimap's
    ///             detail panel opened and closed
    internal static class UIOracle
    {
        public static bool Active;
        private static int startFrame = -1;
        private static StreamWriter writer;
        private static string scriptName;
        private static Step[] script;
        private static int frames;

        /// One step: frames to hold, the left stick, the buttons pressed during its first
        /// `hold` frames ("gui.Action1": GUIManager's GUIActions; "hero.Action1": the hero's
        /// HeroMerchantActions).
        private struct Step
        {
            public int frames;
            public float x, y;
            public string[] buttons;
            public int hold;
            public int stickFrames; // the stick is held this many frames (0: the whole step)
            public string expect;   // the step's buttons are pressed only with this object selected
            public Step(int frames, float x = 0f, float y = 0f, string buttons = null, int hold = 2, int stickFrames = 0, string expect = null)
            {
                this.frames = frames;
                this.x = x;
                this.y = y;
                this.buttons = string.IsNullOrEmpty(buttons) ? new string[0] : buttons.Split(',');
                this.hold = hold;
                this.stickFrames = stickFrames;
                this.expect = expect;
            }
        }

        // A tap of the stick: 4 frames tilted (the input module moves the selection once on the
        // first, then waits moveRepeatFirstDuration), then released for the rest of the step.
        private static Step Tap(float x, float y, int frames = 30) => new Step(frames, x, y, null, 2, 4);
        private static Step Press(string button, int frames = 30) => new Step(frames, 0f, 0f, button);
        // A submit (the input module's A) with the selection checked first: if another object is
        // selected the press is refused and the run marked FAILED (the script has gone astray;
        // nothing it didn't mean is ever pressed).
        private static Step Submit(string selected, int frames = 60) => new Step(frames, 0f, 0f, "gui.Action1", 2, 0, selected);
        private static Step Idle(int frames) => new Step(frames);

        // The main menu from its closed doors ("press any key"): Y opens them (GUIManager hands
        // any button to the panel's OnAnyKeyPressed; Y isn't the input module's submit), the
        // selection moved through Continue, NewGame and Options; Options opened (A, checked to be
        // on Options), its first tab's rows walked up and down, closed with B (closing saves the
        // options: blocked; its tabs aren't switched: showing the Graphics tab again sets the
        // screen's resolution from its list); NewGame's slot panel opened, its slots
        // walked, back with B; the stick held down (the input module's repeat, 0.2 s then every
        // 0.2 s) down to Exit, never pressed; B closes the doors. Never X or the bumpers while
        // the news viewer is up (its Read More and paging), never A on Continue, Exit or a slot,
        // never left or right in Options (they change values) or on a slot (copy, erase).
        private static readonly Step[] TitleScript =
        {
            Idle(120),
            Press("gui.Action4", 150),
            Tap(0f, -1f), Tap(0f, -1f), Tap(0f, 1f), Tap(0f, 1f), Tap(0f, -1f), Tap(0f, -1f),
            Submit("Options", 90),
            Tap(0f, -1f), Tap(0f, -1f), Tap(0f, -1f), Tap(0f, 1f), Tap(0f, 1f),
            Press("gui.Action2", 120),
            Tap(0f, 1f),
            Submit("NewGame", 120),
            Tap(0f, -1f), Tap(0f, -1f), Tap(0f, 1f),
            Press("gui.Action2", 120),
            new Step(90, 0f, -1f),
            Idle(30),
            Press("gui.Action2", 120),
        };

        // The first room of the Golem dungeon's first floor: the HUD idle and with the hero
        // walking; the bag (the hero's Y, HeroMerchantController; the game pauses) opened, its
        // slots walked with the stick and the stick held (the repeat), closed with B; the pause
        // panel (GUI Start) opened, Options and back to BackToGame, closed with B; the minimap's
        // detail panel (the hero's Select, GUIManager) opened and closed. Never A, X or the
        // bumpers in the bag (they move, use or swap items), never the right trigger without a
        // panel (a potion), never MainMenu or ExitToDesktop in the pause panel.
        private static readonly Step[] DungeonScript =
        {
            Idle(60),
            new Step(30, 1f, 0f),
            Idle(30),
            Press("hero.Action4", 60),
            Tap(1f, 0f), Tap(1f, 0f), Tap(1f, 0f), Tap(0f, -1f), Tap(0f, -1f), Tap(-1f, 0f), Tap(0f, 1f),
            new Step(40, 1f, 0f),
            Idle(20),
            Press("gui.Action2", 90),
            Press("gui.ButtonStart", 90),
            Tap(0f, -1f), Tap(0f, 1f),
            Press("gui.Action2", 90),
            Press("hero.ButtonSelect", 90),
            Press("hero.ButtonSelect", 90),
            Idle(30),
        };

        private static int ScriptFrames(Step[] steps) { int n = 0; foreach (var st in steps) n += st.frames; return n; }

        private static bool Current(out Step step, out int inStep)
        {
            step = default;
            inStep = 0;
            if (!Active || startFrame < 0) return false;
            int f = Time.frameCount - startFrame;
            if (f < 0 || f >= frames) return false;
            foreach (var s in script)
            {
                if (f < s.frames)
                {
                    step = s;
                    inStep = f;
                    return true;
                }
                f -= s.frames;
            }
            return false;
        }

        private static Vector2 StickNow()
        {
            if (!Current(out Step s, out int i)) return Vector2.zero;
            if (s.stickFrames > 0 && i >= s.stickFrames) return Vector2.zero;
            return new Vector2(s.x, s.y);
        }

        private static string SetName(PlayerActionSet set)
        {
            if (set == null) return null;
            if (GUIManager.Instance != null && set == GUIManager.Instance.input) return "gui";
            var hero = InputActionsManager.Instance != null ? InputActionsManager.Instance.actions : null;
            if (set == hero) return "hero";
            return null;
        }

        private static readonly System.Reflection.FieldInfo OwnerEnabled = AccessTools.Field(typeof(OneAxisInputControl), "ownerEnabled");
        private static bool strayed;

        /// A step that expects a selection: is that object selected? (Logged once when not.)
        private static int expectFrame = -1;
        private static bool expectOk;

        private static bool SelectionAsExpected(Step s)
        {
            if (s.expect == null) return true;
            int stepStart = Time.frameCount - (Current(out _, out int inStep) ? inStep : 0);
            if (expectFrame == stepStart) return expectOk;
            expectFrame = stepStart;
            expectOk = Check(s);
            return expectOk;
        }

        private static bool Check(Step s)
        {
            var es = EventSystem.current;
            var selected = es != null ? es.currentSelectedGameObject : null;
            if (selected != null && selected.name == s.expect) return true;
            if (!strayed) OraclePlugin.Log.LogError($"ui: {s.expect} should be selected for the press, not {(selected != null ? PathOf(selected.transform) : "nothing")}: not pressed");
            strayed = true;
            return false;
        }

        private static int newsCheckedFrame = -1;
        private static bool newsUp;

        private static bool NewsViewerUp()
        {
            if (newsCheckedFrame != Time.frameCount)
            {
                newsCheckedFrame = Time.frameCount;
                var viewer = UnityEngine.Object.FindObjectOfType<DigitalSun.Moonlighter.InGameNewsViewer>();
                newsUp = viewer != null && viewer.isActiveAndEnabled;
            }
            return newsUp;
        }

        [HarmonyPatch(typeof(TwoAxisInputControl), "UpdateWithAxes")]
        private static class StickHook
        {
            private static void Prefix(TwoAxisInputControl __instance, ref float x, ref float y)
            {
                if (!Active || startFrame < 0) return;
                var hero = InputActionsManager.Instance != null ? InputActionsManager.Instance.actions : null;
                var gui = GUIManager.Instance != null ? GUIManager.Instance.input : null;
                bool left = (hero != null && __instance == hero.LeftStick) || (gui != null && __instance == gui.LeftStick);
                bool other = (hero != null && (__instance == hero.RightStick || __instance == hero.DPad))
                    || (gui != null && (__instance == gui.RightStick || __instance == gui.DPad));
                if (!left && !other) return;
                Vector2 v = left ? StickNow() : Vector2.zero;
                x = v.x;
                y = v.y;
            }
        }

        [HarmonyPatch(typeof(PlayerAction), "Update")]
        private static class ButtonHook
        {
            private static bool Prefix(PlayerAction __instance, ulong updateTick, float deltaTime)
            {
                if (!Active || startFrame < 0) return true;
                string set = SetName(__instance.Owner);
                if (set == null) return true;
                float value = 0f;
                Vector2 stick = StickNow();
                switch (__instance.Name)
                {
                    // The left stick's halves (the two-axis action is made from them).
                    case "LeftStickUp": value = Mathf.Max(0f, stick.y); break;
                    case "LeftStickDown": value = Mathf.Max(0f, -stick.y); break;
                    case "LeftStickRight": value = Mathf.Max(0f, stick.x); break;
                    case "LeftStickLeft": value = Mathf.Max(0f, -stick.x); break;
                    default:
                        if (Current(out Step s, out int i) && i < s.hold && Array.IndexOf(s.buttons, set + "." + __instance.Name) >= 0)
                            value = SelectionAsExpected(s) ? 1f : 0f;
                        // Never X while the news viewer is up: it's its "Read More" (InGameNewsViewer.
                        // LateUpdate: a web page, and the save). (Its bumpers only page the news.)
                        if (value > 0f && set == "gui" && __instance.Name == "Action3" && NewsViewerUp())
                        {
                            OraclePlugin.Log.LogError("ui: the script presses X with the news viewer up: not pressed");
                            strayed = true;
                            value = 0f;
                        }
                        break;
                }
                // PlayerAction.UpdateBindings refreshes this before reading the bindings: WasPressed
                // is false while the set is disabled (the game disables GUIManager's input while a
                // panel opens).
                OwnerEnabled.SetValue(__instance, __instance.Owner.Enabled);
                __instance.CommitWithValue(value, updateTick, deltaTime);
                return false;
            }
        }

        // --- recording ---
        //
        // Canvas.willRenderCanvases also fires for Canvas.ForceUpdateCanvases (the game calls it
        // mid-frame), so each call captures the whole UI and the frame's last capture is what's
        // written: when the next frame's first call comes (or the script ends), it's compared
        // with what was written before and the differences go out.

        private sealed class Snap
        {
            public int f;
            public string header;
            public readonly List<string> nodes = new List<string>();
            public readonly Dictionary<int, int> nodeIds = new Dictionary<int, int>();
            public readonly List<KeyValuePair<int, string>> items = new List<KeyValuePair<int, string>>(); // id -> "e"/"x" + body
            public readonly HashSet<int> ids = new HashSet<int>();
        }

        private static readonly Dictionary<int, string> lastRow = new Dictionary<int, string>();
        private static readonly Dictionary<int, int> known = new Dictionary<int, int>(); // id -> parent id written
        private static HashSet<int> activeLast = new HashSet<int>();
        private static readonly Vector3[] corners = new Vector3[4];
        private static Snap pending;
        private static int startW, startH;
        private static int rows;

        private static void Emit(Json j) => writer.WriteLine(j.EndObj().ToString());

        private static string PathOf(Transform t)
        {
            string p = t.name;
            for (t = t.parent; t != null; t = t.parent) p = t.name + "/" + p;
            return p;
        }

        private static string Name(UnityEngine.Object o) => o != null ? o.name : null;

        /// The object's "node" line (and its parents' first) if it hasn't been written yet, or
        /// again if its parent changed since ("reparented": true).
        private static void Node(Snap snap, Transform t)
        {
            int id = t.GetInstanceID();
            int parent = t.parent != null ? t.parent.GetInstanceID() : 0;
            if (snap.nodeIds.ContainsKey(id)) return;
            bool seen = known.TryGetValue(id, out int was);
            if (seen && was == parent) return;
            snap.nodeIds[id] = parent;
            if (t.parent != null) Node(snap, t.parent);
            var j = new Json().Obj().Prop("ev", "node").Prop("id", id).Prop("parent", parent)
                .Prop("name", t.name).Prop("path", PathOf(t)).Prop("rect", t is RectTransform);
            if (seen) j.Prop("reparented", true);
            j.Key("comps").Arr();
            foreach (var c in t.GetComponents<Component>())
                if (c != null && !(c is Transform)) j.Val(c.GetType().FullName);
            j.EndArr();
            snap.nodes.Add(j.EndObj().ToString());
        }

        private static void Bits(Json j, Vector2 v) => j.Bits(v.x).Bits(v.y);
        private static void Bits(Json j, Vector3 v) => j.Bits(v.x).Bits(v.y).Bits(v.z);
        private static void Bits(Json j, Quaternion q) => j.Bits(q.x).Bits(q.y).Bits(q.z).Bits(q.w);
        private static void Bits(Json j, Color c) => j.Bits(c.r).Bits(c.g).Bits(c.b).Bits(c.a);

        /// A plain Transform above a root canvas.
        private static void CaptureAncestor(Snap snap, Transform t)
        {
            int id = t.GetInstanceID();
            if (!snap.ids.Add(id)) return;
            Node(snap, t);
            var body = new Json().Obj().Key("t").Arr();
            Bits(body, t.localPosition);
            Bits(body, t.localRotation);
            Bits(body, t.localScale);
            body.EndArr();
            snap.items.Add(new KeyValuePair<int, string>(id, "x" + body.EndObj().ToString()));
        }

        private static void CaptureElement(Snap snap, RectTransform rt)
        {
            int id = rt.GetInstanceID();
            if (!snap.ids.Add(id)) return;
            Node(snap, rt);
            var j = new Json().Obj().Key("rt").Arr();
            Bits(j, rt.anchorMin);
            Bits(j, rt.anchorMax);
            Bits(j, rt.anchoredPosition);
            Bits(j, rt.sizeDelta);
            Bits(j, rt.pivot);
            Bits(j, rt.localPosition);
            Bits(j, rt.localRotation);
            Bits(j, rt.localScale);
            Rect r = rt.rect;
            j.Bits(r.x).Bits(r.y).Bits(r.width).Bits(r.height);
            rt.GetWorldCorners(corners);
            for (int i = 0; i < 4; i++) Bits(j, corners[i]);
            j.EndArr();
            j.Key("wp").Arr();
            Bits(j, rt.position);
            j.EndArr();

            var canvas = rt.GetComponent<Canvas>();
            if (canvas != null)
            {
                j.Key("cv").Arr().Val((int)canvas.renderMode).Bits(canvas.scaleFactor).Bits(canvas.referencePixelsPerUnit)
                    .Val(canvas.sortingLayerName).Val(canvas.sortingOrder).Val(canvas.overrideSorting).Val(canvas.isRootCanvas)
                    .Val(canvas.enabled);
                Rect pr = canvas.pixelRect;
                j.Bits(pr.x).Bits(pr.y).Bits(pr.width).Bits(pr.height).Bits(canvas.planeDistance)
                    .Val(canvas.worldCamera != null ? canvas.worldCamera.name : null);
                j.EndArr();
                var cam = canvas.worldCamera;
                if (cam != null && canvas.isRootCanvas)
                {
                    // What UpdateCanvasRectTransform reads of it: orthographic size, aspect, its
                    // pixel and viewport rects, where it is.
                    j.Key("cam").Arr().Val(cam.orthographic).Bits(cam.orthographicSize).Bits(cam.aspect).Bits(cam.fieldOfView);
                    Rect cpr = cam.pixelRect, cvr = cam.rect;
                    j.Bits(cpr.x).Bits(cpr.y).Bits(cpr.width).Bits(cpr.height).Bits(cvr.x).Bits(cvr.y).Bits(cvr.width).Bits(cvr.height);
                    Bits(j, cam.transform.position);
                    Bits(j, cam.transform.rotation);
                    j.Bits(cam.nearClipPlane).Bits(cam.farClipPlane).Val(cam.pixelWidth).Val(cam.pixelHeight);
                    j.EndArr();
                }
            }
            var group = rt.GetComponent<CanvasGroup>();
            if (group != null)
                j.Key("cg").Arr().Bits(group.alpha).Val(group.interactable).Val(group.blocksRaycasts).Val(group.ignoreParentGroups).Val(group.enabled).EndArr();
            var graphic = rt.GetComponent<Graphic>();
            if (graphic != null)
            {
                j.Key("g").Arr().Val(graphic.GetType().Name).Val(graphic.enabled);
                Bits(j, graphic.color);
                var cr = graphic.canvasRenderer;
                j.Bits(cr != null ? cr.GetAlpha() : -1f).Bits(cr != null ? cr.GetInheritedAlpha() : -1f).Val(graphic.raycastTarget)
                    .Val(cr != null && cr.cull);
                j.EndArr();
                if (graphic is Image img)
                {
                    j.Key("img").Arr().Val(Name(img.sprite)).Val(Name(img.overrideSprite)).Val((int)img.type).Bits(img.fillAmount)
                        .Val((int)img.fillMethod).Val(img.fillOrigin).Val(img.fillClockwise).Val(img.preserveAspect).Val(img.fillCenter)
                        .Val(Name(img.material)).EndArr();
                }
                else if (graphic is RawImage raw)
                {
                    Rect uv = raw.uvRect;
                    j.Key("raw").Arr().Val(Name(raw.texture)).Bits(uv.x).Bits(uv.y).Bits(uv.width).Bits(uv.height).EndArr();
                }
                else if (graphic is Text txt)
                {
                    j.Key("txt").Arr().Val(txt.text).Val(Name(txt.font)).Val(txt.fontSize).Val((int)txt.fontStyle).Val((int)txt.alignment)
                        .Bits(txt.lineSpacing).Val(txt.supportRichText).Val(txt.resizeTextForBestFit).Val(txt.resizeTextMinSize)
                        .Val(txt.resizeTextMaxSize).Val((int)txt.horizontalOverflow).Val((int)txt.verticalOverflow)
                        .Bits(txt.preferredWidth).Bits(txt.preferredHeight).EndArr();
                }
            }
            var sel = rt.GetComponent<Selectable>();
            if (sel != null) j.Key("sel").Arr().Val(sel.interactable).Val((int)sel.navigation.mode).Val(sel.enabled).EndArr();
            var mask = rt.GetComponent<Mask>();
            if (mask != null) j.Key("mask").Arr().Val(mask.showMaskGraphic).Val(mask.enabled).EndArr();
            var rmask = rt.GetComponent<RectMask2D>();
            if (rmask != null) j.Key("rect2d").Val(rmask.enabled);
            snap.items.Add(new KeyValuePair<int, string>(id, "e" + j.EndObj().ToString()));
        }

        private static void CaptureTree(Snap snap, Transform t)
        {
            if (!t.gameObject.activeInHierarchy) return;
            if (t is RectTransform rt) CaptureElement(snap, rt);
            for (int i = 0; i < t.childCount; i++) CaptureTree(snap, t.GetChild(i));
        }

        private static Snap Capture(int f)
        {
            var snap = new Snap { f = f };
            Current(out Step step, out int inStep);
            Vector2 stick = StickNow();
            var es = EventSystem.current;
            var selected = es != null ? es.currentSelectedGameObject : null;
            var gm = GameManager.Instance;
            var j = new Json().Obj().Prop("ev", "frame").Prop("f", f);
            j.Key("dt").Bits(Time.deltaTime).Key("udt").Bits(Time.unscaledDeltaTime).Key("ts").Bits(Time.timeScale)
                .Key("rt").Bits(Time.realtimeSinceStartup);
            j.Prop("sw", Screen.width).Prop("sh", Screen.height);
            if (Screen.width != startW || Screen.height != startH)
            {
                if (!strayed) OraclePlugin.Log.LogError($"ui: the screen changed to {Screen.width}x{Screen.height} (from {startW}x{startH})");
                strayed = true;
            }
            j.Key("in").Arr().Bits(stick.x).Bits(stick.y);
            if (inStep < step.hold && (step.expect == null || !strayed))
                foreach (var b in step.buttons) j.Val(b);
            j.EndArr();
            if (selected != null) Node(snap, selected.transform);
            j.Prop("sel", selected != null ? selected.transform.GetInstanceID() : 0).Prop("selPath", selected != null ? PathOf(selected.transform) : null);
            var panel = GUIManager.Instance != null ? GUIManager.Instance.currentInventory : null;
            var popUp = GUIManager.Instance != null ? GUIManager.Instance.currentPopUp : null;
            j.Prop("panel", panel != null ? panel.GetType().Name : null).Prop("popup", popUp != null ? popUp.GetType().Name : null)
                .Prop("paused", gm != null && gm.isGamePaused).Prop("strayed", strayed);
            snap.header = j.EndObj().ToString();

            // Root canvases, in a fixed order (their paths), with what's above them.
            var roots = UnityEngine.Object.FindObjectsOfType<Canvas>().Where(c => c.isRootCanvas && c.isActiveAndEnabled)
                .Select(c => c.transform).Distinct().OrderBy(t => PathOf(t), StringComparer.Ordinal).ThenBy(t => t.GetInstanceID()).ToList();
            foreach (var root in roots)
            {
                var above = new List<Transform>();
                for (var p = root.parent; p != null; p = p.parent) above.Add(p);
                above.Reverse();
                foreach (var p in above)
                {
                    if (p is RectTransform prt) CaptureElement(snap, prt);
                    else CaptureAncestor(snap, p);
                }
                CaptureTree(snap, root);
            }
            return snap;
        }

        private static void Flush(Snap snap)
        {
            writer.WriteLine(snap.header);
            foreach (string n in snap.nodes) writer.WriteLine(n);
            foreach (var n in snap.nodeIds) known[n.Key] = n.Value;
            foreach (var item in snap.items)
            {
                if (lastRow.TryGetValue(item.Key, out string last) && last == item.Value) continue;
                lastRow[item.Key] = item.Value;
                writer.WriteLine("{\"ev\":\"" + item.Value[0] + "\",\"f\":" + snap.f + ",\"id\":" + item.Key + "," + item.Value.Substring(2));
                rows++;
            }
            var gone = activeLast.Where(id => !snap.ids.Contains(id)).OrderBy(id => id).ToList();
            if (gone.Count > 0)
            {
                var off = new Json().Obj().Prop("ev", "off").Prop("f", snap.f).Key("ids").Arr();
                foreach (int id in gone)
                {
                    off.Val(id);
                    lastRow.Remove(id);
                }
                off.EndArr();
                Emit(off);
            }
            activeLast = snap.ids;
        }

        private static void OnWillRenderCanvases()
        {
            try
            {
                if (!Active || startFrame < 0 || writer == null) return;
                int f = Time.frameCount - startFrame;
                if (pending != null && pending.f != f)
                {
                    Flush(pending);
                    pending = null;
                }
                if (f >= 0 && f < frames) pending = Capture(f);
            }
            catch (Exception e)
            {
                OraclePlugin.Log.LogError("ui: record failed: " + e);
                File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "FAILED"), e.ToString());
                Active = false;
            }
        }

        // --- the scripts' setup ---

        /// The main menu up and answering: the news popup (if any) answered with Later.
        private static IEnumerator WaitForMainMenu()
        {
            var log = OraclePlugin.Log;
            float t0 = Time.realtimeSinceStartup;
            while (Time.realtimeSinceStartup - t0 < 180f)
            {
                var gui = GUIManager.Instance;
                var panel = gui != null ? gui.currentInventory : null;
                var popup = gui != null ? gui.currentPopUp : null;
                if ((popup ?? panel) is DigitalSun.Moonlighter.InGameNewsPopup news && news.IsActive())
                {
                    log.LogInfo("ui: dismissing the main menu news popup (Later)");
                    news.Disable();
                    yield return new WaitForSecondsRealtime(2f);
                    continue;
                }
                var menu = UnityEngine.Object.FindObjectOfType<MainMenuPanel>();
                // Its doors closed and listening: the gamepad notice gone (receiveInput), the news
                // looked at (a press opens the doors only then), the panel GUIManager's.
                if (menu != null && menu.isActiveAndEnabled && menu.IsActive() && panel == menu && popup == null
                    && menu.receiveInput && !menu.blockInput
                    && DigitalSun.Moonlighter.InGameNewsViewer.importantNewsAlreadyShown)
                {
                    yield return new WaitForSecondsRealtime(2f);
                    yield break;
                }
                yield return new WaitForSecondsRealtime(0.5f);
            }
            log.LogError("ui: the main menu never came up");
        }

        private static IEnumerator ToDungeon()
        {
            var log = OraclePlugin.Log;
            yield return Harvest.LoadSlot();
            if (Harvest.Slot == null) yield break;
            bool generated = false;
            Action done = () => generated = true;
            DungeonGenerator.OnGeneratedDungeon += done;
            UnityEngine.Random.InitState(7919);
            HeroMerchant.Instance.LoadDungeonScene(ItemMaster.Culture.Golem);
            float start = Time.realtimeSinceStartup;
            while (!generated && Time.realtimeSinceStartup - start < 120f) yield return null;
            DungeonGenerator.OnGeneratedDungeon -= done;
            if (!generated) { log.LogError("ui: floor timed out"); yield break; }
            yield return new WaitForSecondsRealtime(3f);
            for (int tries = 0; tries < 5 && GUIManager.Instance.currentInventory != null; tries++)
            {
                GUIManager.Instance.DisableCurrentPanel();
                yield return new WaitForSecondsRealtime(0.5f);
            }
            if (GameManager.Instance.isGamePaused) GameManager.Instance.UnpauseGame();
            var stale = GUIManager.Instance.currentInventory;
            if (stale != null && !stale.IsActive()) GUIManager.Instance.currentInventory = null;
            yield return new WaitForSecondsRealtime(2f);
        }

        public static IEnumerator Run(MonoBehaviour host, bool quit, string name)
        {
            var log = OraclePlugin.Log;
            scriptName = name == "" ? "title" : name;
            script = scriptName == "title" ? TitleScript : scriptName == "dungeon" ? DungeonScript : null;
            if (script == null)
            {
                log.LogError("-oracle-ui: unknown script " + scriptName + " (title, dungeon)");
                File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "FAILED"), "unknown -oracle-ui script " + scriptName);
                yield break;
            }
            frames = ScriptFrames(script);
            if (scriptName == "title") yield return WaitForMainMenu();
            else
            {
                yield return ToDungeon();
                if (HeroMerchant.Instance == null || DungeonManager.Instance == null)
                {
                    File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "FAILED"), "ui: no dungeon");
                    yield break;
                }
            }
            yield return new WaitForSecondsRealtime(1f);

            writer = new StreamWriter(Path.Combine(OraclePlugin.OutDir, "ui_" + scriptName + ".jsonl"), append: false) { AutoFlush = false };
            var setup = new Json().Obj().Prop("ev", "uiStart").Prop("script", scriptName).Prop("frames", frames)
                .Prop("sw", Screen.width).Prop("sh", Screen.height).Prop("fullScreen", Screen.fullScreen);
            setup.Key("dpi").Bits(Screen.dpi);
            var module = UnityEngine.Object.FindObjectOfType<InControlInputModule>();
            if (module != null)
                setup.Key("moveRepeatFirst").Bits(module.moveRepeatFirstDuration).Key("moveRepeatDelay").Bits(module.moveRepeatDelayDuration)
                    .Prop("forceModuleActive", module.forceModuleActive);
            setup.Prop("mousePresent", Input.mousePresent);
            setup.Key("scalers").Arr();
            foreach (var cs in UnityEngine.Object.FindObjectsOfType<CanvasScaler>().OrderBy(c => PathOf(c.transform), StringComparer.Ordinal))
            {
                setup.Obj().Prop("path", PathOf(cs.transform)).Prop("mode", (int)cs.uiScaleMode).Prop("match", (int)cs.screenMatchMode);
                setup.Key("matchWidthOrHeight").Bits(cs.matchWidthOrHeight).Key("refRes").Arr().Bits(cs.referenceResolution.x).Bits(cs.referenceResolution.y).EndArr();
                setup.Key("refPpu").Bits(cs.referencePixelsPerUnit).Key("scaleFactor").Bits(cs.scaleFactor).Key("dynPpu").Bits(cs.dynamicPixelsPerUnit);
                setup.EndObj();
            }
            setup.EndArr();
            Emit(setup);
            writer.Flush();

            QualitySettings.vSyncCount = 0;
            Application.targetFrameRate = 60;
            // After CanvasUpdateRegistry's own handler (made with the registry, long before now):
            // what's recorded is the frame's layout as it's drawn.
            Canvas.willRenderCanvases += OnWillRenderCanvases;
            startW = Screen.width;
            startH = Screen.height;
            startFrame = Time.frameCount + 1;
            Active = true;
            while (Active && Time.frameCount - startFrame < frames + 2) yield return null;
            bool failed = !Active;
            Active = false;
            Canvas.willRenderCanvases -= OnWillRenderCanvases;
            if (!failed && pending != null) Flush(pending);
            pending = null;
            writer.Flush();
            writer.Close();
            writer = null;
            if (strayed)
            {
                failed = true;
                File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "FAILED"), "ui: the script strayed (a press refused, or the screen changed: see the log)");
            }
            if (!failed)
            {
                File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "UI_DONE"), DateTime.Now.ToString("o"));
                log.LogInfo($"ui: {frames} frames recorded ({rows} rows, {known.Count} objects)");
            }
            if (quit) Application.Quit();
        }
    }
}
