#include "saber_bundle.hpp"
#include <fstream>
#include <array>
#include <cstring>
namespace sfq {
BundleInfo inspectBundle(const std::string& path) {
    BundleInfo r;
    std::ifstream f(path, std::ios::binary);
    if (!f) { r.error="File cannot be opened"; return r; }
    std::array<char, 256> bytes{};
    f.read(bytes.data(), bytes.size());
    auto count=static_cast<size_t>(f.gcount());
    if (count<32 || std::memcmp(bytes.data(),"UnityFS\0",8)!=0) {
        r.error="Not a UnityFS bundle"; return r;
    }
    r.unityFS=true;
    // UnityFS: signature\0, uint32 format, playerVersion\0, engineVersion\0.
    size_t pos=12;
    auto next=[&](std::string& out)->bool {
        if (pos>=count) return false;
        size_t start=pos;
        while(pos<count && bytes[pos]!='\0') ++pos;
        if (pos==count || pos-start>80) return false;
        out.assign(bytes.data()+start,pos-start);
        ++pos; return true;
    };
    std::string player;
    if (!next(player) || !next(r.unityVersion)) r.error="Malformed UnityFS version header";
    return r;
}
}
