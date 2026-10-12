import os, sys, json, collections
import UnityPy

ROOT = r"C:\Users\NoSig\Documents\moonlighter\Moonlighter"
DATA = os.path.join(ROOT, "Moonlighter_Data")
OUT = sys.argv[1]

sources = []
for f in sorted(os.listdir(DATA)):
    p = os.path.join(DATA, f)
    if os.path.isfile(p) and (f.endswith(".assets") or f.startswith("level") or f == "globalgamemanagers"):
        sources.append((f, p))
ab = os.path.join(DATA, "StreamingAssets", "AssetBundles")
for f in sorted(os.listdir(ab)):
    if not f.endswith(".manifest") and f != "AssetBundles":
        sources.append(("bundle:" + f, os.path.join(ab, f)))
sources.append(("dlc:wanderer_dlc", os.path.join(ROOT, "dlc", "wanderer_dlc")))

report = {}
for name, path in sources:
    try:
        env = UnityPy.load(path)
    except Exception as e:
        report[name] = {"error": str(e)}
        continue
    types = collections.Counter()
    tex = []
    audio = []
    scripts = collections.Counter()
    anims = 0
    for obj in env.objects:
        t = obj.type.name
        types[t] += 1
        try:
            if t == "Texture2D":
                d = obj.read()
                tex.append((d.m_Name, d.m_Width, d.m_Height, str(d.m_TextureFormat).split(".")[-1]))
            elif t == "AudioClip":
                d = obj.read()
                size = getattr(getattr(d, "m_Resource", None), "m_Size", 0)
                audio.append((d.m_Name, round(float(d.m_Length), 2), d.m_Frequency, d.m_Channels,
                              str(getattr(d, "m_CompressionFormat", "")).split(".")[-1], int(size)))
            elif t == "MonoBehaviour":
                try:
                    d = obj.read(check_read=False)
                    sc = d.m_Script.read()
                    scripts[sc.m_ClassName] += 1
                except Exception:
                    scripts["<unreadable>"] += 1
        except Exception as e:
            types["<err:" + t + ">"] += 1
    report[name] = {
        "path": path,
        "file_size": os.path.getsize(path),
        "types": dict(types.most_common()),
        "textures": tex,
        "audio": audio,
        "scripts": dict(scripts.most_common()),
    }
    print(name, len(env.objects), "objects,", len(tex), "textures,", len(audio), "audio", flush=True)

json.dump(report, open(OUT, "w"), indent=1)
