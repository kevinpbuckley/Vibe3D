---
name: modeling
display_name: Mesh Modeling
description: Create and edit static/skeletal mesh geometry programmatically (ModelingService) — primitives, booleans, extrude/inset/bevel on selections, remesh/simplify/subdivide, deform, voxel ops, auto-UV, normals, repair, collision, LODs, bake, and saving to assets. Use when the user asks to build, kitbash, block out, fix, clean up, or generate 3D meshes in the editor without Blender.
vibe3d_classes:
  - ModelingService
unreal_classes:
  - DynamicMesh
  - StaticMesh
  - GeometryScript_Primitives
  - GeometryScript_MeshBooleans
  - GeometryScript_UVs
  - GeometryScript_MeshEdits
  - GeometryScript_List
  - StaticMeshEditorSubsystem
keywords:
  - modeling
  - mesh
  - geometry
  - geometry script
  - primitive
  - boolean
  - extrude
  - inset
  - bevel
  - remesh
  - simplify
  - subdivide
  - voxel
  - collision
  - lod
  - bake normal map
  - kitbash
  - blockout
  - procedural mesh
---
<!-- Copyright Buckley Builds LLC 2026 All Rights Reserved. -->

> 🧠 **Brains complement:** IF an `unreal-engine-skills-manager` tool (external MCP) exists in this session, call it with `{action: "load", skill: "meshes-static-and-skeletal"}` for UE domain knowledge on this topic — correct APIs, architecture, best practices — and treat it as the rubric for any review / "best practices" question. If no such tool is available (e.g. running under Claude Code or Codex without that MCP), skip this line entirely and proceed with this skill alone — do NOT attempt the call.

# Mesh Modeling Skill

`ModelingService` (`unreal.ModelingService`) is the programmatic half of Unreal's **Modeling Mode**:
the same operators the 97 interactive tools are built on, exposed as functions on an in-memory
`DynamicMesh` that you build up and then save as a `StaticMesh` / `SkeletalMesh` asset. Nothing
here needs a viewport, a gizmo, or Blender.

**Mental model — a session of mesh handles:**

1. `create_mesh()` / `load_mesh_from_static_mesh(path)` / `load_mesh_from_actor(label)` → a result; check `success`, then use its integer **`.handle`**.
2. Every op mutates the handle's mesh in place and returns an `FModelingResult` (`success`, `message`, `triangle_count`, `vertex_count`).
3. `save_mesh_to_static_mesh(handle, "/Game/Props/SM_Crate")` writes the asset (creates or replaces); Epic's `EditorToolset.LevelToolset.AddToSceneFromAsset` (`assetPath`, `name`, `xform`) places it in the level.
4. `release_mesh(handle)` when done (or `release_all_meshes()`); handles are session-scoped and not saved.

Operations that take a **selection** use a named selection created on the same handle
(`select_all`, `select_by_normal_angle`, `select_in_box`, `select_in_sphere`, `select_by_material_id`,
`expand_contract_selection`, `invert_selection`). Pass `""` as the selection name to mean "the whole mesh".

## Quick start — a crate with a lid seam, UVs, collision, LODs

```python
import unreal
svc = unreal.ModelingService
h = svc.create_mesh().handle
svc.append_box(h, unreal.Transform(), 100, 100, 100)                       # body
lid = svc.create_mesh().handle
svc.append_box(lid, unreal.Transform(unreal.Vector(0, 0, 90)), 104, 104, 4) # lid slab
svc.boolean(h, lid, "Union")
svc.select_by_normal_angle(h, "top", unreal.Vector(0, 0, 1), 5.0)
svc.inset_faces(h, "top", 6.0)                                              # panel line on the lid
svc.extrude_faces(h, "top", -1.5)                                           # recess it
svc.bevel_polygroups(h, 0.8)
svc.recompute_normals(h, 30.0)
svc.auto_uv(h, "PatchBuilder", 0)
r = svc.save_mesh_to_static_mesh(h, "/Game/Props/SM_Crate", True, True)     # nanite off, collision on
print("CREATED:", r.asset_path, r.triangle_count, "tris")
svc.generate_collision("/Game/Props/SM_Crate", "ConvexHulls", 4)
svc.set_lods("/Game/Props/SM_Crate", [1.0, 0.5, 0.25])
await call_tool("EditorToolset.LevelToolset.AddToSceneFromAsset", assetPath="/Game/Props/SM_Crate", name="Crate_01", xform=unreal.Transform())
svc.release_all_meshes()
```

Then **look at it**, framed on the actor, with UE 5.9's native capture tool
(`SceneCaptureToolset.SceneCaptureToolset.CaptureActiveEditorView`, or `CaptureEditorViewport` for a
specific viewport; both return an MCP image block). A successful call is not proof the shape is right.

A level made with `new_level()` has **no lighting**, so a black capture may be a lighting problem.
For daylight, use a DirectionalLight pointing down (`Rotator(roll=0, pitch=-48, yaw=125)`),
a SkyLight and SkyAtmosphere. A dark studio can instead use movable RectLights and a floor.
Use an unbound PostProcessVolume with fixed exposure for consistent material evaluation; tune
exposure to the actual light intensities rather than treating one bias value as universal.

Before any raw `unreal.GeometryScript_*` call, read [geometryscript-cheatsheet.md](geometryscript-cheatsheet.md).
For modeled lettering and logos, read [modeled-lettering.md](modeled-lettering.md).
For lit promotional images, high-resolution capture, or distributable asset packs, read
[showcase-and-delivery.md](showcase-and-delivery.md). These sibling pages are not separate skill
entries: UE rewrites the links to absolute paths when it serves this skill, so read the file at the
path given (see **Reference pages** at the end).

## Function map (Python names)

