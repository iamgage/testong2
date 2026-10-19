# SaberFactory Quest (Beat Saber 1.40.8) — real hook implementation, UNTESTED

This replaces the earlier scaffold with actual game code: hooks, AssetBundle loading, saber
attachment and a ribbon trail. **It has not been compiled against the Quest SDK or run on a headset**
(the environment it was written in has no NDK / network). Expect a short fix-up pass on first build.

## What it does
| Piece | File |
|---|---|
| Hook `SaberModelController::Init` → swap vanilla saber for custom one | `src/main.cpp` |
| Hook `Saber::ManualUpdate` → feed trail | `src/main.cpp` |
| Load Android AssetBundle, instantiate `_CustomSaber`, attach `LeftSaber`/`RightSaber`, hide vanilla mesh + trail | `src/saber_loader.cpp` |
| Trail ribbon mesh (MeshFilter+MeshRenderer, vertex-alpha fade) using `_TrailMaterial` from the bundle | `src/saber_loader.cpp` + `shared/` |
| Config / saber discovery / ribbon math (**host-tested, passes under ASan+UBSan**) | `shared/`, `tests/` |
| Failure handling: any exception → vanilla saber stays | `src/main.cpp` |

## Easiest: build the .qmod on GitHub (no local tooling)
1. Create a free GitHub repo and upload everything in this folder (keep the `.github` folder).
2. Open the **Actions** tab → *Build SaberFactory Quest qmod* → wait for it to finish (or press *Run workflow*).
3. Success: download the **SaberFactoryQuest-qmod** artifact (the `.qmod`). Failure: download **build-logs** and send me `restore.log`/`build.log`.
4. Before installing, set the real game build in `mod.template.json` (`packageVersion`, e.g. `1.40.8_xxxx` — MBF shows it).

## UI
Settings → **SaberFactory** page (BSML): custom-saber toggle, saber dropdown, trail on/off, width and duration sliders. Changes are saved to `config.cfg` and apply when the next song starts.

## Build locally (alternative)
1. Install qpm, Android NDK (r27), then create a project from the official template:
   `qpm template` / https://github.com/QuestPackageManager/Quest-Mod-Template — or use this folder directly.
2. Add dependencies (let qpm pick versions that match 1.40.8):
   `qpm dependency add beatsaber-hook`, `qpm dependency add bs-cordl` (the 1.40.x one, e.g. `4008.*`), `qpm dependency add scotland2`
3. `qpm restore` then `qpm s build` (or `cmake` with the NDK toolchain) → `libsaberfactory-quest.so`
4. Copy `mod.template.json` → `mod.json`, set the exact `packageVersion` your Quest reports, then `qpm qmod build`.
5. Install the `.qmod` via MBF / ModsBeforeFriends.

Portable tests on PC: `tools/host_test.sh`.

## Using it
Put Android-built bundles in `/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberFactoryQuest/Sabers/` named `<name>.saber`.
Edit `config.cfg` in the parent folder (created on first launch): `selected=<name>`, `trailEnabled`, `trailWidth` (0–1), `trailDuration` (s).
Or use the in-game Settings → SaberFactory page.

## Bundle requirements (this is why PC `.saber` files won't work as-is)
* Built in Unity for **Android (ASTC)**, matching Beat Saber's Unity version, **Single Pass Instanced** shaders.
* **No custom C# scripts** in the bundle (they aren't in the game's IL2CPP build). SaberFactory/CustomSabers' `CustomTrail` and descriptor components are ignored.
* Prefab named `_CustomSaber` with children `LeftSaber` and `RightSaber` (blade along local +Z, origin at the hilt).
* Optional `Material` asset named `_TrailMaterial`; its shader must multiply by **vertex color alpha** and be double-sided/transparent. No material → no custom trail (vanilla trail is kept off for that saber).

## Things most likely to need touching on first compile (the honest list)
1. `SaberModelController::Init` parameters — toggle `SFQ_INIT_HAS_COLOR` in `src/main.cpp`.
2. `INSTALL_HOOK(getLogger(), …)` — beatsaber-hook's logger argument differs between 5.x/6.x; adjust to your version.
3. `SafePtrUnity` / `ArrayW` / `csTypeOf` includes and the generated names for `UnityEngine::Mesh::set_vertices/set_colors/set_triangles`.
4. `GetFieldValue<…>(c, "_saberTrail")` return type (optional vs. result wrapper).
5. `Saber::get_saberBladeTopPos/BottomPos`, `Saber::ManualUpdate` existence in 1.40.8 — check the generated headers.
6. `modFiles` vs `lateModFiles` in `mod.json` (this mod initialises in `late_load`).

## Not implemented
* A pixel-for-pixel copy of the PC SaberFactory editor (3D preview, per-part editing). The UI is a BSML settings page in a similar blue style.
* Saber colour tinting, glow, PC-format `.saber` conversion, SaberFactory's own editor/preset format.
* Fix-ups discovered only by running on a headset.

Original SaberFactory is GPL-3.0 (https://github.com/Dennoc/SaberFactory); this port is therefore GPL-3.0 as well.

## PC .saber compatibility status (October 2026)
The loader now validates UnityFS headers and reports the Unity editor version in logs before loading. **This does not convert Windows bundles**. The UnityFS header does not identify the target platform. `ACC_Saber.saber` uses Unity 2018.1.6f1; it still requires asset extraction, reauthoring and an Android Unity bundle rebuild. Do not install this source update expecting PC sabers to load. `shared/saber_bundle.cpp` is host-testable with `tools/host_test.sh`.
