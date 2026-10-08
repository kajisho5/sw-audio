"""SW AUDIO — Blender renders for the IN07 Orbital screen (Blender 4.0.2, Cycles, CPU).
  blender -b -P tools/blender/in07_orbital.py -- core  <out.png> [samples]   the sound core (planet), 640 px, transparent
  blender -b -P tools/blender/in07_orbital.py -- moon  <out.png> [samples]   a layer body / moon, 192 px, transparent
  blender -b -P tools/blender/in07_orbital.py -- daycore <out.png> [samples] [porcelain|armillary|frost]  the core for light screens:
      a white porcelain world with lit teal seams (porcelain, used), 640 px, transparent
  blender -b -P tools/blender/in07_orbital.py -- hero  <out.png> [samples]   the whole system for the sales material, 1920 x 1080
  blender -b -P tools/blender/in07_orbital.py -- body <kind> <out-prefix> [samples] [frames]
      one orbiting body, spinning on its own axis: <out-prefix>_00.png .. one frame per 360/frames degrees (default 96), transparent,
      96 x 96 (ring 160 x 160: displayed up to about 70 px, so 2x for high-density screens).
      kinds: ring (L1, banded with a ring), pearl (L2), crater (L3), crystal (L4, faceted glass with a glowing core),
      lfo (the LFO moon), bead (a unison moonlet); the FX stations fx_drive, fx_chorus, fx_delay, fx_reverb, fx_eq, fx_limit
      (128 x 128). Pack the frames with tools/blender/pack_sheet.py.
      The body turns under fixed lights (the surface moves, the light stays top left), so the screen may step through the
      frames; it still never rotates the image itself.
The sprites are lit from the top left (key) with a weak bottom-right fill and a rim, like every SW AUDIO part. The screen moves them along
their orbits but never turns them, so the light stays where it is. Orbits, trails and the halo are drawn by the screen (they follow the values).
Denoise afterwards with tools/blender/post.py (this Blender build has no OpenImageDenoise)."""
import sys, math, random
import bmesh
import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
mode = argv[0]
if mode == 'body':
    kind, out = argv[1], argv[2]
    samples = int(argv[3]) if len(argv) > 3 else 64
    frames = int(argv[4]) if len(argv) > 4 else 96   # 96 steps a turn: the surface moves under 1 px a step on screen (smooth at 60 fps)
else:
    out = argv[1]
    samples = int(argv[2]) if len(argv) > 2 else 96
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = samples; scene.cycles.use_denoising = False
scene.view_settings.view_transform = 'Standard'
scene.render.image_settings.file_format = 'PNG'; scene.render.image_settings.color_mode = 'RGBA'
world = bpy.data.worlds.new('w'); scene.world = world; world.use_nodes = True
wnt = world.node_tree; wbg = wnt.nodes['Background']; wbg.inputs[1].default_value = 1.0
wtc = wnt.nodes.new('ShaderNodeTexCoord'); wsep = wnt.nodes.new('ShaderNodeSeparateXYZ'); wramp = wnt.nodes.new('ShaderNodeValToRGB')
wnt.links.new(wtc.outputs['Generated'], wsep.inputs[0]); wnt.links.new(wsep.outputs['Z'], wramp.inputs['Fac'])
wramp.color_ramp.elements[0].position = 0.45; wramp.color_ramp.elements[0].color = (0.003, 0.005, 0.005, 1)
wramp.color_ramp.elements[1].position = 0.95; wramp.color_ramp.elements[1].color = (0.16, 0.20, 0.19, 1)
wnt.links.new(wramp.outputs['Color'], wbg.inputs[0])
# the camera sees black (the core is laid over the screen with a screen blend); reflections see the sky
wlp = wnt.nodes.new('ShaderNodeLightPath'); wblack = wnt.nodes.new('ShaderNodeBackground'); wblack.inputs[0].default_value = (0, 0, 0, 1)
wmix = wnt.nodes.new('ShaderNodeMixShader')
wnt.links.new(wlp.outputs['Is Camera Ray'], wmix.inputs['Fac'])
wnt.links.new(wbg.outputs[0], wmix.inputs[1]); wnt.links.new(wblack.outputs[0], wmix.inputs[2])
wnt.links.new(wmix.outputs[0], wnt.nodes['World Output'].inputs['Surface'])

TEAL = (0.0255, 0.637, 0.351)        # #3fd1a0 in linear
TEAL_HI = (0.266, 0.745, 0.546)      # #8de0c3
TEAL_DEEP = (0.0037, 0.102, 0.0545)  # #0b5a41


def node(nt, kind, **inputs):
    n = nt.nodes.new(kind)
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


def glass_material():
    """a thin glass shell (used on a solidified sphere: no lens effect, just reflections and a slight tint)"""
    m = bpy.data.materials.new('glass'); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != 'OUTPUT_MATERIAL':
            nt.nodes.remove(n)
    g = node(nt, 'ShaderNodeBsdfGlass', Color=(0.93, 1.0, 0.97, 1), Roughness=0.01, IOR=1.45)
    nt.links.new(g.outputs[0], nt.nodes['Material Output'].inputs['Surface'])
    return m


