#!/usr/bin/env python3
"""Writes a small synthetic PC-style CustomSabers bundle (UnityFS, SerializedFile v21 with type trees,
LZMA + LZ4 + stored blocks, a .resS stream) plus raw compression test blobs, for tests/test_pc.cpp.
Usage: make_test_bundle.py <outdir>"""
import lzma, os, random, struct, sys

COMMON = (
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
    "int2_storage\0int3_storage\0BoundsInt\0m_CorrespondingSourceObject\0m_PrefabInstance\0m_PrefabAsset\0FileSize\0Hash128\0")


def common_off(s):
    i = COMMON.find(s + "\0")
    while i > 0 and COMMON[i - 1] != "\0":
        i = COMMON.find(s + "\0", i + 1)
    return i


# ---------------------------------------------------------------- type trees
class N:
    def __init__(s, t, n, ch=(), flag=0, arr=False):
        s.t, s.n, s.ch, s.flag, s.arr = t, n, list(ch), flag, arr


A4 = 0x4000
def P(t, n, al=False): return N(t, n, flag=A4 if al else 0)
def STR(n): return N("string", n, [N("Array", "Array", [P("int", "size"), P("char", "data")], flag=A4, arr=True)])
def VEC(n, elem, al=False): return N("vector", n, [N("Array", "Array", [P("int", "size"), elem], flag=A4 if al else 0, arr=True)])
def PPTR(n, t="PPtr<Object>"): return N(t, n, [P("int", "m_FileID"), P("SInt64", "m_PathID")])
def V3(n): return N("Vector3f", n, [P("float", "x"), P("float", "y"), P("float", "z")])
def V2(n): return N("Vector2f", n, [P("float", "x"), P("float", "y")])
def QUAT(n): return N("Quaternionf", n, [P("float", c) for c in "xyzw"])
def PAIR(first, second): return N("pair", "data", [first, second])
def FPN(n): return N("FastPropertyName", n, [STR("name")])

GO_T = N("GameObject", "Base", [
    VEC("m_Component", N("ComponentPair", "data", [PPTR("component", "PPtr<Component>")])),
    P("UInt32", "m_Layer"), STR("m_Name"), P("UInt16", "m_Tag"), P("bool", "m_IsActive", True)])
TR_T = N("Transform", "Base", [PPTR("m_GameObject", "PPtr<GameObject>"), QUAT("m_LocalRotation"), V3("m_LocalPosition"),
                                V3("m_LocalScale"), VEC("m_Children", PPTR("data", "PPtr<Transform>")), PPTR("m_Father", "PPtr<Transform>")])
MF_T = N("MeshFilter", "Base", [PPTR("m_GameObject", "PPtr<GameObject>"), PPTR("m_Mesh")])
MR_T = N("MeshRenderer", "Base", [PPTR("m_GameObject", "PPtr<GameObject>"), P("bool", "m_Enabled", True),
                                   VEC("m_Materials", PPTR("data", "PPtr<Material>"))])
MB_T = N("MonoBehaviour", "Base", [PPTR("m_GameObject", "PPtr<GameObject>"), P("bool", "m_Enabled", True),
                                    PPTR("m_Script", "PPtr<MonoScript>"), STR("m_Name"), PPTR("TrailMaterial", "PPtr<Material>"), P("int", "Length")])
SUBMESH = N("SubMesh", "data", [P("UInt32", "firstByte"), P("UInt32", "indexCount"), P("int", "topology"), P("UInt32", "baseVertex"),
                                 P("UInt32", "firstVertex"), P("UInt32", "vertexCount"),
                                 N("AABB", "localAABB", [V3("m_Center"), V3("m_Extent")])])
