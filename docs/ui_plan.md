# The UI: plan

Lane "ui", 2026-10-13. The game's UI is 20,716 counted lines (97 classes, the "UI" area of
`progress/classes.tsv`), 0% ported; 42.5% of them translate today, the rest wait mostly on Unity UI
APIs (`progress/unity_api_gaps.tsv`, the "Unity UI" rows: Image at 571 call sites, Text at 331,
Text.text, Graphic.color, Image.sprite...). This document says what the UI is made of, where it
lives, how input drives it, what gets translated and what gets hand-written, how the GameCube draws
it, and the order of the work. The numbers come from `tools/extract/ui.py` (data/extracted/ui/),
`mltranslate survey-assembly` (progress/ui_assembly_gaps.tsv, ui_assembly_native.tsv), the
decompiled game and the UI oracle's recordings (`-oracle-ui=<script>`).

## Decisions

1. **Translate UnityEngine.UI.dll** (and InControl, I2 Localization, DOTweenModuleUI, which are
   already source in Assembly-CSharp) with tools/translate, as the game's own code. Hand-write only
   the engine's native side: RectTransform (done: `recomp/src/unity/RectTransform.{h,cpp}`), Canvas,
   CanvasRenderer, CanvasGroup, the TextGenerator and Font, Sprite, Texture, Material, Mesh, Screen,
   and the few reflection spots (UnityEvent's persistent calls, the raycasters' ReflectionMethodsCache).
2. **Translate DOTween.dll** instead of growing the hand-written tweens: 84.6% of its methods
   translate today (the hand-written `unity::Tweens` stays the reference it must match on the
   existing recordings: DelayedCall, DOMove, the shake's draws). The UI leans on DOTween everywhere
   (DOFade 501 uses, DOAnchorPos 142, DOFillAmount 54, sequences, SetUpdate(true)).
3. **Not TextMeshPro, not DOTweenPro.** No UI object of the game has a TMP component or a
   DOTweenAnimation; the only TMP users are two TMP example scripts. Every text is UGUI `Text`.
4. **The UI runs at the game's own reference resolution, 640x360**, on every platform: the game sees
   `Screen.width/height = 640x360`, GUI_New's CanvasScaler (ScaleWithScreenSize 640x360, match
   height) gives scale 1, so a canvas unit is one art pixel, as in the oracle's recordings (made at
   640x360). The platform scales that 640x360 image to its output (GameCube: the same anamorphic or
   letterboxed mapping the world view already uses).