def glow_volume(strength, radius, power=3.0):
    """a soft bright nucleus: emission falling off from the centre, almost no density"""
    m = bpy.data.materials.new('glow'); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != 'OUTPUT_MATERIAL':
            nt.nodes.remove(n)
    vol = node(nt, 'ShaderNodeEmission', Color=(0.55, 1.0, 0.82, 1))
    tc = nt.nodes.new('ShaderNodeTexCoord')
    ln = nt.nodes.new('ShaderNodeVectorMath'); ln.operation = 'LENGTH'
    nt.links.new(tc.outputs['Object'], ln.inputs[0])
    inv = nt.nodes.new('ShaderNodeMapRange'); inv.inputs['From Max'].default_value = radius
    inv.inputs['To Min'].default_value = 1.0; inv.inputs['To Max'].default_value = 0.0
    nt.links.new(ln.outputs['Value'], inv.inputs['Value'])
    pw = nt.nodes.new('ShaderNodeMath'); pw.operation = 'POWER'; pw.inputs[1].default_value = power
    nt.links.new(inv.outputs['Result'], pw.inputs[0])
    mul = nt.nodes.new('ShaderNodeMath'); mul.operation = 'MULTIPLY'; mul.inputs[1].default_value = strength
    nt.links.new(pw.outputs['Value'], mul.inputs[0]); nt.links.new(mul.outputs['Value'], vol.inputs['Strength'])
    nt.links.new(vol.outputs[0], nt.nodes['Material Output'].inputs['Volume'])
    return m


def shell(r, loc, mat, thick=0.025):
    o = sphere(r, loc, mat, 160)
    sm = o.modifiers.new('s', 'SOLIDIFY'); sm.thickness = thick; sm.offset = -1.0
    return o


def nebula_material(strength, radius):
    """the sound inside: an emissive volume, densest at the centre, broken up by noise"""
    m = bpy.data.materials.new('nebula'); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != 'OUTPUT_MATERIAL':
            nt.nodes.remove(n)
    outn = nt.nodes['Material Output']
    vol = node(nt, 'ShaderNodeEmission', Color=(*TEAL_HI, 1))
    tc = nt.nodes.new('ShaderNodeTexCoord')
    noise = node(nt, 'ShaderNodeTexNoise', Scale=2.6, Detail=8.0, Roughness=0.62, Distortion=0.9)
    nt.links.new(tc.outputs['Object'], noise.inputs['Vector'])
    # radial falloff: 1 at the centre, 0 at radius 1 (object space)
    ln = nt.nodes.new('ShaderNodeVectorMath'); ln.operation = 'LENGTH'
    nt.links.new(tc.outputs['Object'], ln.inputs[0])
    fall = nt.nodes.new('ShaderNodeMapRange'); fall.inputs['From Min'].default_value = 0.0; fall.inputs['From Max'].default_value = radius
    fall.inputs['To Min'].default_value = 1.0; fall.inputs['To Max'].default_value = 0.0
    nt.links.new(ln.outputs['Value'], fall.inputs['Value'])
    ramp = nt.nodes.new('ShaderNodeValToRGB'); cr = ramp.color_ramp
    cr.elements[0].position = 0.42; cr.elements[0].color = (0, 0, 0, 1)
    cr.elements[1].position = 0.78; cr.elements[1].color = (1, 1, 1, 1)
    nt.links.new(noise.outputs['Fac'], ramp.inputs['Fac'])
    mul = nt.nodes.new('ShaderNodeMath'); mul.operation = 'MULTIPLY'
    nt.links.new(ramp.outputs['Color'], mul.inputs[0]); nt.links.new(fall.outputs['Result'], mul.inputs[1])
    em = nt.nodes.new('ShaderNodeMath'); em.operation = 'MULTIPLY'; em.inputs[1].default_value = strength
    nt.links.new(mul.outputs['Value'], em.inputs[0]); nt.links.new(em.outputs['Value'], vol.inputs['Strength'])
    nt.links.new(vol.outputs[0], outn.inputs['Volume'])
    return m


def moon_material():
    m = bpy.data.materials.new('moon'); m.use_nodes = True; p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (0.55, 0.72, 0.66, 1)
    p.inputs['Subsurface Weight'].default_value = 0.35
    p.inputs['Subsurface Radius'].default_value = (0.2, 0.6, 0.45)
    p.inputs['Roughness'].default_value = 0.28
    p.inputs['Coat Weight'].default_value = 0.7; p.inputs['Coat Roughness'].default_value = 0.05
    p.inputs['Emission Color'].default_value = (*TEAL_HI, 1); p.inputs['Emission Strength'].default_value = 0.25
    return m


def principled(name):
    m = bpy.data.materials.new(name); m.use_nodes = True
    return m, m.node_tree, m.node_tree.nodes['Principled BSDF']