CHANNEL = N("ChannelInfo", "data", [P("UInt8", "stream"), P("UInt8", "offset"), P("UInt8", "format"), P("UInt8", "dimension")])
MESH_T = N("Mesh", "Base", [STR("m_Name"), VEC("m_SubMeshes", SUBMESH), P("UInt8", "m_MeshCompression", True), P("int", "m_IndexFormat"),
                             VEC("m_IndexBuffer", P("UInt8", "data"), True),
                             N("VertexData", "m_VertexData", [P("UInt32", "m_VertexCount"), VEC("m_Channels", CHANNEL),
                                                              N("TypelessData", "m_DataSize", [P("int", "size"), P("UInt8", "data")])])])
TEXENV = N("UnityTexEnv", "second", [PPTR("m_Texture", "PPtr<Texture>"), V2("m_Scale"), V2("m_Offset")])
MAT_T = N("Material", "Base", [STR("m_Name"), PPTR("m_Shader"), STR("m_ShaderKeywords"), P("int", "m_CustomRenderQueue"),
                                N("UnityPropertySheet", "m_SavedProperties", [
                                    VEC("m_TexEnvs", PAIR(FPN("first"), TEXENV)),
                                    VEC("m_Floats", PAIR(FPN("first"), P("float", "second"))),
                                    VEC("m_Colors", PAIR(FPN("first"), N("ColorRGBA", "second", [P("float", c) for c in "rgba"])))])])
TEX_T = N("Texture2D", "Base", [STR("m_Name"), P("int", "m_Width"), P("int", "m_Height"), P("int", "m_CompleteImageSize"),
                                 P("int", "m_TextureFormat"), P("int", "m_MipCount"),
                                 N("TypelessData", "image data", [P("int", "size"), P("UInt8", "data")]),
                                 N("StreamingInfo", "m_StreamData", [P("UInt32", "offset"), P("UInt32", "size"), STR("path")])])
SH_T = N("Shader", "Base", [STR("m_Name"), N("SerializedShader", "m_ParsedForm", [STR("m_Name")])])

# ---------------------------------------------------------------- value writer
FMT = {"SInt8": "<b", "UInt8": "<B", "char": "<B", "short": "<h", "SInt16": "<h", "UInt16": "<H", "unsigned short": "<H",
       "int": "<i", "SInt32": "<i", "UInt32": "<I", "unsigned int": "<I", "SInt64": "<q", "long long": "<q", "float": "<f", "bool": "<B"}

def pad4(b):
    while len(b) % 4: b.append(0)

def W(n, v, b):
    t = n.t
    al = bool(n.flag & A4)
    if t in FMT:
        b += struct.pack(FMT[t], v)
    elif t == "string":
        d = v.encode(); b += struct.pack("<i", len(d)) + d
        al = bool(n.ch[0].flag & A4)
    elif t == "TypelessData":
        b += struct.pack("<i", len(v)) + bytes(v)
    elif len(n.ch) == 1 and n.ch[0].arr:
        arr = n.ch[0]; el = arr.ch[1]
        b += struct.pack("<i", len(v))
        if el.t == "UInt8" and isinstance(v, (bytes, bytearray)):
            b += v
        else:
            for x in v: W(el, x, b)
        al = bool(arr.flag & A4)
    else:
        for c in n.ch: W(c, v[c.n], b)
    if al: pad4(b)

def tree_blob(root):
    flat = []
    def walk(n, lvl):
        flat.append((n, lvl))
        for c in n.ch: walk(c, lvl + 1)
    walk(root, 0)
    local = bytearray(); cache = {}
    def sref(s):
        o = common_off(s)
        if o >= 0: return 0x80000000 | o
        if s not in cache:
            cache[s] = len(local); local.extend(s.encode() + b"\0")
        return cache[s]
    nodes = bytearray()
    for i, (n, lvl) in enumerate(flat):
        nodes += struct.pack("<HBBIIiiiQ", 1, lvl, 1 if n.arr else 0, sref(n.t), sref(n.n), -1, i, n.flag, 0)
    return struct.pack("<ii", len(flat), len(local)) + bytes(nodes) + bytes(local)

