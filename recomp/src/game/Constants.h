#pragma once
// Constants (Constants.cs): the game's tuning table, read from the "constants" TextAsset.
// Getters keep the C# coercions: missing keys give 0, ints read as floats and vice versa.

#include <string>
#include <vector>

namespace game {
namespace Constants {

void LoadFromText(const std::string& jsonText);
bool LoadFromFile(const std::string& path);

int GetInt(const std::string& key);
float GetFloat(const std::string& key);
bool GetBool(const std::string& key);
// Empty vector where C# returns null (missing key / not a list / wrong element types).
std::vector<float> GetFloatArray(const std::string& key);
std::vector<std::string> GetStringArray(const std::string& key);
// A list of lists of doubles (float[][]); empty where C# returns null (any row not a list,
// any element not written as a double).
std::vector<std::vector<float>> GetFloat2DArray(const std::string& key);

} // namespace Constants
} // namespace game
