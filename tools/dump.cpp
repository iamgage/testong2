#include "../shared/pc_saber.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace sfq::pc;
static void walk(const Saber& s, int n, int d) {
    const Node& nd = s.nodes[n];
    printf("%*s- %s active=%d rEn=%d mesh=%d mats=[", d*2, "", nd.name.c_str(), nd.active, nd.rendererEnabled, nd.mesh);
    for (int m : nd.mats) printf("%d ", m);
    printf("] pos=(%g,%g,%g) scale=(%g,%g,%g) rot=(%g,%g,%g,%g) trailMat=%d\n", nd.pos[0],nd.pos[1],nd.pos[2],nd.scale[0],nd.scale[1],nd.scale[2],nd.rot[0],nd.rot[1],nd.rot[2],nd.rot[3], nd.trailMat);
    for (int c : nd.children) walk(s, c, d+1);
}
int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Saber s; std::string err;
    bool ok = convertBundle(b, s, err);
    printf("ok=%d err=%s unity=%s platform=%d\n", ok, err.c_str(), s.unityVersion.c_str(), s.sourcePlatform);
    for (auto& w : s.warnings) printf("WARN: %s\n", w.c_str());
    printf("meshes=%zu tex=%zu mats=%zu nodes=%zu\n", s.meshes.size(), s.textures.size(), s.materials.size(), s.nodes.size());
    for (size_t i = 0; i < s.meshes.size(); ++i) { auto& m = s.meshes[i]; 
        float mn[3]={1e9,1e9,1e9}, mx[3]={-1e9,-1e9,-1e9};
        for (size_t v=0; v<m.vertexCount(); ++v) for(int k=0;k<3;++k){ mn[k]=std::min(mn[k],m.pos[v*3+k]); mx[k]=std::max(mx[k],m.pos[v*3+k]); }
        printf("mesh %zu '%s' verts=%zu subs=%zu uv0=%zu col=%zu bounds=(%g..%g, %g..%g, %g..%g)\n", i, m.name.c_str(), m.vertexCount(), m.submeshes.size(), m.uv0.size()/2, m.colors.size()/4, mn[0],mx[0],mn[1],mx[1],mn[2],mx[2]); }
    for (size_t i = 0; i < s.textures.size(); ++i) printf("tex %zu '%s' %dx%d\n", i, s.textures[i].name.c_str(), s.textures[i].width, s.textures[i].height);
    for (size_t i = 0; i < s.materials.size(); ++i) { auto& m = s.materials[i];
        printf("mat %zu '%s' shader='%s' kw='%s' rq=%d\n", i, m.name.c_str(), m.shader.c_str(), m.keywords.c_str(), m.renderQueue);
        for (auto& t : m.texs) printf("    tex %s -> %d\n", t.prop.c_str(), t.tex);
        for (auto& fl : m.floats) printf("    float %s=%g\n", fl.first.c_str(), fl.second);
        for (auto& c : m.colors) printf("    color %s=(%g,%g,%g,%g)\n", c.first.c_str(), c.second[0],c.second[1],c.second[2],c.second[3]);
    }
    if (ok) { printf("root=%d left=%d right=%d\n", s.root, s.left, s.right); walk(s, s.root, 0); }
}