# ---------------------------------------------------------------- the saber content
def pptr(i): return {"m_FileID": 0, "m_PathID": i}
def comp(i): return {"component": pptr(i)}
def vec3(x, y, z): return {"x": x, "y": y, "z": z}
IDQ = {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}

def transform(go, father, kids, pos=(0, 0, 0), scale=(1, 1, 1)):
    return {"m_GameObject": pptr(go), "m_LocalRotation": IDQ, "m_LocalPosition": vec3(*pos), "m_LocalScale": vec3(*scale),
            "m_Children": [pptr(k) for k in kids], "m_Father": pptr(father)}
def gameobject(name, comps): return {"m_Component": [comp(c) for c in comps], "m_Layer": 0, "m_Name": name, "m_Tag": 0, "m_IsActive": 1}
def aabb(): return {"m_Center": vec3(0, 0, 0), "m_Extent": vec3(1, 1, 1)}
def submesh(first, n): return {"firstByte": first, "indexCount": n, "topology": 0, "baseVertex": 0, "firstVertex": 0, "vertexCount": 3, "localAABB": aabb()}

# 3 vertices, two streams: stream0 = pos+normal (24 B/vertex), stream1 = uv0 + colour (12 B/vertex)
verts = [((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0), (255, 0, 0, 255)),
         ((0.0, 0.0, 1.0), (0.0, 1.0, 0.0), (1.0, 0.0), (0, 255, 0, 128)),
         ((1.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0), (0, 0, 255, 0))]
s0 = b"".join(struct.pack("<6f", *p, *n) for p, n, _, _ in verts)
s1 = b"".join(struct.pack("<2f", *uv) + bytes(c) for _, _, uv, c in verts)
vdata = s0 + b"\0" * ((-len(s0)) % 16) + s1
channels = [{"stream": 0, "offset": 0, "format": 0, "dimension": 3}, {"stream": 0, "offset": 12, "format": 0, "dimension": 3},
            {"stream": 0, "offset": 0, "format": 0, "dimension": 0}, {"stream": 1, "offset": 8, "format": 2, "dimension": 4},
            {"stream": 1, "offset": 0, "format": 0, "dimension": 2}] + [{"stream": 0, "offset": 0, "format": 0, "dimension": 0}] * 9
mesh = {"m_Name": "TestMesh", "m_SubMeshes": [submesh(0, 3), submesh(6, 3)], "m_MeshCompression": 0, "m_IndexFormat": 0,
        "m_IndexBuffer": struct.pack("<6H", 0, 1, 2, 2, 1, 0),
        "m_VertexData": {"m_VertexCount": 3, "m_Channels": channels, "m_DataSize": vdata}}

dxt1 = struct.pack("<HHI", 0xF800, 0x001F, 0xE4)   # red/blue, first row indices 0,1,2,3, other rows 0
stream_pix = bytes([10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160])   # 2x2 RGBA32
res_s = bytes(range(200, 216)) + stream_pix

def tex(name, w, h, fmt, image, off=0, size=0, path=""):
    return {"m_Name": name, "m_Width": w, "m_Height": h, "m_CompleteImageSize": len(image) or size, "m_TextureFormat": fmt, "m_MipCount": 1,
            "image data": image, "m_StreamData": {"offset": off, "size": size, "path": path}}

def fpn(s): return {"name": s}
mat_main = {"m_Name": "Main", "m_Shader": pptr(11), "m_ShaderKeywords": "FOO BAR", "m_CustomRenderQueue": 3000,
            "m_SavedProperties": {
                "m_TexEnvs": [{"first": fpn("_MainTex"), "second": {"m_Texture": pptr(9), "m_Scale": {"x": 2.0, "y": 2.0}, "m_Offset": {"x": 0.5, "y": 0.0}}}],
                "m_Floats": [{"first": fpn("_Glow"), "second": 0.75}],
                "m_Colors": [{"first": fpn("_Color"), "second": {"r": 1.0, "g": 0.5, "b": 0.25, "a": 1.0}}]}}
