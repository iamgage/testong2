#include "unity_bundle.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

namespace sfq::unity {

// ===========================================================================
// Byte reader
// ===========================================================================
namespace {
struct Reader {
    const uint8_t* d; size_t n; size_t p = 0; bool le;
    Reader(const uint8_t* data, size_t size, bool littleEndian) : d(data), n(size), le(littleEndian) {}
    void need(size_t k) const { if (p > n || k > n - p) throw ParseError("unexpected end of data"); }
    uint8_t u8() { need(1); return d[p++]; }
    uint16_t u16() { need(2); uint16_t v = le ? (uint16_t)(d[p] | d[p + 1] << 8) : (uint16_t)(d[p] << 8 | d[p + 1]); p += 2; return v; }
    uint32_t u32() {
        need(4); uint32_t v;
        if (le) v = (uint32_t)d[p] | (uint32_t)d[p + 1] << 8 | (uint32_t)d[p + 2] << 16 | (uint32_t)d[p + 3] << 24;
        else    v = (uint32_t)d[p + 3] | (uint32_t)d[p + 2] << 8 | (uint32_t)d[p + 1] << 16 | (uint32_t)d[p] << 24;
        p += 4; return v;
    }
    uint64_t u64() {
        uint64_t a = u32(), b = u32();
        return le ? (b << 32 | a) : (a << 32 | b);
    }
    int32_t i32() { return (int32_t)u32(); }
    int64_t i64() { return (int64_t)u64(); }
    float f32() { uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }
    double f64() { uint64_t v = u64(); double f; std::memcpy(&f, &v, 8); return f; }
    std::string cstr() {
        size_t s = p;
        while (p < n && d[p]) ++p;
        if (p >= n) throw ParseError("unterminated string");
        std::string r((const char*)d + s, p - s);
        ++p; return r;
    }
    void skip(size_t k) { need(k); p += k; }
    void align(size_t a) { size_t m = p % a; if (m) skip(a - m); }
};
}  // namespace

// ===========================================================================
// LZ4 block decoder
// ===========================================================================
std::vector<uint8_t> lz4Decompress(const uint8_t* s, size_t sn, size_t dn) {
    std::vector<uint8_t> out(dn);
    size_t ip = 0, op = 0;
    while (ip < sn) {
        uint8_t tok = s[ip++];
        size_t lit = tok >> 4;
        if (lit == 15) { uint8_t b; do { if (ip >= sn) throw ParseError("LZ4: truncated"); b = s[ip++]; lit += b; } while (b == 255); }
        if (lit > sn - ip || lit > dn - op) throw ParseError("LZ4: literal run out of range");
        std::memcpy(out.data() + op, s + ip, lit); ip += lit; op += lit;
        if (ip >= sn) break;   // last sequence only has literals
        if (sn - ip < 2) throw ParseError("LZ4: truncated offset");
        size_t off = (size_t)s[ip] | (size_t)s[ip + 1] << 8; ip += 2;
        if (off == 0 || off > op) throw ParseError("LZ4: bad match offset");
        size_t ml = tok & 15;
        if (ml == 15) { uint8_t b; do { if (ip >= sn) throw ParseError("LZ4: truncated"); b = s[ip++]; ml += b; } while (b == 255); }
        ml += 4;
        if (ml > dn - op) throw ParseError("LZ4: match out of range");
        for (size_t k = 0; k < ml; ++k) { out[op] = out[op - off]; ++op; }
    }
    if (op != dn) throw ParseError("LZ4: size mismatch");
    return out;
}

// ===========================================================================
// LZMA1 decoder (follows the public-domain LzmaSpec reference)
// ===========================================================================
namespace {
struct RC {
    const uint8_t* p; const uint8_t* e; uint32_t range = 0xFFFFFFFFu, code = 0;
    RC(const uint8_t* s, const uint8_t* en) : p(s), e(en) {
        if (en - s < 5) throw ParseError("LZMA: truncated");
        if (*p++ != 0) throw ParseError("LZMA: bad range coder header");
        for (int i = 0; i < 4; ++i) code = (code << 8) | *p++;
    }
    inline void norm() { if (range < (1u << 24)) { range <<= 8; code = (code << 8) | (p < e ? *p++ : 0u); } }
    inline uint32_t bit(uint16_t& pr) {
        uint32_t bound = (range >> 11) * pr, b;
        if (code < bound) { range = bound; pr += (2048 - pr) >> 5; b = 0; }
        else { range -= bound; code -= bound; pr -= pr >> 5; b = 1; }
        norm(); return b;
    }
    inline uint32_t direct(int n) {
        uint32_t r = 0;
        while (n--) {
            range >>= 1; code -= range;
            uint32_t t = 0u - (code >> 31);
            code += range & t;
            norm();
            r = (r << 1) + (t + 1);
        }
        return r;
    }
};
inline uint32_t bitTree(RC& rc, uint16_t* pr, int n) { uint32_t m = 1; for (int i = 0; i < n; ++i) m = (m << 1) + rc.bit(pr[m]); return m - (1u << n); }
inline uint32_t bitTreeRev(RC& rc, uint16_t* pr, int n) { uint32_t m = 1, s = 0; for (int i = 0; i < n; ++i) { uint32_t b = rc.bit(pr[m]); m = (m << 1) + b; s |= b << i; } return s; }

struct LenDec {
    uint16_t choice, choice2, low[16][8], mid[16][8], high[256];
    LenDec() {
        choice = choice2 = 1024;
        for (auto& r : low) std::fill(r, r + 8, (uint16_t)1024);
        for (auto& r : mid) std::fill(r, r + 8, (uint16_t)1024);
        std::fill(high, high + 256, (uint16_t)1024);
    }
    uint32_t dec(RC& rc, uint32_t ps) {
        if (!rc.bit(choice))  return bitTree(rc, low[ps], 3);
        if (!rc.bit(choice2)) return 8 + bitTree(rc, mid[ps], 3);
        return 16 + bitTree(rc, high, 8);
    }
};
}  // namespace

std::vector<uint8_t> lzmaDecompress(const uint8_t* src, size_t sn, size_t dn) {
    if (sn < 5) throw ParseError("LZMA: truncated header");
    unsigned d = src[0];
    if (d >= 9 * 5 * 5) throw ParseError("LZMA: bad properties");
    const unsigned lc = d % 9; d /= 9; const unsigned lp = d % 5; const unsigned pb = d / 5;
    std::vector<uint8_t> out(dn);
    RC rc(src + 5, src + sn);

    std::vector<uint16_t> lit((size_t)0x300 << (lc + lp), 1024);
    uint16_t isMatch[12 << 4], isRep[12], isRepG0[12], isRepG1[12], isRepG2[12], isRep0Long[12 << 4];
    uint16_t posSlot[4][64], posDec[115], align[16];
    std::fill(isMatch, isMatch + (12 << 4), (uint16_t)1024); std::fill(isRep0Long, isRep0Long + (12 << 4), (uint16_t)1024);
    std::fill(isRep, isRep + 12, (uint16_t)1024); std::fill(isRepG0, isRepG0 + 12, (uint16_t)1024);
    std::fill(isRepG1, isRepG1 + 12, (uint16_t)1024); std::fill(isRepG2, isRepG2 + 12, (uint16_t)1024);
    for (auto& r : posSlot) std::fill(r, r + 64, (uint16_t)1024);
    std::fill(posDec, posDec + 115, (uint16_t)1024); std::fill(align, align + 16, (uint16_t)1024);
    LenDec lenDec, repLenDec;

    uint32_t state = 0, rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    size_t op = 0;
    const uint32_t pbMask = (1u << pb) - 1, lpMask = (1u << lp) - 1;
    while (op < dn) {
        const uint32_t ps = (uint32_t)op & pbMask;
        if (!rc.bit(isMatch[(state << 4) + ps])) {
            uint8_t prev = op ? out[op - 1] : 0;
            uint16_t* probs = &lit[(size_t)0x300 * ((((uint32_t)op & lpMask) << lc) + (prev >> (8 - lc)))];
            uint32_t sym = 1;
            if (state >= 7) {
                if (rep0 >= op) throw ParseError("LZMA: bad distance");
                uint32_t mb = out[op - rep0 - 1];
                do {
                    uint32_t mbit = (mb >> 7) & 1; mb <<= 1;
                    uint32_t b = rc.bit(probs[((1 + mbit) << 8) + sym]);
                    sym = (sym << 1) | b;
                    if (mbit != b) break;
                } while (sym < 0x100);
            }
            while (sym < 0x100) sym = (sym << 1) | rc.bit(probs[sym]);
            out[op++] = (uint8_t)sym;
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            continue;
        }
        uint32_t len;
        if (rc.bit(isRep[state])) {
            if (op == 0) throw ParseError("LZMA: rep at start");
            if (!rc.bit(isRepG0[state])) {
                if (!rc.bit(isRep0Long[(state << 4) + ps])) {
                    if (rep0 >= op) throw ParseError("LZMA: bad distance");
                    state = state < 7 ? 9 : 11;
                    out[op] = out[op - rep0 - 1]; ++op;
                    continue;
                }
            } else {
                uint32_t dist;
                if (!rc.bit(isRepG1[state])) dist = rep1;
                else {
                    if (!rc.bit(isRepG2[state])) dist = rep2;
                    else { dist = rep3; rep3 = rep2; }
                    rep2 = rep1;
                }
                rep1 = rep0; rep0 = dist;
            }
            len = repLenDec.dec(rc, ps);
            state = state < 7 ? 8 : 11;
        } else {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = lenDec.dec(rc, ps);
            state = state < 7 ? 7 : 10;
            uint32_t lenState = len > 3 ? 3 : len;
            uint32_t slot = bitTree(rc, posSlot[lenState], 6);
            if (slot < 4) rep0 = slot;
            else {
                uint32_t nd = (slot >> 1) - 1;
                uint32_t dist = (2 | (slot & 1)) << nd;
                if (slot < 14) dist += bitTreeRev(rc, posDec + dist - slot, (int)nd);
                else {
                    dist += rc.direct((int)nd - 4) << 4;
                    dist += bitTreeRev(rc, align, 4);
                }
                rep0 = dist;
                if (rep0 == 0xFFFFFFFFu) break;   // end marker
            }
        }
        len += 2;
        if (rep0 >= op) throw ParseError("LZMA: bad match distance");
        size_t n = std::min<size_t>(len, dn - op);
        for (size_t k = 0; k < n; ++k) { out[op] = out[op - rep0 - 1]; ++op; }
    }
    if (op != dn) throw ParseError("LZMA: stream ended early");
    return out;
}

// ===========================================================================
// UnityFS
// ===========================================================================
const BundleFile* Bundle::find(const std::string& baseName) const {
    for (auto& f : files) {
        auto slash = f.path.find_last_of('/');
        if ((slash == std::string::npos ? f.path : f.path.substr(slash + 1)) == baseName) return &f;
    }
    return nullptr;
}

static std::vector<uint8_t> decompressBlock(unsigned mode, const uint8_t* s, size_t sn, size_t dn) {
    switch (mode) {
        case 0: if (sn < dn) throw ParseError("stored block too small"); return std::vector<uint8_t>(s, s + dn);
        case 1: return lzmaDecompress(s, sn, dn);
        case 2: case 3: return lz4Decompress(s, sn, dn);
        default: throw ParseError("unknown compression mode " + std::to_string(mode));
    }
}

Bundle parseBundle(const std::vector<uint8_t>& bytes) {
    Reader r(bytes.data(), bytes.size(), false);
    std::string sig;
    try { sig = r.cstr(); } catch (...) { throw ParseError("not a Unity bundle"); }
    if (sig != "UnityFS") throw ParseError("not a UnityFS bundle (signature '" + sig.substr(0, 16) + "'); only modern .saber files are supported");
    uint32_t version = r.u32();
    Bundle b;
    r.cstr();                    // player version (5.x.x)
    b.unityVersion = r.cstr();   // engine version, e.g. 2019.4.28f1
    r.i64();                     // total size
    uint32_t cSize = r.u32(), uSize = r.u32(), flags = r.u32();
    if (version >= 7) r.align(16); else if (flags & 0x200) r.align(16);

    size_t infoStart, dataStart;
    if (flags & 0x80) {          // block info stored at the end of the file
        if (cSize > bytes.size()) throw ParseError("bad block info size");
        infoStart = bytes.size() - cSize; dataStart = r.p;
    } else { infoStart = r.p; dataStart = r.p + cSize; }
    if (infoStart > bytes.size() || cSize > bytes.size() - infoStart) throw ParseError("block info out of range");
    auto info = decompressBlock(flags & 0x3F, bytes.data() + infoStart, cSize, uSize);

    Reader ir(info.data(), info.size(), false);
    ir.skip(16);   // hash
    uint32_t blockCount = ir.u32();
    if (blockCount > 100000) throw ParseError("absurd block count");
    struct Blk { uint32_t u, c; uint16_t fl; };
    std::vector<Blk> blocks;
    for (uint32_t i = 0; i < blockCount; ++i) { Blk k; k.u = ir.u32(); k.c = ir.u32(); k.fl = ir.u16(); blocks.push_back(k); }
    uint32_t nodeCount = ir.u32();
    if (nodeCount > 100000) throw ParseError("absurd node count");
    struct Nd { int64_t off, size; uint32_t fl; std::string path; };
    std::vector<Nd> nodes;
    for (uint32_t i = 0; i < nodeCount; ++i) { Nd n; n.off = ir.i64(); n.size = ir.i64(); n.fl = ir.u32(); n.path = ir.cstr(); nodes.push_back(std::move(n)); }

    std::vector<uint8_t> all;
    size_t pos = dataStart;
    for (auto& k : blocks) {
        if (pos > bytes.size() || k.c > bytes.size() - pos) throw ParseError("data block out of range");
        auto chunk = decompressBlock(k.fl & 0x3F, bytes.data() + pos, k.c, k.u);
        all.insert(all.end(), chunk.begin(), chunk.end());
        pos += k.c;
    }
    for (auto& n : nodes) {
        if (n.off < 0 || n.size < 0 || (uint64_t)n.off > all.size() || (uint64_t)n.size > all.size() - (uint64_t)n.off)
            throw ParseError("directory entry out of range");
        BundleFile f; f.path = n.path; f.flags = n.fl;
        f.data.assign(all.begin() + n.off, all.begin() + n.off + n.size);
        b.files.push_back(std::move(f));
    }
    return b;
}

// ===========================================================================
// Value helpers
// ===========================================================================
const Value& Value::operator[](const char* name) const {
    static const Value nul;
    for (auto& f : fields) if (f.first == name) return f.second;
    return nul;
}
bool Value::has(const char* name) const {
    for (auto& f : fields) if (f.first == name) return true;
    return false;
}

// ===========================================================================
// SerializedFile
// ===========================================================================
static const char kCommonStrings[] =
    "AABB\0AnimationClip\0AnimationCurve\0AnimationState\0Array\0Base\0BitField\0bitset\0bool\0char\0ColorRGBA\0Component\0"
    "data\0deque\0double\0dynamic_array\0FastPropertyName\0first\0float\0Font\0GameObject\0Generic Mono\0GradientNEW\0GUID\0"
    "GUIStyle\0int\0list\0long long\0map\0Matrix4x4f\0MdFour\0MonoBehaviour\0MonoScript\0m_ByteSize\0m_Curve\0"
    "m_EditorClassIdentifier\0m_EditorHideFlags\0m_Enabled\0m_ExtensionPtr\0m_GameObject\0m_Index\0m_IsArray\0m_IsStatic\0"
    "m_MetaFlag\0m_Name\0m_ObjectHideFlags\0m_PrefabInternal\0m_PrefabParentObject\0m_Script\0m_StaticEditorFlags\0m_Type\0"
    "m_Version\0Object\0pair\0PPtr<Component>\0PPtr<GameObject>\0PPtr<Material>\0PPtr<MonoBehaviour>\0PPtr<MonoScript>\0"
    "PPtr<Object>\0PPtr<Prefab>\0PPtr<Sprite>\0PPtr<TextAsset>\0PPtr<Texture>\0PPtr<Texture2D>\0PPtr<Transform>\0Prefab\0"
    "Quaternionf\0Rectf\0RectInt\0RectOffset\0second\0set\0short\0size\0SInt16\0SInt32\0SInt64\0SInt8\0staticvector\0string\0"
    "TextAsset\0TextMesh\0Texture\0Texture2D\0Transform\0TypelessData\0UInt16\0UInt32\0UInt64\0UInt8\0unsigned int\0"
    "unsigned long long\0unsigned short\0vector\0Vector2f\0Vector3f\0Vector4f\0m_ScriptingClassIdentifier\0Gradient\0Type*\0"
    "int2_storage\0int3_storage\0BoundsInt\0m_CorrespondingSourceObject\0m_PrefabInstance\0m_PrefabAsset\0FileSize\0Hash128\0";
const char* commonStringsData() { return kCommonStrings; }
size_t commonStringsSize() { return sizeof(kCommonStrings) - 1; }

static std::string nameAt(uint32_t off, const std::string& local) {
    const char* base; size_t size;
    if (off & 0x80000000u) { off &= 0x7FFFFFFFu; base = kCommonStrings; size = sizeof(kCommonStrings) - 1; }
    else { base = local.data(); size = local.size(); }
    if (off >= size) return std::string();
    size_t e = off; while (e < size && base[e]) ++e;
    return std::string(base + off, e - off);
}

static void readTypeTree(Reader& r, uint32_t version, TypeNode& root) {
    uint32_t count = r.u32(), strSize = r.u32();
    if (count > 200000 || strSize > (64u << 20)) throw ParseError("absurd type tree");
    struct Raw { uint8_t level; bool arr; uint32_t t, n; int32_t size, flag; };
    std::vector<Raw> raw(count);
    for (auto& x : raw) {
        r.u16();                 // node version
        x.level = r.u8(); x.arr = r.u8() != 0;
        x.t = r.u32(); x.n = r.u32(); x.size = r.i32(); r.i32(); x.flag = r.i32();
        if (version >= 19) r.u64();   // ref type hash
    }
    r.need(strSize);
    std::string local((const char*)r.d + r.p, strSize);
    r.skip(strSize);
    if (raw.empty()) return;

    // Rebuild the tree from the flat (level, node) list.
    std::vector<TypeNode*> stack;
    auto fill = [&](TypeNode& t, const Raw& x) {
        t.type = nameAt(x.t, local); t.name = nameAt(x.n, local);
        t.isArray = x.arr; t.byteSize = x.size; t.metaFlag = x.flag;
    };
    fill(root, raw[0]);
    stack.push_back(&root);
    for (size_t i = 1; i < raw.size(); ++i) {
        size_t level = raw[i].level;
        if (level == 0 || level > stack.size()) throw ParseError("bad type tree levels");
        stack.resize(level);
        TypeNode& parent = *stack.back();
        parent.children.emplace_back();
        fill(parent.children.back(), raw[i]);
        stack.push_back(&parent.children.back());
    }
}

static SerType readType(Reader& r, uint32_t version, bool enableTree, bool isRef) {
    SerType t;
    t.classId = r.i32();
    if (version >= 16) r.u8();               // is stripped
    int16_t scriptIdx = -1;
    if (version >= 17) scriptIdx = (int16_t)r.u16();
    if (version >= 13) {
        if ((isRef && scriptIdx >= 0) || (version < 16 && t.classId < 0) || (version >= 16 && t.classId == 114)) r.skip(16);
        r.skip(16);                          // type hash
    }
    if (enableTree) {
        readTypeTree(r, version, t.root);
        t.hasTree = !t.root.type.empty();
        if (version >= 21) {
            if (isRef) { r.cstr(); r.cstr(); r.cstr(); }
            else { uint32_t n = r.u32(); if (n > 100000) throw ParseError("absurd dependency list"); r.skip((size_t)n * 4); }
        }
    }
    return t;
}

SerializedFile parseSerialized(const uint8_t* data, size_t size) {
    SerializedFile f; f.data = data; f.size = size;
    Reader r(data, size, false);
    r.u32();                         // metadata size
    r.u32();                         // file size
    f.version = r.u32();
    uint64_t dataOffset = r.u32();
    if (f.version < 17 || f.version > 30) throw ParseError("unsupported SerializedFile version " + std::to_string(f.version) + " (saber built with a very old Unity?)");
    bool little = false;
    if (f.version >= 9) { little = r.u8() == 0; r.skip(3); }
    if (f.version >= 22) { r.u32(); r.i64(); dataOffset = (uint64_t)r.i64(); r.i64(); }
    r.le = little;                   // from here on the metadata uses the file's endianness (little for PC/Android)
    if (!little) throw ParseError("big-endian bundles are not supported");

    f.unityVersion = r.cstr();
    f.unityMajor = std::atoi(f.unityVersion.c_str());
    f.targetPlatform = r.i32();
    f.enableTypeTree = r.u8() != 0;
    uint32_t typeCount = r.u32();
    if (typeCount > 100000) throw ParseError("absurd type count");
    for (uint32_t i = 0; i < typeCount; ++i) f.types.push_back(readType(r, f.version, f.enableTypeTree, false));
    uint32_t objCount = r.u32();
    if (objCount > 10000000) throw ParseError("absurd object count");
    f.objects.reserve(objCount);
    for (uint32_t i = 0; i < objCount; ++i) {
        ObjectInfo o;
        r.align(4);
        o.pathId = r.i64();
        o.byteStart = (f.version >= 22 ? (uint64_t)r.i64() : (uint64_t)r.u32()) + dataOffset;
        o.byteSize = r.u32();
        o.typeIndex = r.i32();
        if (o.typeIndex < 0 || (size_t)o.typeIndex >= f.types.size()) throw ParseError("bad object type index");
        o.classId = f.types[o.typeIndex].classId;
        f.objects.push_back(o);
    }
    return f;
}

const ObjectInfo* SerializedFile::find(int64_t pathId) const {
    // Objects are usually sorted by id in the file, but don't rely on it.
    auto it = std::lower_bound(objects.begin(), objects.end(), pathId, [](const ObjectInfo& o, int64_t id) { return o.pathId < id; });
    if (it != objects.end() && it->pathId == pathId) return &*it;
    for (auto& o : objects) if (o.pathId == pathId) return &o;
    return nullptr;
}

// ===========================================================================
// Type-tree driven object reader
// ===========================================================================
namespace {
bool oneByte(const TypeNode& n) {
    const std::string& t = n.type;
    return t == "UInt8" || t == "SInt8" || t == "char";
}

Value readValue(const TypeNode& n, Reader& r, bool keep, int depth, const std::set<std::string>* only = nullptr) {
    if (depth > 64) throw ParseError("type tree too deep");
    Value v;
    const std::string& t = n.type;
    bool align = (n.metaFlag & 0x4000) != 0;
    auto I = [&](int64_t x) { if (keep) { v.kind = Value::Int; v.i = x; } };
    auto F = [&](double x) { if (keep) { v.kind = Value::Float; v.f = x; } };

    if (t == "SInt8") I((int8_t)r.u8());
    else if (t == "UInt8" || t == "char") I(r.u8());
    else if (t == "short" || t == "SInt16") I((int16_t)r.u16());
    else if (t == "UInt16" || t == "unsigned short") I(r.u16());
    else if (t == "int" || t == "SInt32") I((int32_t)r.u32());
    else if (t == "UInt32" || t == "unsigned int" || t == "Type*") I(r.u32());
    else if (t == "long long" || t == "SInt64") I(r.i64());
    else if (t == "UInt64" || t == "unsigned long long" || t == "FileSize") I((int64_t)r.u64());
    else if (t == "float") F(r.f32());
    else if (t == "double") F(r.f64());
    else if (t == "bool") I(r.u8() != 0);
    else if (t == "string") {
        uint32_t len = r.u32();
        r.need(len);
        if (keep) { v.kind = Value::Str; v.s.assign((const char*)r.d + r.p, len); }
        r.p += len;
        align = n.children.empty() ? true : (n.children[0].metaFlag & 0x4000) != 0;
    } else if (t == "TypelessData") {
        uint32_t len = r.u32();
        r.need(len);
        if (keep) { v.kind = Value::Bytes; v.bytes.assign(r.d + r.p, r.d + r.p + len); }
        r.p += len;
    } else if (n.children.size() == 1 && n.children[0].isArray && n.children[0].children.size() == 2) {
        const TypeNode& arr = n.children[0];
        const TypeNode& elem = arr.children[1];
        align = (arr.metaFlag & 0x4000) != 0;
        uint32_t cnt = r.u32();
        if (cnt > r.n - std::min(r.p, r.n)) throw ParseError("array larger than data");
        if (oneByte(elem)) {
            r.need(cnt);
            if (keep) { v.kind = Value::Bytes; v.bytes.assign(r.d + r.p, r.d + r.p + cnt); }
            r.p += cnt;
        } else {
            if (keep) { v.kind = Value::Array; v.arr.reserve(cnt); }
            for (uint32_t i = 0; i < cnt; ++i) {
                Value e = readValue(elem, r, keep, depth + 1);
                if (keep) v.arr.push_back(std::move(e));
            }
        }
    } else {
        if (keep) v.kind = Value::Struct;
        for (auto& c : n.children) {
            bool k = keep && (!only || only->count(c.name));
            Value cv = readValue(c, r, k, depth + 1);
            if (k) v.fields.emplace_back(c.name, std::move(cv));
        }
    }
    if (align) r.align(4);
    return v;
}
}  // namespace

Value readObject(const SerializedFile& f, const ObjectInfo& o, const std::set<std::string>* only) {
    const SerType& st = f.types[o.typeIndex];
    if (!st.hasTree) throw ParseError("bundle was built without type trees; cannot read objects");
    if (o.byteStart > f.size || o.byteSize > f.size - o.byteStart) throw ParseError("object out of range");
    Reader r(f.data + o.byteStart, o.byteSize, true);
    return readValue(st.root, r, true, 0, only);
}

}  // namespace sfq::unity
