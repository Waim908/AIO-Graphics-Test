#!/usr/bin/env python3
# Parses the original DolphinVS DX8 assets (.x meshes, .bmp/.tga textures) into a
# single embeddable C header for the AIO Graphics Test dolphin scene.
#
# One-time data-generation step (not part of the CI build). Run:
#   python3 tools/gen_dolphin_assets.py <assets_dir> src/dolphin_assets.h
#
# Note: embeds Microsoft DirectX SDK sample data; see scene notes re: licensing.
import re, sys, struct, math

NUM = re.compile(r'-?\d+\.?\d*(?:[eE][-+]?\d+)?')

def strip_comments(t):
    return re.sub(r'//[^\n]*', '', t)

# Real "Mesh <name> {" - not "XSkinMeshHeader", "MeshNormals", "MeshTextureCoords".
MESH_RE = re.compile(r'(?<![A-Za-z])Mesh\s+[A-Za-z_]\w*\s*\{')

def after_block_open(text, pattern, start=0):
    """Return index just past the '{' opening the block matched by `pattern`."""
    if isinstance(pattern, str):
        pattern = re.compile(re.escape(pattern) + r'\s*\{')
    m = pattern.search(text, start)
    if not m:
        raise ValueError("block not found")
    return m.end()

def nums_from(text, start):
    for m in NUM.finditer(text, start):
        yield m.group()

def parse_mesh_geometry(text):
    """Parse the first 'Mesh' block: vertices and triangle faces."""
    p = after_block_open(text, MESH_RE)
    it = nums_from(text, p)
    nv = int(float(next(it)))
    verts = [(float(next(it)), float(next(it)), float(next(it))) for _ in range(nv)]
    nf = int(float(next(it)))
    tris = []
    for _ in range(nf):
        c = int(float(next(it)))
        idx = [int(float(next(it))) for _ in range(c)]
        for k in range(1, c - 1):           # fan-triangulate
            tris.append((idx[0], idx[k], idx[k + 1]))
    return verts, tris

def parse_normals(text):
    p = after_block_open(text, 'MeshNormals')
    it = nums_from(text, p)
    nn = int(float(next(it)))
    return [(float(next(it)), float(next(it)), float(next(it))) for _ in range(nn)]

def parse_texcoords(text):
    p = after_block_open(text, 'MeshTextureCoords')
    it = nums_from(text, p)
    nc = int(float(next(it)))
    return [(float(next(it)), float(next(it))) for _ in range(nc)]

def load_x(path):
    t = strip_comments(open(path, 'r', errors='ignore').read())
    verts, tris = parse_mesh_geometry(t)
    try: nrm = parse_normals(t)
    except ValueError: nrm = None
    try: uv = parse_texcoords(t)
    except ValueError: uv = None
    return verts, tris, nrm, uv

