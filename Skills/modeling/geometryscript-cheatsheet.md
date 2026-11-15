<!-- Copyright Buckley Builds LLC 2026 All Rights Reserved. -->

# GeometryScript from Python: real class names and list types (UE 5.9)

The Python class of a GeometryScript library is its `ScriptName`, not its C++ name, so it cannot be guessed from the
header or the Blueprint category. Every name below was read from `dir(unreal)` in a UE 5.9 editor. When a name is
not here, list them yourself: `[n for n in dir(unreal) if n.startswith("GeometryScript_")]`.

**Before reaching for GeometryScript, check ModelingService.** `svc.get_vertex_positions(h)` /
`svc.set_vertex_positions(h, positions)` replace the `get_all_vertex_positions` + `convert_vector_list_to_array`
round trip, and the selection, bone-weight and asset calls replace most raw loops. To use a library on a session
mesh: `dm = svc.get_dynamic_mesh(h)`; the library edits it in place and the next `svc.*` call sees the result.

## Wrong guesses agents made, and the real name

| Guessed (fails with `module 'unreal' has no attribute ...`) | Real Python class |
|---|---|
| `GeometryScript_MeshBasicEdits`, `GeometryScript_MeshBasicEdit` | `GeometryScript_MeshEdits` |
| `GeometryScript_CreateNewAsset`, `GeometryScript_CreateNewAssetFunctions` | `GeometryScript_NewAssetUtils` |
| `GeometryScript_StaticMeshFunctions`, `GeometryScript_MeshAssetFunctions` | `GeometryScript_AssetUtils` (static AND skeletal mesh read/write) |
| `GeometryScript_MeshPrimitives` | `GeometryScript_Primitives` |
| `GeometryScript_MeshUVs`, `GeometryScript_MeshUVFunctions` | `GeometryScript_UVs` |
| `GeometryScript_MeshNormals` | `GeometryScript_Normals` |
| `GeometryScript_MeshMaterials` | `GeometryScript_Materials` |
| `GeometryScript_MeshBoneWeights`, `GeometryScript_BoneWeightFunctions` | `GeometryScript_BoneWeights` |
| `GeometryScript_MeshSimplify` | `GeometryScript_MeshSimplification` |
| `GeometryScript_MeshRemesh` | `GeometryScript_Remeshing` |
| `GeometryScript_MeshDeform` | `GeometryScript_MeshDeformers` |
| `GeometryScript_MeshVoxel` | `GeometryScript_MeshVoxelProcessing` |
| `GeometryScript_MeshGeodesics` | `GeometryScript_MeshGeodesic` |
| `GeometryScript_MeshPolygroups`, `GeometryScript_Polygroups` | `GeometryScript_PolyGroups` |
| `GeometryScript_ListUtils`, `GeometryScript_ListUtilityFunctions` | `GeometryScript_List` |
| `GeometryScript_SceneUtilityFunctions` | `GeometryScript_SceneUtils` |
| `GeometryScript_MeshBake` | `GeometryScript_Bake` |
| `GeometryScript_MeshWeightMap` | `GeometryScript_WeightMaps` |

## Every library