def pearl_material(emission=0.25, marble=True):
    """the L2 / LFO / moonlet body: pearl with a faint marbling (so its turning shows)"""
    m, nt, p = principled('pearl')
    p.inputs['Subsurface Weight'].default_value = 0.35
    p.inputs['Subsurface Radius'].default_value = (0.2, 0.6, 0.45)
    p.inputs['Roughness'].default_value = 0.28
    p.inputs['Coat Weight'].default_value = 0.7; p.inputs['Coat Roughness'].default_value = 0.05
    p.inputs['Emission Color'].default_value = (*TEAL_HI, 1); p.inputs['Emission Strength'].default_value = emission
    if marble:
        tc = nt.nodes.new('ShaderNodeTexCoord')
        nz = node(nt, 'ShaderNodeTexNoise', Scale=3.2, Detail=6.0, Roughness=0.55, Distortion=2.2)
        nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
        ramp = nt.nodes.new('ShaderNodeValToRGB'); cr = ramp.color_ramp
        cr.elements[0].position = 0.35; cr.elements[0].color = (0.46, 0.64, 0.58, 1)
        cr.elements[1].position = 0.70; cr.elements[1].color = (0.70, 0.84, 0.79, 1)
        nt.links.new(nz.outputs['Fac'], ramp.inputs['Fac']); nt.links.new(ramp.outputs['Color'], p.inputs['Base Color'])
    else:
        p.inputs['Base Color'].default_value = (0.55, 0.72, 0.66, 1)
    return m


