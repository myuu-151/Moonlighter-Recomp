import os, re
ROOT = r"C:\Users\NoSig\Documents\moonlighter\decomp\Assembly-CSharp"
EXCL = re.compile(r"^(InControl|TMPro|FullSerializer|DG\.|XInputDotNetPure|I2\.Loc|MultiplayerWithBindingsExample|MultiplayerBasicExample|"
                  r"VirtualDeviceExample|BindingsExample|BasicExample|TouchExample|InterfaceMovement|UnityStandardAssets|RichTextUtils|DataStructures|"
                  r"Properties|ChromaSDK|DigitalSunGames\.Platforms|DigitalRuby)")
files = {}
for dp, dn, fn in os.walk(ROOT):
    rel = os.path.relpath(dp, ROOT)
    if rel != "." and EXCL.match(rel):
        continue
    for f in fn:
        if f.endswith(".cs"):
            files[os.path.join(rel, f)] = open(os.path.join(dp, f), encoding="utf-8", errors="replace").read()
print("game files:", len(files), "lines:", sum(s.count("\n") for s in files.values()))
edfiles = [k for k, v in files.items() if re.search(r"Editor|Debug", k)]
print("files w/ Editor|Debug in name:", len(edfiles), "lines:", sum(files[k].count("\n") for k in edfiles))

checks = [
 ("Animator (Mecanim)", r"\bAnimator\b"),
 ("  animator.Play/SetTrigger/Bool/Float/Int", r"\.(SetTrigger|SetBool|SetFloat|SetInteger|ResetTrigger)\(|\.Play\(\""),
 ("AnimationCurve", r"AnimationCurve"),
 ("Physics2D queries", r"Physics2D\."),
 ("Rigidbody2D", r"Rigidbody2D"),
 ("Collider2D types", r"(Box|Circle|Polygon|Capsule|Edge)Collider2D|\bCollider2D\b"),
 ("OnTrigger/OnCollision 2D callbacks", r"void OnTrigger(Enter|Stay|Exit)2D|void OnCollision(Enter|Stay|Exit)2D"),
 ("3D physics", r"Physics\.(Raycast|Overlap|SphereCast)|\bRigidbody\b|\bBoxCollider\b|\bSphereCollider\b"),
 ("SpriteRenderer", r"SpriteRenderer"),
 ("Material/shader props", r"\.material\b|\.sharedMaterial|\.SetFloat\(\"_|\.SetColor\(\"_|Shader\.|MaterialPropertyBlock"),
 ("ParticleSystem", r"ParticleSystem"),
 ("Trail/LineRenderer", r"TrailRenderer|LineRenderer"),
 ("Light", r"\bLight\b"),
 ("RenderTexture", r"RenderTexture"),
 ("Camera", r"\bCamera\b"),
 ("uGUI", r"\bImage\b|\bCanvas\b|RectTransform|UnityEngine\.UI|\bScrollRect\b"),
 ("TextMeshPro", r"TextMeshPro|TMP_"),
 ("DOTween", r"DOTween|\.DO[A-Z][a-zA-Z]+\(|\bTweener\b|\bSequence\b"),
 ("Coroutines", r"StartCoroutine"),
 ("Invoke/SendMessage", r"\bInvoke\(\"|InvokeRepeating|SendMessage|BroadcastMessage"),
 ("Instantiate", r"Instantiate\("),
 ("GetComponent*", r"GetComponent(InChildren|InParent|s|sInChildren)?<"),
 ("Resources.Load", r"Resources\.Load"),
 ("AssetBundle", r"AssetBundle"),
 ("MasterAudio", r"MasterAudio"),
 ("AudioSource direct", r"AudioSource"),
 ("InControl", r"InControl|InputDevice\b|PlayerAction"),
 ("I2 Localization", r"I2\.Loc|LocalizationManager|ScriptLocalization"),
 ("Reflection", r"\.GetField\(|\.GetMethod\(|\.GetProperty\(|Activator\.CreateInstance|Type\.GetType"),
 ("LINQ", r"\.(Where|Select|OrderBy|OrderByDescending|First|FirstOrDefault|Any|All|ToList|ToArray|Count)\(.*=>"),
 ("Lambdas", r"=>"),
 ("Threads/Tasks", r"\bawait\b|Task<|new Thread|ThreadPool"),
 ("FullSerializer/JSON", r"fsSerializer|fsJson|JsonUtility|JsonConvert"),
 ("PlayerPrefs", r"PlayerPrefs"),
 ("Tilemap", r"Tilemap"),
 ("VideoPlayer", r"VideoPlayer"),
 ("SpriteMask", r"SpriteMask"),
 ("Random", r"Random\.(Range|value|InitState|seed|insideUnitCircle)|new System\.Random|new Random\("),
 ("Time.*", r"Time\.(deltaTime|time|fixedDeltaTime|unscaledDeltaTime|timeScale)"),
 ("FixedUpdate", r"void FixedUpdate\("),
 ("LateUpdate", r"void LateUpdate\("),
 ("Update", r"void Update\("),
 ("MonoBehaviour subclasses", r":\s*MonoBehaviour\b"),
 ("ScriptableObject subclasses", r":\s*ScriptableObject\b"),
 ("Odin/inspector-only attrs", r"\[(Button|ShowIf|HideIf|InfoBox|FoldoutGroup)"),
 ("Platform #if / PS4/Switch/Xbox", r"PS4|Switch|XboxOne|Sony|Nintendo"),
]
for label, pat in checks:
    r = re.compile(pat)
    refs = 0; nf = 0
    for s in files.values():
        n = len(r.findall(s))
        refs += n; nf += n > 0
    print(f"{label:42s} {refs:6d} refs {nf:5d} files")

# Most-used UnityEngine types overall
tok = re.compile(r"\b(GameObject|Transform|Vector2|Vector3|Quaternion|Color|Sprite|Texture2D|Mathf|Bounds|Rect|LayerMask|AudioClip|Material|Mesh)\b")
from collections import Counter
c = Counter()
for s in files.values():
    c.update(tok.findall(s))
print(c.most_common())
