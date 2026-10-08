"""SW AUDIO — Blender render of the VU meter bezel (Blender 4.0.2, Cycles, CPU).
  blender -b -P tools/blender/vu_bezel.py -- <out.png> [samples]
A 300 x 182 px frame (10 px wide) around a transparent window: black plastic, a thin chrome lip on the inside, and the shadow the frame casts on the
meter face (a shadow catcher). The screen stretches it with border-image (slice 30 of 900 px = 10 px), so every VU meter size uses the same file.
Studio as the other parts: key top left, fill bottom right, rim from behind."""
import sys
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
out = argv[0]; samples = int(argv[1]) if len(argv) > 1 else 128
SC = 3; W, H, FR = 3.00, 1.82, 0.10
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = samples; scene.cycles.use_denoising = False
scene.render.film_transparent = True
scene.render.resolution_x = int(W * 100 * SC); scene.render.resolution_y = int(H * 100 * SC)
scene.render.image_settings.file_format = 'PNG'; scene.render.image_settings.color_mode = 'RGBA'
world = bpy.data.worlds.new('w'); scene.world = world; world.use_nodes = True
bg = world.node_tree.nodes['Background']; bg.inputs[0].default_value = (0.05, 0.05, 0.055, 1); bg.inputs[1].default_value = 0.5

def mat(name, color, rough, metal=0.0, coat=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True; p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (*color, 1); p.inputs['Roughness'].default_value = rough; p.inputs['Metallic'].default_value = metal
    p.inputs['Coat Weight'].default_value = coat; p.inputs['Coat Roughness'].default_value = 0.2
    return m
plastic = mat('plastic', (0.006, 0.006, 0.007), 0.5, 0.0, coat=0.2)
chrome = mat('chrome', (0.8, 0.8, 0.82), 0.18, 1.0)

def box(w, h, d, z0, m, bevel=0.0, loc=(0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(loc[0], loc[1], z0 + d / 2)); o = bpy.context.active_object
    o.scale = (w, h, d); bpy.ops.object.transform_apply(scale=True)
    if bevel:
        b = o.modifiers.new('b', 'BEVEL'); b.width = bevel; b.segments = 5
    bpy.ops.object.shade_smooth(); o.data.materials.append(m); return o

# the frame = outer block minus the window (boolean), chamfered
outer = box(W, H, 0.16, 0.0, plastic, 0.05)
cut = box(W - 2 * FR, H - 2 * FR, 0.6, -0.2, plastic, 0.02)
bo = outer.modifiers.new('cut', 'BOOLEAN'); bo.operation = 'DIFFERENCE'; bo.object = cut; bo.solver = 'EXACT'
bpy.context.view_layer.objects.active = outer
bpy.ops.object.modifier_apply(modifier='cut'); bpy.data.objects.remove(cut, do_unlink=True)
# the chrome lip: a thin ring around the window, just inside the frame
lip_o = box(W - 2 * FR + 0.03, H - 2 * FR + 0.03, 0.05, 0.12, chrome, 0.012)
lip_i = box(W - 2 * FR - 0.015, H - 2 * FR - 0.015, 0.4, -0.1, chrome)
bo = lip_o.modifiers.new('cut', 'BOOLEAN'); bo.operation = 'DIFFERENCE'; bo.object = lip_i; bo.solver = 'EXACT'
bpy.context.view_layer.objects.active = lip_o
bpy.ops.object.modifier_apply(modifier='cut'); bpy.data.objects.remove(lip_i, do_unlink=True)

# the meter face below the frame: it only catches the shadow of the frame
bpy.ops.mesh.primitive_plane_add(size=1, location=(0, 0, -0.10)); face = bpy.context.active_object
face.scale = (W - 2 * FR + 0.02, H - 2 * FR + 0.02, 1); face.is_shadow_catcher = True

def area(loc, energy, size, color=(1, 1, 1)):
    bpy.ops.object.light_add(type='AREA', location=loc); l = bpy.context.active_object
    l.data.energy = energy; l.data.size = size; l.data.color = color
    l.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
area((-3.2, 3.2, 3.0), 700, 2.4, (1.0, 0.97, 0.92)); area((3.0, -3.0, 2.0), 140, 3.0, (0.85, 0.92, 1.0)); area((0, 4.0, 1.4), 200, 1.6)
bpy.ops.object.camera_add(location=(0, 0, 8)); cam = bpy.context.active_object; cam.data.type = 'ORTHO'; cam.data.ortho_scale = W; scene.camera = cam
scene.render.filepath = out; bpy.ops.render.render(write_still=True)
