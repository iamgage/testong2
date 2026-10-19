#include "pc_saber.hpp"
#include "unity_bundle.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

using namespace sfq::unity;

namespace sfq::pc {

// ===========================================================================
// Texture decoding
// ===========================================================================
static inline uint8_t e5(int v) { return (uint8_t)((v << 3) | (v >> 2)); }
static inline uint8_t e6(int v) { return (uint8_t)((v << 2) | (v >> 4)); }

static void rgb565(uint16_t c, uint8_t out[3]) {
    out[0] = e5((c >> 11) & 31); out[1] = e6((c >> 5) & 63); out[2] = e5(c & 31);
}

// Decodes the 8 byte colour half of a DXT block into 16 RGBA pixels.
static void dxtColour(const uint8_t* b, bool allowPunchThrough, uint8_t px[16][4]) {
    uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    uint8_t pal[4][4];
    rgb565(c0, pal[0]); rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || !allowPunchThrough) {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) { pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2); pal[3][k] = 0; }
        pal[2][3] = 255; pal[3][3] = 0;
    }
    uint32_t idx = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    for (int i = 0; i < 16; ++i) std::memcpy(px[i], pal[(idx >> (2 * i)) & 3], 4);
}

static void dxtAlpha(const uint8_t* b, uint8_t a[16]) {
    uint8_t tab[8]; tab[0] = b[0]; tab[1] = b[1];
    if (tab[0] > tab[1]) for (int i = 1; i <= 6; ++i) tab[1 + i] = (uint8_t)(((7 - i) * tab[0] + i * tab[1]) / 7);
    else { for (int i = 1; i <= 4; ++i) tab[1 + i] = (uint8_t)(((5 - i) * tab[0] + i * tab[1]) / 5); tab[6] = 0; tab[7] = 255; }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= (uint64_t)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; ++i) a[i] = tab[(bits >> (3 * i)) & 7];
}

static size_t levelSize(int fmt, int w, int h) {
    size_t bw = (size_t)(w + 3) / 4, bh = (size_t)(h + 3) / 4;
    switch (fmt) {
        case 1: return (size_t)w * h;
        case 3: return (size_t)w * h * 3;
        case 4: case 5: case 14: return (size_t)w * h * 4;
        case 7: case 13: return (size_t)w * h * 2;
        case 10: return bw * bh * 8;
        case 12: return bw * bh * 16;
        default: return 0;
    }
}

bool decodeTexture(int fmt, int w, int h, const uint8_t* d, size_t size, std::vector<uint8_t>& rgba) {
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    size_t need = levelSize(fmt, w, h);
    if (need == 0 || size < need) return false;
    rgba.assign((size_t)w * h * 4, 255);
    const size_t n = (size_t)w * h;
    switch (fmt) {
        case 1: for (size_t i = 0; i < n; ++i) { rgba[i * 4 + 3] = d[i]; } return true;                       // Alpha8
        case 3: for (size_t i = 0; i < n; ++i) { rgba[i*4] = d[i*3]; rgba[i*4+1] = d[i*3+1]; rgba[i*4+2] = d[i*3+2]; } return true;
        case 4: std::memcpy(rgba.data(), d, n * 4); return true;                                                // RGBA32
        case 5: for (size_t i = 0; i < n; ++i) { rgba[i*4] = d[i*4+1]; rgba[i*4+1] = d[i*4+2]; rgba[i*4+2] = d[i*4+3]; rgba[i*4+3] = d[i*4]; } return true;  // ARGB32
        case 14: for (size_t i = 0; i < n; ++i) { rgba[i*4] = d[i*4+2]; rgba[i*4+1] = d[i*4+1]; rgba[i*4+2] = d[i*4]; rgba[i*4+3] = d[i*4+3]; } return true; // BGRA32
        case 7: for (size_t i = 0; i < n; ++i) { uint16_t c = (uint16_t)(d[i*2] | d[i*2+1] << 8); rgb565(c, &rgba[i*4]); } return true;
        case 13: for (size_t i = 0; i < n; ++i) {
            uint16_t c = (uint16_t)(d[i*2] | d[i*2+1] << 8);
            rgba[i*4] = (uint8_t)(((c >> 12) & 15) * 17); rgba[i*4+1] = (uint8_t)(((c >> 8) & 15) * 17);
            rgba[i*4+2] = (uint8_t)(((c >> 4) & 15) * 17); rgba[i*4+3] = (uint8_t)((c & 15) * 17);
        } return true;
        case 10: case 12: {
            const bool isDxt5 = fmt == 12;
            const size_t bw = (size_t)(w + 3) / 4, bh = (size_t)(h + 3) / 4, bs = isDxt5 ? 16 : 8;
            for (size_t by = 0; by < bh; ++by)
                for (size_t bx = 0; bx < bw; ++bx) {
                    const uint8_t* blk = d + (by * bw + bx) * bs;
                    uint8_t px[16][4], al[16];
                    dxtColour(blk + (isDxt5 ? 8 : 0), !isDxt5, px);
                    if (isDxt5) { dxtAlpha(blk, al); for (int i = 0; i < 16; ++i) px[i][3] = al[i]; }
                    for (int py = 0; py < 4; ++py)
                        for (int pxx = 0; pxx < 4; ++pxx) {
                            size_t x = bx * 4 + pxx, y = by * 4 + py;
                            if (x < (size_t)w && y < (size_t)h) std::memcpy(&rgba[(y * w + x) * 4], px[py * 4 + pxx], 4);
                        }
                }
            return true;
        }
        default: return false;
    }
}

