"""SW AUDIO — Blender render of a GT03 stomp box (Blender 4.0.2, Cycles, CPU). Each type has its own finish, layout and engraving.

  blender -b -P tools/blender/pedal.py -- <type> <out.png> [samples] [scale]        type: comp drive fuzz chorus delay reverb

The layout (knobs, LED, name, foot switch) is in tools/blender/pedals.json; the screen reads the same file, so what is drawn here and
what is laid over it (HTML: knobs, LED glow, foot switch, name) line up. Original designs: no real product's name, logo or look.
Studio as the other parts (docs/design/design-system/project/RENDERING.md): key top left, fill bottom right, rim from behind.
Units: 1 px of the screen = 0.01 Blender units; the shell is 140 x 226 px, drawn at scale x (default 2) with 14 px of room around it.
"""
import sys, json, math, os
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
kind, out = argv[0], argv[1]
samples = int(argv[2]) if len(argv) > 2 else 96
SC = int(argv[3]) if len(argv) > 3 else 2
HERE = os.path.dirname(os.path.abspath(__file__))
spec = json.load(open(os.path.join(HERE, 'pedals.json')))
T = spec['types'][kind]
W, H = spec['shell']['w'] / 100.0, spec['shell']['h'] / 100.0
DEPTH = 0.5
PAD = 0.14
hexcol = T['color'].lstrip('#')
base = [(int(hexcol[i:i + 2], 16) / 255.0) ** 2.2 for i in (0, 2, 4)]
luma = 0.2126 * base[0] + 0.7152 * base[1] + 0.0722 * base[2]

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = samples; scene.cycles.use_denoising = False
scene.render.film_transparent = True
scene.render.resolution_x = int(round((W + 2 * PAD) * 100 * SC)); scene.render.resolution_y = int(round((H + 2 * PAD) * 100 * SC))
scene.render.image_settings.file_format = 'PNG'; scene.render.image_settings.color_mode = 'RGBA'
world = bpy.data.worlds.new('w'); scene.world = world; world.use_nodes = True
bg = world.node_tree.nodes['Background']; bg.inputs[0].default_value = (0.05, 0.05, 0.055, 1); bg.inputs[1].default_value = 0.6


def pos(px, py):                     # screen px (origin top left of the shell) -> units (origin the centre, y up)
    return (px / 100 - W / 2, H / 2 - py / 100)


# ---- materials
def principled(m):
    return m.node_tree.nodes['Principled BSDF']