| Python class | C++ class (header) | What you use it for |
|---|---|---|
| `GeometryScript_AssetUtils` | `UGeometryScriptLibrary_StaticMeshFunctions` (MeshAssetFunctions.h) | `copy_mesh_from_static_mesh`, `copy_mesh_to_static_mesh`, `copy_mesh_from_skeletal_mesh`, `copy_mesh_to_skeletal_mesh`, `get_section_material_list_from_static_mesh`, `get_lod_material_list_from_skeletal_mesh`, `copy_skin_weight_profile_to_skeletal_mesh`, `copy_morph_target_to_skeletal_mesh` |
| `GeometryScript_NewAssetUtils` | `UGeometryScriptLibrary_CreateNewAssetFunctions` (CreateNewAssetUtilityFunctions.h) | `create_new_static_mesh_asset_from_mesh`, `create_new_skeletal_mesh_asset_from_mesh`, `create_new_texture2d_asset`, `create_unique_new_asset_path_name` |
| `GeometryScript_MeshEdits` | `UGeometryScriptLibrary_MeshBasicEditFunctions` (MeshBasicEditFunctions.h) | `append_mesh`, `set_vertex_position`, `set_all_mesh_vertex_positions`, `add_vertices_to_mesh`, `add_triangles_to_mesh`, `delete_triangles_from_mesh`, `delete_selected_triangles_from_mesh`, `discard_mesh_attributes` |
| `GeometryScript_MeshQueries` | `UGeometryScriptLibrary_MeshQueryFunctions` (MeshQueryFunctions.h) | `get_vertex_count`, `get_vertex_position`, `get_all_vertex_positions`, `get_all_vertex_i_ds`, `get_all_triangle_i_ds`, `get_mesh_bounding_box`, `get_mesh_volume_area`, `get_is_closed_mesh`, `get_num_open_border_edges`, `get_has_vertex_id_gaps` |
| `GeometryScript_BoneWeights` | `UGeometryScriptLibrary_MeshBoneWeightFunctions` (MeshBoneWeightFunctions.h) | `get_vertex_bone_weights`, `set_vertex_bone_weights`, `get_all_bones_info`, `get_bone_info`, `copy_bones_from_mesh`, `transfer_bone_weights_from_mesh`, `prune_bone_weights`, `compute_smooth_bone_weights` |
| `GeometryScript_MeshSelection` | `UGeometryScriptLibrary_MeshSelectionFunctions` (MeshSelectionFunctions.h) | `select_mesh_elements_in_box`, `select_mesh_elements_by_normal_angle`, `select_mesh_elements_inside_mesh`, `convert_index_array_to_mesh_selection`, `convert_mesh_selection_to_index_array`, `expand_contract_mesh_selection`, `invert_mesh_selection` |
| `GeometryScript_MeshSelectionQueries` | `UGeometryScriptLibrary_MeshSelectionQueryFunctions` | `get_mesh_selection_bounding_box`, `get_mesh_selection_boundary_loops` |
| `GeometryScript_MeshTransforms` | `UGeometryScriptLibrary_MeshTransformFunctions` | `transform_mesh`, `translate_mesh`, `rotate_mesh`, `scale_mesh`, `translate_pivot_to_location`, `*_selection` variants |
| `GeometryScript_MeshSpatial` | `UGeometryScriptLibrary_MeshSpatial` (MeshSpatialFunctions.h) | `build_bvh_for_mesh`, `find_nearest_point_on_mesh`, `find_nearest_points_on_mesh` (batch), `find_nearest_ray_intersection_with_mesh`, `is_point_inside_mesh` |
| `GeometryScript_Primitives` | `UGeometryScriptLibrary_MeshPrimitiveFunctions` | `append_box`, `append_sphere_lat_long`, `append_cylinder`, `append_sweep_polygon`, `append_revolve_polygon`, `append_simple_extrude_polygon`, `append_triangulated_polygon` |
| `GeometryScript_MeshBooleans` | `UGeometryScriptLibrary_MeshBooleanFunctions` | `apply_mesh_boolean`, `apply_mesh_self_union`, `apply_mesh_plane_cut`, `apply_mesh_mirror` |
| `GeometryScript_MeshModeling` | `UGeometryScriptLibrary_MeshModelingFunctions` | `apply_mesh_offset`, `apply_mesh_shell`, `apply_mesh_linear_extrude_faces`, `apply_mesh_inset_outset_faces`, `apply_mesh_bevel_selection` |
| `GeometryScript_MeshDeformers` | `UGeometryScriptLibrary_MeshDeformFunctions` | `apply_displace_from_per_vertex_vectors`, `apply_iterative_smoothing_to_mesh`, `apply_perlin_noise_to_mesh2`, `apply_bend_warp_to_mesh` |
| `GeometryScript_MeshRepair` | `UGeometryScriptLibrary_MeshRepairFunctions` | `compact_mesh`, `weld_mesh_edges`, `fill_all_mesh_holes`, `remove_small_components`, `remove_hidden_triangles`, `select_hidden_triangles_from_outside` |
| `GeometryScript_MeshSimplification` | `UGeometryScriptLibrary_MeshSimplifyFunctions` | `apply_simplify_to_triangle_count`, `apply_simplify_to_tolerance`, `apply_simplify_to_planar` |
| `GeometryScript_Remeshing` | `UGeometryScriptLibrary_RemeshingFunctions` | `apply_uniform_remesh`, `apply_adaptive_remesh` |
| `GeometryScript_MeshSubdivide` | `UGeometryScriptLibrary_MeshSubdivideFunctions` | `apply_pn_tessellation`, `apply_uniform_tessellation`, `apply_selective_tessellation` |
| `GeometryScript_OpenSubdiv` | `UGeometryScriptLibrary_OpenSubdivFunctions` | `apply_polygroup_catmull_clark_sub_d`, `apply_triangle_loop_sub_d` |
| `GeometryScript_MeshVoxelProcessing` | `UGeometryScriptLibrary_MeshVoxelFunctions` | `apply_mesh_solidify`, `apply_mesh_morphology` |
| `GeometryScript_MeshDecomposition` | `UGeometryScriptLibrary_MeshDecompositionFunctions` | `copy_mesh_to_mesh`, `copy_mesh_selection_to_mesh`, `split_mesh_by_components`, `split_mesh_by_material_i_ds` |
| `GeometryScript_MeshComparison` | `UGeometryScriptLibrary_MeshComparisonFunctions` | `measure_distances_between_meshes`, `is_intersecting_mesh`, `is_same_mesh_as` |
| `GeometryScript_Containment` | `UGeometryScriptLibrary_ContainmentFunctions` | `compute_mesh_convex_hull`, `compute_mesh_convex_decomposition`, `compute_mesh_oriented_box` |
| `GeometryScript_Collision` | `UGeometryScriptLibrary_CollisionFunctions` | `set_static_mesh_collision_from_mesh`, `generate_collision_from_mesh`, `get_simple_collision_shape_count` |
| `GeometryScript_UVs` | `UGeometryScriptLibrary_MeshUVFunctions` | `auto_generate_x_atlas_mesh_u_vs`, `auto_generate_patch_builder_mesh_u_vs`, `repack_mesh_u_vs`, `set_num_uv_sets`, `get_mesh_per_vertex_u_vs` |
| `GeometryScript_Normals` | `UGeometryScriptLibrary_MeshNormalsFunctions` | `recompute_normals`, `compute_split_normals`, `compute_tangents`, `flip_normals`, `get_mesh_per_vertex_normals` |
| `GeometryScript_Materials` | `UGeometryScriptLibrary_MeshMaterialFunctions` | `enable_material_i_ds`, `set_material_id_for_mesh_selection`, `remap_material_i_ds`, `get_all_triangle_material_i_ds` |
| `GeometryScript_PolyGroups` | `UGeometryScriptLibrary_MeshPolygroupFunctions` | `compute_polygroups_from_angle_threshold`, `set_polygroup_for_mesh_selection`, `get_triangles_in_polygroup` |
| `GeometryScript_VertexColors` | `UGeometryScriptLibrary_MeshVertexColorFunctions` | `set_mesh_constant_vertex_color`, `set_mesh_selection_vertex_color`, `get_mesh_per_vertex_colors`, `set_mesh_per_vertex_colors` |
| `GeometryScript_WeightMaps` | `UGeometryScriptLibrary_MeshWeightMapFunctions` | `find_or_add_mesh_weight_map`, `get_mesh_weight_map_values`, `set_mesh_weight_map_values` |
| `GeometryScript_MeshSampling` | `UGeometryScriptLibrary_MeshSamplingFunctions` | `compute_point_sampling`, `compute_vertex_weighted_point_sampling` |
| `GeometryScript_MeshGeodesic` | `UGeometryScriptLibrary_MeshGeodesicFunctions` | `get_shortest_vertex_path`, `create_surface_path` |
| `GeometryScript_MeshSculptLayers` | `UGeometryScriptLibrary_MeshSculptLayersFunctions` | `enable_sculpt_layers`, `set_sculpt_layer_weight` |
| `GeometryScript_Bake` | `UGeometryScriptLibrary_MeshBakeFunctions` | `bake_texture`, `bake_vertex`, `make_bake_type_*` |
| `GeometryScript_SceneUtils` | `UGeometryScriptLibrary_SceneUtilityFunctions` | `copy_mesh_from_component` (render geometry of any mesh component), `set_component_material_list` |
| `GeometryScript_MeshPoolUtility` | `UGeometryScriptLibrary_MeshPoolFunctions` | `get_global_mesh_pool` (temporary meshes in Blueprints; in Python just use `unreal.DynamicMesh()`) |
| `GeometryScript_List` | `UGeometryScriptLibrary_ListUtilityFunctions` | `convert_*_list_to_array`, `convert_array_to_*_list`, `get_*_list_length`, `get_*_list_item` (see below) |
| `GeometryScript_VectorMath` | `UGeometryScriptLibrary_VectorMathFunctions` | element-wise math on Vector/Scalar lists (`vector_length`, `vector_dot`, `scalar_multiply`, ...) |
| `GeometryScript_TextureUtils` | `UGeometryScriptLibrary_TextureMapFunctions` | `sample_texture2d_at_uv_positions` |
| `GeometryScript_EditorTextureUtils` | `UGeometryScriptLibrary_EditorTextureMapFunctions` | `channel_pack` |
| `GeometryScript_VolumeBake` | `UGeometryScriptLibrary_VolumeTextureBakeFunctions` | `bake_signed_distance_to_volume_texture` |
| `GeometryScript_EditorDynamicMeshUtil` | `UGeometryScriptLibrary_EditorDynamicMeshFunctions` | `stash_debug_mesh`, `fetch_debug_mesh` |
| `GeometryScript_Box`, `GeometryScript_OrientedBox`, `GeometryScript_Ray`, `GeometryScript_Transform` | ShapeFunctions.h | plain value math (boxes, rays, frames) |
| `GeometryScript_SimplePolygon`, `GeometryScript_PolygonList`, `GeometryScript_PolyPath` | PolygonFunctions.h, PolyPathFunctions.h | 2D polygons and paths for extrudes, revolves and sweeps |
| `GeometryScript_PointSetSampling` | PointSetFunctions.h | `downsample_points`, `k_means_cluster_to_arrays` |

