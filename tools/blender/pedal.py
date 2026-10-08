"""SW AUDIO — Blender render of a stomp-box shell for the GT03 screen (Blender 4.0.2, Cycles, CPU).

  blender -b -P tools/blender/pedal.py -- <out.png> <#rrggbb> [samples] [scale]

Same studio as the other parts (docs/design/design-system/project/RENDERING.md): key light top left, fill bottom right,
rim from behind; the image is never rotated afterwards. The knobs, the LED glow, the foot switch and the name are laid over
the shell by the screen (HTML), so the shell has recessed seats for them: three knob seats on top, an LED seat, a foot-switch plate.
Units: 1 px of the screen = 0.01 Blender units. The shell is 140 x 226 px, drawn at scale x (default 2).
"""
import sys, math
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
out = argv[0]
hexcol = argv[1].lstrip('#')
samples = int(argv[2]) if len(argv) > 2 else 96
SC = int(argv[3]) if len(argv) > 3 else 2
rgb = [int(hexcol[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
lin = [c ** 2.2 for c in rgb]

W, H, DEPTH = 1.40, 2.26, 0.5            # shell size in units
PAD = 0.14                               # room around the shell for the soft edge (px/100)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = samples
scene.cycles.use_denoising = False   # this build has no OpenImageDenoise: render with many samples, then tools/blender/post.py denoises with OpenCV
scene.render.film_transparent = True
scene.render.resolution_x = int(round((W + 2 * PAD) * 100 * SC))
scene.render.resolution_y = int(round((H + 2 * PAD) * 100 * SC))
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_mode = 'RGBA'
scene.view_settings.view_transform = 'Filmic' if 'Filmic' in [v.identifier for v in bpy.types.ColorManagedViewSettings.bl_rna.properties['view_transform'].enum_items] else 'Standard'
scene.view_settings.look = 'None'

# world: a dim, neutral dome (reflections for the enamel)
world = bpy.data.worlds.new('w'); scene.world = world; world.use_nodes = True
bg = world.node_tree.nodes['Background']; bg.inputs[0].default_value = (0.05, 0.05, 0.055, 1); bg.inputs[1].default_value = 0.6


def mat(name, base, rough, metal=0.0, coat=0.0, coat_rough=0.15, bump=None):
    m = bpy.data.materials.new(name); m.use_nodes = True
    p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (base[0], base[1], base[2], 1)
    p.inputs['Roughness'].default_value = rough
    p.inputs['Metallic'].default_value = metal
    p.inputs['Coat Weight'].default_value = coat
    p.inputs['Coat Roughness'].default_value = coat_rough
    if bump:                                      # fine speckle of the enamel
        nt = m.node_tree
        noise = nt.nodes.new('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value = 900; noise.inputs['Detail'].default_value = 2
        b = nt.nodes.new('ShaderNodeBump'); b.inputs['Strength'].default_value = bump; b.inputs['Distance'].default_value = 0.002
        nt.links.new(noise.outputs['Fac'], b.inputs['Height']); nt.links.new(b.outputs['Normal'], p.inputs['Normal'])
    return m


def rounded_box(name, w, h, d, r, bevel_w, z0=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1)
    o = bpy.context.active_object; o.name = name
    o.scale = (w, h, d); bpy.ops.object.transform_apply(scale=True)
    o.location = (0, 0, z0 + d / 2)
    bv = o.modifiers.new('bevel', 'BEVEL'); bv.width = bevel_w; bv.segments = 8; bv.limit_method = 'NONE'
    sub = o.modifiers.new('sub', 'SUBSURF'); sub.levels = 1; sub.render_levels = 2
    bpy.ops.object.shade_smooth()
    return o


def cylinder(name, x, y, r, d, z0, m, verts=96, bevel=0.0):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=d, location=(x, y, z0 + d / 2))
    o = bpy.context.active_object; o.name = name
    if bevel:
        bv = o.modifiers.new('bevel', 'BEVEL'); bv.width = bevel; bv.segments = 4
    bpy.ops.object.shade_smooth()
    o.data.materials.append(m)
    return o


enamel = mat('enamel', lin, 0.38, 0.0, coat=0.55, coat_rough=0.2, bump=0.12)
dark = mat('seat', (0.012, 0.012, 0.014), 0.5)
chrome = mat('chrome', (0.8, 0.8, 0.82), 0.22, 1.0)

# the shell: a bevelled block; the top face is slightly lower at the edge, so the key light draws a soft gradient
shell = rounded_box('shell', W, H, DEPTH, 0.0, 0.13)
shell.data.materials.append(enamel)

TOP = DEPTH
# px layout (origin: top left of the shell) -> units (origin: centre, y up)
def pos(px, py):
    return (px / 100 - W / 2, H / 2 - py / 100)

# knob seats: a shallow dark well with a chrome ring (the knob is laid over it by the screen)
for kx in (28, 70, 112):                 # the three knobs A, B, C of the specification (Pedal k A/B/C)
    x, y = pos(kx, 40)
    cylinder('ring', x, y, 0.175, 0.010, TOP - 0.006, chrome, bevel=0.003)
    cylinder('seat', x, y, 0.158, 0.012, TOP - 0.004, dark)

# LED seat
x, y = pos(70, 133)
cylinder('ledring', x, y, 0.095, 0.012, TOP - 0.006, chrome, bevel=0.004)
cylinder('ledseat', x, y, 0.068, 0.014, TOP - 0.004, dark)

# foot-switch plate
x, y = pos(70, 194)
cylinder('plate', x, y, 0.275, 0.012, TOP - 0.006, chrome, bevel=0.004)
cylinder('plate2', x, y, 0.245, 0.014, TOP - 0.004, dark)

# ground-level catcher is not needed (transparent film); the screen draws the drop shadow

# lights: key top left (large, soft), fill bottom right, rim from behind
def area(name, loc, energy, size, color=(1, 1, 1)):
    bpy.ops.object.light_add(type='AREA', location=loc)
    l = bpy.context.active_object; l.name = name
    l.data.energy = energy; l.data.size = size; l.data.color = color
    d = Vector((0, 0, 0.3)) - Vector(loc)
    l.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    return l

area('key', (-3.6, 3.8, 3.4), 640, 2.6, (1.0, 0.97, 0.92))
area('fill', (3.4, -3.6, 2.4), 110, 3.0, (0.85, 0.92, 1.0))
area('rim', (0.0, 4.2, 1.6), 140, 1.6, (1, 1, 1))

# camera: straight down, orthographic, framed on the shell with its margin
bpy.ops.object.camera_add(location=(0, 0, 6))
cam = bpy.context.active_object
cam.data.type = 'ORTHO'
cam.data.ortho_scale = max(W + 2 * PAD, H + 2 * PAD) if (W + 2 * PAD) > (H + 2 * PAD) else (H + 2 * PAD)
scene.camera = cam

scene.render.filepath = out
bpy.ops.render.render(write_still=True)
print('rendered', out)