def mat(name, color, rough, metal=0.0, coat=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True; p = principled(m)
    p.inputs['Base Color'].default_value = (color[0], color[1], color[2], 1); p.inputs['Roughness'].default_value = rough
    p.inputs['Metallic'].default_value = metal; p.inputs['Coat Weight'].default_value = coat; p.inputs['Coat Roughness'].default_value = 0.18
    return m


def add_bump(m, scale, strength, distance, detail=2.0, stretch=None, kind_='noise'):
    nt = m.node_tree; p = principled(m)
    tc = nt.nodes.new('ShaderNodeTexCoord')
    mp = nt.nodes.new('ShaderNodeMapping')
    if stretch:
        mp.inputs['Scale'].default_value = stretch
    nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
    if kind_ == 'voronoi':
        tex = nt.nodes.new('ShaderNodeTexVoronoi'); tex.inputs['Scale'].default_value = scale; out_ = tex.outputs['Distance']
    else:
        tex = nt.nodes.new('ShaderNodeTexNoise'); tex.inputs['Scale'].default_value = scale; tex.inputs['Detail'].default_value = detail; out_ = tex.outputs['Fac']
    nt.links.new(mp.outputs['Vector'], tex.inputs['Vector'])
    b = nt.nodes.new('ShaderNodeBump'); b.inputs['Strength'].default_value = strength; b.inputs['Distance'].default_value = distance
    nt.links.new(out_, b.inputs['Height']); nt.links.new(b.outputs['Normal'], p.inputs['Normal'])


finish = T['finish']
if finish == 'brushed':        # anodised aluminium: horizontal brushing
    body = mat('body', base, 0.40, 0.4, coat=0.15); add_bump(body, 40, 0.5, 0.003, 4, stretch=(1.0, 0.012, 1.0))
elif finish == 'hammered':     # hammered powder coat
    body = mat('body', base, 0.42, 0.0, coat=0.25); add_bump(body, 38, 0.9, 0.012, kind_='voronoi')
elif finish == 'flake':        # metal-flake: coarse sparkle
    body = mat('body', base, 0.30, 0.3, coat=0.8); add_bump(body, 420, 0.7, 0.004, kind_='voronoi')
elif finish == 'wrinkle':      # wrinkle paint
    body = mat('body', base, 0.55, 0.0, coat=0.0); add_bump(body, 55, 1.2, 0.02, 9)
elif finish == 'cream':        # satin enamel with wooden cheeks
    body = mat('body', base, 0.45, 0.0, coat=0.3); add_bump(body, 700, 0.1, 0.002)
else:                          # gloss enamel
    body = mat('body', base, 0.22, 0.0, coat=1.0)

paint_col = (0.015, 0.015, 0.017) if luma > 0.25 else (0.85, 0.82, 0.76)
paint = mat('paint', paint_col, 0.55)
dark = mat('dark', (0.012, 0.012, 0.014), 0.5)
chrome = mat('chrome', (0.8, 0.8, 0.82), 0.2, 1.0)
glass = mat('glass', (0.02, 0.025, 0.03), 0.08, 0.0, coat=1.0)
wood = mat('wood', (0.2, 0.09, 0.035), 0.5, 0.0, coat=0.5); add_bump(wood, 14, 0.35, 0.006, 6, stretch=(1.0, 0.08, 1.0))


# ---- geometry helpers
def finish_obj(o, m, bevel=0.0, seg=3):
    if bevel:
        b = o.modifiers.new('b', 'BEVEL'); b.width = bevel; b.segments = seg
    bpy.ops.object.shade_smooth(); o.data.materials.append(m); return o


def box(cx, cy, w, h, d, z0, m, bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(cx, cy, z0 + d / 2)); o = bpy.context.active_object
    o.scale = (w, h, d); bpy.ops.object.transform_apply(scale=True); return finish_obj(o, m, bevel)


def disc(px, py, r, d, z0, m, bevel=0.0, verts=96):
    x, y = pos(px, py)
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=d, location=(x, y, z0 + d / 2)); o = bpy.context.active_object
    return finish_obj(o, m, bevel)


TOP = DEPTH
shell = box(0, 0, W, H, DEPTH, 0.0, body, 0.0)
bv = shell.modifiers.new('bevel', 'BEVEL'); bv.width = T['radius']; bv.segments = 8; bv.limit_method = 'NONE'
sub = shell.modifiers.new('sub', 'SUBSURF'); sub.levels = 1; sub.render_levels = 2

if finish == 'cream':          # wooden cheeks on both sides
    for sx in (-1, 1):
        box(sx * (W / 2 + 0.03), 0, 0.12, H + 0.02, DEPTH * 0.9, 0.0, wood, 0.03)

# knob seats (the knob image is laid over by the screen)
for kx, ky, ks in T['knobs']:
    r = ks / 200.0
    disc(kx, ky, r + 0.045, 0.010, TOP - 0.006, chrome, 0.003)
    disc(kx, ky, r + 0.025, 0.012, TOP - 0.004, dark)

# LED seat
lx, ly, ls = T['led']
disc(lx, ly, ls / 200.0 + 0.045, 0.014, TOP - 0.006, chrome, 0.005)
disc(lx, ly, ls / 200.0 + 0.012, 0.016, TOP - 0.004, dark)

# foot-switch plate
sx_, sy_, ss = T['stomp']
disc(sx_, sy_, ss / 200.0 + 0.055, 0.012, TOP - 0.006, chrome, 0.004)
disc(sx_, sy_, ss / 200.0 + 0.025, 0.014, TOP - 0.004, dark)