Names are snake_case of the C++ function, with UE's splitting of capital runs: `GetAllVertexIDs` becomes
`get_all_vertex_i_ds`, `AutoGenerateXAtlasMeshUVs` becomes `auto_generate_x_atlas_mesh_u_vs`. When unsure, `dir()` the class.

## List types: which one a function wants

GeometryScript passes arrays as wrapper structs. They are not interchangeable and not Python lists: a
`GeometryScriptVectorList` where a `GeometryScriptScalarList` is expected fails, and so does a plain list.

| Struct (Python) | Element | Make from a Python list | Read into a Python list |
|---|---|---|---|
| `unreal.GeometryScriptIndexList` | int (vertex, triangle, edge, material or group IDs) | `GeometryScript_List.convert_array_to_index_list(ints, unreal.GeometryScriptIndexType.VERTEX)` | `GeometryScript_List.convert_index_list_to_array(lst)` |
| `unreal.GeometryScriptTriangleList` | `IntVector` (3 vertex IDs) | `convert_array_to_triangle_list(int_vectors)` | `convert_triangle_list_to_array(lst)` |
| `unreal.GeometryScriptScalarList` | float (per-vertex weights, distances, noise) | `convert_array_to_scalar_list(floats)` | `convert_scalar_list_to_array(lst)` |
| `unreal.GeometryScriptVectorList` | `Vector` (positions, normals, offsets) | `convert_array_to_vector_list(vectors)` | `convert_vector_list_to_array(lst)` |
| `unreal.GeometryScriptUVList` | `Vector2D` | `convert_array_to_uv_list(uvs)` | `convert_uv_list_to_array(lst)` |
| `unreal.GeometryScriptColorList` | `LinearColor` | `convert_array_to_color_list(colors)` | `convert_color_list_to_array(lst)` |

