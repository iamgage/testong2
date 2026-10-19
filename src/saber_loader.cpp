#include "saber_loader.hpp"
#include "log.hpp"
#include "pc_saber_unity.hpp"

#include <fstream>
#include <filesystem>
#include <sstream>

#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"

#include "UnityEngine/AssetBundle.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Behaviour.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/MeshFilter.hpp"
#include "UnityEngine/Mesh.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Time.hpp"
#include "GlobalNamespace/SaberType.hpp"

namespace fs = std::filesystem;

namespace sfq {

static std::string readFile(const std::string& p);
static const std::string kDir     = "/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberFactoryQuest";
static const std::string kSabers  = kDir + "/Sabers";
static const std::string kCfgFile = kDir + "/config.cfg";

// Asset names expected inside the Android AssetBundle (see README for the Unity-side setup).
static constexpr const char* kPrefabName   = "_CustomSaber";
static constexpr const char* kLeftName     = "LeftSaber";
static constexpr const char* kRightName    = "RightSaber";
static constexpr const char* kTrailMatName = "_TrailMaterial";

static Config g_cfg;
static std::string g_loadedName;   // name of the bundle currently loaded ("" = none)
static SafePtrUnity<UnityEngine::AssetBundle> g_bundle;
static SafePtrUnity<UnityEngine::GameObject>  g_prefab;
static SafePtrUnity<UnityEngine::Material>    g_trailMat;
static bool g_pcMode = false;      // true = selected saber is a PC bundle rebuilt by pcunity (not an Android AssetBundle)

const Config& config() { return g_cfg; }

std::vector<SaberEntry> listSabers() {
    std::error_code ec; fs::create_directories(kSabers, ec);
    return discoverSabers(kSabers);
}

void updateConfig(const std::function<void(Config&)>& edit) {
    std::error_code ec; fs::create_directories(kDir, ec);
    Config c = fs::exists(kCfgFile, ec) ? parseConfig(readFile(kCfgFile)) : Config{};
    edit(c);
    c = clampConfig(c);
    std::ofstream(kCfgFile, std::ios::trunc) << serializeConfig(c);
    g_cfg = c;
}

static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

static void unloadBundle() {
    if (g_bundle) { g_bundle->Unload(false); }   // false = keep already-instantiated objects alive
    g_bundle.emplace(nullptr); g_prefab.emplace(nullptr); g_trailMat.emplace(nullptr);
    pcunity::unload(); g_pcMode = false;
    g_loadedName.clear();
}

// Loads an asset by name+type through the IL2CPP runtime (name based, so it does not depend on
// the exact generated wrapper for AssetBundle::LoadAsset in the installed bs-cordl).
template <typename T>
static T* loadAsset(UnityEngine::AssetBundle* b, const char* name, System::Type* type) {
    try {
        UnityEngine::Object* r = il2cpp_utils::RunMethodRethrow<UnityEngine::Object*, false>(b, "LoadAsset", StringW(name), type);
        return reinterpret_cast<T*>(r);
    } catch (...) {
        return nullptr;
    }
}

bool prepareSelection() {
    std::error_code ec;
    fs::create_directories(kSabers, ec);
    if (!fs::exists(kCfgFile, ec)) { std::ofstream(kCfgFile) << serializeConfig(Config{}); }
    g_cfg = parseConfig(readFile(kCfgFile));

    if (!g_cfg.enabled || g_cfg.selected.empty()) { if (!g_loadedName.empty()) unloadBundle(); return false; }
    if (g_cfg.selected == g_loadedName && (g_pcMode ? pcunity::loaded() : (g_bundle && g_prefab))) return true;

    unloadBundle();
    auto list = discoverSabers(kSabers);
    int idx = indexOfName(list, g_cfg.selected);
    SFQ_LOG("selected '%s'; %d .saber file(s) in %s; match index %d", g_cfg.selected.c_str(), (int)list.size(), kSabers.c_str(), idx);
    if (idx < 0) { SFQ_ERR("Saber '%s' not found in %s", g_cfg.selected.c_str(), kSabers.c_str()); return false; }

    UnityEngine::AssetBundle* bundle = UnityEngine::AssetBundle::LoadFromFile(StringW(list[idx].path));
    if (!bundle) {
        // Not an Android bundle -> try it as a PC (Windows) saber: parse it ourselves and rebuild it from Unity objects.
        SFQ_LOG("'%s' is not loadable as an Android bundle; trying PC saber converter", list[idx].path.c_str());
        std::string err;
        if (pcunity::load(list[idx].path, kDir, err)) {
            g_pcMode = true;
            g_loadedName = g_cfg.selected;
            SFQ_LOG("Loaded PC saber '%s'", g_loadedName.c_str());
            return true;
        }
        SFQ_ERR("Could not load '%s' as an Android bundle or as a PC saber: %s", list[idx].path.c_str(), err.c_str());
        return false;
    }
    g_bundle.emplace(bundle);

    auto* prefab = loadAsset<UnityEngine::GameObject>(bundle, kPrefabName, csTypeOf(UnityEngine::GameObject*));
    if (!prefab) { SFQ_ERR("Asset '%s' missing from bundle", kPrefabName); unloadBundle(); return false; }
    g_prefab.emplace(prefab);

    if (auto* mat = loadAsset<UnityEngine::Material>(bundle, kTrailMatName, csTypeOf(UnityEngine::Material*)))
        g_trailMat.emplace(mat);
    else
        SFQ_LOG("No '%s' in bundle: custom trail disabled for this saber", kTrailMatName);

    g_loadedName = g_cfg.selected;
    SFQ_LOG("Loaded saber bundle '%s'", g_loadedName.c_str());
    return true;
}

// ---- per hand state -------------------------------------------------------
struct Hand {
    SafePtrUnity<UnityEngine::GameObject> trailGO;
    SafePtrUnity<UnityEngine::Mesh>       mesh;
    GlobalNamespace::Saber*               saber = nullptr;   // identity only, never dereferenced here
    TrailHistory                          history;
};
static Hand g_hands[2];

static int handIndex(GlobalNamespace::Saber* s) {
    return static_cast<int>(s->get_saberType()) == 0 ? 0 : 1;   // SaberA = left, SaberB = right
}

static void destroyTrail(Hand& h) {
    if (h.trailGO && h.trailGO.isAlive()) UnityEngine::Object::Destroy(h.trailGO.ptr());
    h.trailGO.emplace(nullptr); h.mesh.emplace(nullptr);
    h.history.clear(); h.saber = nullptr;
}

static void hideVanilla(GlobalNamespace::SaberModelController* c) {
    auto renderers = c->GetComponentsInChildren<UnityEngine::MeshRenderer*>(true);
    for (auto* r : renderers) if (r) r->set_enabled(false);
    // Vanilla ribbon: SaberModelController._saberTrail (a MonoBehaviour). Looked up by name so a
    // renamed generated field shows up as a log line instead of a compile error.
    auto trail = il2cpp_utils::GetFieldValue<UnityEngine::Behaviour*>(c, "_saberTrail");
    if (trail && *trail) (*trail)->set_enabled(false);
}

// Material of the vanilla saber (first renderer that has one). PC saber materials whose shader is not in the game are
// built as copies of it.
static UnityEngine::Material* vanillaMaterial(GlobalNamespace::SaberModelController* c) {
    auto renderers = c->GetComponentsInChildren<UnityEngine::MeshRenderer*>(true);
    for (auto* r : renderers) {
        if (!r) continue;
        UnityEngine::Material* m = r->get_sharedMaterial();
        if (m && m->get_shader()) return m;
    }
    return nullptr;
}

static std::string nameOf(StringW s) { return s ? std::string(s) : std::string("?"); }

// Writes every shader the game has to the log once, so PC shader names can be mapped in shader_map.cfg without guessing.
static void dumpShaders() {
    static bool done = false;
    if (done) return;
    done = true;
    try {
        auto all = UnityEngine::Resources::FindObjectsOfTypeAll(csTypeOf(UnityEngine::Shader*));
        SFQ_LOG("shaders available in this game: %d", (int)all.size());
        for (auto o : all) {   // elements are UnityW<Object> wrappers in this SDK version, not raw pointers
            if (!o) continue;
            SFQ_LOG("  shader: %s", nameOf(o->get_name()).c_str());
        }
    } catch (...) { SFQ_ERR("could not list shaders"); }
}

// Diagnostics: what actually ended up in the scene for this hand (helps when a saber is invisible).
static void logEquipped(UnityEngine::Transform* child, int hand) {
    try {
        auto rs = child->GetComponentsInChildren<UnityEngine::MeshRenderer*>(true);
        int on = 0;
        for (auto* r : rs) if (r && r->get_enabled()) ++on;
        SFQ_LOG("hand %d: %d mesh renderer(s) under the custom saber, %d enabled, root activeInHierarchy=%d",
                hand, (int)rs.size(), on, (int)child->get_gameObject()->get_activeInHierarchy());
        int shown = 0;
        for (auto* r : rs) {
            if (!r || shown >= 8) break;
            UnityEngine::Material* m = r->get_sharedMaterial();
            UnityEngine::Shader* sh = m ? m->get_shader() : nullptr;
            SFQ_LOG("  renderer '%s' enabled=%d shader='%s'", nameOf(r->get_gameObject()->get_name()).c_str(),
                    (int)r->get_enabled(), sh ? nameOf(sh->get_name()).c_str() : "(none)");
            ++shown;
        }
    } catch (...) { SFQ_ERR("logEquipped failed"); }
}

void equip(GlobalNamespace::SaberModelController* controller, GlobalNamespace::Saber* saber) {
    if (!controller || !saber) return;
    if (g_pcMode ? !pcunity::loaded() : (!g_prefab || !g_prefab.isAlive())) return;
    const int hi = handIndex(saber);

    UnityEngine::Transform* child = nullptr;
    UnityEngine::GameObject* root = nullptr;   // Android bundle only: the instantiated prefab root (destroyed after re-parenting)
    if (g_pcMode) {
        UnityEngine::Material* vm = vanillaMaterial(controller);
        SFQ_LOG("vanilla saber material (template for PC materials): '%s' shader '%s'", vm ? nameOf(vm->get_name()).c_str() : "(none found)",
                vm && vm->get_shader() ? nameOf(vm->get_shader()->get_name()).c_str() : "(none)");
        dumpShaders();
        child = pcunity::instantiateHand(hi, controller->get_gameObject()->get_layer(), vm);
        if (!child) { SFQ_ERR("PC saber: could not build hand %d", hi); return; }
    } else {
        const char* childName = hi == 0 ? kLeftName : kRightName;
        try {
            UnityEngine::Object* o = il2cpp_utils::RunMethodRethrow<UnityEngine::Object*, false>(
                classof(UnityEngine::Object*), "Instantiate", reinterpret_cast<UnityEngine::Object*>(g_prefab.ptr()));
            root = reinterpret_cast<UnityEngine::GameObject*>(o);
        } catch (...) {
            root = nullptr;
        }
        if (!root) { SFQ_ERR("Instantiate failed"); return; }
        child = root->get_transform()->Find(StringW(childName));
        if (!child) {
            SFQ_ERR("Prefab has no child '%s'", childName);
            UnityEngine::Object::Destroy(root);
            return;
        }
    }

    hideVanilla(controller);

    UnityEngine::Transform* parent = controller->get_transform();
    child->SetParent(parent, false);
    child->set_localPosition(UnityEngine::Vector3::get_zero());
    child->set_localRotation(UnityEngine::Quaternion::get_identity());
    if (root) UnityEngine::Object::Destroy(root);   // drop the other hand + prefab root; child was re-parented

    // ---- trail ----
    Hand& h = g_hands[hi];
    destroyTrail(h);
    UnityEngine::Material* trailMat = g_pcMode ? pcunity::trailMaterial(hi)
                                               : ((g_trailMat && g_trailMat.isAlive()) ? g_trailMat.ptr() : nullptr);
    if (g_cfg.trailEnabled && trailMat) {
        UnityEngine::GameObject* go = UnityEngine::GameObject::New_ctor(StringW(hi == 0 ? "SFQTrailL" : "SFQTrailR"));
        UnityEngine::MeshFilter* mf = go->AddComponent<UnityEngine::MeshFilter*>();
        UnityEngine::MeshRenderer* mr = go->AddComponent<UnityEngine::MeshRenderer*>();
        UnityEngine::Mesh* mesh = UnityEngine::Mesh::New_ctor();
        mf->set_sharedMesh(mesh);
        mr->set_sharedMaterial(trailMat);
        h.trailGO.emplace(go); h.mesh.emplace(mesh); h.saber = saber;
    }
    SFQ_LOG("Equipped %s saber on hand %d", g_pcMode ? "PC" : "custom", hi);
    logEquipped(child, hi);
}

void tickTrail(GlobalNamespace::Saber* saber) {
    Hand& h = g_hands[handIndex(saber)];
    if (h.saber != saber || !h.mesh || !h.mesh.isAlive()) return;

    const float now = UnityEngine::Time::get_time();
    auto top = saber->get_saberBladeTopPos();
    auto bot = saber->get_saberBladeBottomPos();
    h.history.push({{bot.x, bot.y, bot.z}, {top.x, top.y, top.z}, now});

    auto data = h.history.build(now, g_cfg.trailWidth, g_cfg.trailDuration);
    auto* mesh = h.mesh.ptr();
    mesh->Clear();
    if (data.vertices.empty()) return;

    ArrayW<UnityEngine::Vector3> verts(data.vertices.size());
    ArrayW<UnityEngine::Color>   cols(data.vertices.size());
    ArrayW<UnityEngine::Vector2> uvs(data.vertices.size());
    ArrayW<int>                  tris(data.triangles.size());
    for (size_t i = 0; i < data.vertices.size(); ++i) {
        verts[i] = UnityEngine::Vector3(data.vertices[i].x, data.vertices[i].y, data.vertices[i].z);
        cols[i]  = UnityEngine::Color(1.f, 1.f, 1.f, data.alphas[i]);
        uvs[i]   = UnityEngine::Vector2(data.uAge[i], (float)(i & 1));   // u = 0 newest .. 1 oldest, v = across the ribbon
    }
    for (size_t i = 0; i < data.triangles.size(); ++i) tris[i] = data.triangles[i];
    mesh->set_vertices(verts);
    mesh->set_colors(cols);
    mesh->set_uv(uvs);
    mesh->set_triangles(tris);
    mesh->RecalculateBounds();
}

}  // namespace sfq
