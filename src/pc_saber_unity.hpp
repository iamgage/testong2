#pragma once
#include <string>

namespace UnityEngine { class Transform; class Material; class Shader; }

// Runtime support for PC (Windows) CustomSabers .saber files: the bundle is parsed in C++ (shared/)
// and the saber is rebuilt out of real Unity objects (Mesh, Texture2D, Material, GameObject).
namespace sfq::pcunity {

// Reads + converts the bundle. Cheap on Unity objects (nothing is created yet).
// `modDir` is where an optional shader_map.cfg lives. Returns false and fills `err` on failure.
bool load(const std::string& path, const std::string& modDir, std::string& err);
bool loaded();
void unload();

// Builds a fresh copy of the given hand's object tree (0 = LeftSaber, 1 = RightSaber), unparented.
// Unity assets (meshes/textures/materials) are created on first use and shared between hands.
// `fallbackShader` (taken from the vanilla saber) is used when a bundle shader isn't in the game.
UnityEngine::Transform* instantiateHand(int hand, int layer, UnityEngine::Shader* fallbackShader);

// Material of the saber's CustomTrail for that hand, or nullptr. Valid after instantiateHand().
UnityEngine::Material* trailMaterial(int hand);

}  // namespace sfq::pcunity