| Area | Functions |
|---|---|
| Session | `create_mesh()`, `load_mesh_from_static_mesh(path, lod=0)`, `load_mesh_from_skeletal_mesh(path, lod=0)`, `load_mesh_from_actor(label)`, `copy_mesh(handle)`, `release_mesh(handle)`, `release_all_meshes()`, `list_meshes()`, `get_mesh_info(handle)` |
| Bulk vertex access | `get_vertex_positions(handle)` → plain list of `Vector` indexed by vertex ID, `set_vertex_positions(handle, positions)` (same layout back; no `GeometryScriptVectorList` conversion) |
| Primitives (append into a handle) | `append_box`, `append_sphere`, `append_sphere_box`, `append_cylinder`, `append_cone`, `append_capsule`, `append_torus`, `append_rectangle`, `append_disc`, `append_stairs`, `append_curved_stairs`, `append_extrude_polygon(handle, transform, points2d, height)`, `append_revolve_polygon(handle, transform, points2d, radius, steps, degrees)`, `append_sweep_polyline(handle, transform, profile2d, path_transforms)` (open tube — prefer the next one for solids), `append_loft(handle, transform, closed_profile2d, frames)` (capped, always outward; frame scale = chord), `append_mesh(handle, other_handle, transform)`, `append_mesh_at_transforms(handle, other, [transforms])`, `append_mesh_along_polyline(handle, other, points, spacing, up)` |
| Booleans | `boolean(target, tool, "Union|Subtract|Intersection", tool_transform)`, `self_union`, `plane_cut(handle, plane_transform, fill_holes)`, `mirror(handle, plane_transform, weld)`, `cut_groove_along_polyline(handle, points, width, depth, up)` |
| Selections | `select_all(handle, name)`, `select_by_normal_angle(handle, name, normal, max_angle_deg)`, `select_in_box(handle, name, box_min, box_max)`, `select_in_sphere(handle, name, center, radius)`, `select_by_material_id(handle, name, material_id)`, `select_by_polygroup(handle, name, group_id)`, `select_connected(handle, name, point)` (whole connected piece nearest the point), `select_by_bone_weight(handle, name, "bone,bone", min_weight=0.25)` (vertices those bones drag — region finding for weight repair), `expand_contract_selection(handle, name, new_name, iterations, contract)`, `invert_selection(handle, name, new_name)`, `selection_count(handle, name)`, `selection_bounds(handle, name)` → `(min, max)` (Python drops the bool; an empty selection gives zero vectors), `clear_selections(handle)` |
| Poly edit on selections | `extrude_faces(handle, sel, distance, direction)`, `offset_faces(handle, sel, distance)`, `inset_faces(handle, sel, distance)`, `outset_faces(handle, sel, distance)`, `delete_faces(handle, sel)`, `translate_selection(handle, sel, delta)`, `bevel_polygroups(handle, distance, subdivisions)`, `offset_mesh(handle, distance)`, `shell_mesh(handle, thickness)` |
| Mesh | `remesh(handle, target_triangles, edge_length=0)`, `simplify_to_triangle_count(handle, n)`, `simplify_to_vertex_count(handle, n)`, `simplify_to_tolerance(handle, tolerance)`, `simplify_planar(handle)` (lossless, coplanar only), `subdivide(handle, level, "PN|Uniform|CatmullClark|Loop")`, `smooth(handle, sel, iterations, alpha)`, `fill_holes(handle)`, `weld_edges(handle, tolerance)`, `repair(handle)` (degenerates + small components), `remove_hidden_triangles(handle)`, `split_by_components(handle)` → new handles |
| Queries, sampling, hulls | `ray_cast(handle, origin, direction)` / `nearest_point(handle, point)` → `{found, position, triangle_id, distance}`, `is_point_inside(handle, point)`, `sample_surface_points(handle, radius, max)` → transforms for scattering, `convex_hull(handle)`, `convex_decomposition(handle, num_hulls)`, `swept_hull(handle, frame)` → NEW handles, `measure_distance(a, b)` → max/mean/rms (verify LODs and simplifications) |
| Deform | `bend(handle, orientation, angle, extent)`, `twist(...)`, `flare(handle, orientation, percent_x, percent_y, extent)`, `noise(handle, sel, magnitude, frequency, seed)`, `displace_from_texture(handle, sel, texture_path, magnitude, uv_layer)` |
| Voxel | `voxel_solidify(handle, grid_resolution)`, `voxel_morphology(handle, "Dilate|Contract|Open|Close", distance, grid_resolution)` |
| UVs / normals / groups / materials / colors | `auto_uv(handle, "XAtlas|PatchBuilder", uv_layer)`, `project_uv(handle, "Planar|Box|Cylinder", transform, uv_layer, sel)`, `repack_uv(handle, uv_layer, resolution)`, `set_num_uv_layers`, `recompute_normals(handle, hard_angle_deg)`, `flip_normals`, `ensure_outward(handle)` (flip if the enclosed volume is negative), `compute_polygroups(handle, "Angle|UVIslands|Components|Polygons", crease_angle)`, `set_material_id(handle, sel, id)`, `remap_material_id(handle, from, to)`, `set_vertex_color(handle, sel, color)`, `set_polygroup(handle, sel, group_id=-1)`, `recompute_uvs(handle, uv_layer, "Polygroups|UVIslands", "SpectralConformal|Conformal|ExpMap", sel)`, `transform_uv(handle, uv_layer, sel, translation, scale, rotation_deg, origin)`, `layout_uv(handle, uv_layer, "Repack|Stack|Normalize|Transform", sel, resolution)`, `pack_uv_per_material(handle, uv_layer, resolution)`, `get_uv_stats(handle, uv_layer)` → islands/coverage/overlap/texel density, `world_to_uv(handle, point, uv_layer)` |
| Transform | `transform_mesh(handle, transform)`, `translate_mesh`, `rotate_mesh`, `scale_mesh`, `recenter_mesh(handle, "Bounds|Base")` |
| Generated-prop intake | `intake_generated_mesh(file, asset_path, target_size_cm, fit_axis="Height", yaw_deg=0, pivot="BottomCenter", max_triangles=20000, max_texture_size=1024, collision="ConvexHulls", max_convex_hulls=4, lod_percents="", target_box_cm=Vector(), enable_nanite=False, force=False)` → `ModelingIntakeReport` (import GLB/FBX/OBJ, merge, scale, pivot, decimate, cap textures, LODs, collision; see **Generated props**) |
| Posed reads and garment fit | `load_mesh_from_skinned_component(actor_label, component_name="", world_space=False, lod_index=0, prefer_play_world=True)` (the component's CURRENT pose; PIE first), `unpose_mesh(handle)`, `measure_penetration(handle, other, "Inside|Outside", margin, max_distance=5, sel="", out_selection="")` → `ModelingPenetrationReport`, `push_inside(handle, other, margin, sel, max_distance, smooth_iterations)`, `push_outside(...)`, `select_hidden_by(handle, name, other, margin, max_distance, all_vertices=True)` — see **Fitting garments** |
| Accessory re-skin | `skin_to_surface(handle, source_handle, mode="Surface|Inpaint|Rigid", rigid_bone_name="", bones_from_skeletal_mesh="", smooth_iterations=0, smooth_strength=0)` (weights from the skinned surface underneath), `remap_lod_bones(asset_path, reference_skeletal_mesh_path)` (move weight off bones the leader drops at lower LODs) — see **Accessories that follow a surface** |
| Skeletal LODs in place | `load_mesh_from_skeletal_mesh(path, lod)` (source model; the handle remembers where it came from), `save_mesh_to_skeletal_mesh_lod(handle, asset_path="", lod_index=-1)` (write ONE LOD back, validated; keeps skeleton, slots and the other LODs; lod_index == LOD count appends), `inspect_skeletal_mesh(path)` (slots, per-LOD source / verts / tris / bones used) — see **Editing a skeletal mesh LOD in place** |
| Assets | `save_mesh_to_static_mesh(handle, path, replace_existing, enable_collision, enable_nanite)`, `save_mesh_to_skeletal_mesh(handle, path, skeleton_path="")` ("" creates `<path>_Skeleton` from the mesh bones; on an existing asset it replaces LOD0 through the same validated writer), `set_asset_materials(asset_path, "/Game/M_A,/Game/M_B")`, `transfer_bone_weights(handle, source_skeletal_mesh_path)`, `smooth_bone_weights(handle, skeleton_path)`, `prune_bone_weights(handle, "bone,bone")`, `scale_bone_weights(handle, sel, "bone,bone", scale, receiver_bone="")` (regional weight repair; freed weight -> receiver or strongest other influence), `smooth_selection_bone_weights(handle, sel, iterations=3, alpha=0.5)` (feather weight seams), `inspect_bone_weights(handle, sel="")` (per-bone verts/avg/max, weight sums, max influences), `create_bones(handle, [ModelingBoneDef(name, parent_name, transform)])`, `bind_selection_to_bone(handle, sel, bone, weight=1)`, `list_bones(handle)` → per-bone vertex counts, `generate_collision(asset_path, "MinVolumeShapes|ConvexHulls|AlignedBoxes|OrientedBoxes|MinimalSpheres|Capsules|SweptHulls", max_hulls)`, `set_lods(asset_path, [percent_triangles...])`, `bake_textures(target_handle, source_handle, "TangentNormal,AmbientOcclusion,Curvature,ObjectNormal,Position,VertexColor,MaterialID,UVShell,Height", resolution, out_folder, base_name)`, `bake_texture_transfer(target, source, "T_A,T_B", resolution, out_folder, base_name)`, `create_noise_texture(asset_path, w, h, cells, octaves)`, `draw_on_texture(asset_path, "Line|Dots|Rect|Fill", uv_points, color, thickness_px, spacing_px)`; image import is Epic's `EditorToolset.TextureToolset.ImportFile` + `EditorToolset.ObjectToolset.SetProperties`, and placing an asset in the level is `EditorToolset.LevelToolset.AddToSceneFromAsset`; `load_mesh_from_actor` also reads skeletal / dynamic mesh actors |

## GeometryScript coverage

ModelingService wraps ~115 of GeometryScript's 622 functions — every operator behind a Modeling
Mode tool plus the asset plumbing. What it deliberately does not wrap, and how to reach it:

| Not wrapped | Why | Reach it with |
|---|---|---|
| `GeometryScript_List`, `GeometryScript_VectorMath`, `GeometryScript_Transform`, `GeometryScript_Box`, `GeometryScript_OrientedBox`, `GeometryScript_Ray`, `GeometryScript_SimplePolygon`, `GeometryScript_PolyPath`, `GeometryScript_PolygonList`, `GeometryScript_PointSetSampling` | Pure math / container helpers, not editor operations | Call directly — they take plain values |
| `GeometryScript_WeightMaps`, `GeometryScript_MeshSculptLayers`, `GeometryScript_MeshGeodesic`, `GeometryScript_TextureUtils`, `GeometryScript_VolumeBake`, `GeometryScript_EditorTextureUtils` | Niche; would double the API for rare use | `mesh = svc.get_dynamic_mesh(handle)` then e.g. `unreal.GeometryScript_MeshGeodesic.create_surface_path(mesh, ...)` |
| Per-function option structs (remesh smoothing type, bake bit depth, XAtlas iterations, ...) | The wrappers fix sensible defaults | Same trick — the handle's `UDynamicMesh` is the live object; GeometryScript calls edit it in place and the next `svc.*` call sees the result |

**Do not guess GeometryScript class names.** The Python class is the library's `ScriptName`, not its C++ name:
`GeometryScript_MeshEdits` (not `MeshBasicEdits`), `GeometryScript_NewAssetUtils` (not `CreateNewAsset`),
`GeometryScript_AssetUtils` (static and skeletal mesh read/write), `GeometryScript_Primitives`, `GeometryScript_UVs`.
Arrays travel as typed wrapper structs (`GeometryScriptVectorList`, `GeometryScriptScalarList`, `GeometryScriptIndexList`,
...) that are not interchangeable. Read [geometryscript-cheatsheet.md](geometryscript-cheatsheet.md) before the first raw
GeometryScript call: every library's real Python name, which list type each function takes, and how out-parameters come
back. For vertex positions you do not need a list type at all: `svc.get_vertex_positions(h)` / `svc.set_vertex_positions(h, positions)`.

Discover any of them with `search_symbols` (e.g. pattern `GeometryScript_`), then `get_symbol` with
`name="unreal.GeometryScript_<Library>"`.

Exact signatures: `get_symbol` with `name="unreal.ModelingService"`. The long tail — anything not
here — is reachable directly: the mesh behind a handle is `svc.get_dynamic_mesh(handle)`, which you
can pass to any `unreal.GeometryScript_*` library function in the same script.

## Critical rules

- **Units are centimeters, +Z up, origin at the base by default** for `append_box`/`append_cylinder`
  (`origin="Center"` to change). A 100 cm crate at `Transform()` sits on the floor.
- **`unreal.Rotator(a, b, c)` is `(roll, pitch, yaw)`** — not the C++ `FRotator(Pitch, Yaw, Roll)`
  order. Passing a yaw positionally silently *pitches* the part instead of turning it, and nothing
  errors: a mug handle comes out as a horizontal donut, a sofa spawns upside down, a light aims at
  the sky. **Always pass by keyword** — `unreal.Rotator(roll=0, pitch=0, yaw=90)` — and confirm with
  `get_mesh_info` bounds (a flipped part shows negative Z) or by reading `.roll/.pitch/.yaw` back.
- **Laying a cylinder/cone/capsule on an axis: `pitch=-90` gives +X, `pitch=+90` gives −X.** These
  primitives are built along local +Z from their base, and the intuitive `pitch=90` sends them
  *backwards* — a windmill's windshaft and hub grew back into the roof instead of out to the sails,
  which only showed up as an unexpected `bounds_max.x`. `roll=-90` gives +Y, `roll=+90` gives −Y.
  Check `get_mesh_info` bounds after the first one and reuse whatever proved correct.
- **Low-step primitives are polygons, and their flats sit inside the nominal radius.** A torus or
  cylinder with `steps=8` has radius `R` only at its corners and `R·cos(π/8) = 0.92·R` at the middle
  of each face. Parts placed at radius `R` between corners therefore miss it entirely: an octagonal
  gallery deck left its railing posts hanging in the air, connected to nothing. Either raise the step
  count until the difference is under your part's thickness (24 steps → 0.9% error), or place the
  parts at the corner angles.
- **Booleans want closed meshes.** Run `get_mesh_info` — `is_closed` false or `open_border_edges > 0`
  means fill holes / weld first, or the boolean produces flaps. `self_union` cleans up kitbash overlaps.
- **Selections go stale when topology changes.** After boolean, remesh, bevel, subdivide, etc.
  re-run the `select_*` call before using the name again; the service refuses a selection made
  before the mesh changed (`success: false`, "stale") rather than acting on wrong triangles.
  `extrude_faces`, `offset_faces`, `inset_faces` and `outset_faces` are the exception: the selection
  they used then holds the faces they moved (an inset's inner panel), so inset → extrude chains.
- **Order for a clean asset:** model → `repair` → `recompute_normals` → `auto_uv` → `save` →
  `generate_collision` → `set_lods`. UVs before save, collision and LODs after (they act on the asset).
- **`save_mesh_to_static_mesh` replaces LOD0 of an existing asset** when `replace_existing` is true;
  its materials are kept. Existing collision/LODs on that asset are not touched — regenerate them if
  the silhouette changed.
- **Log for rollback:** `print("CREATED:", path)` after each save; there is no undo for handle ops
  (they are in memory), but assets go through the editor and `TransactionService` checkpoints apply.
- **Verify with evidence.** `get_mesh_info` after each stage (triangle/vertex counts, bounds, closed),
  and a viewport capture once it is in the level.
- **Swept surfaces can come out inside-out.** After `append_sweep_polyline` + `fill_holes`, check
  `get_mesh_info(h).volume`; a negative volume means the normals face inward and `self_union` /
  `boolean` will silently discard the part as negative space. Call `flip_normals(h)` first. Sweep
  frame semantics: with frame rotation yaw 90 the profile's X runs along world -X from the frame
  position and its Y along world Z; put the profile scale (e.g. a wing chord) in the frame's Scale.
- **XAtlas needs a compact mesh.** `fill_holes` and boolean cuts leave id gaps; `auto_uv` compacts
  automatically from 2.2, older builds need `unreal.GeometryScript_MeshRepair.compact_mesh(svc.get_dynamic_mesh(h))` first.
- **Budget triangles.** Voxel ops and PN tessellation explode counts; follow them with
  `simplify_to_triangle_count`. Keep props under ~20k tris unless Nanite is on.
- **Don't boolean two surfaces that nearly coincide.** A coffee disc unioned at the exact radius of
  the mug's inner wall produced slivers and 214 open border edges. Either leave a clear margin
  (~2% of the feature size) or skip the boolean: appending the part into the same handle as its own
  nested closed component is valid for a StaticMesh and is what you want for liquid in a vessel,
  glass in a frame, or anything that never needs to be one watertight shell.
- **`repair()` can open a closed mesh.** Collapsing degenerates leaves holes. It now re-closes a
  mesh that arrived watertight (`keep_closed=True` by default) — pass `False` for the raw result —
  but always re-check `is_closed` before a boolean or voxel op.

## Common mistakes

- Calling `extrude_faces` with `""` (whole mesh) — that shells the whole object outward. Select faces first.
- Forgetting `auto_uv` before `bake_textures` or before saving a mesh that will take a textured material.
- `set_lods` with `[1.0]` only — that removes extra LODs. Give one entry per LOD, LOD0 first.
- Loading a mesh from an actor and saving back to a *new* path, then wondering why the level didn't change — spawn (`EditorToolset.LevelToolset.AddToSceneFromAsset`) or reassign the actor's mesh.
- **Passing `material_id` positionally.** These wrappers have long tails of optional arguments
  (`append_stairs(handle, transform, step_width, step_height, step_depth, num_steps, floating, material_id)`),
  so one missing argument silently feeds your material id to `floating`. Pass the tail by keyword:
  `svc.append_stairs(h, T(), step_width=40, step_height=10, step_depth=20, num_steps=4, material_id=7)`.
- Assuming a rotation worked because the call returned `success`. Nothing here validates orientation —
  check `get_mesh_info` bounds, then look at it.

## Material slots

A mesh carries **material IDs**; the saved asset carries **material slots**, one per distinct ID in
ascending order. So the workflow is: give each part its id as you append it, then point the slots at
real materials after saving.

```python
svc.append_revolve_polygon(h, T(), cup_profile, 0, 48, 360, 0)          # id 0 = ceramic
svc.append_cylinder(h, T(V(0,0,6.6)), 3.7, 1.2, 48, 0, True, "Base", 1) # id 1 = coffee
print(svc.get_mesh_info(h).material_ids)                                # -> [0, 1]  ALWAYS check
svc.save_mesh_to_static_mesh(h, "/Game/Props/SM_Mug", True, True)
svc.set_asset_materials("/Game/Props/SM_Mug", "/Game/M_Ceramic,/Game/M_Coffee")
```

- `get_mesh_info(h).material_ids` is the check that a part actually took its id — do it before saving,
  not after wondering why the asset has one slot.
- A part built in **another handle** keeps its ids through `append_mesh`, so either way works.
- To give an id to something already merged in, select it and set it:
  `svc.select_connected(h, "shade", point_on_it)` then `svc.set_material_id(h, "shade", 1)`.
- Without `set_asset_materials` every slot renders as the default grey — a mesh with correct ids
  still looks untextured in the viewport.

## Generated props: image-to-3D model → game asset in one call

Models from image-to-3D services (Hyper3D Rodin, Tripo, Tonetta Forge, ...) arrive at an arbitrary scale with
the pivot in the middle, far too many triangles and 2-4K textures. `intake_generated_mesh` does the whole clean-up
inside the editor — no Blender pass:

```python
r = svc.intake_generated_mesh("C:/gen/knife_tray/knife_tray.glb", "/Game/Props/KnifeTray/SM_KnifeTray",
        target_size_cm=48.0, fit_axis="Width",        # the size you KNOW: a door's height, a tray's width
        yaw_deg=0.0, pivot="BottomCenter",            # "TopCenter" for hanging lamps
        max_triangles=12000, max_texture_size=1024,   # 1024 for web/mobile, 2048 for a hero prop
        collision="ConvexHulls", max_convex_hulls=4,  # "Box" for simple shapes, "None" for decor nobody touches
        lod_percents="1,0.5,0.25")                    # "" = one LOD (small or thin props)
print(r.message)            # tris before/after, final size, scale, LODs, collision shapes, slots, textures
print(r.bounds_min, r.bounds_max, r.textures)
```

What it does, in order: imports the file into a staging folder (GLB/glTF; FBX and OBJ work too), merges every
mesh into ONE StaticMesh at the asset path, turns it `yaw_deg` about Z, scales it (`fit_axis`: `Height` = Z,
`Width` = X, `Depth` = Y, `Longest`, `Box` = stretch to `target_box_cm` per axis, `None`), moves the pivot,
decimates to `max_triangles`, caps every texture's Maximum Texture Size (the built, cooked and loaded size; the
source pixels stay full-size in the .uasset, so raising the cap later needs no re-import), builds LODs and simple collision, keeps the
materials and textures next to the mesh as `MI_<Name>_<i>` (`M_` for a plain material) / `T_<Name>_<Role>`
(BaseColor, Normal, ...; `<Name>` is the asset name without `SM_`), and
deletes everything else the import made.

- **Idempotent.** The asset is tagged with the file's hash and the settings; the same call again returns
  `up_to_date=True` without touching anything. A new file or a changed setting replaces the mesh **in place**, so
  actors already placed keep working (`force=True` redoes it regardless).
- **Only intake's own assets are replaced.** A StaticMesh at the asset path that intake did not make is refused unless
  `force=True`. On a re-run, the previous run's materials and textures are deleted once nothing references them;
  other assets with the same names are never deleted (the new ones get `_2`, `_3`).
- **Read the report before placing.** `message` warns `stretched N%` when a `Box` fit distorted the proportions —
  prefer a uniform fit on the one dimension you know. A source of 0 triangles, a skinned file (imports as a
  SkeletalMesh) or an unknown `fit_axis` / `pivot` / `collision` fails with the reason.
- **Thin parts and decimation.** Lattices, trusses, chain links and wire break apart when decimated: give them a
  higher `max_triangles` and `lod_percents=""` rather than accepting holes.
- **Where the front faces is the model's business.** Generators pick their own forward axis. Render the result
  from several sides (or `CaptureEditorViewport` after placing it) and fix a wrong facing with `yaw_deg` in 90°
  steps; then re-run the same call (it replaces in place).

The full loop for a generated prop:

1. **Reference sheet first.** Build an all-angle identity sheet of the prop (if a `reference-images` skill is
   installed, use it) and look at it; re-roll the whole sheet when views disagree.
2. **Generate from the sheet's views with multi-view input**, never from one image: a single three-quarter view
   makes the generator invent every hidden side (a crystal shard came back as a cup).
3. **`intake_generated_mesh`** with the real-world size.
4. **Look at it**: a turnaround render (the reference-images skill's turnaround tooling, or captures from 4-8 sides)
   next to the sheet. Fix facing or size by re-running intake with new settings.
5. **Place it** with `EditorToolset.LevelToolset.AddToSceneFromAsset`; with the `BottomCenter` pivot the actor's
   location is the point on the floor.

## Return types

- `FModelingResult`: `success`, `message`, `handle`, `triangle_count`, `vertex_count`, `asset_path`.
- `FModelingMeshInfo` (`get_mesh_info`): `handle`, `triangle_count`, `vertex_count`, `is_closed`, `open_border_edges`, `connected_components`, `bounds_min`, `bounds_max`, `surface_area`, `volume`, `num_uv_layers`, `has_vertex_colors`, `material_ids`.
- `FModelingSplitResult` (`split_by_components`): `success`, `handles`.
- `FModelingPenetrationReport` (`measure_penetration`): `success`, `message`, `vertices_checked`, `violations`, `max_depth`, `average_depth`, `worst_position`, `worst_vertex`.
- `FModelingIntakeReport` (`intake_generated_mesh`): `success`, `message`, `asset_path`, `up_to_date`, `source_mesh_count`, `source_triangles`, `triangle_count`, `vertex_count`, `source_size`, `scale_applied`, `bounds_min`, `bounds_max`, `lod_count`, `collision_shapes`, `material_paths`, `textures`.

## Lofted solids (wings, fins, hulls)

`append_loft(h, transform, profile, frames, material_id)` sweeps a **closed** 2D profile through a list of
frames, caps both ends, and guarantees an outward-facing closed solid (it checks the enclosed volume and
flips when the sweep came out inside-out). Frame semantics:

- The profile lies in each frame's local **YZ** plane: profile X runs along the frame's Y axis, profile Y
  along its Z axis.
- The frame's **scale** multiplies the profile, axis for axis: **scale.Y scales profile X, scale.Z scales
  profile Y** (scale.X does nothing). So with identity-rotation frames marching along world X, a frame
  scale of `(1, half_width, half_height)` gives you a section that many cm wide and tall — which makes a
  hull or fuselage a table of `(x, z, half_width, half_height)` rows and nothing else:
  ```python
  prof = [V2(-1,-0.9), V2(-0.55,-1), V2(0.55,-1), V2(1,-0.9), V2(1,0.72), V2(0.6,1), V2(-0.6,1), V2(-1,0.72)]
  sections = [(-980,200,16,18), (-300,178,50,70), (60,188,64,96), (500,196,54,66), (760,176,14,15)]
  frames = [T(V(x, 0, z), R(0,0,0), V(1.0, w, h)) for (x, z, w, h) in sections]
  svc.append_loft(h, T(), prof, frames, 0)
  ```
- Frames are positioned along the path; two frames make a straight tapered section.
- Verify the mapping with a unit square through two frames and read `get_mesh_info` bounds before
  committing to a complex shape — it costs one call and saves rebuilding a hull.

A wing running out along +Y with its chord toward −X and 6 % thickness:

```python
airfoil = [unreal.Vector2D(x, y) for x, y in ((0,0),(0.05,0.03),(0.3,0.06),(0.7,0.04),(1,0),(0.7,-0.03),(0.3,-0.04),(0.05,-0.02))]
frames = [unreal.Transform(unreal.Vector(880, 100, 0), unreal.Rotator(0, 0, 90), unreal.Vector(480, 480, 480)),   # root: LE at x=880, chord 480
          unreal.Transform(unreal.Vector(600, 640, 0), unreal.Rotator(0, 0, 90), unreal.Vector(160, 160, 160))]   # tip
svc.append_loft(h, unreal.Transform(), airfoil, frames, 0)
```

`ensure_outward(h)` does the same volume check on any closed part you built another way (revolves, raw
sweeps) — run it before `self_union`, which silently discards inside-out parts as negative space.
Apply this to each separate part before appending it to an assembly, especially after reflecting
polygon coordinates. The assembly's total signed volume can be positive while a smaller component
is still inside-out; a single check on the combined handle will miss that component.

## Rigging control surfaces (rudders, flaps, canopies, turrets)

Bones and skin weights are authored straight on the session mesh; saving with an empty skeleton path
creates the Skeleton asset for you.

**Plan the components before you model.** `select_connected` is the only selection that grabs a moving
part exactly, so build each part that will get its own bone in its **own handle**, `self_union` it
there, and only then `append_mesh` it into the assembly. Appending never welds parts together, so the
result is one mesh whose pieces are still separately selectable — no cutting required:

```python
tower = svc.create_mesh().handle; ...; svc.self_union(tower)   # stays on the root bone
cap   = svc.create_mesh().handle; ...; svc.self_union(cap)     # will yaw
sails = svc.create_mesh().handle; ...; svc.self_union(sails)   # will spin
h = svc.create_mesh().handle
for part in (tower, cap, sails):
    svc.append_mesh(h, part, unreal.Transform()); svc.release_mesh(part)
assert svc.get_mesh_info(h).connected_components == 3           # check before rigging
```

Check `connected_components` matches the number of pieces you expect *before* rigging — a piece that
merged into its neighbour, or split into two, is far cheaper to fix now than after binding. Parts that
merely touch do **not** union: overlap them by a few cm, or they stay separate and a "solid" assembly
comes apart. A box selection is the fallback when two pieces genuinely are one component, but it will
also catch anything else inside the box (a shaft passing through a hub), and those stray triangles
tear when the bone moves.

1. **Isolate each moving piece** as its own connected component — either built separately as above, or
   cut free with a through-slot along the hinge line and at both ends (`boolean` Subtract with thin boxes).
2. **Grab it** with `select_connected(h, "flap_L", point_on_the_flap)` — no radius to tune, it takes the
   whole piece nearest the point. `selection_bounds` then gives you the hinge line (the piece's forward edge).
3. **Define the hierarchy** with `create_bones(h, [ModelingBoneDef, ...])`: the first bone is the root,
   every other one names a parent listed earlier. Transforms are in mesh space; point each bone's **X
   axis along its hinge** so the surface deflects by rolling the bone (a swept hinge just needs the
   right yaw). Everything starts bound to the root.
4. **Bind** each piece: `bind_selection_to_bone(h, "flap_L", "flap_L", 1.0)`.
5. **Verify** with `list_bones(h)` — every control bone should report the vertex count of its piece and
   the root the rest; a bone with 0 vertices means the selection missed.
6. **Save**: `save_mesh_to_skeletal_mesh(h, "/Game/Vehicles/SK_Jet", "", True, True)` creates
   `SK_Jet` plus `SK_Jet_Skeleton`; then `set_asset_materials("/Game/Vehicles/SK_Jet", "/Game/M_Paint,/Game/M_Glass")`
   fills the material slots (one slot per material ID).

```python
B = unreal.ModelingBoneDef
hinge_yaw = 0.0   # yaw of the hinge line in the XY plane
svc.create_bones(h, [
    B(name="root"),
    B(name="flap_L", parent_name="root", transform=unreal.Transform(unreal.Vector(534, 100, 0), unreal.Rotator(0, 0, 90 + hinge_yaw), unreal.Vector(1, 1, 1))),
])
```

Animate the result like any skeletal mesh — an AnimSequence keyed on the control bones (see the
`animation` skill), a Control Rig, or `SetBoneRotationByName` on a PoseableMeshComponent for a quick
check in a level.

For continuous or variable-driven motion (a rotor, a windmill sail, a turret) the usual answer is an
**Anim Blueprint driving the bone directly**, with no animation asset at all: Local Space Ref Pose →
Local To Component → one Modify Bone per control bone → Component To Local → Output, and an
EventGraph that accumulates the angle on `Event Blueprint Update Animation`. If VibeUE is installed,
its `animation-blueprint` skill has the whole recipe, including the trap that a Modify Bone node
ignores its Rotation pin until you set `rotation_mode` to additive in component space.

## Repairing skin weights (clothing that follows the wrong bone)

The classic failure on a transferred garment: a collar rides up with the head, a hem is glued to
one thigh. The cause is weight on bones that shouldn't own the region, and the fix is four calls —
no Blender round-trip:

```python
h = svc.load_mesh_from_skeletal_mesh("/Game/.../SK_Jacket").handle
n = svc.select_by_bone_weight(h, "collar", "head,neck_02", 0.15)   # the vertices those bones drag
print(svc.selection_bounds(h, "collar"))                           # sanity: is this really the collar?
svc.scale_bone_weights(h, "collar", "head,neck_02", 0.25, "spine_05")  # keep 25%, rest -> spine_05
svc.smooth_selection_bone_weights(h, "collar", 3, 0.5)             # feather the seam
print(svc.inspect_bone_weights(h, "collar").message)               # per-bone verts/avg/max + sums
svc.save_mesh_to_skeletal_mesh_lod(h)                              # back into the LOD it came from (repeat per LOD)
```

- `select_by_bone_weight` builds the region from the weights themselves — more reliable than a box
  when the garment's geometry is irregular. Intersect with `select_in_box` output via
  `expand_contract_selection`/`invert_selection` when only part of a bone's territory is wrong.
- `scale_bone_weights(h, sel, bones, scale, receiver)`: scale 0 removes the influence; with no
  receiver the freed weight goes to each vertex's strongest other influence. Vertices bound *only*
  to the listed bones are skipped unless a receiver is named — read the message.
- `smooth_selection_bone_weights` is a Laplacian pass over the selection: run it whenever a repair
  or a rigid `bind_selection_to_bone` leaves a visible cliff at the boundary.
- `inspect_bone_weights(h, sel="")` is the audit: influencing bones, per-bone average/max, weight
  sums (should be 1.0 ± quantization), max influences. Check it before saving, and verify the pose
  in the animation skill afterwards — bad weights are invisible in the ref pose.

## Editing a skeletal mesh LOD in place (garments, accessories, bodies)

Characters are skeletal: shirts, dresses, accessories and bodies all carry a skeleton, material slots, skin weights
and several LODs. Do not rebuild the asset to change its geometry; round-trip the LOD you are fixing:

```python
print(svc.inspect_skeletal_mesh("/Game/Chars/SKM_Shirt").message)   # slots, LODs (source model or generated), verts, bones used
for lod in range(3):
    h = svc.load_mesh_from_skeletal_mesh("/Game/Chars/SKM_Shirt", lod).handle
    # ... any edit: move vertices, smooth, repair weights, push out of the body ...
    r = svc.save_mesh_to_skeletal_mesh_lod(h)      # back to the asset and LOD it came from
    print(r.success, r.message)
    svc.release_mesh(h)
svc.save_mesh_to_skeletal_mesh_lod(low_handle, "/Game/Chars/SKM_Shirt", 3)   # lod_index == LOD count appends a LOD
```

- **Load reads the source model** (the editable vertex layout). Never mix in positions read through
  `GeometryScriptMeshReadLOD` with the default `MAX_AVAILABLE`/render data: render data splits vertices along seams, so
  its vertex count differs and a writer fails (`dm verts 10364 != weight verts 10350`). A LOD that was *generated by
  reduction* has no source model; the load message says so, and writing it back turns it into a custom LOD.
- **Save validates before it writes**, and writes nothing on failure: every vertex must carry skin weight (an
  unweighted vertex collapses to the root at runtime), every bone on the mesh must exist in the asset's skeleton
  (matched by name, so the order does not matter), bone and material indices must be in range, and a posed handle
  must be `unpose_mesh`-ed first. Afterwards it reads the LOD back and compares the vertex count.
- **What is kept:** skeleton and reference skeleton, every material slot and the LOD's material map, the other LODs,
  the LOD's recompute-normals/tangents settings, which section uses which slot, and — when the topology is unchanged
  since the load — the original vertex order (morph targets and anything else indexed by vertex). The LOD stops
  being auto-generated from LOD0 (it is a custom LOD now).
- A topology change (remesh, delete faces) is allowed; give new triangles a material ID the asset has a slot for and
  re-weight new vertices (`transfer_bone_weights`, `skin_to_surface`).
- **Normals.** `save_mesh_to_skeletal_mesh_lod` keeps the LOD's build settings: when they recompute normals, the
  engine rebuilds them from the new geometry and the handle's own normals are not used (the message says so).
  `save_mesh_to_skeletal_mesh` on an existing asset stores the handle's normals as they are, as it always has.
- **Lower LODs generated from the one you wrote** (no source model of their own) are regenerated by the engine from
  the new geometry; custom LODs are not touched.

**Changed behaviour of the two older calls** (scripts written before these sections existed):

- `load_mesh_from_skeletal_mesh(path, lod)` now **fails** for a LOD the asset does not have. It used to succeed: the
  engine silently read the nearest existing LOD while the result said the one asked for. Read the LOD count from
  `inspect_skeletal_mesh` instead of probing.
- `save_mesh_to_skeletal_mesh` on an **existing** asset (`replace_existing` defaults to True) replaces LOD0 through
  the validated writer. Kept that used to be lost: which section uses which material slot. Matched by name instead of
  by index: bones. Refused with nothing written, where a broken asset used to be saved: vertices without skin weight,
  bones the asset's skeleton lacks, a material ID with no slot, a posed handle. Creating a new asset is unchanged.

## Fitting garments: skin under cloth, cloth off skin, in the pose the game uses

Poke-through depends on the pose: a fit that is clean in the reference pose can be 10+ cm wrong seated, and
runtime post-process / corrective bones only exist on the running component. So read the meshes **as posed** —
from PIE when it is running — fit them against each other, then take the result back to the reference pose and
write it into the asset LOD:

```python
# the character is posed in PIE (or in the editor world); both handles in the same (component) space
body  = svc.load_mesh_from_skinned_component("BP_Ava", "Body",  False).handle
dress = svc.load_mesh_from_skinned_component("BP_Ava", "Dress", False).handle   # a leader-pose follower reads the leader's pose
r = svc.measure_penetration(body, dress, "Inside", 0.3, 5.0, "", "poking")      # skin poking through the dress
print(r.violations, r.max_depth, r.worst_position)
svc.push_inside(body, dress, 0.3, "", 5.0, 2)          # skin moved under the dress, feathered over 2 rings
svc.unpose_mesh(body)                                  # back to the reference pose (edits carried through the inverse skinning)
svc.save_mesh_to_skeletal_mesh_lod(body)               # into the body asset LOD it came from
```

| Need | Call |
|---|---|
| How bad is it (per garment, per pose) | `measure_penetration(a, b, "Inside", margin)`: `a` belongs under `b` (skin under cloth, an inner layer under an outer one); `"Outside"`: `a` belongs over `b` (cloth over skin, a collar off the face). `violations`, `max_depth`, `average_depth`, `worst_position`; `out_selection` stores the offenders |
| Push skin in under the cloth | `push_inside(body, garment, margin)` |
| Push cloth out of the body or face | `push_outside(garment, body, margin)` |
| Drop skin (or an inner garment) fully hidden under cloth | `select_hidden_by(body, "hidden", garment, margin)` then `delete_faces(body, "hidden")` |

- **Only covered vertices are judged.** A vertex counts when it is within `max_distance` of the other surface AND
  projects onto it (the offset runs along that face's normal). A head above a collar or hands past a sleeve are
  never dragged into the cloth; raise `max_distance` only for loose garments. The other mesh's face normals define
  "outside", so it needs consistent outward normals (any imported garment has them).
- **Push rather than delete for dresses and open garments.** Deleting skin under a dress leaves holes where the
  dress opens or moves (a see-through crotch, hollow legs). Push the skin in with a margin, and keep deletion for
  regions that are covered in every pose.
- **Fit in every pose the game uses.** One pose's fix can break another. Pose the character (PIE, each clip or
  seated/standing state), read, measure, push, unpose, save — once per pose, worst first — and re-measure the
  earlier poses at the end. (If VibeMetaHumans is installed, its wardrobe tools wrap this for MetaHuman bodies and
  their post-process bones; this service is the generic mesh-vs-mesh layer underneath.)
- **A posed handle cannot be saved as a reference pose** (`save_mesh_to_skeletal_mesh_lod` refuses it, and a
  `copy_mesh` of it stays posed). `unpose_mesh` first. Normals are not re-skinned in the pose; only positions are.
- The handle's vertex IDs are the asset LOD's source-model IDs, so selections, weights and the write-back all line
  up; morph targets and cloth simulation are not applied to the posed read.

## Accessories that follow a surface (ties, dog tags, scarves, holsters)

A static accessory attached to one bone rides rigidly while the cloth under it bends: a tie stays put while the
neck turns, dog tags float off a slumped chest. Skin it to the surface it lies on instead — no Blender, no hand-
written nearest-point loop:

```python
body = "/Game/Chars/Ava/SKM_Ava_Body"
# 1. the accessory, placed where it should sit, in the body's component space (here: from its static mesh + the fitted transform)
acc = svc.load_mesh_from_static_mesh("/Game/Props/SM_Tie").handle
svc.transform_mesh(acc, fitted_transform_in_body_space)
# 2. the surface it lies on, in the SAME space: the garment asset (its own reference pose; fine when it matches the body's) ...
shirt = svc.load_mesh_from_skeletal_mesh("/Game/Chars/Ava/SKM_Ava_Shirt").handle
#    ... or the garment exactly as displayed on a placed character (follower of the body):
# shirt = svc.load_mesh_from_skinned_component("BP_Ava", "Shirt", False).handle
# 3. weights from the closest surface point; the result carries the BODY's skeleton so it can follow the body
print(svc.skin_to_surface(acc, shirt, "Surface", "", body).message)     # top bones with their share
svc.save_mesh_to_skeletal_mesh(acc, "/Game/Props/SKM_Tie", "<the body's Skeleton asset path>")
# 4. lower LODs: move weight off bones the body drops at LOD1+ (they stop updating and the accessory streaks)
print(svc.remap_lod_bones("/Game/Props/SKM_Tie", body).message)
```

- **Modes.** `Surface` copies the weights of the closest surface point (thin pieces lying on the cloth). `Inpaint`
  keeps the clear matches and smoothly fills the rest (pieces that stand off the surface: a buckle, a holster).
  `Rigid` puts every vertex 100% on one bone — `rigid_bone_name`, or the bone the surface under it is most weighted
  to — for hard items that must not deform (a badge, a watch face). `smooth_iterations` / `smooth_strength` blur
  the transferred weights.
- **Both meshes must be in the same space.** The weights come from proximity: an accessory in world space against
  a garment in component space finds nothing sensible. Load the surface at the reference pose (asset) or as
  displayed (`load_mesh_from_skinned_component` with `world_space=False`) and put the accessory in that space.
- **`bones_from_skeletal_mesh`** gives the result another mesh's skeleton (the body it will follow); weights are
  matched by bone name. Without it the result carries the source's bones.
- **Attach** the saved mesh as a leader-pose follower of the body (a SkeletalMeshComponent with `set_leader_pose_component(body)`;
  if VibeMetaHumans is installed, its wardrobe service does the component swap for MetaHumans).
- **`remap_lod_bones(asset, reference)`** reads which bones the reference (the leader) keeps at each LOD, moves every
  influence on a dropped bone to its nearest kept ancestor and writes each LOD back in place. The follower's last LOD
  also covers the leader's lower LODs. Bones the leader does not have at all (the follower's own) are left alone.
  Only LODs with something to move are written; a written LOD that was generated by reduction becomes a custom LOD.
  Run it after any re-skin of a follower, and whenever a garment or accessory "streaks" or "explodes" only at distance.

## Texturing pipeline (UVs you control, masks you can make, pixels you can draw)

Auto-unwraps (`auto_uv`) are fine for baking AO, useless for markings: hundreds of seams and uneven
texel density. For anything an artist would paint, build the islands yourself and verify them.

1. **Define islands.** Assign a polygroup per panel/region: `compute_polygroups(h, "Angle", 30)` for
   hard-surface parts, then override with selections — `select_in_box` / `select_by_normal_angle` /
   `select_connected` + `set_polygroup(h, sel, -1)` (a new group) so a wing top, a fuselage side or a
   whole rudder is one island.
2. **Unwrap** each island conformally: `recompute_uvs(h, 0, "Polygroups", "SpectralConformal")`. Big
   round shapes (fuselage) can instead take `project_uv(h, "Cylinder", transform, 0, sel)`.
3. **Pack** — `layout_uv(h, 0, "Repack", "", 4096)` for one texture set, or
   `pack_uv_per_material(h, 0, 4096)` so every material slot (fuselage / wings / details) fills its own
   0–1 range and gets its own 4K.
4. **Verify** with `get_uv_stats(h)`: islands, coverage, `overlap_fraction` (must be ~0 unless you
   packed per material), `num_unset_triangles` (must be 0) and `texels_per_cm_at_1k`. Fix before
   baking. `world_to_uv(h, point)` tells you where a spot on the mesh lands on the texture.
5. **Masks without painting.** Select regions on the mesh, `set_vertex_color(h, sel, color)` per
   channel (R walkways, G stripes, B dirt…) and bake with `bake_textures(h, h, "VertexColor", …)`.
   `"MaterialID"` bakes a per-slot mask for free; `"UVShell"` draws the island outlines — panel lines
   when islands are panels; `"Height"` and the normals/AO/curvature bakes remain.
6. **Pixels.** `create_noise_texture(path, 2048, 2048, cells, octaves)` for tileable grunge/wear;
   `draw_on_texture(path, "Line|Dots|Rect|Fill", uv_points, color, thickness_px, spacing_px)` to draw
   rivet rows, stripes and blocks straight into an 8-bit texture in UV space (use `world_to_uv` to
   place them). For textures made elsewhere (a PolyHaven PBR set downloaded with `urllib` in the same Python script works),
   use Epic `ImportFile` then `SetProperties` inside `execute_python`. `ImportFile` is async: `await` it before `SetProperties`.
   `instance` is the imported texture's object path; `values` is a JSON STRING (`json.dumps`), not a dict.
   `CompressionSettings` = `TC_Default|TC_Normalmap|TC_Masks|TC_Grayscale|TC_HDR`; normal maps also want `LODGroup` = `TEXTUREGROUP_WorldNormalMap`:
   ```python
   import json
   await call_tool("EditorToolset.TextureToolset.ImportFile", folderPath="/Game/Textures", assetName="T_Rust_N",
                   sourceFile="C:/dl/rust_n.png", bReplaceExisting=True, bSave=False)
   await call_tool("EditorToolset.ObjectToolset.SetProperties", instance="/Game/Textures/T_Rust_N.T_Rust_N",
                   values=json.dumps({"SRGB": False, "CompressionSettings": "TC_Normalmap", "LODGroup": "TEXTUREGROUP_WorldNormalMap"}))
   ```
   The result is unsaved after `SetProperties`: save it afterwards (`unreal.EditorAssetLibrary.save_asset("/Game/Textures/T_Rust_N")`);
   `ImportFile`'s `bSave` would run before the property edit.
   `bake_texture_transfer(target, source, "T_A,T_B", …)` re-projects textures from one unwrap onto
   another (after re-unwrapping, or from a detail mesh).
7. **Geometry detail for normal maps.** Build it on a copy: `append_mesh_along_polyline(h, rivet, points,
   spacing)` for fastener rows, `append_mesh_at_transforms(h, bolt, sample_surface_points(...))` for
   scatter, `cut_groove_along_polyline(h, points, width, depth)` for recessed panel lines — then
   `bake_textures(game_mesh, detail_mesh, "TangentNormal,AmbientOcclusion", 4096, …)`.

Crisp markings (roundels, numbers) are still best as decals: `set_num_uv_layers(h, 2)` +
`project_uv(h, "Planar", transform, 1, sel)` gives a clamped UV1 the material samples a decal through.

## Reference pages

- [geometryscript-cheatsheet.md](geometryscript-cheatsheet.md) — real Python class names of every GeometryScript library, the list type each function takes, out-parameter tuples
- [modeled-lettering.md](modeled-lettering.md) — extruded logo lettering from font contours
- [showcase-and-delivery.md](showcase-and-delivery.md) — lighting, high-resolution capture, asset packs
- [scripts/exercise_all.txt](scripts/exercise_all.txt) — calls every ModelingService function and prints `RESULT: PASS`