# ---- engraving (paint on top of the shell)
def tick_arc(px, py, r_in, r_out, n, a0, a1, w=0.012):
    x0, y0 = pos(px, py)
    for i in range(n):
        a = math.radians(a0 + (a1 - a0) * i / max(1, n - 1))      # 0 = up, clockwise
        cx, cy = x0 + math.sin(a) * (r_in + r_out) / 2, y0 + math.cos(a) * (r_in + r_out) / 2
        bpy.ops.mesh.primitive_cube_add(size=1, location=(cx, cy, TOP + 0.002)); o = bpy.context.active_object
        o.scale = (w, r_out - r_in, 0.004); o.rotation_euler = (0, 0, -a); o.data.materials.append(paint)


def stroke(points, w, z=0.0, m=None):
    """a polyline of thin raised bars; points in screen px"""
    m = m or paint
    pts = [pos(*p) for p in points]
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        L = math.hypot(bx - ax, by - ay); ang = math.atan2(by - ay, bx - ax)
        bpy.ops.mesh.primitive_cube_add(size=1, location=((ax + bx) / 2, (ay + by) / 2, TOP + z + 0.002)); o = bpy.context.active_object
        o.scale = (L + w * 0.6, w, 0.004); o.rotation_euler = (0, 0, ang); o.data.materials.append(m)
        bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=w / 2, depth=0.004, location=(bx, by, TOP + z + 0.002)); c = bpy.context.active_object
        c.data.materials.append(m)


for kx, ky, ks in T['knobs']:      # a tick scale around every knob, 270 degrees
    tick_arc(kx, ky, ks / 200.0 + 0.06, ks / 200.0 + 0.09, 11 if ks > 30 else 7, -135, 135)

if kind == 'comp':             # a meter window with five dark lenses
    x, y = pos(70, 124)
    box(x, y, 0.62, 0.15, 0.012, TOP - 0.004, dark, 0.01)
    for i in range(5):
        px_, py_ = pos(46 + i * 12, 124)
        bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=0.025, depth=0.014, location=(px_, py_, TOP + 0.003)); o = bpy.context.active_object
        o.data.materials.append(glass); bpy.ops.object.shade_smooth()
elif kind == 'drive':          # corner screws and a double line under the big knob
    for cx_, cy_ in ((12, 12), (128, 12), (12, 214), (128, 214)):
        disc(cx_, cy_, 0.045, 0.012, TOP - 0.004, chrome, 0.004, 24)
    stroke([(22, 121), (118, 121)], 0.010); stroke([(22, 125), (118, 125)], 0.010)
elif kind == 'fuzz':           # a lightning bolt
    bolt = [(118, 134), (104, 152), (113, 152), (106, 170), (124, 146), (114, 146)]
    stroke(bolt + [bolt[0]], 0.02)
elif kind == 'chorus':         # a sine wave
    stroke([(28 + i * 84 / 40, 146 + 8 * math.sin(i / 40 * 4 * math.pi)) for i in range(41)], 0.013)
elif kind == 'delay':          # echo steps that fade out
    for i, hgt in enumerate((26, 19, 13, 8, 5)):
        stroke([(94 + i * 9, 52 - hgt / 2), (94 + i * 9, 52 + hgt / 2)], 0.014)
elif kind == 'reverb':         # decay bars
    for i, hgt in enumerate((20, 15, 11, 8, 5, 3)):
        stroke([(34 + i * 14, 150 - hgt / 2), (34 + i * 14, 150 + hgt / 2)], 0.014)


def area(name, loc, energy, size, color=(1, 1, 1)):
    bpy.ops.object.light_add(type='AREA', location=loc); l = bpy.context.active_object; l.name = name
    l.data.energy = energy; l.data.size = size; l.data.color = color
    l.rotation_euler = (Vector((0, 0, 0.3)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()


area('key', (-3.6, 3.8, 3.4), 640, 2.6, (1.0, 0.97, 0.92)); area('fill', (3.4, -3.6, 2.4), 110, 3.0, (0.85, 0.92, 1.0)); area('rim', (0.0, 4.2, 1.6), 140, 1.6)
bpy.ops.object.camera_add(location=(0, 0, 6)); cam = bpy.context.active_object; cam.data.type = 'ORTHO'
cam.data.ortho_scale = H + 2 * PAD; scene.camera = cam
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
print('rendered', kind, out)
