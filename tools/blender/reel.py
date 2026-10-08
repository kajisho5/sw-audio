"""SW AUDIO — Blender render of a tape reel (Blender 4.0.2, Cycles, CPU), for DL02 and SA01.
  blender -b -P tools/blender/reel.py -- <out.png> [samples]
A brushed-aluminium flange with three round windows (the tape pack shows through), a rolled rim, a hub with three drive notches and a spindle hole.
48 px on the screen, drawn at 6x. The screen turns this image, so the light is a soft top light with no strong highlight (a lit image must not be
turned: see RENDERING.md); a fixed highlight is laid over it by the screen."""
import sys, math
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
out = argv[0]; samples = int(argv[1]) if len(argv) > 1 else 128
SC = 6; D = 0.48
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = samples; scene.cycles.use_denoising = False
scene.render.film_transparent = True
res = int(D * 100 * SC); scene.render.resolution_x = res; scene.render.resolution_y = res
scene.render.image_settings.file_format = 'PNG'; scene.render.image_settings.color_mode = 'RGBA'
world = bpy.data.worlds.new('w'); scene.world = world; world.use_nodes = True
bg = world.node_tree.nodes['Background']; bg.inputs[0].default_value = (0.5, 0.5, 0.52, 1); bg.inputs[1].default_value = 0.7   # an even sky: soft, no hot spot

def mat(name, color, rough, metal):
    m = bpy.data.materials.new(name); m.use_nodes = True; p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (*color, 1); p.inputs['Roughness'].default_value = rough; p.inputs['Metallic'].default_value = metal
    return m
alu = mat('alu', (0.30, 0.30, 0.32), 0.5, 0.85)
# brushed: concentric (radial noise stretched along the angle is hard; a fine noise bump is enough at 48 px)
nt = alu.node_tree; p = nt.nodes['Principled BSDF']
noise = nt.nodes.new('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value = 500; noise.inputs['Detail'].default_value = 3
bm = nt.nodes.new('ShaderNodeBump'); bm.inputs['Strength'].default_value = 0.15; bm.inputs['Distance'].default_value = 0.002
nt.links.new(noise.outputs['Fac'], bm.inputs['Height']); nt.links.new(bm.outputs['Normal'], p.inputs['Normal'])
dark = mat('dark', (0.02, 0.02, 0.022), 0.5, 0.0)

def cyl(r, d, z0, m, verts=128, bevel=0.0, loc=(0, 0)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=d, location=(loc[0], loc[1], z0 + d / 2)); o = bpy.context.active_object
    if bevel:
        b = o.modifiers.new('b', 'BEVEL'); b.width = bevel; b.segments = 4
    bpy.ops.object.shade_smooth(); o.data.materials.append(m); return o

def cut(target, tool):
    b = target.modifiers.new('cut', 'BOOLEAN'); b.operation = 'DIFFERENCE'; b.object = tool; b.solver = 'EXACT'
    bpy.context.view_layer.objects.active = target; bpy.ops.object.modifier_apply(modifier='cut'); bpy.data.objects.remove(tool, do_unlink=True)

R = D / 2
# an open reel: a rolled rim (torus), three spokes, a hub with three drive notches and a spindle hole; the gaps are transparent (the tape pack shows through)
bpy.ops.mesh.primitive_torus_add(major_radius=R * 0.93, minor_radius=R * 0.075, major_segments=128, minor_segments=24); rim = bpy.context.active_object
rim.scale = (1, 1, 0.55); bpy.ops.object.shade_smooth(); rim.data.materials.append(alu)
bpy.ops.mesh.primitive_torus_add(major_radius=R * 0.80, minor_radius=R * 0.02, major_segments=128, minor_segments=12, location=(0, 0, 0.0)); lip = bpy.context.active_object
lip.scale = (1, 1, 0.6); bpy.ops.object.shade_smooth(); lip.data.materials.append(alu)
for k in range(3):
    a_ = math.radians(90 + 120 * k)
    L = R * 0.93 - R * 0.20
    bpy.ops.mesh.primitive_cube_add(size=1, location=(math.cos(a_) * (R * 0.20 + L / 2), math.sin(a_) * (R * 0.20 + L / 2), 0.0)); sp = bpy.context.active_object
    sp.scale = (L, R * 0.16, 0.026); sp.rotation_euler = (0, 0, a_); bpy.ops.object.transform_apply(scale=True)
    bv = sp.modifiers.new('b', 'BEVEL'); bv.width = 0.008; bv.segments = 3
    bpy.ops.object.shade_smooth(); sp.data.materials.append(alu)
hub = cyl(R * 0.27, 0.05, -0.025, alu, 96, bevel=0.01)
spin = cyl(R * 0.075, 0.07, -0.035, dark, 48)
for k in range(3):
    a_ = math.radians(30 + 120 * k)
    cyl(R * 0.04, 0.06, -0.03, dark, 24, loc=(math.cos(a_) * R * 0.19, math.sin(a_) * R * 0.19))

def area(loc, energy, size):
    bpy.ops.object.light_add(type='AREA', location=loc); l = bpy.context.active_object
    l.data.energy = energy; l.data.size = size
    l.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
area((0, 0, 3.0), 300, 3.0); area((-1.8, 1.8, 1.6), 90, 1.6)
bpy.ops.object.camera_add(location=(0, 0, 6)); cam = bpy.context.active_object; cam.data.type = 'ORTHO'; cam.data.ortho_scale = D; scene.camera = cam
scene.render.filepath = out; bpy.ops.render.render(write_still=True)
