# Headless Blender renderer for a CMB-exported posed/bind mesh (tools/cmb_export.cpp JSON).
# Builds a mesh from the group triangle-soup and renders front + 3/4 views to PNGs, so the
# rig pose (e.g. the #7 stretched right arm) is visible and comparable. No armature needed —
# the posed verts already encode the pose.
#   blender -b --python tools/blender_render_rig.py -- <in.json> <out_prefix>
import bpy, json, sys, math

argv = sys.argv[sys.argv.index("--") + 1:]
in_json, out_prefix = argv[0], argv[1]
# Optional 3rd arg: comma-separated mesh_ids to KEEP (e.g. "24,26" = child body+head). Empty=all.
keep = set(int(x) for x in argv[2].split(",")) if len(argv) > 2 and argv[2] else None

d = json.load(open(in_json))

# Collect verts/faces from all groups (each group: verts list, tris = consecutive triples).
verts, faces = [], []
for g in d["groups"]:
    if keep is not None and g.get("mesh_id") not in keep:
        continue
    vs = g["verts"]
    base = len(verts)
    for v in vs:
        # OoT3D space is Y-up, large scale. Map to Blender Z-up: (x,y,z)->(x, z, y).
        verts.append((v[0], v[2], v[1]))
    for i in range(0, len(vs) - 2, 3):
        faces.append((base + i, base + i + 1, base + i + 2))

# Clear default scene.
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete()

mesh = bpy.data.meshes.new("rig")
mesh.from_pydata(verts, [], faces)
mesh.update()
obj = bpy.data.objects.new("rig", mesh)
bpy.context.collection.objects.link(obj)

# Bounding box / center.
xs = [v[0] for v in verts]; ys = [v[1] for v in verts]; zs = [v[2] for v in verts]
cx, cy, cz = (min(xs)+max(xs))/2, (min(ys)+max(ys))/2, (min(zs)+max(zs))/2
ext = max(max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs))
rad = ext * 0.65

# Material: flat light gray, backface-cull off (default), simple.
matrl = bpy.data.materials.new("m"); matrl.diffuse_color = (0.7, 0.72, 0.75, 1)
obj.data.materials.append(matrl)

# Light.
light_data = bpy.data.lights.new("L", type="SUN"); light_data.energy = 4
light = bpy.data.objects.new("L", light_data); bpy.context.collection.objects.link(light)
light.rotation_euler = (math.radians(55), 0, math.radians(30))

# Camera (orthographic, frames the figure). Rendered from several yaw angles around Z.
cam_data = bpy.data.cameras.new("C"); cam_data.type = "ORTHO"; cam_data.ortho_scale = ext * 1.15
cam_data.clip_start = 1.0; cam_data.clip_end = ext * 6  # scene is thousands of units
cam = bpy.data.objects.new("C", cam_data); bpy.context.collection.objects.link(cam)
bpy.context.scene.camera = cam

scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.render.image_settings.file_format = "PNG"
scene.render.resolution_x = 480; scene.render.resolution_y = 640
scene.render.film_transparent = False
try:
    scene.display.shading.light = "STUDIO"
    scene.display.shading.show_cavity = True
except Exception:
    pass

def render_from(yaw_deg, name):
    a = math.radians(yaw_deg)
    # camera placed on a ring in the XY plane (Blender), looking at center, slight downward.
    ex = cx + rad * math.sin(a)
    ey = cy - rad * math.cos(a)
    ez = cz + rad * 0.05
    cam.location = (ex, ey, ez)
    # point camera at center
    dirv = (cx - ex, cy - ey, cz - ez)
    # yaw/pitch from direction
    import mathutils
    q = mathutils.Vector(dirv).to_track_quat('-Z', 'Y')
    cam.rotation_euler = q.to_euler()
    scene.render.filepath = f"{out_prefix}_{name}.png"
    bpy.ops.render.render(write_still=True)

# Front (figure facing +Y in OoT? we view from -Y => yaw 180), and 3/4.
render_from(180, "front")
render_from(140, "q34")
render_from(90, "side")
print("RENDERED", out_prefix)
