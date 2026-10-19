#pragma once
#include <string>
namespace sfq {
struct BundleInfo { bool unityFS=false; std::string unityVersion; std::string error; };
// Inspects UnityFS metadata only. UnityFS does not encode the build target in its header.
BundleInfo inspectBundle(const std::string& path);
}