def banded_material():
    """L1 (wavetable): latitude bands broken up like a gas giant, teal and pearl"""
    m, nt, p = principled('banded')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    wv = nt.nodes.new('ShaderNodeTexWave'); wv.wave_type = 'BANDS'; wv.bands_direction = 'Z'
    wv.inputs['Scale'].default_value = 1.6; wv.inputs['Distortion'].default_value = 7.0
    wv.inputs['Detail'].default_value = 4.0; wv.inputs['Detail Scale'].default_value = 1.4
    nt.links.new(tc.outputs['Object'], wv.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB'); cr = ramp.color_ramp
    cr.elements[0].position = 0.0; cr.elements[0].color = (0.05, 0.22, 0.17, 1)
    cr.elements[1].position = 1.0; cr.elements[1].color = (0.36, 0.62, 0.53, 1)
    e = cr.elements.new(0.45); e.color = (0.62, 0.80, 0.73, 1)
    e = cr.elements.new(0.70); e.color = (0.86, 0.93, 0.90, 1)
    nt.links.new(wv.outputs['Fac'], ramp.inputs['Fac']); nt.links.new(ramp.outputs['Color'], p.inputs['Base Color'])
    p.inputs['Roughness'].default_value = 0.42
    p.inputs['Coat Weight'].default_value = 0.35; p.inputs['Coat Roughness'].default_value = 0.08
    p.inputs['Emission Color'].default_value = (*TEAL, 1); p.inputs['Emission Strength'].default_value = 0.06
    return m


def ring_material(r0, r1):
    """the ring of L1: bands across its width (with a gap), tinted pearl, partly see-through"""
    m, nt, p = principled('ring')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    ln = nt.nodes.new('ShaderNodeVectorMath'); ln.operation = 'LENGTH'
    nt.links.new(tc.outputs['Object'], ln.inputs[0])
    mr = nt.nodes.new('ShaderNodeMapRange'); mr.inputs['From Min'].default_value = r0; mr.inputs['From Max'].default_value = r1
    nt.links.new(ln.outputs['Value'], mr.inputs['Value'])
    ramp = nt.nodes.new('ShaderNodeValToRGB'); cr = ramp.color_ramp
    stops = [(0.0, 0.10), (0.10, 0.70), (0.30, 0.55), (0.36, 0.05), (0.42, 0.05), (0.48, 0.85), (0.72, 0.65), (0.90, 0.30), (1.0, 0.0)]
    cr.elements[0].position, cr.elements[0].color = stops[0][0], (stops[0][1],) * 3 + (1,)
    cr.elements[1].position, cr.elements[1].color = stops[-1][0], (stops[-1][1],) * 3 + (1,)
    for pos, v in stops[1:-1]:
        e = cr.elements.new(pos); e.color = (v, v, v, 1)
    nt.links.new(mr.outputs['Result'], ramp.inputs['Fac'])
    p.inputs['Base Color'].default_value = (0.62, 0.82, 0.75, 1)
    p.inputs['Roughness'].default_value = 0.55
    nt.links.new(ramp.outputs['Color'], p.inputs['Alpha'])
    m.blend_method = 'BLEND'
    return m


def crater_material():
    """L3 (samples, noise): a rocky moon with craters"""
    m, nt, p = principled('crater')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    vo = node(nt, 'ShaderNodeTexVoronoi', Scale=3.4, Randomness=0.9)
    nt.links.new(tc.outputs['Object'], vo.inputs['Vector'])
    bowl = nt.nodes.new('ShaderNodeValToRGB'); cr = bowl.color_ramp      # distance to a crater centre -> height (bowl + rim)
    cr.elements[0].position = 0.0; cr.elements[0].color = (0.15, 0.15, 0.15, 1)
    cr.elements[1].position = 0.55; cr.elements[1].color = (0.6, 0.6, 0.6, 1)
    e = cr.elements.new(0.32); e.color = (1.0, 1.0, 1.0, 1)
    nt.links.new(vo.outputs['Distance'], bowl.inputs['Fac'])
    nz = node(nt, 'ShaderNodeTexNoise', Scale=9.0, Detail=10.0, Roughness=0.6)
    nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
    mix = nt.nodes.new('ShaderNodeMath'); mix.operation = 'MULTIPLY_ADD'
    nt.links.new(nz.outputs['Fac'], mix.inputs[0]); mix.inputs[1].default_value = 0.35
    nt.links.new(bowl.outputs['Color'], mix.inputs[2])
    bump = node(nt, 'ShaderNodeBump', Strength=0.55, Distance=0.08)
    nt.links.new(mix.outputs['Value'], bump.inputs['Height']); nt.links.new(bump.outputs['Normal'], p.inputs['Normal'])
    tone = nt.nodes.new('ShaderNodeValToRGB'); ct = tone.color_ramp
    ct.elements[0].position = 0.3; ct.elements[0].color = (0.20, 0.26, 0.25, 1)
    ct.elements[1].position = 0.75; ct.elements[1].color = (0.52, 0.60, 0.57, 1)
    nt.links.new(nz.outputs['Fac'], tone.inputs['Fac']); nt.links.new(tone.outputs['Color'], p.inputs['Base Color'])
    p.inputs['Roughness'].default_value = 0.85
    return m


def crystal_material():
    """L4 (FM, bells): a faceted gem; the facets catch the key light as it turns"""
    m, nt, p = principled('crystal')
    p.inputs['Base Color'].default_value = (0.30, 0.72, 0.58, 1)
    p.inputs['Transmission Weight'].default_value = 0.35
    p.inputs['Roughness'].default_value = 0.10
    p.inputs['IOR'].default_value = 1.6
    p.inputs['Coat Weight'].default_value = 1.0; p.inputs['Coat Roughness'].default_value = 0.02
    p.inputs['Emission Color'].default_value = (*TEAL_HI, 1); p.inputs['Emission Strength'].default_value = 0.12
    return m


def drive_material():
    """FX DRIVE: dark basalt with glowing teal cracks (heat, in the category colour)"""
    m, nt, p = principled('fxdrive')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    vo = nt.nodes.new('ShaderNodeTexVoronoi'); vo.feature = 'DISTANCE_TO_EDGE'; vo.inputs['Scale'].default_value = 3.2
    nt.links.new(tc.outputs['Object'], vo.inputs['Vector'])
    crack = nt.nodes.new('ShaderNodeValToRGB'); cr = crack.color_ramp
    cr.elements[0].position = 0.0; cr.elements[0].color = (1, 1, 1, 1)
    cr.elements[1].position = 0.022; cr.elements[1].color = (0, 0, 0, 1)
    nt.links.new(vo.outputs['Distance'], crack.inputs['Fac'])
    nz = node(nt, 'ShaderNodeTexNoise', Scale=6.0, Detail=8.0, Roughness=0.6)
    nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
    mask_n = node(nt, 'ShaderNodeTexNoise', Scale=1.6, Detail=2.0, Roughness=0.5)
    nt.links.new(tc.outputs['Object'], mask_n.inputs['Vector'])
    mask = nt.nodes.new('ShaderNodeValToRGB'); mr_ = mask.color_ramp
    mr_.elements[0].position = 0.45; mr_.elements[0].color = (0.08, 0.08, 0.08, 1)
    mr_.elements[1].position = 0.62; mr_.elements[1].color = (1, 1, 1, 1)
    nt.links.new(mask_n.outputs['Fac'], mask.inputs['Fac'])
    hot = nt.nodes.new('ShaderNodeMath'); hot.operation = 'MULTIPLY'
    nt.links.new(crack.outputs['Color'], hot.inputs[0]); nt.links.new(mask.outputs['Color'], hot.inputs[1])
    em = nt.nodes.new('ShaderNodeMath'); em.operation = 'MULTIPLY'; em.inputs[1].default_value = 4.0
    nt.links.new(hot.outputs['Value'], em.inputs[0]); nt.links.new(em.outputs['Value'], p.inputs['Emission Strength'])
    p.inputs['Emission Color'].default_value = (*TEAL_HI, 1)
    tone = nt.nodes.new('ShaderNodeValToRGB'); ct = tone.color_ramp
    ct.elements[0].position = 0.3; ct.elements[0].color = (0.020, 0.026, 0.026, 1)
    ct.elements[1].position = 0.8; ct.elements[1].color = (0.090, 0.110, 0.105, 1)
    nt.links.new(nz.outputs['Fac'], tone.inputs['Fac']); nt.links.new(tone.outputs['Color'], p.inputs['Base Color'])
    bump = node(nt, 'ShaderNodeBump', Strength=0.4, Distance=0.05)
    nt.links.new(nz.outputs['Fac'], bump.inputs['Height']); nt.links.new(bump.outputs['Normal'], p.inputs['Normal'])
    p.inputs['Roughness'].default_value = 0.75
    return m


def eq_material():
    """FX EQ: three bands (low, mid, high) with wavy edges that drift as it turns"""
    m, nt, p = principled('fxeq')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ'); nt.links.new(tc.outputs['Object'], sep.inputs[0])
    nz = node(nt, 'ShaderNodeTexNoise', Scale=2.4, Detail=3.0, Roughness=0.5)
    nt.links.new(tc.outputs['Object'], nz.inputs['Vector'])
    wob = nt.nodes.new('ShaderNodeMath'); wob.operation = 'MULTIPLY_ADD'; wob.inputs[1].default_value = 0.22
    nt.links.new(nz.outputs['Fac'], wob.inputs[0]); nt.links.new(sep.outputs['Z'], wob.inputs[2])
    mr = nt.nodes.new('ShaderNodeMapRange'); mr.inputs['From Min'].default_value = -0.7; mr.inputs['From Max'].default_value = 1.1
    nt.links.new(wob.outputs['Value'], mr.inputs['Value'])
    ramp = nt.nodes.new('ShaderNodeValToRGB'); cr = ramp.color_ramp; cr.interpolation = 'CONSTANT'
    cr.elements[0].position = 0.0; cr.elements[0].color = (0.02, 0.24, 0.17, 1)
    cr.elements[1].position = 0.64; cr.elements[1].color = (0.62, 0.92, 0.80, 1)
    e = cr.elements.new(0.36); e.color = (0.04, 0.50, 0.34, 1)
    nt.links.new(mr.outputs['Result'], ramp.inputs['Fac']); nt.links.new(ramp.outputs['Color'], p.inputs['Base Color'])
    p.inputs['Roughness'].default_value = 0.35; p.inputs['Coat Weight'].default_value = 0.5
    return m


def metal_material(color, rough, name):
    m, nt, p = principled(name)
    p.inputs['Base Color'].default_value = (*color, 1); p.inputs['Metallic'].default_value = 1.0; p.inputs['Roughness'].default_value = rough
    return m


def soft_material(color, alpha, name):
    m, nt, p = principled(name)
    p.inputs['Base Color'].default_value = (*color, 1); p.inputs['Roughness'].default_value = 0.5; p.inputs['Alpha'].default_value = alpha
    m.blend_method = 'BLEND'
    return m


def haze_material(radius, density):
    """FX REVERB: a soft atmosphere, densest near the planet and fading out (scatters, so it has alpha)"""
    m = bpy.data.materials.new('haze'); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != 'OUTPUT_MATERIAL':
            nt.nodes.remove(n)
    pv = nt.nodes.new('ShaderNodeVolumePrincipled')
    pv.inputs['Color'].default_value = (0.55, 0.92, 0.80, 1); pv.inputs['Emission Color'].default_value = (*TEAL_HI, 1)
    tc = nt.nodes.new('ShaderNodeTexCoord')
    ln = nt.nodes.new('ShaderNodeVectorMath'); ln.operation = 'LENGTH'; nt.links.new(tc.outputs['Object'], ln.inputs[0])
    fall = nt.nodes.new('ShaderNodeMapRange'); fall.inputs['From Min'].default_value = 0.75; fall.inputs['From Max'].default_value = radius
    fall.inputs['To Min'].default_value = 1.0; fall.inputs['To Max'].default_value = 0.0
    nt.links.new(ln.outputs['Value'], fall.inputs['Value'])
    sq = nt.nodes.new('ShaderNodeMath'); sq.operation = 'POWER'; sq.inputs[1].default_value = 2.0
    nt.links.new(fall.outputs['Result'], sq.inputs[0])
    d = nt.nodes.new('ShaderNodeMath'); d.operation = 'MULTIPLY'; d.inputs[1].default_value = density
    nt.links.new(sq.outputs['Value'], d.inputs[0]); nt.links.new(d.outputs['Value'], pv.inputs['Density'])
    e = nt.nodes.new('ShaderNodeMath'); e.operation = 'MULTIPLY'; e.inputs[1].default_value = 0.6
    nt.links.new(sq.outputs['Value'], e.inputs[0]); nt.links.new(e.outputs['Value'], pv.inputs['Emission Strength'])
    nt.links.new(pv.outputs[0], nt.nodes['Material Output'].inputs['Volume'])
    return m


def pivot():
    bpy.ops.object.empty_add(location=(0, 0, 0)); return bpy.context.active_object


def annulus(r0, r1, mat, segs=192):
    me = bpy.data.meshes.new('annulus'); bm = bmesh.new()
    inner = [bm.verts.new((r0 * math.cos(2 * math.pi * i / segs), r0 * math.sin(2 * math.pi * i / segs), 0)) for i in range(segs)]
    outer = [bm.verts.new((r1 * math.cos(2 * math.pi * i / segs), r1 * math.sin(2 * math.pi * i / segs), 0)) for i in range(segs)]
    for i in range(segs):
        j = (i + 1) % segs
        bm.faces.new((inner[i], outer[i], outer[j], inner[j]))
    bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new('ring', me); scene.collection.objects.link(ob); me.materials.append(mat)
    return ob


def emit_material(name, color, strength):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != 'OUTPUT_MATERIAL':
            nt.nodes.remove(n)
    em = node(nt, 'ShaderNodeEmission', Color=(*color, 1), Strength=strength)
    nt.links.new(em.outputs[0], nt.nodes['Material Output'].inputs['Surface'])
    return m


def orbit_curve(R, rot, mat, depth, a0=0.0, a1=2 * math.pi, taper=False, n=256):
    """a circle of radius R (or the arc a0..a1) turned by rot; taper: thin at a0, full at a1 (the lit arc ends at the body)"""
    cu = bpy.data.curves.new('orbit', 'CURVE'); cu.dimensions = '3D'; cu.bevel_depth = depth; cu.bevel_resolution = 4
    closed = a1 - a0 >= 2 * math.pi - 1e-9
    sp = cu.splines.new('POLY'); sp.points.add(n - 1); sp.use_cyclic_u = closed
    for i in range(n):
        t = i / n if closed else i / (n - 1)
        a = a0 + (a1 - a0) * t
        v = rot @ Vector((R * math.cos(a), R * math.sin(a), 0.0))
        sp.points[i].co = (v.x, v.y, v.z, 1.0)
        sp.points[i].radius = (0.15 + 0.85 * t ** 1.5) if taper else 1.0
    ob = bpy.data.objects.new('orbit', cu); scene.collection.objects.link(ob); cu.materials.append(mat)
    return ob


def sphere(r, loc, mat, segs=96):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=segs // 2, radius=r, location=loc)
    o = bpy.context.active_object; bpy.ops.object.shade_smooth(); o.data.materials.append(mat); return o


def sun(direction_from, strength, angle_deg=6.0):
    bpy.ops.object.light_add(type='SUN'); l = bpy.context.active_object
    l.data.energy = strength; l.data.angle = math.radians(angle_deg)
    l.rotation_euler = (-Vector(direction_from)).to_track_quat('-Z', 'Y').to_euler()
    return l


def area(loc, energy, size, glossy=True):
    bpy.ops.object.light_add(type='AREA', location=loc); l = bpy.context.active_object
    l.visible_glossy = glossy
    l.data.energy = energy; l.data.size = size; l.data.shape = 'DISK'
    l.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    l.visible_transmission = False   # seen through the glass the rim light showed as a second highlight (top right): only reflections show the key
    return l


def studio_lights(scale=1.0):
    # camera looks along +Y; top left = (-x, -y, +z). The key is a soft box: it shows as a reflection on the glass.
    # All light comes from the top left (DS): the rim is behind and above on the left too, and the fill is not seen in reflections
    area((-3.2, -4.0, 3.6), 600 * scale, 1.6)                 # key, top left, in front
    area((3.0, -3.0, -2.6), 40 * scale, 6.0, glossy=False)    # fill, bottom right, very soft, diffuse only
    area((-1.8, 5.0, 2.6), 450 * scale, 1.4)                  # rim, behind, top left


def ortho_camera(scale):
    bpy.ops.object.camera_add(location=(0, -10, 0), rotation=(math.radians(90), 0, 0))
    cam = bpy.context.active_object; cam.data.type = 'ORTHO'; cam.data.ortho_scale = scale; scene.camera = cam


if mode == 'body':
    scene.render.film_transparent = True
    scene.cycles.film_transparent_glass = True
    res, ortho, tilt = 96, 2.4, (math.radians(18), math.radians(-14), 0.0)   # the spin axis leans toward the camera (we see the north) and to the left
    parts = []
    if kind == 'ring':
        res, ortho = 160, 4.3
        parts.append(sphere(1.0, (0, 0, 0), banded_material(), 128))
        r = annulus(1.35, 2.05, ring_material(1.35, 2.05)); r.rotation_mode = 'ZYX'; r.rotation_euler = tilt   # in the equator plane
    elif kind == 'crater':
        parts.append(sphere(1.0, (0, 0, 0), crater_material(), 160))
    elif kind == 'crystal':
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=1.0); o = bpy.context.active_object   # flat facets
        o.data.materials.append(crystal_material()); parts.append(o)
        sphere(0.5, (0, 0, 0), emit_material('bellcore', (0.55, 1.0, 0.82), 1.6), 48)   # a glow inside, seen through the gem
        scene.cycles.film_transparent_glass = False   # the gem stays opaque in alpha (refraction shows the studio, not holes)
        scene.cycles.transmission_bounces = 12; scene.cycles.max_bounces = 16
    elif kind == 'fx_drive':
        res = 128; parts.append(sphere(1.0, (0, 0, 0), drive_material(), 160))
    elif kind == 'fx_chorus':   # a pearl with two moons close by, turning together: voices a little apart
        res, ortho = 128, 2.9
        pv = pivot(); parts.append(pv)
        for o in (sphere(0.66, (0, 0, 0), pearl_material(0.3), 96), sphere(0.2, (1.0, 0, 0), pearl_material(0.7, False), 48), sphere(0.17, (-0.98, 0.2, 0), pearl_material(0.7, False), 48)):
            o.parent = pv
    elif kind == 'fx_delay':    # a planet with three thin rings, fainter outward: echoes
        res, ortho = 128, 3.2
        parts.append(sphere(0.62, (0, 0, 0), pearl_material(0.3), 128))
        for (r0, r1, a) in ((0.80, 0.85, 0.9), (1.06, 1.10, 0.55), (1.32, 1.35, 0.3)):
            ring = annulus(r0, r1, soft_material((0.72, 0.88, 0.82), a, 'echo')); ring.rotation_mode = 'ZYX'; ring.rotation_euler = tilt
    elif kind == 'fx_reverb':   # a soft gas planet in a wide haze: the room around the sound
        res, ortho = 128, 2.9
        parts.append(sphere(0.72, (0, 0, 0), banded_material(), 128))
        sphere(1.3, (0, 0, 0), haze_material(1.3, 1.1), 64)
        scene.cycles.volume_step_rate = 0.5
    elif kind == 'fx_eq':
        res = 128; parts.append(sphere(1.0, (0, 0, 0), eq_material(), 160))
    elif kind == 'fx_limit':    # a metal sphere held by a band with rivets: the ceiling
        res = 128
        pv = pivot(); parts.append(pv)
        ceramic, _, cp = principled('ceramic'); cp.inputs['Base Color'].default_value = (0.78, 0.83, 0.81, 1); cp.inputs['Roughness'].default_value = 0.3
        cp.inputs['Coat Weight'].default_value = 0.6
        core = sphere(0.78, (0, 0, 0), ceramic, 128); core.parent = pv   # a light ceramic ball (metal would mirror the dark studio and lose its lower half)
        bpy.ops.mesh.primitive_torus_add(major_radius=0.82, minor_radius=0.13, major_segments=96, minor_segments=24)
        band = bpy.context.active_object; bpy.ops.object.shade_smooth()
        band.data.materials.append(metal_material((0.10, 0.55, 0.40), 0.4, 'anodized')); band.parent = pv
        for i in range(10):
            a = 2 * math.pi * i / 10
            sphere(0.05, (0.95 * math.cos(a), 0.95 * math.sin(a), 0), metal_material((0.9, 0.92, 0.91), 0.25, 'rivet'), 16).parent = pv
    elif kind in ('pearl', 'lfo', 'bead'):
        parts.append(sphere(1.0, (0, 0, 0), pearl_material({'pearl': 0.25, 'lfo': 0.9, 'bead': 0.6}[kind], kind != 'bead'), 96))
        if kind == 'bead':
            res, frames = 64, 1
        if kind == 'lfo':
            frames = 1
    ortho_camera(ortho)
    scene.render.resolution_x = scene.render.resolution_y = res
    studio_lights()
    body = parts[0]; body.rotation_mode = 'ZYX'
    for f in range(frames):
        body.rotation_euler = (tilt[0], tilt[1], 2 * math.pi * f / frames)   # Z (the spin) first, then the lean
        scene.render.filepath = f'{out}_{f:03d}.png'
        bpy.ops.render.render(write_still=True)
    sys.exit(0)

