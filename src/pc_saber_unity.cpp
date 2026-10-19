#include "pc_saber_unity.hpp"
#include "log.hpp"
#include "../shared/pc_saber.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <vector>

#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"

#include "UnityEngine/Object.hpp"
#include "UnityEngine/HideFlags.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/MeshFilter.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Mesh.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Texture.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Vector4.hpp"
#include "UnityEngine/Quaternion.hpp"

namespace sfq::pcunity {

static pc::Saber g_saber;
static bool g_loaded = false;
static std::string g_modDir;
static std::map<std::string, std::string> g_shaderMap;   // bundle shader name -> game shader name (shader_map.cfg)

// Unity assets, created lazily. SafePtrUnity keeps the managed wrappers alive for the GC; hideFlags keeps
// Unity's "unload unused assets" pass (run on scene changes) from destroying objects only we reference.
static bool g_built = false;
static std::vector<SafePtrUnity<UnityEngine::Mesh>>      g_meshes;
static std::vector<SafePtrUnity<UnityEngine::Texture2D>> g_textures;
static std::vector<SafePtrUnity<UnityEngine::Material>>  g_materials;
static SafePtrUnity<UnityEngine::Material>               g_defaultMat;

void unload() {
    g_saber = pc::Saber{};
    g_loaded = false; g_built = false;
    g_meshes.clear(); g_textures.clear(); g_materials.clear();
    g_defaultMat.emplace(nullptr);
}
bool loaded() { return g_loaded; }

static std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static void loadShaderMap() {
    g_shaderMap.clear();
    std::ifstream f(g_modDir + "/shader_map.cfg");
    if (!f) {
        // Create a documented, empty template so users know the file exists.
        std::ofstream o(g_modDir + "/shader_map.cfg");
        o << "# Maps shader names used by PC sabers to shaders that exist in Beat Saber on Quest.\n"
             "# Format:  PCShaderName=GameShaderName   (e.g.  Custom/CustomSaber=Custom/Glowing)\n"
             "# Sabers whose shader can't be found fall back to the vanilla saber shader; check logcat\n"
             "# (tag SaberFactoryQuest) for lines starting with 'shader' to see what was resolved.\n";
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        g_shaderMap[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
}

bool load(const std::string& path, const std::string& modDir, std::string& err) {
    unload();
    g_modDir = modDir;
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open file"; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    pc::Saber s;
    if (!pc::convertBundle(bytes, s, err)) return false;
    for (auto& w : s.warnings) SFQ_ERR("pc saber warning: %s", w.c_str());
    SFQ_LOG("pc saber parsed: unity %s, platform %d, %d nodes, %d meshes, %d materials, %d textures",
            s.unityVersion.c_str(), s.sourcePlatform, (int)s.nodes.size(), (int)s.meshes.size(),
            (int)s.materials.size(), (int)s.textures.size());
    g_saber = std::move(s);
    g_loaded = true;
    loadShaderMap();
    return true;
}

// ---------------------------------------------------------------------------------------------
// Asset construction
// ---------------------------------------------------------------------------------------------
static UnityEngine::Mesh* makeMesh(const pc::Mesh& m) {
    UnityEngine::Mesh* mesh = UnityEngine::Mesh::New_ctor();
    mesh->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
    const size_t n = m.vertexCount();

    ArrayW<UnityEngine::Vector3> pos(n);
    for (size_t i = 0; i < n; ++i) pos[i] = UnityEngine::Vector3(m.pos[i * 3], m.pos[i * 3 + 1], m.pos[i * 3 + 2]);
    mesh->set_vertices(pos);                     // must come first: it sizes the mesh
    if (m.normals.size() == n * 3) {
        ArrayW<UnityEngine::Vector3> a(n);
        for (size_t i = 0; i < n; ++i) a[i] = UnityEngine::Vector3(m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2]);
        mesh->set_normals(a);
    }
    if (m.tangents.size() == n * 4) {
        ArrayW<UnityEngine::Vector4> a(n);
        for (size_t i = 0; i < n; ++i) a[i] = UnityEngine::Vector4(m.tangents[i * 4], m.tangents[i * 4 + 1], m.tangents[i * 4 + 2], m.tangents[i * 4 + 3]);
        mesh->set_tangents(a);
    }
    if (m.uv0.size() == n * 2) {
        ArrayW<UnityEngine::Vector2> a(n);
        for (size_t i = 0; i < n; ++i) a[i] = UnityEngine::Vector2(m.uv0[i * 2], m.uv0[i * 2 + 1]);
        mesh->set_uv(a);
    }
    if (m.uv1.size() == n * 2) {
        ArrayW<UnityEngine::Vector2> a(n);
        for (size_t i = 0; i < n; ++i) a[i] = UnityEngine::Vector2(m.uv1[i * 2], m.uv1[i * 2 + 1]);
        mesh->set_uv2(a);
    }
    if (m.colors.size() == n * 4) {
        ArrayW<UnityEngine::Color> a(n);
        for (size_t i = 0; i < n; ++i) a[i] = UnityEngine::Color(m.colors[i * 4], m.colors[i * 4 + 1], m.colors[i * 4 + 2], m.colors[i * 4 + 3]);
        mesh->set_colors(a);
    }
    mesh->set_subMeshCount((int)m.submeshes.size());
    for (size_t s = 0; s < m.submeshes.size(); ++s) {
        ArrayW<int> tri(m.submeshes[s].size());
        for (size_t i = 0; i < tri.size(); ++i) tri[i] = m.submeshes[s][i];
        mesh->SetTriangles(tri, (int)s);
    }
    mesh->RecalculateBounds();
    if (m.normals.empty()) mesh->RecalculateNormals();
    return mesh;
}

static UnityEngine::Texture2D* makeTexture(const pc::Texture& t) {
    UnityEngine::Texture2D* tex = UnityEngine::Texture2D::New_ctor(t.width, t.height, UnityEngine::TextureFormat::RGBA32, false);
    tex->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
    ArrayW<uint8_t> raw(t.rgba.size());
    std::memcpy(raw.begin(), t.rgba.data(), t.rgba.size());
    tex->LoadRawTextureData(raw);
    tex->Apply(false, false);
    tex->set_filterMode(UnityEngine::FilterMode::Bilinear);
    return tex;
}

static UnityEngine::Shader* findShader(const char* name) {
    UnityEngine::Shader* s = UnityEngine::Shader::Find(StringW(name));
    return s;
}

// Resolution order: shader_map.cfg entry -> the bundle's own shader name (works for shaders that ship
// with the game) -> the vanilla saber's shader.
static UnityEngine::Shader* resolveShader(const std::string& pcName, UnityEngine::Shader* fallback) {
    UnityEngine::Shader* s = nullptr;
    auto it = g_shaderMap.find(pcName);
    if (it != g_shaderMap.end()) {
        s = findShader(it->second.c_str());
        SFQ_LOG("shader '%s' -> mapped '%s' : %s", pcName.c_str(), it->second.c_str(), s ? "found" : "NOT found");
        if (s) return s;
    }
    if (!pcName.empty()) {
        s = findShader(pcName.c_str());
        SFQ_LOG("shader '%s' : %s", pcName.c_str(), s ? "found in game" : "not in game, using fallback");
        if (s) return s;
    }
    return fallback;
}

static UnityEngine::Material* makeMaterial(const pc::Material& m, UnityEngine::Shader* fallback) {
    UnityEngine::Shader* shader = resolveShader(m.shader, fallback);
    if (!shader) return nullptr;
    UnityEngine::Material* mat = UnityEngine::Material::New_ctor(shader);
    mat->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
    for (auto& kw : [&] { std::vector<std::string> v; std::istringstream ss(m.keywords); std::string w; while (ss >> w) v.push_back(w); return v; }())
        mat->EnableKeyword(StringW(kw));
    for (auto& f : m.floats) { StringW n(f.first); if (mat->HasProperty(n)) mat->SetFloat(n, f.second); }
    for (auto& c : m.colors) {
        StringW n(c.first);
        if (mat->HasProperty(n)) mat->SetColor(n, UnityEngine::Color(c.second[0], c.second[1], c.second[2], c.second[3]));
    }
    for (auto& te : m.texs) {
        if (te.tex < 0 || te.tex >= (int)g_textures.size() || !g_textures[te.tex]) continue;
        StringW n(te.prop);
        if (!mat->HasProperty(n)) continue;
        mat->SetTexture(n, g_textures[te.tex].ptr());
        mat->SetTextureScale(n, UnityEngine::Vector2(te.sx, te.sy));
        mat->SetTextureOffset(n, UnityEngine::Vector2(te.ox, te.oy));
    }
    if (m.renderQueue >= 0) mat->set_renderQueue(m.renderQueue);
    return mat;
}

static void buildAssets(UnityEngine::Shader* fallback) {
    if (g_built) return;
    for (auto& m : g_saber.meshes) {
        SafePtrUnity<UnityEngine::Mesh> sp; sp.emplace(makeMesh(m)); g_meshes.push_back(sp);
    }
    for (auto& t : g_saber.textures) {
        SafePtrUnity<UnityEngine::Texture2D> sp; sp.emplace(makeTexture(t)); g_textures.push_back(sp);
    }
    for (auto& m : g_saber.materials) {
        SafePtrUnity<UnityEngine::Material> sp; sp.emplace(makeMaterial(m, fallback)); g_materials.push_back(sp);
    }
    if (fallback) {
        UnityEngine::Material* d = UnityEngine::Material::New_ctor(fallback);
        d->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
        g_defaultMat.emplace(d);
    }
    g_built = true;
    SFQ_LOG("pc saber assets built: %d meshes, %d textures, %d materials", (int)g_meshes.size(), (int)g_textures.size(), (int)g_materials.size());
}

// ---------------------------------------------------------------------------------------------
// Hierarchy
// ---------------------------------------------------------------------------------------------
static UnityEngine::Transform* buildNode(int idx, UnityEngine::Transform* parent, int layer, int depth) {
    if (idx < 0 || idx >= (int)g_saber.nodes.size() || depth > 64) return nullptr;
    const pc::Node& n = g_saber.nodes[idx];
    UnityEngine::GameObject* go = UnityEngine::GameObject::New_ctor(StringW(n.name));
    UnityEngine::Transform* t = go->get_transform();
    if (parent) t->SetParent(parent, false);
    go->set_layer(layer);
    t->set_localPosition(UnityEngine::Vector3(n.pos[0], n.pos[1], n.pos[2]));
    t->set_localRotation(UnityEngine::Quaternion(n.rot[0], n.rot[1], n.rot[2], n.rot[3]));
    t->set_localScale(UnityEngine::Vector3(n.scale[0], n.scale[1], n.scale[2]));

    if (n.mesh >= 0 && n.mesh < (int)g_meshes.size() && g_meshes[n.mesh]) {
        UnityEngine::MeshFilter* mf = go->AddComponent<UnityEngine::MeshFilter*>();
        mf->set_sharedMesh(g_meshes[n.mesh].ptr());
        UnityEngine::MeshRenderer* mr = go->AddComponent<UnityEngine::MeshRenderer*>();
        const size_t subs = g_saber.meshes[n.mesh].submeshes.size();
        ArrayW<UnityEngine::Material*> mats(subs);
        for (size_t s = 0; s < subs; ++s) {
            int mi = n.mats.empty() ? -1 : n.mats[std::min(s, n.mats.size() - 1)];
            UnityEngine::Material* m = (mi >= 0 && mi < (int)g_materials.size() && g_materials[mi]) ? g_materials[mi].ptr()
                                       : (g_defaultMat ? g_defaultMat.ptr() : nullptr);
            mats[s] = m;
        }
        mr->set_sharedMaterials(mats);
        mr->set_enabled(n.rendererEnabled);
    }
    for (int c : n.children) buildNode(c, t, layer, depth + 1);
    go->SetActive(n.active);
    return t;
}

UnityEngine::Transform* instantiateHand(int hand, int layer, UnityEngine::Shader* fallbackShader) {
    if (!g_loaded) return nullptr;
    buildAssets(fallbackShader);
    return buildNode(hand == 0 ? g_saber.left : g_saber.right, nullptr, layer, 0);
}

UnityEngine::Material* trailMaterial(int hand) {
    if (!g_loaded || !g_built) return nullptr;
    int mi = g_saber.trailMatFor(hand);
    if (mi < 0 || mi >= (int)g_materials.size() || !g_materials[mi]) return nullptr;
    return g_materials[mi].ptr();
}

}  // namespace sfq::pcunity