mat_trail = {"m_Name": "_TrailMat", "m_Shader": pptr(11), "m_ShaderKeywords": "", "m_CustomRenderQueue": -1,
             "m_SavedProperties": {"m_TexEnvs": [{"first": fpn("_MainTex"), "second": {"m_Texture": pptr(10), "m_Scale": {"x": 1.0, "y": 1.0}, "m_Offset": {"x": 0.0, "y": 0.0}}}],
                                   "m_Floats": [], "m_Colors": []}}

CAB = "CAB-0123456789abcdef"
objs = [  # (pathId, classId, tree, value)
    (1, 1, GO_T, gameobject("_CustomSaber", [2])),
    (2, 4, TR_T, transform(1, 0, [4, 13])),
    (3, 1, GO_T, gameobject("LeftSaber", [4, 5, 6, 15])),
    (4, 4, TR_T, transform(3, 2, [], pos=(0.1, 0.2, 0.3), scale=(1, 1, 2))),
    (5, 33, MF_T, {"m_GameObject": pptr(3), "m_Mesh": pptr(7)}),
    (6, 23, MR_T, {"m_GameObject": pptr(3), "m_Enabled": 1, "m_Materials": [pptr(8), pptr(8)]}),
    (7, 43, MESH_T, mesh),
    (8, 21, MAT_T, mat_main),
    (9, 28, TEX_T, tex("Dxt", 4, 4, 10, dxt1)),
    (10, 28, TEX_T, tex("Streamed", 2, 2, 4, b"", 16, 16, "archive:/" + CAB + "/" + CAB + ".resS")),
    (11, 48, SH_T, {"m_Name": "ShaderAsset", "m_ParsedForm": {"m_Name": "Custom/TestSaber"}}),
    (12, 1, GO_T, gameobject("RightSaber", [13])),
    (13, 4, TR_T, transform(12, 2, [])),
    (15, 114, MB_T, {"m_GameObject": pptr(3), "m_Enabled": 1, "m_Script": pptr(0), "m_Name": "", "TrailMaterial": pptr(16), "Length": 20}),
    (16, 21, MAT_T, mat_trail),
]

def build_serialized(unity="2019.4.28f1"):
    classes = []
    for _, cid, tree, _ in objs:
        if cid not in [c for c, _ in classes]: classes.append((cid, tree))
    meta = bytearray()
    meta += unity.encode() + b"\0" + struct.pack("<i", 19) + b"\x01"       # unity version, StandaloneWindows64, type trees on
    meta += struct.pack("<I", len(classes))
    for cid, tree in classes:
        meta += struct.pack("<iBh", cid, 0, -1)
        if cid == 114: meta += b"\x11" * 16
        meta += b"\x22" * 16 + tree_blob(tree) + struct.pack("<i", 0)   # type dependencies
    meta += struct.pack("<I", len(objs))
    data = bytearray(); table = []
    for pid, cid, tree, val in objs:
        b = bytearray(); W(tree, val, b)
        while len(data) % 8: data.append(0)
        table.append((pid, len(data), len(b), [c for c, _ in classes].index(cid)))
        data += b
    HDR = 20
    for pid, off, size, ti in table:
        while (HDR + len(meta)) % 4: meta.append(0)
        meta += struct.pack("<qIIi", pid, off, size, ti)
    meta += struct.pack("<I", 0) + struct.pack("<I", 0) + struct.pack("<I", 0) + b"\0"   # scripts, externals, ref types, user info
    data_offset = (HDR + len(meta) + 15) // 16 * 16
    body = bytes(meta) + b"\0" * (data_offset - HDR - len(meta)) + bytes(data)
    header = struct.pack(">IIII", len(meta), HDR + len(body), 21, data_offset) + b"\x00\x00\x00\x00"
    return header + body

# ---------------------------------------------------------------- compression helpers
def lzma_unity(d):
    c = lzma.compress(d, format=lzma.FORMAT_ALONE, filters=[{"id": lzma.FILTER_LZMA1, "preset": 6, "dict_size": 1 << 20}])
    return c[:5] + c[13:]      # props + dict size, drop the 8 byte length field

