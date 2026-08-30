<!-- Copyright Buckley Builds LLC 2026 All Rights Reserved. -->

# Vibe3D — Fab listing copy

Draft store text. Paste into the Fab product page; keep the headline under the field limit.

---

## Short description

Build, unwrap, bake, rig and save meshes from an AI agent or a Python script — Modeling Mode's
operators as one editor toolset, no viewport required.

## Long description

**Vibe3D turns Unreal's Modeling Mode into an API.**

The 97 interactive modeling tools are built on a layer of geometry operators. Vibe3D exposes that
layer — 120 functions — as a single AICallable service on Unreal Engine 5.8's native toolset
registry, and as plain editor Python. An agent connected to the editor's MCP endpoint, a build
script, or a tools programmer in the Python console can all drive the same API.

Meshes live in an in-editor session as integer handles wrapping a `UDynamicMesh`: create one, append
primitives, cut it with booleans, select faces, extrude and bevel, unwrap it, bake maps from a
high-poly copy, add bones and skin weights, then save it as a StaticMesh or SkeletalMesh with
collision and LODs. Nothing needs a gizmo, a viewport or a round trip through Blender.

**What's in the box**

- **Primitives & lofts** — box, sphere, cylinder, cone, capsule, torus, disc, stairs, revolve,
  extrude-polygon, sweep, and a capped `append_loft` that guarantees an outward-facing solid.
- **Booleans & poly editing** — union / subtract / intersection, self-union, plane cut, mirror,
  groove cutting; extrude, inset, outset, offset, bevel and shell on named selections.
- **Selections that make sense to a script** — by normal angle, box, sphere, material id, polygroup,
  or connected component; expand / contract / invert; stale selections are refused rather than
  silently acting on the wrong triangles.
- **Cleanup & topology** — remesh, simplify (to triangles, vertices, tolerance, or losslessly
  planar), subdivide, smooth, fill holes, weld, repair, remove hidden triangles, split components.
- **UVs & texturing** — XAtlas / PatchBuilder auto-UV, planar / box / cylinder projection, conformal
  re-unwrap per polygroup, repack, per-material packing, and `get_uv_stats` so you can verify island
  count, coverage, overlap and texel density before you bake.
- **Baking** — tangent normal, AO, curvature, object normal, position, vertex colour, material id,
  UV shell and height, plus texture transfer between unwraps.
- **Rigging** — author bones and skin weights on the session mesh, bind selections to bones, and
  save a SkeletalMesh with its Skeleton created for you.
- **Assets** — save / replace StaticMesh and SkeletalMesh, assign material slots, generate collision
  (convex hulls, boxes, spheres, capsules, swept hulls), set LODs, spawn actors.
- **Deformation and voxels** — bend, twist, flare, noise, texture displacement, solidify, morphology.

**Built for agents**

Vibe3D ships a companion **agent skill** that is registered with the editor at start-up: the session
model, loft and sweep frame semantics, the texturing pipeline, rigging control surfaces, the
gotchas that actually bite, and the verification habits that stop an agent from reporting success on
a broken mesh. `Vibe3D.GenerateAgentConfig` writes a matching guide into your project's
`CLAUDE.md` / `GEMINI.md` / `AGENTS.md` inside a managed block, so re-running refreshes only that
section.

Anything the service does not wrap is still reachable: `get_dynamic_mesh(handle)` hands you the live
`UDynamicMesh` to pass to any `GeometryScript_*` library in the same script.

**Works with, but does not require, [VibeUE](https://github.com/kevinpbuckley/VibeUE).**

---

## Technical notes for the listing body

- Editor-only plugin (module type `Editor`) for Unreal Engine 5.8, Win64 / Linux / Mac.
- Depends on the engine plugins GeometryScripting, GeometryProcessing, MeshModelingToolset,
  ToolsetRegistry, PythonScriptPlugin and EditorScriptingUtilities — all enabled automatically.
  ToolsetRegistry and PythonScriptPlugin are Experimental in 5.8.
- No third-party libraries, no network calls, no telemetry.
