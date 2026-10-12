#include "game/HoleOutlines.h"

#include <algorithm>
#include <climits>

#include "unity/Mathf.h"

namespace game {

using unity::Vector2;

namespace {

struct Grid {
    const std::vector<int>& cells;
    int slicesX, slicesY;
    const std::vector<int>& check;

    bool CheckGrid(int i, int j) const { return i > -1 && j > -1 && i < slicesX && j < slicesY; }
    bool InCheck(int v) const { return std::find(check.begin(), check.end(), v) != check.end(); }
    bool CheckGridTrue(int i, int j) const { return CheckGrid(i, j) && InCheck(cells[static_cast<size_t>(i + j * slicesX)]); }
    bool CheckGridFalse(int i, int j) const { return CheckGrid(i, j) && !InCheck(cells[static_cast<size_t>(i + j * slicesX)]); }
};

// List<Vector2>.IndexOf: Vector2.Equals, exact per component.
int IndexOf(const std::vector<Vector2>& list, Vector2 v)
{
    for (size_t i = 0; i < list.size(); i++)
        if (unity::Equals(list[i], v)) return static_cast<int>(i);
    return -1;
}

Vector2 GetNextVertex(Vector2 lastVertex)
{
    using unity::Mathf::Sign;
    if (Sign(lastVertex.x) == 1.0f && Sign(lastVertex.y) == -1.0f) return {lastVertex.x, 0.0f - lastVertex.y};
    if (Sign(lastVertex.x) == 1.0f && Sign(lastVertex.y) == 1.0f) return {0.0f - lastVertex.x, lastVertex.y};
    if (Sign(lastVertex.x) == -1.0f && Sign(lastVertex.y) == 1.0f) return {lastVertex.x, 0.0f - lastVertex.y};
    return {0.0f - lastVertex.x, lastVertex.y};
}

std::vector<Vector2> CleanVertexes(const std::vector<Vector2>& vertexes)
{
    const size_t n = vertexes.size();
    Vector2 vector = vertexes[0] - vertexes[n - 1];
    Vector2 vector2 = vertexes[0];
    std::vector<Vector2> list;
    list.push_back(vertexes[0]);
    for (size_t i = 1; i <= n; i++) {
        size_t index = i % n;
        if (vertexes[index] - vector2 == vector && !list.empty()) list.pop_back(); // (C# throws on an empty list; never here)
        if (i < n) {
            list.push_back(vertexes[index]);
            vector = vertexes[index] - vector2;
            vector2 = vertexes[index];
        }
    }
    return list;
}

std::vector<std::vector<Vector2>> CleanRepeatedPaths(const std::vector<std::vector<Vector2>>& paths)
{
    std::vector<std::vector<Vector2>> list;
    for (size_t i = 0; i < paths.size(); i++) {
        bool flag = false;
        for (size_t j = 0; j < list.size(); j++) {
            size_t num = 0;
            for (size_t k = 0; k < paths[i].size(); k++)
                if (list[j].size() == paths[i].size() && IndexOf(list[j], paths[i][k]) != -1) num++;
            if (num == paths[i].size()) {
                flag = true;
                break;
            }
        }
        if (!flag) list.push_back(paths[i]);
    }
    return list;
}

// System.Math.Min / Max (float).
float MathMin(float val1, float val2) { return val1 < val2 || val1 != val1 ? val1 : val2; }
float MathMax(float val1, float val2) { return val1 > val2 || val1 != val1 ? val1 : val2; }

// PolygonInside(inner, outer): every point of inner's first path inside outer's first path.
bool PolygonInside(const GridCollider& innerPoly, const GridCollider& outerPoly)
{
    for (const Vector2& p : innerPoly.paths[0])
        if (!IsPointInPolygon(p, outerPoly.paths[0])) return false;
    return true;
}

} // namespace

bool IsPointInPolygon(Vector2 p, const std::vector<Vector2>& polygon)
{
    float num = polygon[0].x;
    float num2 = polygon[0].x;
    float num3 = polygon[0].y;
    float num4 = polygon[0].y;
    for (size_t i = 1; i < polygon.size(); i++) {
        Vector2 aux = polygon[i];
        num = MathMin(aux.x, num);
        num2 = MathMax(aux.x, num2);
        num3 = MathMin(aux.y, num3);
        num4 = MathMax(aux.y, num4);
    }
    if (p.x < num || p.x > num2 || p.y < num3 || p.y > num4) return false;
    bool flag = false;
    size_t num5 = 0;
    size_t num6 = polygon.size() - 1;
    while (num5 < polygon.size()) {
        if ((polygon[num5].y > p.y) != (polygon[num6].y > p.y) &&
            p.x < (polygon[num6].x - polygon[num5].x) * (p.y - polygon[num5].y) / (polygon[num6].y - polygon[num5].y) + polygon[num5].x)
            flag = !flag;
        num6 = num5++;
    }
    return flag;
}

std::vector<Vector2> InnerPerimeterFromCurrentPerimeter(const std::vector<Vector2>& currentPerimeter, float width)
{
    const int n = static_cast<int>(currentPerimeter.size());
    std::vector<Vector2> array(currentPerimeter.size());
    for (int i = 0; i < n; i++) {
        int num = i - 1;
        if (num < 0) num = n - 1;
        int num2 = (i + 1) % n;
        const Vector2 cur = currentPerimeter[static_cast<size_t>(i)];
        Vector2 vector = unity::Normalized(currentPerimeter[static_cast<size_t>(num)] - cur) -
                         unity::Normalized(currentPerimeter[static_cast<size_t>(num2)] - cur);
        array[static_cast<size_t>(i)] = cur + unity::Normalized(Vector2{0.0f - vector.y, vector.x}) * width;
    }
    return array;
}

std::vector<std::vector<Vector2>> TraceGridOutlines(const std::vector<int>& cells, int gridSlicesX, int gridSlicesY, const std::vector<int>& check)
{
    const Grid g{cells, gridSlicesX, gridSlicesY, check};
    std::vector<Vector2> list;
    for (int i = 0; i < gridSlicesX; i++) {
        for (int j = 0; j < gridSlicesY; j++) {
            const Vector2 here{static_cast<float>(i), static_cast<float>(j)};
            if (g.CheckGridTrue(i, j) && (!g.CheckGrid(i + 1, j) || g.CheckGridFalse(i + 1, j)) && (!g.CheckGrid(i, j - 1) || g.CheckGridFalse(i, j - 1)) &&
                IndexOf(list, here) == -1)
                list.push_back(here);
            if (g.CheckGridTrue(i, j) && g.CheckGridTrue(i + 1, j) && g.CheckGridTrue(i, j - 1) && g.CheckGridFalse(i + 1, j - 1) && IndexOf(list, here) == -1)
                list.push_back(here);
        }
    }
    std::vector<std::vector<Vector2>> list2;
    for (size_t k = 0; k < list.size(); k++) {
        Vector2 vector = list[k];
        const float num = 0.5f;
        const float num2 = 0.5f;
        Vector2 vector2 = vector;
        Vector2 vector3{num, 0.0f - num2};
        std::vector<Vector2> list3;
        while (list3.empty() || vector3 + vector2 != list3[0]) {
            list3.push_back(vector2 + vector3);
            bool flag = false;
            Vector2 vector4{num, 0.0f - num2};
            Vector2 lastVertex{num, 0.0f - num2};
            int num3 = INT_MAX, num4 = INT_MAX, num5 = INT_MAX, num6 = INT_MAX;
            float x = 2.1474836e+09f, y = 2.1474836e+09f, x2 = 2.1474836e+09f, y2 = 2.1474836e+09f;
            const int vx = static_cast<int>(vector2.x), vy = static_cast<int>(vector2.y);
            if (vector3 == Vector2{num, 0.0f - num2}) {
                num3 = vx + 1;
                num4 = vy - 1;
                x = 0.0f - num;
                y = num2;
                num5 = vx + 1;
                num6 = vy;
                x2 = 0.0f - num;
                y2 = 0.0f - num2;
            } else if (vector3 == Vector2{num, num2}) {
                num3 = vx + 1;
                num4 = vy + 1;
                x = 0.0f - num;
                y = 0.0f - num2;
                num5 = vx;
                num6 = vy + 1;
                x2 = num;
                y2 = 0.0f - num2;
            } else if (vector3 == Vector2{0.0f - num, num2}) {
                num3 = vx - 1;
                num4 = vy + 1;
                x = num;
                y = 0.0f - num2;
                num5 = vx - 1;
                num6 = vy;
                x2 = num;
                y2 = num2;
            } else if (vector3 == Vector2{0.0f - num, 0.0f - num2}) {
                num3 = vx - 1;
                num4 = vy - 1;
                x = num;
                y = num2;
                num5 = vx;
                num6 = vy - 1;
                x2 = 0.0f - num;
                y2 = num2;
            }
            if (g.CheckGridTrue(num3, num4) && g.CheckGrid(num3, num4) && g.CheckGrid(num5, num6) && g.CheckGridTrue(num5, num6)) {
                flag = true;
                vector4 = {static_cast<float>(num3), static_cast<float>(num4)};
                lastVertex = {x, y};
            } else if (g.CheckGridTrue(num5, num6) && g.CheckGrid(num5, num6)) {
                flag = true;
                vector4 = {static_cast<float>(num5), static_cast<float>(num6)};
                lastVertex = {x2, y2};
            }
            if (flag) {
                vector2 = vector4;
                vector3 = GetNextVertex(lastVertex);
            } else {
                vector3 = GetNextVertex(vector3);
            }
        }
        list2.push_back(CleanVertexes(list3));
    }
    return CleanRepeatedPaths(list2);
}

std::vector<GridCollider> GenerateCollidersFromGrid(const std::vector<int>& cells, int gridSlicesX, int gridSlicesY, const std::vector<int>& check,
                                                    float tileSize, int colliderShrinkAmount)
{
    const int gridX = 480, gridY = 264; // the generator's
    std::vector<std::vector<Vector2>> list2 = TraceGridOutlines(cells, gridSlicesX, gridSlicesY, check);
    int num7 = 0;
    std::vector<GridCollider> list4;
    for (const std::vector<Vector2>& item : list2) {
        std::vector<Vector2> list5;
        int num8 = 0;
        for (size_t l = 0; l < item.size(); l++) {
            Vector2 vector5 = item[l];
            float num9 = tileSize;
            vector5 = {vector5.x * num9 + num9 / 2.0f - static_cast<float>(gridX) / 2.0f, vector5.y * num9 + num9 / 2.0f - static_cast<float>(gridY) / 2.0f};
            int num10 = 10;
            if (vector5.x == static_cast<float>(gridSlicesX) * num9 * 0.5f) vector5.x += static_cast<float>(num10);
            if (vector5.x == static_cast<float>(-gridSlicesX) * num9 * 0.5f) vector5.x -= static_cast<float>(num10);
            if (vector5.y == static_cast<float>(gridSlicesY) * num9 * 0.5f) vector5.y += static_cast<float>(num10);
            if (vector5.y == static_cast<float>(-gridSlicesY) * num9 * 0.5f) vector5.y -= static_cast<float>(num10);
            list5.push_back(vector5);
        }
        if (list5.empty() || num8 >= static_cast<int>(item.size()) / 2) continue;
        GridCollider collider;
        collider.name = "Collider_" + std::to_string(num7++);
        std::vector<Vector2> array = list5;
        if (colliderShrinkAmount != 0) array = InnerPerimeterFromCurrentPerimeter(array, static_cast<float>(colliderShrinkAmount));
        collider.paths.push_back(array);
        bool flag2 = false;
        for (GridCollider& item2 : list4) {
            if (PolygonInside(item2, collider) || PolygonInside(collider, item2)) {
                // DestroyImmediate(gameObject): the outline joins item2 as its next path.
                flag2 = true;
                item2.paths.push_back(InnerPerimeterFromCurrentPerimeter(list5, static_cast<float>(colliderShrinkAmount)));
                break;
            }
        }
        if (!flag2) list4.push_back(collider);
    }
    return list4;
}

} // namespace game
