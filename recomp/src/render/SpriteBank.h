#pragma once
// A dungeon's sprites (tools/extract/sprites.py): sprites/<culture>/index.bin, and each
// sprite's GameCube texels in sprites/<culture>/<n>.tex, read when it's first drawn.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "game/Session.h"

namespace render {

enum class TexFormat : uint8_t { RGB5A3 = 0, CI8 = 1 };

struct Sprite {
    std::string name;
    std::string folder;       // its dungeon's sprites/<culture>/
    int index = 0;            // its <n>.tex
    int width = 0, height = 0; // pixels
    float left = 0.0f, bottom = 0.0f; // the image's lower-left corner from the pivot, units
    float unitsPerPixel = 1.0f;
    TexFormat format = TexFormat::RGB5A3;
};

class SpriteBank {
public:
    // A dungeon's sprites ("sprites/golem/") first, then the other dungeons' (a corrupted room
    // shows its own dungeon's art).
    void Load(game::DataSource& data, const std::string& folder, const std::vector<std::string>& others = {});
    const Sprite* Find(const std::string& name) const;
    std::string TexelsPath(const Sprite& s) const { return s.folder + std::to_string(s.index) + ".tex"; }
    const std::string& Folder() const { return folder_; }

    // The texels' size for a format (padded to whole tiles; CI8 with its 512-byte palette).
    static size_t TexelBytes(const Sprite& s);
    // Texels -> RGBA (rows from the top), for the PC's preview.
    static std::vector<uint8_t> DecodeRGBA(const Sprite& s, const std::string& texels);

private:
    void LoadIndex(game::DataSource& data, const std::string& folder);

    std::string folder_;
    std::map<std::string, Sprite> byName_; // the first dungeon to have a name wins
};

} // namespace render