All conversion functions are on `unreal.GeometryScript_List`. In Python they return the converted value
(`arr = unreal.GeometryScript_List.convert_vector_list_to_array(vl)`), not an out-argument.

Functions that take or return a list, by type:

- **VectorList**: `MeshQueries.get_all_vertex_positions` (out), `MeshEdits.set_all_mesh_vertex_positions`,
  `MeshEdits.add_vertices_to_mesh`, `MeshDeformers.apply_displace_from_per_vertex_vectors`,
  `MeshDeformers.compute_perlin_noise` (positions in; returns a **ScalarList**), `MeshSpatial.find_nearest_points_on_mesh`
  (query points in; positions and barycentrics out), `MeshSpatial.find_nearest_ray_intersections_with_mesh`,
  `Normals.get_mesh_per_vertex_normals` / `set_mesh_per_vertex_normals`, `Normals.get_mesh_per_vertex_tangents` /
  `set_mesh_per_vertex_tangents`, `VectorMath.vector_*`.
- **ScalarList**: `WeightMaps.get_mesh_weight_map_values` / `set_mesh_weight_map_values`,
  `MeshSampling.compute_vertex_weighted_point_sampling` (vertex weights), `MeshBooleans.apply_mesh_iso_curves`
  (per-vertex value), `UVs.get_mesh_uv_distortion_metrics` (out), `MeshSpatial.find_nearest_ray_intersections_with_mesh`
  (hit distances out), `VectorMath.scalar_*`, `VectorMath.vector_length` / `vector_dot` / `vector_to_scalar` (return one).
  Per-vertex scalars are a ScalarList even when you computed them from vectors: `vector_length(vl)` turns one into the other.
