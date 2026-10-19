# PC saber tools merged into main source

This source tree includes **both** previous updates:

- Runtime UnityFS detection: `shared/saber_bundle.hpp`, `shared/saber_bundle.cpp`, and `src/saber_loader.cpp`.
- Offline PC saber extraction utility: `tools/convert_pc_saber.py` (requires UnityPy; see `PC_SABER_STATUS.md`).

The extractor is a developer-side tool and is **not** called by the Android runtime. Its output is not yet converted to an Android AssetBundle or wired to in-game rendering. Ordinary Windows `.saber` files therefore **do not yet load on Quest**.

This ZIP is a merged **source project**, not a compiled `.qmod`.