def load_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    hdr = struct.unpack_from('<I', d, 14)[0]  # info header size
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    flip = h > 0
    h = abs(h)
    pal = 14 + hdr  # palette offset (for <= 8bpp)
    bpr = ((bpp * w + 31) // 32) * 4
    out = bytearray(w * h * 4)
    for y in range(h):
        sy = (h - 1 - y) if flip else y
        row = off + sy * bpr
        for x in range(w):
            if bpp == 8:
                idx = d[row + x]
                b, g, r = d[pal + idx*4], d[pal + idx*4 + 1], d[pal + idx*4 + 2]; a = 255
            elif bpp == 24:
                b, g, r = d[row + x*3], d[row + x*3 + 1], d[row + x*3 + 2]; a = 255
            else:  # 32
                b, g, r, a = d[row + x*4], d[row + x*4+1], d[row + x*4+2], d[row + x*4+3]
            o = (y * w + x) * 4
            out[o:o+4] = bytes((r, g, b, a))
    return w, h, out

def compute_normals(verts, tris):
    nrm = [[0.0, 0.0, 0.0] for _ in verts]
    for a, b, c in tris:
        ax, ay, az = verts[a]; bx, by, bz = verts[b]; cx, cy, cz = verts[c]
        ux, uy, uz = bx-ax, by-ay, bz-az
        vx, vy, vz = cx-ax, cy-ay, cz-az
        nx, ny, nz = uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx
        for i in (a, b, c):
            nrm[i][0] += nx; nrm[i][1] += ny; nrm[i][2] += nz
    out = []
    for n in nrm:
        l = (n[0]**2 + n[1]**2 + n[2]**2) ** 0.5 or 1.0
        out.append((n[0]/l, n[1]/l, n[2]/l))
    return out

def load_tga_luma(path):
    d = open(path, 'rb').read()
    idlen = d[0]; itype = d[2]
    w, h = struct.unpack_from('<HH', d, 12)
    bpp = d[16]; desc = d[17]
    topdown = bool(desc & 0x20)
    off = 18 + idlen
    bytespp = bpp // 8
    out = bytearray(w * h)  # single luminance channel
    for y in range(h):
        sy = y if topdown else (h - 1 - y)
        for x in range(w):
            px = off + (sy * w + x) * bytespp
            b, g, r = d[px], d[px+1], d[px+2]
            out[y * w + x] = (r * 30 + g * 59 + b * 11) // 100
    return w, h, out

def emit_floats(f, name, data, comps):
    f.write(f"static const float {name}[] = {{\n")
    for i, v in enumerate(data):
        f.write("  " + ", ".join(f"{c:.6f}f" for c in v) + ",\n")
    f.write("};\n")

def emit_u16(f, name, data):
    f.write(f"static const unsigned short {name}[] = {{\n")
    for i in range(0, len(data), 12):
        f.write("  " + ", ".join(str(x) for x in data[i:i+12]) + ",\n")
    f.write("};\n")

def emit_bytes(f, name, data):
    f.write(f"static const unsigned char {name}[{len(data)}] = {{\n")
    for i in range(0, len(data), 20):
        f.write("  " + ",".join(str(b) for b in data[i:i+20]) + ",\n")
    f.write("};\n")

def main():
    src, out = sys.argv[1], sys.argv[2]
    d = src.rstrip('/')
    v1, tris, n1, uv = load_x(f"{d}/Dolphin1.x")
    v2, _, n2, _ = load_x(f"{d}/Dolphin2.x")
    v3, _, n3, _ = load_x(f"{d}/Dolphin3.x")
    sv, stris, sn, suv = load_x(f"{d}/seafloor.x")
    # Dolphin has no texcoords in the .x; synthesize a CYLINDRICAL unwrap around
    # the body's long (nose->tail) axis = X (verified: X range >> Y,Z). This wraps
    # the skin around the rounded body instead of projecting it flat onto the side
    # silhouette, removing the front/back duplication and the back/belly smearing
    # the old planar projection produced.
    #   U = position along the body length (nose -> tail)
    #   V = angle around the body in the Y-Z cross-section, folded so dorsal(+Y)->0
    #       and belly(-Y)->1. abs() mirrors the left/right flanks (a dolphin skin is
    #       bilaterally symmetric) and puts the only fold on the belly midline, where
    #       both +pi and -pi map to V=1, so there is no visible seam.
    if not uv:
        xs = [p[0] for p in v1]; ys = [p[1] for p in v1]; zs = [p[2] for p in v1]
        x0, x1 = min(xs), max(xs)
        yc = (min(ys) + max(ys)) / 2.0
        zc = (min(zs) + max(zs)) / 2.0
        uv = []
        for p in v1:
            u = (p[0] - x0) / (x1 - x0 + 1e-6)
            ang = math.atan2(p[2] - zc, p[1] - yc)   # 0 at dorsal (+Y), +-pi at belly
            uv.append((u, abs(ang) / math.pi))
    print(f"dolphin: {len(v1)} verts, {len(tris)} tris, normals={len(n1) if n1 else 0}, uv={len(uv) if uv else 0}")
    print(f"keyframes match: {len(v1)==len(v2)==len(v3)}")
    print(f"seafloor: {len(sv)} verts, {len(stris)} tris, normals={len(sn) if sn else 0}, uv={len(suv) if suv else 0}")
    dw, dh, dtex = load_bmp(f"{d}/dolphin.bmp")
    sw, sh, stex = load_bmp(f"{d}/seafloor.bmp")
    print(f"dolphin.bmp {dw}x{dh}, seafloor.bmp {sw}x{sh}")
    caust = []
    for i in range(32):
        cw, ch, cl = load_tga_luma(f"{d}/caust{i:02d}.tga")
        caust.append(cl)
    print(f"caustics: 32x {cw}x{ch} luma, {sum(len(c) for c in caust)} bytes total")
    if len(sys.argv) > 3 and sys.argv[3] == '--noemit':
        return

    # flatten faces -> indices
    didx = [i for t in tris for i in t]
    sidx = [i for t in stris for i in t]
    with open(out, 'w') as f:
        f.write("// AIO Graphics Test - embedded DolphinVS assets (generated; do not edit).\n")
        f.write("// Source: Microsoft DirectX SDK DolphinVS sample. See gen_dolphin_assets.py.\n")
        f.write("#ifndef AIO_DOLPHIN_ASSETS_H\n#define AIO_DOLPHIN_ASSETS_H\n\n")
        f.write(f"#define DOLPHIN_NVERTS {len(v1)}\n#define DOLPHIN_NINDICES {len(didx)}\n")
        f.write(f"#define SEAFLOOR_NVERTS {len(sv)}\n#define SEAFLOOR_NINDICES {len(sidx)}\n")
        f.write(f"#define DOLTEX_W {dw}\n#define DOLTEX_H {dh}\n")
        f.write(f"#define SEATEX_W {sw}\n#define SEATEX_H {sh}\n")
        f.write(f"#define CAUST_W {cw}\n#define CAUST_H {ch}\n#define CAUST_FRAMES 32\n\n")
        emit_floats(f, "dolphin_pos1", v1, 3)
        emit_floats(f, "dolphin_pos2", v2, 3)
        emit_floats(f, "dolphin_pos3", v3, 3)
        emit_floats(f, "dolphin_nrm1", n1, 3)
        emit_floats(f, "dolphin_nrm2", n2, 3)
        emit_floats(f, "dolphin_nrm3", n3, 3)
        emit_floats(f, "dolphin_uv", uv, 2)
        emit_u16(f, "dolphin_idx", didx)
        emit_floats(f, "seafloor_pos", sv, 3)
        emit_floats(f, "seafloor_nrm", sn if sn else compute_normals(sv, stris), 3)
        emit_floats(f, "seafloor_uv", suv if suv else [(0,0)]*len(sv), 2)
        emit_u16(f, "seafloor_idx", sidx)
        emit_bytes(f, "dolphin_tex", dtex)
        emit_bytes(f, "seafloor_tex", stex)
        flat = bytearray()
        for c in caust: flat += c
        emit_bytes(f, "caust_tex", flat)
        f.write("\n#endif\n")
    print(f"wrote {out}")

main()
