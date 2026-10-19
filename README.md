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
4. Before installing, the workflow removes `packageVersion` from the built `.qmod`, so the mod manager does not version-lock it.

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

## PC (Windows) `.saber` files — now supported, converted on the headset
Drop an ordinary PC CustomSabers `.saber` into the `Sabers` folder and select it; no Unity, no PC step.
When Unity refuses to load a bundle as an Android bundle (which is what happens with every PC bundle), the mod
falls back to its own reader (`shared/unity_bundle.*`, `shared/pc_saber.*`, `src/pc_saber_unity.*`):

1. Parses the UnityFS container (LZMA / LZ4 / stored blocks) and the type-tree serialized data.
2. Finds the `_CustomSaber` prefab and its `LeftSaber` / `RightSaber` hierarchy (names, transforms, active flags).
3. Decodes meshes (positions, normals, tangents, UV0/UV1, vertex colours, sub meshes) and textures
   (RGBA32 / RGB24 / ARGB32 / BGRA32 / RGB565 / RGBA4444 / Alpha8 / **DXT1 / DXT5**) into RGBA8.
4. Rebuilds real Unity `Mesh`, `Texture2D`, `Material`, `GameObject` objects at runtime, copying every material
   property the new shader has (`_Color`, `_MainTex`, floats, keywords, render queue, texture tiling).
5. Uses the saber's `CustomTrail.TrailMaterial` (if present) for the ribbon trail. The ribbon now has UVs
   (u = 0 newest .. 1 oldest, v across) so textured trail materials work.

The conversion happens the first time the saber spawns after selecting it (a short hitch for big textures); the
result is kept in memory until you pick another saber. Android-built bundles still load natively, unchanged.

### Limits of PC conversion (be aware)
* If a PC saber has no convertible meshes at all, loading now fails (logged) and the vanilla saber stays, instead of loading an invisible saber.
* Unity built-in primitive meshes (the default Cylinder/Cube/Sphere referenced from unity default resources) are not in the bundle and are skipped.
* **Shaders cannot be converted.** PC shaders are compiled for DirectX. Each material's shader is looked up *by name* in the
  game; if the game has no such shader the **vanilla saber's shader** is used instead, so some sabers will look different
  (glow/transparency especially). Fix per saber with `shader_map.cfg` in the mod folder (`PCShaderName=GameShaderName`,
  created on first load). Logcat lines starting `shader '...'` (tag `SaberFactoryQuest`) show what was resolved.
* Not supported: BC7 / crunched / ASTC / ETC textures (replaced by white + a logged warning), compressed meshes
  (re-export with Mesh Compression *Off*), bundles from Unity older than 2017 (2017/2018, e.g. 2018.1.6f1, and newer are fine), bundles built without type trees,
  skinned meshes, particle systems, lights, animations, scripts other than the `CustomTrail` material lookup.
* No saber colour tinting (CustomSabers' `_CustomColors` mechanism), no trail length/granularity from `CustomTrail`
  (the trail uses the in-game width/duration settings).
* **Untested on a headset.** The parser/decoders are covered by host tests (`tools/host_test.sh`, run in CI before
  the build); the Unity object construction was only checked against mock classes. Expect to fix compile errors in
  `src/pc_saber_unity.cpp` the same way as for the rest of the project (see the list below), and to tune shaders.

## Android bundle requirements (native path)
* Built in Unity for **Android (ASTC)**, matching Beat Saber's Unity version, **Single Pass Instanced** shaders.
* **No custom C# scripts** in the bundle. CustomSabers' `CustomTrail` and descriptor components are ignored.
* Prefab named `_CustomSaber` with children `LeftSaber` and `RightSaber` (blade along local +Z, origin at the hilt).
* Optional `Material` asset named `_TrailMaterial`; its shader must multiply by **vertex color alpha** and be double-sided/transparent.

## Things most likely to need touching on first compile (the honest list)
1. `SaberModelController::Init` parameters — toggle `SFQ_INIT_HAS_COLOR` in `src/main.cpp`.
2. `INSTALL_HOOK(getLogger(), …)` — beatsaber-hook's logger argument differs between 5.x/6.x; adjust to your version.
3. `SafePtrUnity` / `ArrayW` / `csTypeOf` includes and the generated names for `UnityEngine::Mesh::set_vertices/set_colors/set_triangles`.
4. `GetFieldValue<…>(c, "_saberTrail")` return type (optional vs. result wrapper).
5. `Saber::get_saberBladeTopPos/BottomPos`, `Saber::ManualUpdate` existence in 1.40.8 — check the generated headers.
6. `modFiles` vs `lateModFiles` in `mod.json` (this mod initialises in `late_load`).
7. `src/pc_saber_unity.cpp`: exact generated signatures of `Mesh::set_vertices/set_uv/set_uv2/set_colors/SetTriangles`, `Texture2D::New_ctor/LoadRawTextureData/Apply`, `Material::New_ctor(Shader*)/HasProperty/SetFloat/SetColor/SetTexture/SetTextureScale/SetTextureOffset`, `GameObject::AddComponent`, `MeshRenderer::set_sharedMaterials`.

## Not implemented
* A pixel-for-pixel copy of the PC SaberFactory editor (3D preview, per-part editing). The UI is a BSML settings page in a similar blue style.
* Saber colour tinting, glow, SaberFactory's own editor/preset format.
* Fix-ups discovered only by running on a headset.

Original SaberFactory is GPL-3.0 (https://github.com/Dennoc/SaberFactory); this port is therefore GPL-3.0 as well.
