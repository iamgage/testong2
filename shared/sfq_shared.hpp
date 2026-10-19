// Portable (no Unity / no IL2CPP) logic. Compiled for both host tests and the Quest mod.
#pragma once
#include <deque>
#include <string>
#include <vector>

namespace sfq {

struct V3 { float x = 0, y = 0, z = 0; };

// ---- Saber discovery ------------------------------------------------------
struct SaberEntry { std::string path; std::string name; };
// Non-recursive scan for *.saber, sorted by name (case-insensitive).
std::vector<SaberEntry> discoverSabers(const std::string& dir);
// Wraps around. Returns -1 for an empty list.
int cycleIndex(int current, int count, int delta);
int indexOfName(const std::vector<SaberEntry>& list, const std::string& name);

// ---- Config (simple key=value text file) ----------------------------------
struct Config {
    bool        enabled       = true;
    std::string selected;               // saber name (file stem); empty = vanilla
    bool        trailEnabled  = true;
    float       trailWidth    = 1.0f;   // fraction of blade length covered by the ribbon (0..1)
    float       trailDuration = 0.4f;   // seconds
};
Config parseConfig(const std::string& text);
std::string serializeConfig(const Config& c);
Config clampConfig(Config c);

// ---- Trail ribbon ---------------------------------------------------------
struct TrailPose { V3 base, tip; float time = 0; };
struct TrailMeshData {
    std::vector<V3>    vertices;   // 2 per pose: [tip-side, base-side]
    std::vector<float> alphas;     // 1 per vertex, 0..1 (newest = 1)
    std::vector<float> uAge;       // 1 per vertex, 0..1 (newest = 0): texture u coordinate along the ribbon
    std::vector<int>   triangles;  // 2 triangles per segment, double sided
};
class TrailHistory {
public:
    void push(const TrailPose& p);
    void clear() { poses_.clear(); }
    size_t size() const { return poses_.size(); }
    // Drops poses older than duration, returns ribbon for the remaining ones.
    TrailMeshData build(float now, float width, float duration);
private:
    std::deque<TrailPose> poses_;
};

}  // namespace sfq
