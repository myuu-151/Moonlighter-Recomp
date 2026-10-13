#pragma once
// For the oracle tests built against the translated game code (ML_TRANSLATED=ON): the hand
// port's game::Constants functions, answered by the translated Constants.cs, which reads the
// "constants" TextAsset with the game's own FullSerializer JSON parser.

#include <string>
#include <vector>

namespace translated {
namespace Constants {

// Points Resources.Load at the file's folder and runs Constants.ReadFile("<file name>").
bool LoadFromFile(const std::string& path);

int GetInt(const std::string& key);
float GetFloat(const std::string& key);
bool GetBool(const std::string& key);
std::vector<float> GetFloatArray(const std::string& key);
std::vector<std::string> GetStringArray(const std::string& key);
std::vector<std::vector<float>> GetFloat2DArray(const std::string& key);

} // namespace Constants
} // namespace translated

namespace game {
namespace Constants = ::translated::Constants;
} // namespace game
