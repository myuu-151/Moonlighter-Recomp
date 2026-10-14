"""The game's localization: I2 Localization's LanguageSource ("I2Languages", resources.assets),
its languages and every term's translations, and each language's characters (what its texts
need of the fonts: the font probe, `-oracle-fonts`, renders these; docs/ui_plan.md, "Text").

The asset is read by hand, field by field in the serialization order of I2's
LanguageSourceAsset (mSource: LanguageSourceData), since the type tree generator's tree for it
doesn't read; the reader checks it ends on the asset's last byte.

Needs UnityPy.

    python tools/extract/localization.py --game <the game's folder> --out <folder>
    -> <folder>/localization.json
       {"languages": [{"name", "code", "flags", "enabled"}],      (I2's LanguageData; flags bit 0:
                                                                    disabled)
        "terms": [{"term", "type" (I2's eTermType: 0 text, 1 font, 5 sprite...),
                   "translations": [one per language, "" when none], "flags": [...]}],
        "assets": [{"file", "id"}],                               (the source's Assets: fonts, sprites)
        "missingTranslation": I2's OnMissingTranslation (1: the fallback language's),
        "charsets": {language name: its text terms' characters, sorted, as one string}}
"""
import argparse
import json
import struct
from pathlib import Path

import UnityPy


class Reader:
    def __init__(self, raw):
        self.raw, self.p = raw, 0

    def i32(self):
        v = struct.unpack_from('<i', self.raw, self.p)[0]
        self.p += 4
        return v

    def i64(self):
        v = struct.unpack_from('<q', self.raw, self.p)[0]
        self.p += 8
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.raw, self.p)[0]
        self.p += 4
        return v

    def align(self):
        self.p = (self.p + 3) & ~3

    def u8(self):
        v = self.raw[self.p]
        self.p += 1
        return v

    def flag(self):
        v = self.u8()
        self.align()
        return v

    def bytes_(self):
        n = self.i32()
        v = bytes(self.raw[self.p:self.p + n])
        self.p += n
        self.align()
        return v

    def string(self):
        n = self.i32()
        if not 0 <= n <= len(self.raw) - self.p:
            raise ValueError(f'bad string length {n} at {self.p}')
        v = self.raw[self.p:self.p + n].decode('utf-8')
        self.p += n
        self.align()
        return v

    def pptr(self):
        return self.i32(), self.i64()


def read_source(raw):
    r = Reader(raw)
    r.pptr()                   # m_GameObject
    r.flag()                   # m_Enabled
    r.pptr()                   # m_Script
    name = r.string()          # m_Name
    for _ in range(3):         # UserAgreesToHaveItOnTheScene, ...InsideThePluginsFolder, GoogleLiveSyncIsUptoDate
        r.flag()
    terms = []
    for _ in range(r.i32()):   # mTerms
        term = r.string()
        kind = r.i32()
        translations = [r.string() for _ in range(r.i32())]
        flags = list(r.bytes_())
        [r.string() for _ in range(r.i32())]  # Languages_Touch
        terms.append({'term': term, 'type': kind, 'translations': translations, 'flags': flags})
    r.flag()                   # CaseInsensitiveTerms
    missing = r.i32()          # OnMissingTranslation
    r.string()                 # mTerm_AppName
    languages = []
    for _ in range(r.i32()):   # mLanguages
        lname, code, flags = r.string(), r.string(), r.flag()
        languages.append({'name': lname, 'code': code, 'flags': flags, 'enabled': (flags & 1) == 0})
    r.flag()                   # IgnoreDeviceLanguage
    r.i32()                    # _AllowUnloadingLanguages
    for _ in range(4):         # Google_WebServiceURL, _SpreadsheetKey, _SpreadsheetName, _LastUpdatedVersion
        r.string()
    r.i32(), r.i32(), r.i32()  # GoogleUpdateFrequency, GoogleInEditorCheckFrequency, GoogleUpdateSynchronization
    r.f32()                    # GoogleUpdateDelay
    assets = [r.pptr() for _ in range(r.i32())]
    if r.p != len(raw):
        raise ValueError(f'{name}: read {r.p} of {len(raw)} bytes')
    return name, languages, terms, missing, assets


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    env = UnityPy.load(str(Path(args.game) / 'Moonlighter_Data' / 'resources.assets'))
    sf = next(iter(env.files.values()))
    found = None
    for obj in env.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        try:
            mb = obj.read(check_read=False)
            if mb.m_Name != 'I2Languages':
                continue
            script = mb.m_Script.read()
        except Exception:
            continue
        if script.m_ClassName == 'LanguageSourceAsset':
            if found is not None:
                raise SystemExit('resources.assets has two I2Languages sources')
            found = obj
    if found is None:
        raise SystemExit('resources.assets has no I2Languages LanguageSourceAsset')
    name, languages, terms, missing, assets = read_source(found.get_raw_data())
    files = ['resources.assets'] + [e.path.split('/')[-1] for e in sf.externals]
    charsets = {}
    for i, lang in enumerate(languages):
        chars = set()
        for t in terms:
            if t['type'] == 0 and i < len(t['translations']):
                chars.update(c for c in t['translations'][i] if c >= ' ')
        charsets[lang['name']] = ''.join(sorted(chars))
    out = {'languages': languages, 'terms': terms,
           'assets': [{'file': files[fid] if 0 <= fid < len(files) else fid, 'id': pid} for fid, pid in assets],
           'missingTranslation': missing, 'charsets': charsets}
    target = Path(args.out) / 'localization.json'
    target.write_text(json.dumps(out, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    enabled = [l['name'] for l in languages if l['enabled']]
    print(f'   {len(terms)} terms, {len(enabled)} languages: {", ".join(enabled)}', flush=True)


if __name__ == '__main__':
    main()
