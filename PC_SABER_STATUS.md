# PC saber compatibility status

This update adds `tools/convert_pc_saber.py`, an optional offline extractor for supported UnityFS meshes and textures. It does **not** add direct PC `.saber` loading on Quest, and it does not generate Android AssetBundles or a functional MBF `.qmod`.

Run `python -m pip install UnityPy Pillow` then `python tools/convert_pc_saber.py ACC_Saber.saber converted/` on a machine with internet access. The script writes a `conversion_report.json` and any meshes/textures it can decode. Mesh export availability depends on the UnityPy version and Unity asset type. Conversion of materials, GameObject hierarchy, and shaders, plus Android Unity bundle building and native integration, remain necessary.

Do not distribute the output as a working Quest saber without rebuilding it for Android and testing in Beat Saber 1.40.8.