5. **Text: the game's own glyphs, recorded.** The fonts are dynamic TrueType (FreeType at run
   time). The port takes the glyph metrics and bitmaps from the game itself (an oracle font probe:
   `Font.GetCharacterInfo` and the font texture for every font, size and style the game uses, over
   the localization's characters) and hand-writes the TextGenerator's layout against recorded
   `TextGenerator` output. The GameCube gets pre-rendered glyph pages (I4/IA4), per language.
6. **Exactness as everywhere:** layout, colours, alphas, fills, selections and text quads are
   compared bit for bit against recordings; the drawing itself (GX) is presentation.

## What the UI is built from

Counted over the extracted UI trees (all scenes, resources.assets, sharedassets, bundles; the Town
scene's copies of Canvas - Shop/Town counted once): 15,338 objects, 10,088 RectTransforms.

| Component | Count | Notes |
|---|---|---|
| CanvasRenderer | 9,245 | one per Graphic |
| Image | 6,735 | Simple 5,687, Sliced 777, Filled 258, Tiled 13; 44 sliced sprites have borders |
| Text | 1,751 | rich text on 1,738; best fit on 4 |
| ContentSizeFitter | 1,011 | |
| I2 Localize | 693 | the texts' terms, and the font per language (secondary term FONT_BIG) |
| Button / Selectable | 429 / 253 | navigation explicit on 625, none on 64, vertical 11; transition none 530, colour 110, sprite swap 60 |
| LayoutElement | 384 | |
| Animator | 329 | UI Animators run in unscaled time (Canvas - Will, Canvas - Dungeon) |
| Vertical / Horizontal / Grid LayoutGroup | 124 / 108 / 24 | |
| Canvas | 93 | 15 root canvases with a CanvasScaler |
| CanvasGroup | 90 | alpha fades (191 uses in code) |
| ScrollRect, Scrollbar | 41, 19 | options lists, minimap, credits |
| RectMask2D / Mask | 38 / 36 | Mask: stencil; RectMask2D: clip rects |
| Toggle, Dropdown, Slider, RawImage | 18, 11, 6, 4 | options, minimap (RawImage) |
| GraphicRaycaster | 6 | the mouse; unused with a pad |

**Canvases.** Everything the player sees on screen hangs under one root canvas, **GUI_New**
(Managers scene): ScreenSpaceOverlay, sorting layer UI, CanvasScaler ScaleWithScreenSize, reference
640x360, match width-or-height 1 (height), reference pixels per unit 1. Its `Content` holds the HUD
(`Content/HUD`: Hero_Info with WillHealthBar, Gold, Equipment, SlimeDrops, AmuletsEffect;
Minimap_Panel, Pendant_Info, Bag_Info, BossHealthBar, Shop_Prices_Update, AutoSave), the
DungeonLoadingPanel and Fade, and `Panels` / `PopUps` where the scenes' PanelInstancers put the
panel prefabs. Sub-sections of the HUD are nested canvases (their own batches and sort orders: the
AutoSave icon at UIEffects 2000, the loading panel 800, the fade 1000). The other roots: **Canvas -
GUI** (ScreenSpaceCamera on GUICamera: orthographic size 178 for a 360-pixel screen, so scale
0.98888886, plane distance 100), **Canvas - InfoPopup** and **Canvas - Credits** (overlay, sort
orders 32767 and 32766), **@WorldCanvas** (world space: floating quantities), world-space canvases
in rooms (the DungeonRoom and boss room prefabs, layer Effects -10), the shop levels' world canvases
(Shop_Lvl0..4, ConstantPixelSize) and the tutorial dungeons'. The intro's Canvas - Intro is the only
1920x1080-referenced scaler (match width).

**Layout.** RectTransforms with anchors and pivots everywhere; layout groups and content size
fitters rebuild through `LayoutRebuilder` / `CanvasUpdateRegistry` (managed: translated). Code calls
`LayoutRebuilder.ForceRebuildLayoutImmediate` in 26 classes. The native part is the RectTransform's
rect and position rules, now hand-written and checked (below).

**Selection.** Unity's EventSystem with InControl's `InControlInputModule` (Assembly-CSharp),
`moveRepeatFirstDuration` and `moveRepeatDelayDuration` 0.2 s (level0's, not the code's 0.8/0.1),
explicit navigation on menus, pause and options, recomputed explicit navigation on the inventory
slots (`InventoryPanel.RecalculateSlotNavigation`), `SelectableEvents` (the game's) moving the main
menu's selector arrows by `SetParent` onto the selected button.

**Text and fonts.** Nine Unity fonts, all dynamic TrueType but Arial: the pixel fonts m5x7 (777
texts, at 16), Fairfax (544 at 10, 49 at 9, 31 at 12), m3x6, APPLE ][, and the CJK and fallback
fonts Zpix (4.4 MB of font data), DroidSansFallback (3.4 MB), JejuGothic (2.5 MB),
JF-Dot-Shinonome16B (1.9 MB), chained as fallbacks. Rendering modes 0 (smooth) and 2 (hinted
raster). The font per language comes from I2 (`LocalizeTarget_UnityUI_Text`: the FONT_BIG term) and
`FontSizeManager` sets size and line spacing per language.

**Sprites.** No SpriteAtlas assets: the legacy Sprite Packer. Of the 1,240 sprites the UI trees
reference, 632 are packed (287 tight, 345 rectangle), 608 loose; pixels per unit 1 for all but a
handful. UI sprites in use: about 856 (33 Mpx of sprite rects, including the credits' 9.2 Mpx and the
main menu's 7.4 Mpx of large art): the HUD's own images 0.3 Mpx plus 2.3 Mpx of icons its scripts
reference, the bag (Canvas - Will) 1.0 Mpx, the town 1.1 Mpx plus 4.7 Mpx referenced, the shop 1.0
Mpx. Item icons come from the items bundle through the item data.

**Tweens on UI** (game code): DOFade 501 uses in 75 files, DOAnchorPos* 142, DOColor 72, DOScale 59,
DOLocalMove 47, DOSizeDelta 31, DOFillAmount 54, DOText 8, DOPunch* 13, sequences 29,
DOVirtual.DelayedCall 451; `SetUpdate(true)` (unscaled) 147, `SetUpdate(false)` 307. Pausing
(`GameManager.PauseGame`: timeScale 0) freezes the scaled ones: the bag opens paused, its Animators
and the health bar's tweens run unscaled.

## Where the UI lives

| Where | What |
|---|---|
| level0, Managers.unity | GUI_New (HUD, loading panel, fade; HUDManager, GUIManager), Canvas - GUI, Canvas - InfoPopup, Canvas - Credits, @WorldCanvas, the EventSystem with InControlInputModule; a PanelInstancer for the popups (Resources `Canvas/PopUp - Options`, `- Pause`, `- ImageConfirmation`, `- SidePanelCarousel`) |
| level1, MainMenu.unity | a PanelInstancer: Resources `Canvas/Canvas - MainMenu` into GUI_New/Content/Panels (MainMenuPanel, StartGamePanel, InGameNewsViewer) |
| level2, Dungeon.unity | a PanelInstancer: `Canvas/Canvas - Dungeon` (MinimapDetailPanel, DungeonResultsPanel) |
| level3, Town.unity | Canvas - Shop and Canvas - Town in the scene (3,675 objects; ScenePanelPlacer) |
| hero_merchant bundle | the HeroMerchant prefab instantiates `Canvas/Canvas - Will` (HeroInventoryPanel, NotebookPanel, ChestInventoryPanel, CalendarPanel, WishlistPanel) |
| resources.assets | the panel prefabs above, the dialogs (InteractiveDialog, MultipleOptionsDialog, OptionsDialog...), InGameNewsPopup, GameSlot, tutorials, Shop_Lvl0..4 |
| sharedassets0 | floating indicators (damage, gold), UpdatePriceSlot, ItemStack, QuestItem, AngelAmuletEffect |
| dungeon bundles | the rooms' world canvases |

`tools/extract/ui.py` writes all of these, object by object with every serialized field of every UI
component and UI script (references as paths or asset names), plus `sprites.json` and `fonts.json`.

## How input drives it

Two InControl action sets: `GUIManager.input` (GUIActions) and the hero's
`InputActionsManager.actions` (HeroMerchantActions), each with Action1-4, bumpers, triggers, sticks,
D-pad, Select, Start. The EventSystem's `InControlInputModule` takes Submit = GUI Action1, Cancel =
GUI Action2, Move = the hero's LeftStick; `GUIManager.OnInControlInputProccessed` hands GUI buttons
to the current panel (`currentPopUp ?? currentInventory`: OnAButtonPressed, OnBButtonPressed, any
key, sticks, D-pad, Start), except the minimap (the hero's Select). The hero's controller opens the
bag (hero Action4) and the pause panel (GUI Start). InControl updates in its Update (-100), the
EventSystem at -1000: a press is seen by panels the frame it's made, by the EventSystem a frame
later. `PlayerAction.UpdateBindings` refreshes `ownerEnabled` (the game disables GUI input while a
panel opens). InControl, the input module, GUIManager and the panels are game code: translated. The
platform supplies an InControl `InputDevice` (the native side): on the GameCube the pad's A, B, X,
Y, Start, the stick and D-pad map one to one; Z to Select (minimap); L and R analog to the triggers
(roll, potion); the bumpers (weapon swap, bag and options tabs) to the C-stick left and right. The
legacy `Input` paths (the main menu's Return key) aren't needed with a pad.

The UI oracle drives the same actions (`oracle/MoonlighterOracle/UIOracle.cs`: PlayerAction.Update
and the two-axis UpdateWithAxes, both sets, names as `gui.Action1`, `hero.Select`).

## What gets translated, what gets written

`mltranslate survey-assembly` (tools/translate/AssemblySurvey.cs) puts the shipped managed
assemblies through the translator:

| Assembly | Types | Methods (lines) | Translate today | APIs missing (native / managed / BCL) | Unsupported |
|---|---|---|---|---|---|
| UnityEngine.UI | 112 | 1,728 (14,368) | 73.1% (lines 45.7%) | 426 (191 / 210 / 25) | array covariance in Dropdown (2) |
| DOTween | 95 | 1,107 (11,237) | 84.6% (lines 79.3%) | 131 (71 / 25 / 35) | `new T()` in a plugin lookup, ulong to decimal |
| TextMeshPro | 93 | 1,308 (20,033) | 59.3% | 376 | (not needed) |
| DOTweenPro | 13 | 60 (502) | 71.7% | 69 | (not needed) |

No virtual generic methods, unsafe code or pointers in UnityEngine.UI or DOTween. What needs a
hand-written replacement besides the native APIs: `ReflectionMethodsCache` (the raycasters' physics
calls by reflection: mouse only, stub), `UnityEvent` persistent calls (a button's onClick listeners
are resolved by reflection: generate a table from the extracted `m_OnClick` data: target object,
component, method name, argument), DOTween's `DOTweenComponent.Awake` and `Utils.GetLooseScriptType`
(reflection), `ObjectPool<T>.Get`'s `new T()` (a translator construct to add).

**The native API list** (progress/ui_assembly_native.tsv, by type, methods/lines blocked over the
four assemblies; the hand-written layer's work, `recomp/src/unity` behind `recomp/src/bind`):

| Type | What the UI needs |
|---|---|
| RectTransform | anchors, anchoredPosition, sizeDelta, pivot, rect, the layout's reapplyDrivenProperties; done here (layout, canvas roots, matrices), to bind into the facade |
| Canvas | renderMode, scaleFactor, referencePixelsPerUnit, rootCanvas, isRootCanvas, sorting layer/order, overrideSorting, pixelPerfect, pixelRect, worldCamera, planeDistance, `willRenderCanvases`, ForceUpdateCanvases, the default materials |
| CanvasRenderer | SetMesh, SetMaterial, SetTexture, SetColor/GetColor, SetAlpha/GetAlpha, GetInheritedAlpha, cull, hasMoved, EnableRectClipping/DisableRectClipping, SetPopMaterial, absoluteDepth, the UIVertex stream helpers |
| CanvasGroup | alpha, interactable, blocksRaycasts, ignoreParentGroups (inherited alpha into CanvasRenderer) |
| TextGenerator, Font | Populate (verts, characters, lines), GetPreferredWidth/Height, the generation settings; Font's characterInfo, RequestCharactersInTexture, textureRebuilt, dynamic, fontSize, lineHeight |
| Sprite, Texture2D | rect, textureRect, textureRectOffset, border, pivot, pixelsPerUnit, packed, vertices/uv/triangles (tight meshes), texture; DataUtility.GetInnerUV/GetOuterUV/GetPadding/GetMinSize |
| Material, Shader | the UI default material, the stencil materials of Mask (StencilMaterial.Add), property ids |
| Mesh | what VertexHelper.FillMesh writes (VertexHelper itself is managed) |
| RectTransformUtility | PixelAdjustRect, PixelAdjustPoint (pixel-perfect canvases: the main menu's), ScreenPointToLocalPointInRectangle |
| Camera, Screen, Display, Input | screen size (640x360), the camera's projection for world/camera canvases, `Input.mousePresent` (the input module runs only with a mouse or `forceModuleActive`) |

Managed UnityEngine types the translation needs bound (C#, translatable or trivial): Color32,
Vector4, Matrix4x4 (MultiplyPoint, MultiplyPoint3x4), Rect's members, UIVertex, UICharInfo,
UILineInfo, TextGenerationSettings, Bounds, Ray, UnityEvent and UnityEvent<T>, DrivenRectTransformTracker.

## RectTransform: done

`recomp/src/unity/RectTransform.{h,cpp}`, read from UnityPlayer.dll (Unity 2019.2's public symbols,
`tools/unity_dll`) and UnityEngine.CoreModule/UI:

- the rect from the parent's (`CalculateRectInParentSpace`, `UpdatePosAndRect`), localPosition from
  anchoredPosition (`CalculateLocalPosition2`) and back (`UpdateRectTransform`), all float32 in the
  engine's order;
- the setters: a value within 10 ulps (`CompareVector2fRobust`) is ignored; otherwise stored and
  the rect and position redone, into the children while a rect changes
  (`UpdatePosAndRectRecursive`); a localPosition written directly is worked back into
  anchoredPosition when next read (`UpdateAnchorPositionIfTransformChanged`);
- the engine's "changed?" tests compare as floats: a rect of no size keeps (0, 0, 0, 0) though its
  pivot's products give -0, a camera canvas keeps rotation (0, 0, 0, 1) under a camera at
  (-0, -0, -0, 1) (both found by the test);
- `Canvas::UpdateCanvasRectTransform`: overlay (screen / scaleFactor, at the screen's centre,
  scaled) and orthographic camera canvases (sized from the frustum, scaled, aligned planeDistance in
  front of the camera);
- `localToWorldMatrix` (`CalculateGlobalMatrix`), `position`, `TransformPoint` in the engine's SIMD
  association; `GetWorldCorners` (managed: the matrix's MultiplyPoint in doubles);
  `CanvasScaler`'s ScaleWithScreenSize (managed; replaced by the translated UnityEngine.UI later).

`ui_rect` (recomp/tests/ui_rect_test.cpp) checks it against the UI oracle's recordings, bit for bit:
each recorded row (rect, localPosition, world corners, Transform.position, each root canvas made
from nothing, the scaler's factor), then the whole hierarchy replayed through the class's setters
with every live object compared after every frame. Not modelled yet: `SetParent` keeping the world
position (InverseTransformPosition with its rcpps reciprocal: the replay takes a reparented
object's next values as recorded), nested non-root canvases' alignment with a camera.

## The oracle

`-oracle-ui=<script>` (UIOracle.cs) records, after each frame's canvas update (the frame's last
`Canvas.willRenderCanvases`), every RectTransform under an active root canvas when anything of it
changed: anchors, pivot, sizeDelta, anchoredPosition, local position, rotation and scale, rect,
world corners, position; Canvas (mode, scale factor, sorting, pixel rect, its camera), CanvasGroup,
Graphic (colour, CanvasRenderer alpha and inherited alpha, culled), Image (sprite, override,
type, fill), Text (string, font, size, style, alignment, line spacing, preferred size), Selectable,
masks; the EventSystem's selection, the open panel and popup, the clocks, the input. Saving stays
blocked; the news popup is answered Later; X (Read More) is never pressed with the news viewer up;
a submit names the object that must be selected; a recording whose screen changes fails (showing
Options' Graphics tab a second time set the screen to 3840x1080).

| Script | What |
|---|---|
| `title` | the main menu's doors opened, Continue/NewGame/Options, Options opened and walked (first tab), the slot panel opened and walked, the stick held (the repeat), the doors closed |
| `dungeon` | the Golem floor's first room: HUD idle and walking, the bag opened (paused) and walked, the pause panel, the minimap's detail panel |

Recorded through the queue (640x360, 60 fps; copies in a worktree's `data/oracle`):

| Request | File | Frames | Rows | Objects | ui_rect |
|---|---|---|---|---|---|
| ui-title-3 | ui_title.jsonl | 1,410 | 13,413 | 412 (41 reparentings) | ALL MATCH: 13,413 rows, 362,258 object-frames replayed |
| ui-dungeon-2 | ui_dungeon.jsonl | 1,020 | 12,650 | 775 (6 reparentings) | ALL MATCH: 12,650 rows, 458,357 object-frames replayed |

Earlier attempts and what they taught the oracle: ui-title-1 (no reparenting recorded: the selector
arrows took the old parent's rect), ui-title-2 (the Graphics tab set the screen to 3840x1080; a
submit checked on its second frame), ui-dungeon-1 (Start and Select named by their fields, so
never pressed; its layout ALL MATCH all the same).

Next probes the later lanes need: Graphic meshes (the VertexHelper output of a set of Images:
sliced, filled, tiled), the TextGenerator's output (verts and lines of every visible Text) and a
font probe (CharacterInfo and the font textures), the layout groups' inputs (preferred sizes).

## The GameCube

**Resolution.** The UI runs at 640x360 (decision 4): GUI_New's scale is 1, canvas units are pixels.
MLGC draws the world at 640x360 units into the 640x480 EFB, anamorphic by default (1.333 lines per
unit, the TV's 16:9 undoes it) or letterboxed (START+Z: 640x360 at line 60). The UI uses the same
mapping, so world and HUD line up and the art keeps its 1x pixels horizontally. PAL (640x528, not
handled yet in MLGC) is the same with 528/360. Screen.width/height stay 640x360 whatever the output,
so the game's layout is the recorded one.

**Drawing.** MLGC draws immediate-mode GX in `MoonGame::Render` (`DungeonView`, a full-screen
Octave widget; one TEV stage MODULATE, alpha blend, no batching). The UI adds a pass:

1. the translated UI fills each CanvasRenderer's mesh (VertexHelper: positions in canvas space,
   colours, UVs) and material/texture, as Unity's batcher gets them;
2. a hand-written canvas batcher sorts canvases (world and camera canvases into the sprite draw list
   by sorting layer and order, `render::SortDrawList`; overlay canvases on top by sort order, then
   nested canvases, then hierarchy order), transforms vertices by the canvas root's matrix into the
   640x360 screen, and emits GX quads/triangles with vertex colour × the CanvasRenderer colour ×
   inherited alpha (TEV: texture × rasterized colour, as the world's sprites);
3. RectMask2D → GX scissor (its clip rect in screen pixels, intersected down the hierarchy; Unity
   clips per fragment at pixel centres, which at scale 1 and on the pixel grid is the scissor box);
   Mask (stencil) → the same scissor when its graphic is a plain rectangle (the options lists'
   masks are), else destination alpha or Z as a one-bit stencil;
4. text: glyph quads from the TextGenerator with an I4/IA4 glyph page texture.

**Textures and memory.** UI sprites as the world's are: CI8 with an RGB5A3 palette when 256
colours do, else RGB5A3 (`tools/extract/sprites.py`'s formats), one texture per sprite or per
screen page. Resident: the HUD (about 0.3 Mpx of its own images, 2.3 Mpx of icons its scripts
reference, of which a run shows a few dozen: load icons on use), the fonts' Latin glyph pages
(tens of KB), the cursor and frames: under 1 MB. Per screen, streamed from disc when it opens: the
bag 1.0 Mpx (about 1 MB CI8), the town and shop 1-2 MB, Options 0.2 MB; the main menu's and the
credits' big art (7-9 Mpx) only on those screens, with the world unloaded. CJK glyph pages per
language (only the localization's characters, 1-3 MB I4). A UI budget of 3 MB resident + 2 MB per
open screen fits beside the world's per-room art in 24 MB; ARAM (16 MB) can hold the closed screens'
textures (Octave's `Texture::StashFrom`), which MLGC doesn't use yet.

## Order of work (the lanes)

1. **ui-translate** (next): units for UnityEngine.UI.dll in tools/translate (a second assembly into
   the same output; the survey shows the way), bindings for its native API to new facade pieces
   (Canvas, CanvasRenderer, CanvasGroup, the facade's RectTransform on `unity::RectTransform`),
   stubs where drawing isn't needed yet (TextGenerator returns nothing, meshes go nowhere). Check:
   the UI prefab loader builds Canvas - MainMenu from `data/extracted/ui/resources.assets.json`
   (bind/Prefabs taught RectTransforms and UI components, `cs_SetField` for their fields).
2. **DOTween translated** (in parallel): DOTween.dll through the translator, proven on the existing
   enemy recordings where the hand-written tweens are, then the UI's tweens.
3. **title** (first screen): MainMenuPanel, the EventSystem and InControlInputModule translated,
   driven by the recorded input; checked against `ui_title.jsonl`: the selection frame by frame,
   every RectTransform (the doors' DOAnchorPos), colours and alphas (the fades), texts.
4. **HUD**: HUDManager, WillHealthBar (DOFillAmount), gold (DOText), equipment, minimap panel;
   against `ui_dungeon.jsonl` and a recording with damage and gold pickups.
5. **bag**: HeroInventoryPanel, InventoryPanel, slots, item icons; against `ui_dungeon.jsonl`.
6. **text**: the font probe oracle, the TextGenerator (layout, rich text, best fit), I2's font
   choice; against recorded generator output.
7. **shop**: Canvas - Shop, the shop's world canvases (Shop_Lvl*), haggling panels; a recorded shop day.
8. **GameCube drawing**: MLGC's canvas batcher, scissor masks, UI textures and glyph pages in
   `make_data.py`, memory measured on hardware (USB Gecko).

Lanes 1 and 2 first and side by side; 3 depends on 1 and 2; 4 and 5 can run in parallel after 3;
6 and 8 can start any time (6 needs only the oracle, 8 only the extracted data and a stub batch).
