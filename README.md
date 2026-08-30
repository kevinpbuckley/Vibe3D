# Vibe3D

Procedural 3D modeling as an **AI Editor Toolset** for Unreal Engine 5.8. Vibe3D exposes the
operators behind Modeling Mode — primitives, booleans, lofts, poly-edit, remeshing, deformation,
voxels, UVs, baking, rigging, collision, LODs — as one AICallable service on Unreal's native
ToolsetRegistry / MCP endpoint, plus a companion agent skill that teaches the workflow.

An agent connected to the editor's MCP server can build, unwrap, bake, rig and save a mesh without a
viewport: meshes live in an in-editor session as integer handles wrapping a `UDynamicMesh`.

## Install

```
cd <YourProject>/Plugins
git clone https://github.com/kevinpbuckley/Vibe3D.git
```

Build the project, then enable **Vibe3D** in Edit > Plugins (Editor category) and restart. Requires UE 5.8 with the engine plugins
**GeometryScripting**, **ToolsetRegistry**, **PythonScriptPlugin** and **EditorScriptingUtilities**
(all enabled automatically as dependencies). Works alongside [VibeUE](https://github.com/kevinpbuckley/VibeUE)
but does not depend on it.

## What you get

- **Toolset `Vibe3D.ModelingService`** on the MCP endpoint (`list_toolsets` / `describe_toolset`) —
  ~120 tools. Same functions from Python: `unreal.ModelingService.<snake_case>(...)`.
- **Skill `modeling`** (`Content/Skills/modeling/SKILL.md`): the session model, frame semantics for
  sweeps and lofts, the texturing pipeline (islands → unwrap → pack → verify → masks → pixels →
  detail normals), rigging control surfaces, gotchas. Registered at editor start; re-read with the
  console command `Vibe3D.ReloadSkills`.
- `get_dynamic_mesh(handle)` hands the mesh to any `unreal.GeometryScript_*` library for the long tail.
- **Agent guide**: console command `Vibe3D.GenerateAgentConfig [ClaudeCode|Gemini|Codex|Hermes|Cursor|Copilot|All] [import]`
  writes `Content/samples/AGENTS.md.sample` into your project's `CLAUDE.md` / `GEMINI.md` / `AGENTS.md`
  (default `All`) inside a `<!-- BEGIN Vibe3D -->…<!-- END Vibe3D -->` block, so re-running refreshes
  only that block and leaves the rest of the file alone. `import` writes a one-line `@path` for the
  two clients that resolve imports (Claude Code, Gemini CLI).

Quick taste (editor Python):

```python
import unreal
svc = unreal.ModelingService
h = svc.create_mesh().handle
svc.append_box(h, unreal.Transform(), 100, 60, 40)
svc.select_by_normal_angle(h, "top", unreal.Vector(0, 0, 1), 5)
svc.inset_faces(h, "top", 4); svc.extrude_faces(h, "top", -2)
svc.auto_uv(h, "XAtlas"); print(svc.get_uv_stats(h).message)
svc.save_mesh_to_static_mesh(h, "/Game/Props/SM_Crate", True, True, False, True)
svc.generate_collision("/Game/Props/SM_Crate", "ConvexHulls", 4)
```

## Testing

- Automation: `Automation RunTests Vibe3D.Modeling` (12 suites, self-provisioning; they write only under
  `/Game/Developers/Vibe3DModelingTests` and delete it). The editor must be in the foreground — the
  automation framework waits for an interactive frame rate.
- Coverage: run `Content/Skills/modeling/scripts/exercise_all.txt` as editor Python — it calls every
  public function and ends with `RESULT: PASS`.
- Agent prompts: `test_prompts/modeling/modeling_tests.md`.

## Layout

```
Vibe3D.uplugin
Source/Vibe3D/            module (registers UToolsetDefinition subclasses with the ToolsetRegistry)
  Public/Vibe3D/UModelingService.h
  Private/Vibe3D/UModelingService.cpp, ModelingServiceTests.cpp
Content/Skills/modeling/  SKILL.md + scripts/exercise_all.txt
Content/Python/init_unreal.py   registers the skills as native AgentSkills
test_prompts/modeling/    sequential agent prompts covering every function
```

## License

Copyright Buckley Builds LLC 2026. All rights reserved.
