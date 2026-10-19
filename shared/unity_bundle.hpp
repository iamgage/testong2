// Portable (no Unity / no IL2CPP) reader for Unity AssetBundles ("UnityFS") and the
// SerializedFile inside them. Used to read PC (Windows) .saber files directly on the Quest.
// Compiled for both host tests and the Quest mod.
#pragma once
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sfq::unity {

struct ParseError : std::runtime_error { using std::runtime_error::runtime_error; };

// ---- decompressors (throw ParseError on corrupt input) ---------------------
std::vector<uint8_t> lz4Decompress(const uint8_t* src, size_t srcSize, size_t dstSize);
// Unity style: 5 byte props (lc/lp/pb byte + dict size), then the raw LZMA stream (no size field).
std::vector<uint8_t> lzmaDecompress(const uint8_t* src, size_t srcSize, size_t dstSize);

// ---- UnityFS container -----------------------------------------------------
struct BundleFile { std::string path; uint32_t flags = 0; std::vector<uint8_t> data; };
struct Bundle {
    std::string unityVersion;
    std::vector<BundleFile> files;
    const BundleFile* find(const std::string& baseName) const;   // match by file name only
};
Bundle parseBundle(const std::vector<uint8_t>& bytes);   // UnityFS only; throws ParseError

// ---- Generic value tree produced from a type tree ---------------------------
struct Value {
    enum Kind : uint8_t { Null, Int, Float, Str, Bytes, Array, Struct } kind = Null;
    int64_t i = 0;
    double f = 0;
    std::string s;
    std::vector<uint8_t> bytes;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> fields;

    const Value& operator[](const char* name) const;   // struct field; Null value if missing
    bool has(const char* name) const;
    int64_t asInt(int64_t def = 0) const { return kind == Int ? i : kind == Float ? (int64_t)f : def; }
    double asFloat(double def = 0) const { return kind == Float ? f : kind == Int ? (double)i : def; }
};

// ---- SerializedFile --------------------------------------------------------
struct TypeNode {
    std::string type, name;
    bool isArray = false;
    int32_t byteSize = 0;
    int32_t metaFlag = 0;
    std::vector<TypeNode> children;
};
struct SerType { int32_t classId = 0; bool hasTree = false; TypeNode root; };
struct ObjectInfo { int64_t pathId = 0; uint64_t byteStart = 0; uint32_t byteSize = 0; int typeIndex = 0; int32_t classId = 0; };

struct SerializedFile {
    const uint8_t* data = nullptr;   // not owned (points into the Bundle)
    size_t size = 0;
    uint32_t version = 0;
    std::string unityVersion;
    int unityMajor = 0;
    int32_t targetPlatform = 0;      // 13 = Android, 5/19 = Windows, ...
    bool enableTypeTree = false;
    std::vector<SerType> types;
    std::vector<ObjectInfo> objects;
    const ObjectInfo* find(int64_t pathId) const;
};
SerializedFile parseSerialized(const uint8_t* data, size_t size);   // throws ParseError

// Reads one object through its type tree. `only` (optional) = top level fields to keep; the rest
// are skipped without allocating (keeps big Shader / Mesh objects cheap).
Value readObject(const SerializedFile& f, const ObjectInfo& o, const std::set<std::string>* only = nullptr);

}  // namespace sfq::unity
