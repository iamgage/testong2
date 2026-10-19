#include "sfq_shared.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace sfq {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::vector<SaberEntry> discoverSabers(const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<SaberEntry> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        if (lower(it->path().extension().string()) != ".saber") continue;
        out.push_back({it->path().string(), it->path().stem().string()});
    }
    std::sort(out.begin(), out.end(), [](const SaberEntry& a, const SaberEntry& b) {
        return lower(a.name) < lower(b.name);
    });
    return out;
}

int cycleIndex(int current, int count, int delta) {
    if (count <= 0) return -1;
    int v = (current + delta) % count;
    return v < 0 ? v + count : v;
}

int indexOfName(const std::vector<SaberEntry>& list, const std::string& name) {
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == name) return (int)i;
    return -1;
}

Config clampConfig(Config c) {
    c.trailWidth    = std::clamp(c.trailWidth, 0.0f, 1.0f);
    c.trailDuration = std::clamp(c.trailDuration, 0.05f, 2.0f);
    return c;
}

static bool parseBool(const std::string& v) {
    auto l = lower(v);
    return l == "1" || l == "true" || l == "yes" || l == "on";
}

Config parseConfig(const std::string& text) {
    Config c;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = lower(line.substr(0, eq)), v = line.substr(eq + 1);
        if      (k == "enabled")        c.enabled = parseBool(v);
        else if (k == "selected")       c.selected = v;
        else if (k == "trailenabled")   c.trailEnabled = parseBool(v);
        else if (k == "trailwidth")     c.trailWidth = (float)std::atof(v.c_str());
        else if (k == "trailduration")  c.trailDuration = (float)std::atof(v.c_str());
    }
    return clampConfig(c);
}

std::string serializeConfig(const Config& c) {
    std::ostringstream o;
    o << "# SaberFactory Quest config. 'selected' is a .saber file name without extension.\n"
      << "enabled=" << (c.enabled ? 1 : 0) << "\n"
      << "selected=" << c.selected << "\n"
      << "trailEnabled=" << (c.trailEnabled ? 1 : 0) << "\n"
      << "trailWidth=" << c.trailWidth << "\n"
      << "trailDuration=" << c.trailDuration << "\n";
    return o.str();
}

void TrailHistory::push(const TrailPose& p) {
    poses_.push_back(p);
    if (poses_.size() > 512) poses_.pop_front();
}

TrailMeshData TrailHistory::build(float now, float width, float duration) {
    TrailMeshData m;
    while (!poses_.empty() && now - poses_.front().time > duration) poses_.pop_front();
    if (poses_.size() < 2 || width <= 0 || duration <= 0) return m;
    width = std::clamp(width, 0.0f, 1.0f);
    for (const auto& p : poses_) {
        float age = std::clamp((now - p.time) / duration, 0.0f, 1.0f);
        float a = 1.0f - age;
        V3 inner{p.tip.x + (p.base.x - p.tip.x) * width,
                 p.tip.y + (p.base.y - p.tip.y) * width,
                 p.tip.z + (p.base.z - p.tip.z) * width};
        m.vertices.push_back(p.tip);  m.alphas.push_back(a);  m.uAge.push_back(age);
        m.vertices.push_back(inner);  m.alphas.push_back(a);  m.uAge.push_back(age);
    }
    int segs = (int)poses_.size() - 1;
    for (int i = 0; i < segs; ++i) {
        int a = i * 2, b = a + 1, c = a + 2, d = a + 3;
        m.triangles.insert(m.triangles.end(), {a, c, b,  b, c, d});   // front
        m.triangles.insert(m.triangles.end(), {a, b, c,  b, d, c});   // back
    }
    return m;
}

}  // namespace sfq
