"""SW AUDIO — Blender render of the small black pedal knob of the GT03 screen (Blender 4.0.2, Cycles, CPU).
  blender -b -P tools/blender/knob.py -- <out.png> [samples]
Transparent background and no baked shadow (the screen adds a drop shadow), so it sits on any enamel colour. The pointer is not part of the
image: the screen turns a pointer line, the knob itself is never rotated (the light would turn with it). 30 px on the screen, drawn at 6x."""
import sys, math
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
out = argv[0]; samples = int(argv[1]) if len(argv) > 1 else 160
SC = 6; D = 0.30; PAD = 0.0
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = samples; scene.cycles.use_denoising = False
scene.render.film_transparent = True
res = int(round(D * 100 * SC)); scene.render.resolution_x = res; scene.render.resolution_y = res
scene.render.image_settings.file_format = 'PNG'; scene.render.image_settings.color_mode = 'RGBA'
world = bpy.data.worlds.new('w'); scene.world = world; world.use_nodes = True
bg = world.node_tree.nodes['Background']; bg.inputs[0].default_value = (0.07, 0.07, 0.075, 1); bg.inputs[1].default_value = 0.9

def mat(name, base, rough, metal, coat=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True; p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (*base, 1); p.inputs['Roughness'].default_value = rough; p.inputs['Metallic'].default_value = metal
    p.inputs['Coat Weight'].default_value = coat; p.inputs['Coat Roughness'].default_value = 0.12
    return m
black = mat('black', (0.012, 0.012, 0.014), 0.32, 0.0, coat=0.6)
ring = mat('ring', (0.8, 0.8, 0.82), 0.2, 1.0)

R = D / 2
# skirt with 60 knurls, a metal ring, the cap
bpy.ops.mesh.primitive_cylinder_add(vertices=60, radius=R * 0.98, depth=0.09, location=(0, 0, 0.045))
sk = bpy.context.active_object; sk.data.materials.append(black)
bv = sk.modifiers.new('b', 'BEVEL'); bv.width = 0.012; bv.segments = 3
bpy.ops.object.shade_smooth()
bpy.ops.mesh.primitive_cylinder_add(vertices=96, radius=R * 0.80, depth=0.10, location=(0, 0, 0.07))
rg = bpy.context.active_object; rg.data.materials.append(ring)
bv = rg.modifiers.new('b', 'BEVEL'); bv.width = 0.006; bv.segments = 3
bpy.ops.object.shade_smooth()
bpy.ops.mesh.primitive_cylinder_add(vertices=96, radius=R * 0.72, depth=0.11, location=(0, 0, 0.078))
cp = bpy.context.active_object; cp.data.materials.append(black)
bv = cp.modifiers.new('b', 'BEVEL'); bv.width = 0.022; bv.segments = 6
bpy.ops.object.shade_smooth()

def area(loc, energy, size, color):
    bpy.ops.object.light_add(type='AREA', location=loc); l = bpy.context.active_object
    l.data.energy = energy; l.data.size = size; l.data.color = color
    l.rotation_euler = (Vector((0, 0, 0.1)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
area((-2.4, 2.6, 2.4), 300, 1.8, (1.0, 0.97, 0.92)); area((2.2, -2.4, 1.6), 60, 1.8, (0.85, 0.92, 1.0)); area((0, 2.8, 0.9), 90, 1.0, (1, 1, 1))
bpy.ops.object.camera_add(location=(0, 0, 4)); cam = bpy.context.active_object; cam.data.type = 'ORTHO'; cam.data.ortho_scale = D; scene.camera = cam
scene.render.filepath = out; bpy.ops.render.render(write_still=True)
