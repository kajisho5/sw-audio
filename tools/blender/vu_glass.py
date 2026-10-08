"""SW AUDIO — Blender render of the glass over a VU meter face (Blender 4.0.2, Cycles, CPU).
  blender -b -P tools/blender/vu_glass.py -- <out.png> [samples]
A slightly domed pane of glass: transparent where it does not reflect (alpha comes from the Fresnel mix of a transparent and a glossy shader),
so only the reflections of the studio's soft boxes remain: a broad soft sheen from the top left, a thin bright strip, a faint warm fill at the bottom
right and the darkening toward the rim. 280 x 162 px on the screen (the largest face), drawn at 3x; stretched to the other sizes."""
import sys
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
out = argv[0]; samples = int(argv[1]) if len(argv) > 1 else 160
SC = 3; W, H = 2.80, 1.62
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = samples; scene.cycles.use_denoising = False
scene.render.film_transparent = True
scene.render.resolution_x = int(W * 100 * SC); scene.render.resolution_y = int(H * 100 * SC)
scene.render.image_settings.file_format = 'PNG'; scene.render.image_settings.color_mode = 'RGBA'
scene.view_settings.view_transform = 'Standard'
world = bpy.data.worlds.new('w'); scene.world = world; world.use_nodes = True
bg = world.node_tree.nodes['Background']; bg.inputs[0].default_value = (0, 0, 0, 1); bg.inputs[1].default_value = 0.0

# the pane: a plane with a gentle dome
bpy.ops.mesh.primitive_grid_add(x_subdivisions=96, y_subdivisions=56, size=1); pane = bpy.context.active_object
pane.scale = (W, H, 1); bpy.ops.object.transform_apply(scale=True)
me = pane.data
for v in me.vertices:
    nx, ny = v.co.x / (W / 2), v.co.y / (H / 2)
    v.co.z = 0.16 * (1 - 0.5 * (nx * nx + ny * ny))                 # a dome: normals tilt up to about 15 degrees at the edge
me.update()
bpy.ops.object.shade_smooth()
m = bpy.data.materials.new('glass'); m.use_nodes = True; nt = m.node_tree
for n in list(nt.nodes):
    nt.nodes.remove(n)
outn = nt.nodes.new('ShaderNodeOutputMaterial')
tr = nt.nodes.new('ShaderNodeBsdfTransparent')
gl = nt.nodes.new('ShaderNodeBsdfGlossy'); gl.inputs['Roughness'].default_value = 0.03
fr = nt.nodes.new('ShaderNodeFresnel'); fr.inputs['IOR'].default_value = 3.2
mix = nt.nodes.new('ShaderNodeMixShader')
nt.links.new(fr.outputs['Fac'], mix.inputs['Fac']); nt.links.new(tr.outputs['BSDF'], mix.inputs[1]); nt.links.new(gl.outputs['BSDF'], mix.inputs[2])
nt.links.new(mix.outputs['Shader'], outn.inputs['Surface'])
pane.data.materials.append(m)

def softbox(name, loc, size_x, size_y, strength, color=(1, 1, 1), target=(0, 0, 0), falloff=False):
    bpy.ops.mesh.primitive_plane_add(size=1, location=loc); p = bpy.context.active_object; p.name = name
    p.scale = (size_x, size_y, 1)
    p.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('Z', 'Y').to_euler()
    mt = bpy.data.materials.new(name); mt.use_nodes = True; n = mt.node_tree
    for x in list(n.nodes):
        n.nodes.remove(x)
    o = n.nodes.new('ShaderNodeOutputMaterial'); e = n.nodes.new('ShaderNodeEmission')
    e.inputs['Strength'].default_value = strength; e.inputs['Color'].default_value = (*color, 1)
    if falloff:                      # bright along one edge, fading across the box (a gradient, so the reflection is a soft sheen, not a hard patch)
        tc = n.nodes.new('ShaderNodeTexCoord'); g = n.nodes.new('ShaderNodeTexGradient'); g.gradient_type = 'LINEAR'
        mp = n.nodes.new('ShaderNodeMapping'); mp.inputs['Location'].default_value = (0.5, 0.5, 0)
        mr = n.nodes.new('ShaderNodeMath'); mr.operation = 'MULTIPLY'; mr.inputs[1].default_value = strength
        n.links.new(tc.outputs['UV'], mp.inputs['Vector']); n.links.new(mp.outputs['Vector'], g.inputs['Vector'])
        pw = n.nodes.new('ShaderNodeMath'); pw.operation = 'POWER'; pw.inputs[1].default_value = 2.0
        n.links.new(g.outputs['Fac'], pw.inputs[0]); n.links.new(pw.outputs['Value'], mr.inputs[0]); n.links.new(mr.outputs['Value'], e.inputs['Strength'])
    n.links.new(e.outputs['Emission'], o.inputs['Surface']); p.data.materials.append(mt)
    p.visible_camera = False                                          # seen only in the reflection

softbox('sheen', (-1.4, 1.1, 3.0), 4.0, 3.0, 9.0, (1.0, 0.98, 0.94), falloff=True)   # broad, top left, fading toward the middle
softbox('strip', (-0.9, 1.5, 3.2), 3.4, 0.10, 90.0)                                  # a thin bright strip across the top
softbox('warm', (1.6, -1.4, 3.0), 2.6, 1.6, 2.2, (1.0, 0.85, 0.65), falloff=True)    # faint warm, bottom right
bpy.ops.object.camera_add(location=(0, 0, 6)); cam = bpy.context.active_object; cam.data.type = 'ORTHO'; cam.data.ortho_scale = W; scene.camera = cam
scene.render.filepath = out; bpy.ops.render.render(write_still=True)
