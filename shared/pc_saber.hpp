// Converts a PC (Windows) CustomSabers .saber AssetBundle into a plain, engine-independent model
// (meshes, decoded textures, material parameters, GameObject hierarchy). The Quest side then
// rebuilds real Unity objects from this model at runtime. Portable + host-testable.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sfq::pc {

struct Texture {
    std::string name;
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;     // width*height*4, rows in Unity order (bottom row first)
};
struct TexEnv { std::string prop; int tex = -1; float sx = 1, sy = 1, ox = 0, oy = 0; };
struct Material {
    std::string name, shader, keywords;
    int renderQueue = -1;
    std::vector<TexEnv> texs;
    std::vector<std::pair<std::string, float>> floats;
    std::vector<std::pair<std::string, std::array<float, 4>>> colors;
};
struct Mesh {
    std::string name;
    std::vector<float> pos, normals, tangents, uv0, uv1, colors;   // 3/3/4/2/2/4 floats per vertex (empty = absent)
    std::vector<std::vector<int>> submeshes;                       // triangle lists
    size_t vertexCount() const { return pos.size() / 3; }
};
struct Node {
    std::string name;
    float pos[3] = {0, 0, 0}, rot[4] = {0, 0, 0, 1}, scale[3] = {1, 1, 1};
    bool active = true, rendererEnabled = true;
    int mesh = -1;
    std::vector<int> mats;          // material index per sub mesh, -1 = unknown
    int trailMat = -1;              // CustomTrail.TrailMaterial found on this node
    int trailLength = 0;            // CustomTrail.Length (frames), 0 = unknown
    std::vector<int> children;
};
struct Saber {
    std::vector<Mesh> meshes;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Node> nodes;
    int root = -1, left = -1, right = -1;      // node indices ("_CustomSaber", "LeftSaber", "RightSaber")
    int sourcePlatform = 0;                     // RuntimePlatform of the bundle: 13 = Android
    std::string unityVersion;
    std::vector<std::string> warnings;          // non fatal problems (unsupported texture format, ...)
    int trailMatFor(int hand) const;            // trail material of the subtree below left/right (-1 = none)
};

// Decodes a texture level 0 to RGBA8. Returns false for unsupported formats.
bool decodeTexture(int unityFormat, int w, int h, const uint8_t* data, size_t size, std::vector<uint8_t>& rgba);

// Parses + converts. Returns false and sets `err` when the file is not a usable saber bundle.
bool convertBundle(const std::vector<uint8_t>& fileBytes, Saber& out, std::string& err);

}  // namespace sfq::pc
