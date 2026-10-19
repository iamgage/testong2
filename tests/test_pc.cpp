// Host tests for the PC .saber reader. Usage: test_pc <dir produced by tools/make_test_bundle.py>
#include "../shared/pc_saber.hpp"
#include "../shared/unity_bundle.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace sfq;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
#define NEAR(a, b) CHECK(std::fabs((double)(a) - (double)(b)) < 1e-4)

static std::vector<uint8_t> slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "/tmp/sfq_pc_test";

    // ---- decompressors against known data ----
    auto blob = slurp(dir + "/blob.bin");
    CHECK(blob.size() > 50000);
    auto lz4 = slurp(dir + "/blob.lz4"), lzma = slurp(dir + "/blob.lzma");
    CHECK(unity::lz4Decompress(lz4.data(), lz4.size(), blob.size()) == blob);
    CHECK(unity::lzmaDecompress(lzma.data(), lzma.size(), blob.size()) == blob);
    try { unity::lz4Decompress(lz4.data(), lz4.size() / 2, blob.size()); CHECK(false); } catch (const unity::ParseError&) {}
    try { unity::lzmaDecompress(lzma.data(), 40, blob.size()); CHECK(false); } catch (const unity::ParseError&) {}

    // ---- DXT1 / DXT5 / misc decoders ----
    {
        const uint8_t dxt1[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0, 0, 0};
        std::vector<uint8_t> px;
        CHECK(pc::decodeTexture(10, 4, 4, dxt1, 8, px) && px.size() == 64);
        CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255);            // idx0 = c0 = red
        CHECK(px[4] == 0 && px[5] == 0 && px[6] == 255);                            // idx1 = c1 = blue
        CHECK(px[8] == 170 && px[9] == 0 && px[10] == 85);                          // idx2 = (2*c0+c1)/3
        CHECK(px[12] == 85 && px[13] == 0 && px[14] == 170);                        // idx3 = (c0+2*c1)/3
        CHECK(px[16] == 255 && px[17] == 0);                                        // row 1 -> idx0
        const uint8_t dxt5[16] = {255, 0, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24, 0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
        CHECK(pc::decodeTexture(12, 4, 4, dxt5, 16, px));
        CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0);                           // colour block: idx0 = red
        CHECK(px[3] == 0 && px[63] == 0);                                           // alpha indices all 1 -> a1 = 0
        const uint8_t one[4] = {1, 2, 3, 4};
        CHECK(pc::decodeTexture(14, 1, 1, one, 4, px) && px[0] == 3 && px[1] == 2 && px[2] == 1 && px[3] == 4);   // BGRA32
        CHECK(pc::decodeTexture(5, 1, 1, one, 4, px) && px[0] == 2 && px[1] == 3 && px[2] == 4 && px[3] == 1);    // ARGB32
        CHECK(!pc::decodeTexture(25, 4, 4, dxt5, 16, px));                          // BC7 unsupported -> false
        CHECK(!pc::decodeTexture(10, 4, 4, dxt1, 4, px));                           // too little data
    }

    // ---- bundle ----
    auto file = slurp(dir + "/test.saber");
    CHECK(!file.empty());
    unity::Bundle b = unity::parseBundle(file);
    CHECK(b.unityVersion == "2019.4.28f1");
    CHECK(b.files.size() == 2);
    CHECK(b.find("CAB-0123456789abcdef.resS") != nullptr);
    {
        auto bad = file; bad[0] = 'X';
        try { unity::parseBundle(bad); CHECK(false); } catch (const unity::ParseError&) {}
        try { unity::parseBundle(std::vector<uint8_t>(file.begin(), file.begin() + 100)); CHECK(false); } catch (const unity::ParseError&) {}
    }

    // ---- full conversion ----
    pc::Saber s; std::string err;
    CHECK(pc::convertBundle(file, s, err));
    if (!err.empty()) std::printf("err: %s\n", err.c_str());
    for (auto& w : s.warnings) std::printf("warning: %s\n", w.c_str());
    CHECK(s.warnings.empty());
    CHECK(s.sourcePlatform == 19);
    CHECK(s.nodes.size() == 3);
    CHECK(s.root >= 0 && s.nodes[s.root].name == "_CustomSaber");
    CHECK(s.left >= 0 && s.nodes[s.left].name == "LeftSaber");
    CHECK(s.right >= 0 && s.nodes[s.right].name == "RightSaber");
    if (s.left >= 0) {
        const auto& n = s.nodes[s.left];
        NEAR(n.pos[0], 0.1); NEAR(n.pos[1], 0.2); NEAR(n.pos[2], 0.3); NEAR(n.scale[2], 2.0); NEAR(n.rot[3], 1.0);
        CHECK(n.active && n.rendererEnabled && n.mesh == 0 && n.mats.size() == 2 && n.mats[0] == n.mats[1] && n.mats[0] >= 0);
        CHECK(n.trailMat >= 0 && n.trailLength == 20);
    }
    CHECK(s.meshes.size() == 1);
    if (!s.meshes.empty()) {
        const auto& m = s.meshes[0];
        CHECK(m.name == "TestMesh" && m.vertexCount() == 3);
        CHECK(m.pos.size() == 9 && m.normals.size() == 9 && m.uv0.size() == 6 && m.colors.size() == 12 && m.tangents.empty() && m.uv1.empty());
        NEAR(m.pos[5], 1.0); NEAR(m.pos[6], 1.0);                 // vertex 1 z, vertex 2 x
        NEAR(m.normals[4], 1.0); NEAR(m.normals[6], 1.0);
        NEAR(m.uv0[2], 1.0); NEAR(m.uv0[5], 1.0);                 // stream 1 (after 16 byte alignment padding) read correctly
        NEAR(m.colors[0], 1.0); NEAR(m.colors[1], 0.0); NEAR(m.colors[7], 128 / 255.0); NEAR(m.colors[11], 0.0);
        CHECK(m.submeshes.size() == 2);
        CHECK((m.submeshes[0] == std::vector<int>{0, 1, 2}) && (m.submeshes[1] == std::vector<int>{2, 1, 0}));
    }
    CHECK(s.materials.size() == 2);
    if (s.materials.size() == 2) {
        const auto& m = s.materials[0];
        CHECK(m.name == "Main" && m.shader == "Custom/TestSaber" && m.keywords == "FOO BAR" && m.renderQueue == 3000);
        CHECK(m.texs.size() == 1 && m.texs[0].prop == "_MainTex" && m.texs[0].tex >= 0);
        NEAR(m.texs[0].sx, 2.0); NEAR(m.texs[0].ox, 0.5);
        CHECK(m.floats.size() == 1 && m.floats[0].first == "_Glow"); NEAR(m.floats[0].second, 0.75);
        CHECK(m.colors.size() == 1 && m.colors[0].first == "_Color"); NEAR(m.colors[0].second[1], 0.5); NEAR(m.colors[0].second[2], 0.25);
        CHECK(s.materials[1].name == "_TrailMat" && s.materials[1].renderQueue == -1);
    }
    CHECK(s.trailMatFor(0) >= 0 && s.materials[s.trailMatFor(0)].name == "_TrailMat");
    CHECK(s.trailMatFor(1) == -1);
    CHECK(s.textures.size() == 2);
    if (s.textures.size() == 2) {
        const auto& d = s.textures[s.materials[0].texs[0].tex];     // DXT1, inline data
        CHECK(d.width == 4 && d.height == 4 && d.rgba.size() == 64 && d.rgba[0] == 255 && d.rgba[6] == 255);
        const auto& st = s.textures[s.materials[1].texs[0].tex];    // RGBA32 streamed from .resS
        CHECK(st.width == 2 && st.height == 2 && st.rgba.size() == 16);
        CHECK(st.rgba[0] == 10 && st.rgba[3] == 40 && st.rgba[15] == 160);
    }

    // ---- error reporting ----
    {
        pc::Saber s2; std::string e2;
        CHECK(!pc::convertBundle(std::vector<uint8_t>{'U', 'n', 'i', 't', 'y', 'W', 'e', 'b', 0}, s2, e2) && !e2.empty());
        std::vector<uint8_t> garbage(512, 0x5A);
        CHECK(!pc::convertBundle(garbage, s2, e2) && !e2.empty());
    }
    // truncated copies must fail cleanly (never crash / hang)
    for (size_t cut : {size_t(60), size_t(200), file.size() / 2, file.size() - 1}) {
        pc::Saber s3; std::string e3;
        std::vector<uint8_t> t(file.begin(), file.begin() + cut);
        CHECK(!pc::convertBundle(t, s3, e3));
    }
    // random corruption fuzz: must always return (true or false) without UB (run under ASan/UBSan)
    {
        unsigned seed = 12345;
        for (int it = 0; it < 300; ++it) {
            auto f = file;
            for (int k = 0; k < 4; ++k) { seed = seed * 1103515245u + 12345u; f[(seed >> 8) % f.size()] ^= (uint8_t)(seed >> 3) | 1; }
            pc::Saber s4; std::string e4;
            pc::convertBundle(f, s4, e4);
        }
        // fuzz the (uncompressed) serialized file directly so the type tree reader sees corrupt data too
        auto ser = slurp(dir + "/serialized.bin");
        for (int it = 0; it < 2000; ++it) {
            auto f = ser;
            for (int k = 0; k < 3; ++k) { seed = seed * 1103515245u + 12345u; f[(seed >> 8) % f.size()] ^= (uint8_t)(seed >> 3) | 1; }
            try {
                auto sf = unity::parseSerialized(f.data(), f.size());
                for (auto& o : sf.objects) { try { unity::readObject(sf, o); } catch (const unity::ParseError&) {} }
            } catch (const unity::ParseError&) {}
        }
    }

    if (g_fail) { std::printf("%d check(s) FAILED\n", g_fail); return 1; }
    std::printf("pc saber tests passed\n");
    return 0;
}