- **IndexList**: `MeshEdits.delete_triangles_from_mesh` / `delete_vertices_from_mesh`, the `MeshQueries.get_all_*_i_ds`
  family (out), `Materials.get_all_triangle_material_i_ds` / `set_all_triangle_material_i_ds` /
  `set_material_id_on_triangles`, `PolyGroups.get_all_triangle_polygroup_i_ds` / `get_triangles_in_polygroup`,
  `MeshSelection.convert_index_list_to_mesh_selection` / `convert_mesh_selection_to_index_list`,
  `MeshDecomposition.get_sub_mesh_from_mesh`, `MeshGeodesic.get_shortest_vertex_path` (out).
- **TriangleList**: `MeshEdits.add_triangles_to_mesh`, `MeshQueries.get_all_triangle_indices` (out),
  `Normals.get_mesh_per_corner_normal_element_i_ds` (out).
- **UVList**: `UVs.get_mesh_per_vertex_u_vs` (out), `TextureUtils.sample_texture2d_at_uv_positions` (in; colours out as a ColorList).
- **ColorList**: `VertexColors.get_mesh_per_vertex_colors` / `set_mesh_per_vertex_colors`.

Selections are a different struct again (`unreal.GeometryScriptMeshSelection`); go between them and index lists with
`MeshSelection.convert_index_array_to_mesh_selection(mesh, ids, unreal.GeometryScriptMeshSelectionType.VERTICES)` and
`convert_mesh_selection_to_index_array`.

## Calling conventions that trip scripts

- **Out parameters come back as a tuple, after the return value.** `get_all_vertex_positions(mesh, skip_gaps)` returns
  `(mesh, position_list, has_vertex_id_gaps)`; `copy_mesh_from_static_mesh(sm, dm, options, read_lod)` returns
  `(dm, outcome)`. Unpack them; do not index the result as if it were the list.
- **`skip_gaps`.** Vertex IDs are not dense after deletes, booleans or simplification. `get_all_vertex_positions(dm, False)`
  keeps the slots (the list index is the vertex ID) and `set_all_mesh_vertex_positions` expects that layout back.
  `svc.get_vertex_positions` / `svc.set_vertex_positions` always use it.
- **Skeletal meshes: read SOURCE_MODEL when you will write back.** `GeometryScriptMeshReadLOD.lod_type` defaults to
  `MAX_AVAILABLE`, and render data splits vertices along UV and normal seams, so its vertex count differs from the
  source model a writer expects (a jam script failed with `dm verts 10364 != weight verts 10350`).
  `svc.load_mesh_from_skeletal_mesh(path, lod)` reads the source model and `svc.save_mesh_to_skeletal_mesh_lod(h)`
  validates and writes the LOD back in place, so a skeletal round trip needs no raw `copy_mesh_to_skeletal_mesh`.
- Every library function takes the `UDynamicMesh` first and edits it in place; the returned mesh is the same object.
