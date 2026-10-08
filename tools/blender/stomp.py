"""SW AUDIO — Blender render of the chrome foot-switch cap of the GT03 pedals (Blender 4.0.2, Cycles, CPU).
  blender -b -P tools/blender/stomp.py -- <out.png> [samples]
Same studio as tools/blender/pedal.py. 44 px on the screen, drawn at 4x (176 px) with room for the soft shadow."""
import sys
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
out = argv[0]; samples = int(argv[1]) if len(argv) > 1 else 128
SC = 4; D = 0.44; PAD = 0.07
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = samples; scene.cycles.use_denoising = False
scene.render.film_transparent = True
res = int(round((D + 2 * PAD) * 100 * SC)); scene.render.resolution_x = res; scene.render.resolution_y = res
scene.render.image_settings.file_format = 'PNG'; scene.render.image_settings.color_mode = 'RGBA'
world = bpy.data.worlds.new('w'); scene.world = world; world.use_nodes = True
bg = world.node_tree.nodes['Background']; bg.inputs[0].default_value = (0.06, 0.06, 0.065, 1); bg.inputs[1].default_value = 0.8

def mat(name, base, rough, metal):
    m = bpy.data.materials.new(name); m.use_nodes = True; p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (*base, 1); p.inputs['Roughness'].default_value = rough; p.inputs['Metallic'].default_value = metal
    return m
chrome = mat('chrome', (0.85, 0.85, 0.87), 0.16, 1.0)

# a round cap with a rolled edge and a slightly dished top
bpy.ops.mesh.primitive_cylinder_add(vertices=128, radius=D / 2, depth=0.16, location=(0, 0, 0.08))
cap = bpy.context.active_object
bv = cap.modifiers.new('b', 'BEVEL'); bv.width = 0.05; bv.segments = 8
bpy.ops.object.shade_smooth(); cap.data.materials.append(chrome)
bpy.ops.mesh.primitive_cylinder_add(vertices=128, radius=D / 2 - 0.07, depth=0.01, location=(0, 0, 0.158))
dish = bpy.context.active_object; bpy.ops.object.shade_smooth(); dish.data.materials.append(mat('dish', (0.55, 0.55, 0.58), 0.3, 1.0))

def area(loc, energy, size, color):
    bpy.ops.object.light_add(type='AREA', location=loc); l = bpy.context.active_object
    l.data.energy = energy; l.data.size = size; l.data.color = color
    l.rotation_euler = (Vector((0, 0, 0.1)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
area((-2.6, 2.8, 2.6), 380, 2.0, (1.0, 0.97, 0.92)); area((2.4, -2.6, 1.8), 80, 2.0, (0.85, 0.92, 1.0)); area((0, 3.0, 1.0), 110, 1.2, (1, 1, 1))
bpy.ops.object.camera_add(location=(0, 0, 5)); cam = bpy.context.active_object; cam.data.type = 'ORTHO'; cam.data.ortho_scale = D + 2 * PAD; scene.camera = cam
scene.render.filepath = out; bpy.ops.render.render(write_still=True)