if mode == 'daycore':
    # light screens: a core of its own (not the dark screens' glow, which needs night around it), made of the same white
    # porcelain as the light knobs, with the sound shown as teal light. argv[3] = porcelain | armillary | frost
    variant = argv[3] if len(argv) > 3 else 'porcelain'
    scene.render.film_transparent = True
    scene.cycles.film_transparent_glass = True
    scene.cycles.volume_step_rate = 0.5
    wramp.color_ramp.elements[0].color = (0.10, 0.13, 0.12, 1); wramp.color_ramp.elements[1].color = (0.45, 0.50, 0.48, 1)
    tilt = (math.radians(18), math.radians(-14), 0.0)   # as the bodies: the axis leans toward the camera and to the left

    def porcelain(name='porcelain'):
        m, nt, p = principled(name)
        p.inputs['Base Color'].default_value = (0.62, 0.68, 0.66, 1); p.inputs['Roughness'].default_value = 0.34
        p.inputs['Subsurface Weight'].default_value = 0.2; p.inputs['Subsurface Radius'].default_value = (0.3, 0.5, 0.45)
        p.inputs['Coat Weight'].default_value = 0.8; p.inputs['Coat Roughness'].default_value = 0.06
        return m

    def light_line(major, minor, z, strength, parent):
        bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=192, minor_segments=16, location=(0, 0, z))
        t = bpy.context.active_object; bpy.ops.object.shade_smooth()
        m, nt, p = principled('seam')
        p.inputs['Base Color'].default_value = (*TEAL, 1); p.inputs['Emission Color'].default_value = (*TEAL, 1)
        p.inputs['Emission Strength'].default_value = strength
        t.data.materials.append(m); t.parent = parent
        return t

    def ring(major, minor, mat, rot):
        bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=192, minor_segments=24)
        t = bpy.context.active_object; bpy.ops.object.shade_smooth(); t.data.materials.append(mat)
        t.rotation_mode = 'ZYX'; t.rotation_euler = rot
        return t

    pv = pivot(); pv.rotation_mode = 'ZYX'; pv.rotation_euler = tilt
    if variant == 'porcelain':    # a white ball with a lit seam round its equator and two fainter latitudes
        sphere(1.0, (0, 0, 0), porcelain(), 160).parent = pv
        light_line(1.0, 0.018, 0.0, 1.4, pv)   # strength kept under clipping: brighter turns the teal cyan
        for lat in (-46, 46):
            light_line(math.cos(math.radians(lat)), 0.007, math.sin(math.radians(lat)), 0.8, pv)
    elif variant == 'armillary':  # a smaller ball held in orbit rings (anodized teal and satin white), like an instrument
        sphere(0.64, (0, 0, 0), porcelain(), 160).parent = pv
        light_line(0.64, 0.014, 0.0, 1.4, pv)
        teal = metal_material((0.10, 0.55, 0.40), 0.32, 'anodized')
        satin = metal_material((0.86, 0.89, 0.88), 0.28, 'satin')
        ring(0.98, 0.030, teal, (math.radians(76), math.radians(-14), 0.0))
        ring(0.87, 0.022, satin, (math.radians(-62), math.radians(0), math.radians(38)))
        ring(0.77, 0.020, teal, (math.radians(24), math.radians(48), math.radians(-20)))
    else:                         # frosted glass lit from inside by a teal nucleus
        g, gnt, gp = principled('frost')
        gp.inputs['Base Color'].default_value = (0.95, 1.0, 0.98, 1); gp.inputs['Transmission Weight'].default_value = 1.0
        gp.inputs['Roughness'].default_value = 0.42; gp.inputs['IOR'].default_value = 1.45
        sphere(1.0, (0, 0, 0), g, 160)
        sphere(0.55, (0, 0, 0), glow_volume(14.0, 0.55, 2.0), 64)
    ortho_camera(2.3)
    scene.render.resolution_x = scene.render.resolution_y = 640
    studio_lights()

