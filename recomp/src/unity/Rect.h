#pragma once
// UnityEngine.Rect / Vector2Int (fields only; behaviour added as game code needs it).

namespace unity {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct Vector2Int {
    int x = 0;
    int y = 0;
};

} // namespace unity