def lz4_compress(data):
    out = bytearray(); n = len(data); i = 0; anchor = 0; table = {}
    def emit(lit, off, ml):
        tok = (min(len(lit), 15) << 4) | (min(ml - 4, 15) if off else 0)
        out.append(tok)
        r = len(lit) - 15
        if len(lit) >= 15:
            while r >= 255: out.append(255); r -= 255
            out.append(r)
        out.extend(lit)
        if off:
            out.extend(struct.pack("<H", off))
            r = ml - 4 - 15
            if ml - 4 >= 15:
                while r >= 255: out.append(255); r -= 255
                out.append(r)
    while i + 12 < n:
        key = bytes(data[i:i + 4]); cand = table.get(key); table[key] = i
        if cand is not None and i - cand < 65536:
            ml = 4
            while i + ml < n - 5 and data[cand + ml] == data[i + ml]: ml += 1
            emit(data[anchor:i], i - cand, ml); i += ml; anchor = i
        else:
            i += 1
    emit(data[anchor:], 0, 0)
    return bytes(out)

def build_bundle(ser, res, unity="2019.4.28f1"):
    files = [(CAB, 4, ser), (CAB + ".resS", 0, res)]
    allb = bytearray(); nodes = []
    for name, fl, d in files:
        nodes.append((len(allb), len(d), fl, name)); allb += d
    third = len(allb) // 3
    chunks = [bytes(allb[:third]), bytes(allb[third:2 * third]), bytes(allb[2 * third:])]
    comp_chunks = [(1, lzma_unity(chunks[0])), (2, lz4_compress(chunks[1])), (0, chunks[2])]
    info = bytearray(b"\0" * 16) + struct.pack(">i", 3)
    for (mode, c), raw in zip(comp_chunks, chunks):
        info += struct.pack(">IIH", len(raw), len(c), mode)
    info += struct.pack(">i", len(nodes))
    for off, size, fl, name in nodes:
        info += struct.pack(">qqI", off, size, fl) + name.encode() + b"\0"
    cinfo = lz4_compress(bytes(info))
    hdr = b"UnityFS\0" + struct.pack(">I", 6) + b"5.x.x\0" + unity.encode() + b"\0"
    total = len(hdr) + 8 + 12 + len(cinfo) + sum(len(c) for _, c in comp_chunks)
    hdr += struct.pack(">qIII", total, len(cinfo), len(info), 2 | 0x40)
    return hdr + cinfo + b"".join(c for _, c in comp_chunks)

def main(out):
    os.makedirs(out, exist_ok=True)
    ser = build_serialized()
    open(os.path.join(out, "test.saber"), "wb").write(build_bundle(ser, res_s + b"\x00" * 4096 + res_s * 8))
    open(os.path.join(out, "serialized.bin"), "wb").write(ser)
    # Same content stamped as Unity 2018.1.6f1 (the version most older CustomSabers bundles were built with).
    u18 = "2018.1.6f1"
    open(os.path.join(out, "test2018.saber"), "wb").write(build_bundle(build_serialized(u18), res_s + b"\x00" * 4096 + res_s * 8, u18))
    rnd = random.Random(7)
    blob = bytearray()
    for _ in range(400):
        blob += bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 40)))
        blob += bytes([rnd.randrange(4)]) * rnd.randrange(1, 300)
        if len(blob) > 10: blob += blob[-rnd.randrange(1, 10):] * rnd.randrange(1, 50)
    blob = bytes(blob)
    open(os.path.join(out, "blob.bin"), "wb").write(blob)
    open(os.path.join(out, "blob.lz4"), "wb").write(lz4_compress(blob))
    open(os.path.join(out, "blob.lzma"), "wb").write(lzma_unity(blob))
    print("wrote test data to", out, "(blob %d bytes)" % len(blob))

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/sfq_pc_test")
