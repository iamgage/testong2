#!/usr/bin/env python3
"""Extract supported meshes and textures from PC .saber UnityFS bundles.

Requires UnityPy and Pillow. Produces intermediate assets, NOT an Android AssetBundle.
Unity prefab rebuilding and Quest shader conversion require Unity Editor or a native runtime.
"""
import argparse, json, re, sys
from pathlib import Path

def safe_name(name):
    return re.sub(r'[^a-zA-Z0-9_.-]+', '_', name or 'unnamed')[:90]

def convert(source, target):
    try:
        import UnityPy
    except ImportError:
        raise RuntimeError('UnityPy is required: python -m pip install UnityPy Pillow')
    target.mkdir(parents=True, exist_ok=True)
    env = UnityPy.load(str(source))
    report = {'input': str(source), 'meshes': [], 'textures': [], 'prefabs': [], 'materials': [], 'warnings': []}
    for obj in env.objects:
        typ = obj.type.name
        try:
            data = obj.read()
            name = safe_name(getattr(data, 'm_Name', '') or f'{typ}_{obj.path_id}')
            if typ == 'Mesh':
                # UnityPy exposes mesh export through Mesh.export() in supported versions.
                if not hasattr(data, 'export'):
                    report['warnings'].append(f'Mesh {name}: exporter unavailable in installed UnityPy')
                    continue
                output = target / f'{name}_{obj.path_id}.obj'
                mesh_data = data.export()
                output.write_text(mesh_data if isinstance(mesh_data, str) else mesh_data.decode('utf-8'), encoding='utf-8')
                report['meshes'].append(output.name)
            elif typ == 'Texture2D':
                output = target / f'{name}_{obj.path_id}.png'
                data.image.save(output)
                report['textures'].append(output.name)
            elif typ == 'Material':
                report['materials'].append({'name': name, 'path_id': obj.path_id})
            elif typ == 'GameObject':
                report['prefabs'].append({'name': name, 'path_id': obj.path_id})
        except Exception as exc:
            report['warnings'].append(f'{typ} {obj.path_id}: {exc}')
    report['warnings'].append('This is intermediate extraction only: GameObject hierarchy, animations, shaders, and Android bundle still require reconstruction.')
    (target / 'conversion_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('saber', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    try:
        r = convert(a.saber, a.output)
        print(json.dumps({'meshes':len(r['meshes']), 'textures':len(r['textures']), 'warnings':len(r['warnings'])}, indent=2))
    except Exception as exc:
        print(f'Conversion unavailable: {exc}', file=sys.stderr)
        sys.exit(2)
