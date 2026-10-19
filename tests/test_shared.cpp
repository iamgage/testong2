#include "../shared/sfq_shared.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace sfq;
int main() {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "sfq_shared_test";
    fs::remove_all(dir); fs::create_directories(dir);
    std::ofstream(dir / "Zed.saber").put('x');
    std::ofstream(dir / "alpha.SABER").put('x');
    std::ofstream(dir / "notes.txt").put('x');
    auto list = discoverSabers(dir.string());
    assert(list.size() == 2 && list[0].name == "alpha" && list[1].name == "Zed");
    assert(indexOfName(list, "Zed") == 1 && indexOfName(list, "nope") == -1);
    assert(discoverSabers("/definitely/not/here").empty());
    fs::remove_all(dir);

    assert(cycleIndex(0, 3, -1) == 2 && cycleIndex(2, 3, 1) == 0 && cycleIndex(0, 0, 1) == -1);

    Config c; c.selected = "Zed"; c.trailWidth = 0.5f; c.trailDuration = 0.8f; c.trailEnabled = false;
    Config r = parseConfig(serializeConfig(c));
    assert(r.selected == "Zed" && !r.trailEnabled && std::fabs(r.trailWidth - 0.5f) < 1e-4 && std::fabs(r.trailDuration - 0.8f) < 1e-4);
    Config bad = parseConfig("trailWidth=99\ntrailDuration=-4\r\nselected=A B\r\nenabled=false\n");
    assert(bad.trailWidth == 1.0f && bad.trailDuration == 0.05f && bad.selected == "A B" && !bad.enabled);

    TrailHistory h;
    h.push({{0,0,0},{0,0,1}, 0.0f});
    h.push({{0,0,0},{1,0,1}, 0.1f});
    h.push({{0,0,0},{2,0,1}, 0.2f});
    auto m = h.build(0.2f, 1.0f, 0.4f);
    assert(m.vertices.size() == 6 && m.alphas.size() == 6 && m.triangles.size() == 24);
    assert(std::fabs(m.alphas[0] - 0.5f) < 1e-4 && std::fabs(m.alphas[4] - 1.0f) < 1e-4);
    for (int t : m.triangles) assert(t >= 0 && t < 6);
    assert(m.uAge.size() == 6 && std::fabs(m.uAge[0] - 0.5f) < 1e-4 && std::fabs(m.uAge[5]) < 1e-4);   // texture u: 0 = newest
    auto half = h.build(0.2f, 0.5f, 0.4f);
    assert(std::fabs(half.vertices[1].z - 0.5f) < 1e-4);
    auto pruned = h.build(0.55f, 1.0f, 0.4f);
    assert(pruned.vertices.empty() && h.size() == 1);
    assert(h.build(0.2f, 0.0f, 0.4f).vertices.empty());
    std::cout << "shared tests passed\n";
}