if mode in ('core', 'moon'):
    scene.render.film_transparent = True
    scene.cycles.film_transparent_glass = True
    scene.cycles.volume_step_rate = 0.5
    if mode == 'core':
        scene.render.film_transparent = False   # glows have no alpha: the screen lays this on with mix-blend-mode: screen
        res = 640
        shell(1.0, (0, 0, 0), glass_material())
        sphere(0.95, (0, 0, 0), nebula_material(4.5, 0.95), 96)
        sphere(0.45, (0, 0, 0), glow_volume(10.0, 0.45, 2.5), 64)
        ortho_camera(2.3)
    else:
        res = 192
        sphere(1.0, (0, 0, 0), moon_material(), 96)
        ortho_camera(2.4)
    scene.render.resolution_x = scene.render.resolution_y = res
    studio_lights()
elif mode == 'hero':
    # the whole system: perspective, bloom, for the sales material
    scene.render.film_transparent = False
    scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
    random.seed(7)
    shell(1.2, (0, 0, 0), glass_material(), 0.03)
    sphere(1.14, (0, 0, 0), nebula_material(4.5, 1.14), 96)
    sphere(0.54, (0, 0, 0), glow_volume(10.0, 0.54, 2.5), 64)
    orbit_mat = emit_material('orbit', TEAL, 0.35)       # below the bloom threshold: a thin line
    lit_mat = emit_material('lit', TEAL_HI, 6.0)          # the lit arc (the level) blooms
    dust_mat = emit_material('dust', (0.6, 0.95, 0.82), 1.6)
    star_mat = emit_material('star', (0.85, 0.95, 1.0), 1.4)
    moon_m = moon_material()
    # (radius, turn about Z, tilt about X, body angle, level). Nearly flat discs seen from about 20 degrees above, like the screen's ellipses
    orbits = [(3.6, -8, 4, 0.55, 0.82), (2.95, 6, -3, 2.45, 0.64), (2.3, -4, 6, 4.1, 0.30), (1.7, 10, -5, 5.3, 0.45)]
    for (R, tz, tx, ang, lvl) in orbits:
        rot = (Matrix.Rotation(math.radians(tz), 3, 'Z') @ Matrix.Rotation(math.radians(tx), 3, 'X'))
        orbit_curve(R, rot, orbit_mat, 0.004)
        orbit_curve(R, rot, lit_mat, 0.012, ang - lvl * 0.35 * 2 * math.pi, ang, taper=True)   # the lit arc: the level, ending at the body
        pos = rot @ Vector((R * math.cos(ang), R * math.sin(ang), 0.0))
        sphere(0.11 + 0.09 * lvl, pos, moon_m, 64)
        for k in range(24):   # a little dust just behind the body
            a2 = ang - random.random() ** 2 * 0.35
            p2 = rot @ Vector((R * math.cos(a2), R * math.sin(a2), 0.0)) + Vector([random.gauss(0, 0.012) for _ in range(3)])
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=random.uniform(0.004, 0.009), location=p2)
            bpy.context.active_object.data.materials.append(dust_mat)
    cam_pos, target = Vector((0, -11.4, 4.3)), Vector((0, 0, -0.35))
    fwd = (target - cam_pos).normalized(); right = fwd.cross(Vector((0, 0, 1))).normalized(); up = right.cross(fwd)
    for k in range(150):   # faint stars far behind the system, inside the view
        d = (fwd + right * random.uniform(-0.48, 0.48) + up * random.uniform(-0.28, 0.28)).normalized()
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=random.uniform(0.015, 0.05), location=cam_pos + d * random.uniform(45, 60))
        bpy.context.active_object.data.materials.append(star_mat)
    bpy.ops.object.camera_add(location=cam_pos); cam = bpy.context.active_object
    cam.rotation_euler = (target - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam.data.lens = 44; scene.camera = cam
    studio_lights(1.0)
    # bloom in the compositor
    scene.use_nodes = True; ct = scene.node_tree
    for n in list(ct.nodes):
        ct.nodes.remove(n)
    rl = ct.nodes.new('CompositorNodeRLayers'); gl = ct.nodes.new('CompositorNodeGlare'); comp = ct.nodes.new('CompositorNodeComposite')
    gl.glare_type = 'FOG_GLOW'; gl.quality = 'HIGH'; gl.threshold = 0.6; gl.size = 8; gl.mix = 0.15
    ct.links.new(rl.outputs['Image'], gl.inputs['Image']); ct.links.new(gl.outputs['Image'], comp.inputs['Image'])

scene.render.filepath = out
bpy.ops.render.render(write_still=True)