// ===========================================================================
// Object graph conversion
// ===========================================================================
namespace {
enum : int { kGameObject = 1, kTransform = 4, kMeshRenderer = 23, kMeshFilter = 33, kMonoBehaviour = 114, kRectTransform = 224 };

int64_t pptrId(const Value& p) {
    if (p.kind != Value::Struct) return 0;
    if (p["m_FileID"].asInt() != 0) return 0;     // external file (builtin resources) -> unknown
    return p["m_PathID"].asInt();
}

float halfToFloat(uint16_t h) {
    int s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp((float)m, -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp((float)(m + 1024), e - 25);
    return s ? -v : v;
}

size_t fmtSize(int f) { static const int s[] = {4, 2, 1, 1, 2, 2, 1, 1, 2, 2, 4, 4}; return (f >= 0 && f < 12) ? (size_t)s[f] : 0; }

float readComp(const uint8_t* p, int fmt) {
    switch (fmt) {
        case 0: { float f; std::memcpy(&f, p, 4); return f; }
        case 1: return halfToFloat((uint16_t)(p[0] | p[1] << 8));
        case 2: return p[0] / 255.f;
        case 3: return std::max(-1.f, (int8_t)p[0] / 127.f);
        case 4: return (uint16_t)(p[0] | p[1] << 8) / 65535.f;
        case 5: return std::max(-1.f, (int16_t)(p[0] | p[1] << 8) / 32767.f);
        case 6: return p[0];
        case 7: return (int8_t)p[0];
        case 8: return (uint16_t)(p[0] | p[1] << 8);
        case 9: return (int16_t)(p[0] | p[1] << 8);
        case 10: { uint32_t u; std::memcpy(&u, p, 4); return (float)u; }
        case 11: { int32_t u; std::memcpy(&u, p, 4); return (float)u; }
    }
    return 0;
}

struct Ctx {
    const Bundle& b; const SerializedFile& sf; Saber& out;
    std::map<int64_t, int> meshMap, texMap, matMap;
    std::set<int64_t> visiting;
    Ctx(const Bundle& bb, const SerializedFile& s, Saber& o) : b(bb), sf(s), out(o) {}

    void warn(const std::string& m) { out.warnings.push_back(m); }
    int classOf(int64_t id) const { auto* o = sf.find(id); return o ? o->classId : -1; }
    bool read(int64_t id, Value& v, const std::set<std::string>* only = nullptr) {
        auto* o = sf.find(id);
        if (!o) return false;
        v = readObject(sf, *o, only);
        return true;
    }

    // ---- mesh ----
    int mesh(int64_t id) {
        if (!id) return -1;
        auto it = meshMap.find(id);
        if (it != meshMap.end()) return it->second;
        int idx = -1;
        try { idx = buildMesh(id); } catch (const ParseError& e) { warn(std::string("mesh: ") + e.what()); }
        meshMap[id] = idx;
        return idx;
    }
    int buildMesh(int64_t id) {
        Value m;
        if (!read(id, m)) return -1;
        Mesh mesh; mesh.name = m["m_Name"].s;
        // The channel format enum (float/half/unorm8/...) is the same from Unity 2017 on. Older CustomSabers bundles are
        // very commonly built with 2018.1.6f1, so those must work. Before 2017 the enum differs -> not supported.
        if (sf.unityMajor < 2017) { warn("mesh '" + mesh.name + "': Unity " + sf.unityVersion + " vertex layout not supported (need 2017+)"); return -1; }
        if (m["m_MeshCompression"].asInt() != 0) { warn("mesh '" + mesh.name + "': uses mesh compression (re-export with compression Off)"); return -1; }
        const Value& vd = m["m_VertexData"];
        const size_t count = (size_t)vd["m_VertexCount"].asInt();
        const auto& data = vd["m_DataSize"].bytes;
        if (count == 0 || data.empty()) { warn("mesh '" + mesh.name + "': no vertex data"); return -1; }

        struct Ch { int stream, offset, fmt, dim; };
        std::vector<Ch> ch;
        for (auto& c : vd["m_Channels"].arr)
            ch.push_back({(int)c["stream"].asInt(), (int)c["offset"].asInt(), (int)c["format"].asInt(), (int)(c["dimension"].asInt() & 15)});
        size_t stride[8] = {0}, sOff[8] = {0};
        for (auto& c : ch) {
            if (c.dim == 0) continue;
            if (c.stream < 0 || c.stream > 7 || !fmtSize(c.fmt)) throw ParseError("bad vertex channel");
            stride[c.stream] += (size_t)c.dim * fmtSize(c.fmt);
        }
        size_t run = 0;
        for (int s = 0; s < 8; ++s) { sOff[s] = run; run += stride[s] * count; run = (run + 15) & ~(size_t)15; }
        for (auto& c : ch)
            if (c.dim && (sOff[c.stream] + stride[c.stream] * count > data.size() || (size_t)c.offset + c.dim * fmtSize(c.fmt) > stride[c.stream]))
                throw ParseError("vertex data too small");

        auto readCh = [&](size_t chIdx, int comps, std::vector<float>& dst) {
            if (chIdx >= ch.size() || ch[chIdx].dim == 0) return;
            const Ch& c = ch[chIdx];
            const size_t fs = fmtSize(c.fmt);
            dst.assign(count * comps, 0.f);
            for (size_t v = 0; v < count; ++v) {
                const uint8_t* p = data.data() + sOff[c.stream] + v * stride[c.stream] + c.offset;
                for (int k = 0; k < comps; ++k)
                    dst[v * comps + k] = k < c.dim ? readComp(p + k * fs, c.fmt) : (k == 3 && comps == 4 ? 1.f : 0.f);
            }
        };
        readCh(0, 3, mesh.pos); readCh(1, 3, mesh.normals);
        if (sf.unityMajor < 2018) { readCh(2, 4, mesh.colors); readCh(3, 2, mesh.uv0); readCh(4, 2, mesh.uv1); readCh(5, 4, mesh.tangents); }
        else                      { readCh(2, 4, mesh.tangents); readCh(3, 4, mesh.colors); readCh(4, 2, mesh.uv0); readCh(5, 2, mesh.uv1); }
        if (mesh.pos.empty()) throw ParseError("mesh has no positions");

        const auto& ib = m["m_IndexBuffer"].bytes;
        const bool wide = m.has("m_IndexFormat") ? m["m_IndexFormat"].asInt() == 1 : m["m_Use16BitIndices"].asInt() == 0;
        const size_t isz = wide ? 4 : 2;
        for (auto& sm : m["m_SubMeshes"].arr) {
            const int topo = (int)sm["topology"].asInt();
            const size_t first = (size_t)sm["firstByte"].asInt(), n = (size_t)sm["indexCount"].asInt();
            const int64_t baseV = sm["baseVertex"].asInt();
            std::vector<int> tri;
            if (first > ib.size() || n > (ib.size() - first) / isz) throw ParseError("index buffer too small");
            std::vector<int> raw(n);
            for (size_t i = 0; i < n; ++i) {
                const uint8_t* p = ib.data() + first + i * isz;
                int64_t v = wide ? (int64_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24) : (int64_t)(p[0] | p[1] << 8);
                v += baseV;
                if (v < 0 || (size_t)v >= count) throw ParseError("index out of range");
                raw[i] = (int)v;
            }
            if (topo == 0) tri = raw;
            else if (topo == 2) { for (size_t i = 0; i + 3 < n; i += 4) { tri.insert(tri.end(), {raw[i], raw[i+1], raw[i+2], raw[i], raw[i+2], raw[i+3]}); } }
            else warn("mesh '" + mesh.name + "': skipped a non-triangle sub mesh");
            mesh.submeshes.push_back(std::move(tri));
        }
        if (mesh.submeshes.empty()) throw ParseError("mesh has no sub meshes");
        out.meshes.push_back(std::move(mesh));
        return (int)out.meshes.size() - 1;
    }

    // ---- texture ----
    int texture(int64_t id) {
        if (!id) return -1;
        auto it = texMap.find(id);
        if (it != texMap.end()) return it->second;
        int idx = -1;
        try { idx = buildTexture(id); } catch (const ParseError& e) { warn(std::string("texture: ") + e.what()); }
        texMap[id] = idx;
        return idx;
    }
    int buildTexture(int64_t id) {
        Value t;
        if (!read(id, t)) return -1;
        Texture tex; tex.name = t["m_Name"].s;
        tex.width = (int)t["m_Width"].asInt(); tex.height = (int)t["m_Height"].asInt();
        const int fmt = (int)t["m_TextureFormat"].asInt();
        const uint8_t* src = nullptr; size_t srcSize = 0;
        const auto& inl = t["image data"].bytes;
        if (!inl.empty()) { src = inl.data(); srcSize = inl.size(); }
        else {
            const Value& sd = t["m_StreamData"];
            const size_t off = (size_t)sd["offset"].asInt(), size = (size_t)sd["size"].asInt();
            if (size) {
                const std::string& path = sd["path"].s;
                auto slash = path.find_last_of('/');
                const BundleFile* f = b.find(slash == std::string::npos ? path : path.substr(slash + 1));
                if (!f) throw ParseError("texture '" + tex.name + "': stream file '" + path + "' missing from bundle");
                if (off > f->data.size() || size > f->data.size() - off) throw ParseError("texture '" + tex.name + "': stream out of range");
                src = f->data.data() + off; srcSize = size;
            }
        }
        if (!src || !decodeTexture(fmt, tex.width, tex.height, src, srcSize, tex.rgba)) {
            warn("texture '" + tex.name + "': unsupported format " + std::to_string(fmt) + " (supported: RGBA32/RGB24/DXT1/DXT5/...); using white");
            tex.width = tex.height = 4; tex.rgba.assign(4 * 4 * 4, 255);
        }
        out.textures.push_back(std::move(tex));
        return (int)out.textures.size() - 1;
    }

    // ---- material ----
    std::string shaderName(int64_t id) {
        if (!id) return std::string();
        try {
            static const std::set<std::string> only = {"m_Name", "m_ParsedForm"};
            Value s;
            if (!read(id, s, &only)) return std::string();
            const Value& pf = s["m_ParsedForm"];
            return !pf["m_Name"].s.empty() ? pf["m_Name"].s : s["m_Name"].s;
        } catch (const ParseError& e) { warn(std::string("shader: ") + e.what()); return std::string(); }
    }
    static std::string propName(const Value& v) { return v.kind == Value::Str ? v.s : v["name"].s; }
    int material(int64_t id) {
        if (!id) return -1;
        auto it = matMap.find(id);
        if (it != matMap.end()) return it->second;
        int idx = -1;
        try { idx = buildMaterial(id); } catch (const ParseError& e) { warn(std::string("material: ") + e.what()); }
        matMap[id] = idx;
        return idx;
    }
    int buildMaterial(int64_t id) {
        Value v;
        if (!read(id, v)) return -1;
        Material m; m.name = v["m_Name"].s; m.keywords = v["m_ShaderKeywords"].s;
        m.renderQueue = (int)v["m_CustomRenderQueue"].asInt(-1);
        m.shader = shaderName(pptrId(v["m_Shader"]));
        const Value& sp = v["m_SavedProperties"];
        for (auto& e : sp["m_TexEnvs"].arr) {
            const Value& second = e["second"];
            TexEnv te; te.prop = propName(e["first"]);
            te.tex = texture(pptrId(second["m_Texture"]));
            te.sx = (float)second["m_Scale"]["x"].asFloat(1); te.sy = (float)second["m_Scale"]["y"].asFloat(1);
            te.ox = (float)second["m_Offset"]["x"].asFloat(0); te.oy = (float)second["m_Offset"]["y"].asFloat(0);
            if (!te.prop.empty()) m.texs.push_back(te);
        }
        for (auto& e : sp["m_Floats"].arr) { std::string n = propName(e["first"]); if (!n.empty()) m.floats.emplace_back(n, (float)e["second"].asFloat()); }
        for (auto& e : sp["m_Colors"].arr) {
            std::string n = propName(e["first"]); const Value& c = e["second"];
            if (!n.empty()) m.colors.emplace_back(n, std::array<float, 4>{(float)c["r"].asFloat(), (float)c["g"].asFloat(), (float)c["b"].asFloat(), (float)c["a"].asFloat(1)});
        }
        out.materials.push_back(std::move(m));
        return (int)out.materials.size() - 1;
    }

    // ---- hierarchy ----
    int node(int64_t transformId, int depth) {
        if (depth > 64 || !visiting.insert(transformId).second) throw ParseError("hierarchy too deep or cyclic");
        Value tr;
        if (!read(transformId, tr)) throw ParseError("missing Transform");
        static const std::set<std::string> goOnly = {"m_Component", "m_Name", "m_IsActive"};
        Value go;
        if (!read(pptrId(tr["m_GameObject"]), go, &goOnly)) throw ParseError("Transform without GameObject");

        const int me = (int)out.nodes.size();
        out.nodes.emplace_back();
        {
            Node& n = out.nodes[me];
            n.name = go["m_Name"].s; n.active = go["m_IsActive"].asInt(1) != 0;
            const Value& r = tr["m_LocalRotation"]; const Value& p = tr["m_LocalPosition"]; const Value& s = tr["m_LocalScale"];
            n.rot[0] = (float)r["x"].asFloat(); n.rot[1] = (float)r["y"].asFloat(); n.rot[2] = (float)r["z"].asFloat(); n.rot[3] = (float)r["w"].asFloat(1);
            n.pos[0] = (float)p["x"].asFloat(); n.pos[1] = (float)p["y"].asFloat(); n.pos[2] = (float)p["z"].asFloat();
            n.scale[0] = (float)s["x"].asFloat(1); n.scale[1] = (float)s["y"].asFloat(1); n.scale[2] = (float)s["z"].asFloat(1);
        }
        int meshIdx = -1, trailMat = -1, trailLen = 0; std::vector<int> mats; bool rEnabled = true;
        for (auto& cp : go["m_Component"].arr) {
            int64_t cid = pptrId(cp.has("component") ? cp["component"] : cp["second"]);
            switch (classOf(cid)) {
                case kMeshFilter: { Value mf; if (read(cid, mf)) meshIdx = mesh(pptrId(mf["m_Mesh"])); break; }
                case kMeshRenderer: {
                    Value mr; if (!read(cid, mr)) break;
                    rEnabled = mr["m_Enabled"].asInt(1) != 0;
                    for (auto& mp : mr["m_Materials"].arr) mats.push_back(material(pptrId(mp)));
                    break;
                }
                case kMonoBehaviour: {   // CustomSabers' CustomTrail: only the material is used
                    static const std::set<std::string> only = {"TrailMaterial", "Length"};
                    try {
                        Value mb; if (!read(cid, mb, &only)) break;
                        if (mb.has("TrailMaterial")) {
                            int m = material(pptrId(mb["TrailMaterial"]));
                            if (m >= 0 && trailMat < 0) { trailMat = m; trailLen = (int)mb["Length"].asInt(); }
                        }
                    } catch (const ParseError&) {}
                    break;
                }
            }
        }
        std::vector<int> kids;
        for (auto& c : tr["m_Children"].arr) {
            int64_t cid = pptrId(c);
            int cc = classOf(cid);
            if (cc == kTransform || cc == kRectTransform) kids.push_back(node(cid, depth + 1));
        }
        Node& n = out.nodes[me];
        n.mesh = meshIdx; n.mats = std::move(mats); n.rendererEnabled = rEnabled;
        n.trailMat = trailMat; n.trailLength = trailLen; n.children = std::move(kids);
        visiting.erase(transformId);
        return me;
    }
};

int findChild(const Saber& s, int parent, const char* name) {
    if (parent < 0) return -1;
    for (int c : s.nodes[parent].children) if (s.nodes[c].name == name) return c;
    return -1;
}
int findTrail(const Saber& s, int n, int depth = 0) {
    if (n < 0 || depth > 64) return -1;
    if (s.nodes[n].trailMat >= 0) return s.nodes[n].trailMat;
    for (int c : s.nodes[n].children) { int t = findTrail(s, c, depth + 1); if (t >= 0) return t; }
    return -1;
}
}  // namespace

int Saber::trailMatFor(int hand) const { return findTrail(*this, hand == 0 ? left : right); }

bool convertBundle(const std::vector<uint8_t>& bytes, Saber& out, std::string& err) {
    try {
        Bundle b = parseBundle(bytes);
        out.unityVersion = b.unityVersion;
        const BundleFile* sfFile = nullptr;
        for (auto& f : b.files) if (f.flags & 4) { sfFile = &f; break; }
        if (!sfFile)
            for (auto& f : b.files) {
                auto& p = f.path;
                auto ends = [&](const char* e) { size_t n = std::strlen(e); return p.size() >= n && p.compare(p.size() - n, n, e) == 0; };
                if (!ends(".resS") && !ends(".resource")) { sfFile = &f; break; }
            }
        if (!sfFile) { err = "bundle contains no asset file"; return false; }
        SerializedFile sf = parseSerialized(sfFile->data.data(), sfFile->data.size());
        out.sourcePlatform = sf.targetPlatform;
        if (!sf.enableTypeTree) { err = "bundle was built without type trees (DisableWriteTypeTree); cannot read it"; return false; }

        Ctx ctx(b, sf, out);
        int64_t rootTransform = 0;
        static const std::set<std::string> nameOnly = {"m_Name", "m_Component"};
        for (auto& o : sf.objects) {
            if (o.classId != kGameObject) continue;
            Value go = readObject(sf, o, &nameOnly);
            if (go["m_Name"].s != "_CustomSaber") continue;
            for (auto& cp : go["m_Component"].arr) {
                int64_t cid = pptrId(cp.has("component") ? cp["component"] : cp["second"]);
                int c = ctx.classOf(cid);
                if (c == kTransform || c == kRectTransform) { rootTransform = cid; break; }
            }
            break;
        }
        if (!rootTransform) { err = "no '_CustomSaber' prefab found in bundle (is this a CustomSabers .saber file?)"; return false; }
        out.root = ctx.node(rootTransform, 0);
        out.left = findChild(out, out.root, "LeftSaber");
        out.right = findChild(out, out.root, "RightSaber");
        if (out.left < 0 || out.right < 0) { err = "prefab '_CustomSaber' needs children 'LeftSaber' and 'RightSaber'"; return false; }
        // A saber whose meshes all failed to convert would load "successfully" and be invisible in game. Fail instead so the
        // caller falls back to the vanilla saber, and say why.
        auto hasMesh = [&](int hand) {
            std::vector<int> st{hand};
            while (!st.empty()) {
                int n = st.back(); st.pop_back();
                if (out.nodes[n].mesh >= 0) return true;
                for (int c : out.nodes[n].children) st.push_back(c);
            }
            return false;
        };
        if (!hasMesh(out.left) && !hasMesh(out.right)) {
            err = "no usable meshes under LeftSaber/RightSaber";
            if (!out.warnings.empty()) err += " (" + out.warnings.front() + ")";
            return false;
        }
        return true;
    } catch (const ParseError& e) {
        err = e.what();
        return false;
    }
}

}  // namespace sfq::pc
