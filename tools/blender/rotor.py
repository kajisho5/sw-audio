"""SW AUDIO — Blender render of the two rotors of the rotary speaker MD05, seen from above (Blender 4.0.2, Cycles, CPU).
  blender -b -P tools/blender/rotor.py -- <horn|drum> <out.png> [samples]
horn: a two-bell horn rotor (a bar with two flared bells); drum: the baffle drum (a ring with an open scoop). The screen turns the image
(40 px, drawn at 6x), so the light is soft and from the top (see RENDERING.md)."""
import sys, math
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
kind, out = argv[0], argv[1]; samples = int(argv[2]) if len(argv) > 2 else 128
SC = 6; D = 0.40
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = samples; scene.cycles.use_denoising = False
scene.render.film_transparent = True
res = int(D * 100 * SC); scene.render.resolution_x = res; scene.render.resolution_y = res
scene.render.image_settings.file_format = 'PNG'; scene.render.image_settings.color_mode = 'RGBA'
world = bpy.data.worlds.new('w'); scene.world = world; world.use_nodes = True
bg = world.node_tree.nodes['Background']; bg.inputs[0].default_value = (0.5, 0.5, 0.52, 1); bg.inputs[1].default_value = 0.6

def mat(name, color, rough, metal=0.0, coat=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True; p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (*color, 1); p.inputs['Roughness'].default_value = rough; p.inputs['Metallic'].default_value = metal
    p.inputs['Coat Weight'].default_value = coat
    return m
wood = mat('wood', (0.17, 0.065, 0.022), 0.45, 0.0, coat=0.4)
black = mat('black', (0.015, 0.015, 0.017), 0.55)
brass = mat('brass', (0.75, 0.52, 0.2), 0.3, 1.0)
R = D / 2

def cyl(r, d, z0, m, verts=96, loc=(0, 0), r2=None):
    if r2 is None:
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=d, location=(loc[0], loc[1], z0 + d / 2))
    else:
        bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=r2, depth=d, location=(loc[0], loc[1], z0 + d / 2))
    o = bpy.context.active_object; bpy.ops.object.shade_smooth(); o.data.materials.append(m); return o

if kind == 'horn':
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0.05)); bar = bpy.context.active_object      # the throat bar
    bar.scale = (R * 1.5, R * 0.22, 0.08); bpy.ops.object.transform_apply(scale=True); bar.data.materials.append(wood)
    for sgn in (-1, 1):                                    # two flared bells, mouth facing outwards
        bpy.ops.mesh.primitive_cone_add(vertices=64, radius1=R * 0.30, radius2=R * 0.62, depth=R * 0.55, location=(sgn * R * 0.68, 0, 0.06))
        b = bpy.context.active_object; b.rotation_euler = (0, math.radians(90 * sgn), 0); bpy.ops.object.shade_smooth(); b.data.materials.append(wood)
        cyl(R * 0.50, 0.01, 0.0, black, 48, loc=(sgn * R * 0.9, 0))   # the dark mouth
    cyl(R * 0.18, 0.12, 0.0, brass, 48)                       # hub
else:
    # the drum: a ring with an open scoop (a quarter of the wall is missing) and a baffle plate
    for k in range(48):
        a = math.radians(360 * k / 48)
        if 20 < (360 * k / 48) < 110:
            continue
        bpy.ops.mesh.primitive_cube_add(size=1, location=(math.cos(a) * R * 0.88, math.sin(a) * R * 0.88, 0.06)); w = bpy.context.active_object
        w.scale = (R * 0.07, R * 0.16, 0.12); w.rotation_euler = (0, 0, a + math.pi / 2); bpy.ops.object.transform_apply(scale=True); w.data.materials.append(wood)
    cyl(R * 0.80, 0.015, 0.0, black, 96)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0.05)); bf = bpy.context.active_object      # baffle
    bf.scale = (R * 1.5, R * 0.06, 0.09); bf.rotation_euler = (0, 0, math.radians(55)); bpy.ops.object.transform_apply(scale=True); bf.data.materials.append(wood)
    cyl(R * 0.14, 0.14, 0.0, brass, 48)

def area(loc, energy, size):
    bpy.ops.object.light_add(type='AREA', location=loc); l = bpy.context.active_object
    l.data.energy = energy; l.data.size = size
    l.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
area((0, 0, 3.0), 230, 3.0); area((-1.6, 1.6, 1.6), 70, 1.6)
bpy.ops.object.camera_add(location=(0, 0, 6)); cam = bpy.context.active_object; cam.data.type = 'ORTHO'; cam.data.ortho_scale = D; scene.camera = cam
scene.render.filepath = out; bpy.ops.render.render(write_still=True)
