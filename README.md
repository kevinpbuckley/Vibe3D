> [!WARNING]
> **Unreal Engine 5.9 build: not fully tested yet.** This `5-9` branch builds and loads in UE 5.9, but
> it has not been tested end to end. Expect rough edges and report problems as issues. For production
> work, use the stable `main` branch.

# Vibe3D

Procedural 3D modeling as an **AI Editor Toolset** for Unreal Engine 5.9. Vibe3D exposes the
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

Build the project, then enable **Vibe3D** in Edit > Plugins (Editor category) and restart. Requires UE 5.9 with the engine plugins
**GeometryScripting**, **ToolsetRegistry**, **PythonScriptPlugin** and **EditorScriptingUtilities**
(all enabled automatically as dependencies). Works alongside [VibeUE](https://github.com/kevinpbuckley/VibeUE)
but does not depend on it.

## What you get

- **Toolset `Vibe3D.ModelingService`** on the MCP endpoint (`list_toolsets` / `describe_tools`) —
  ~135 tools. Same functions from Python: `unreal.ModelingService.<snake_case>(...)`.
- **Character and prop work, not only static modeling**: edit any skeletal mesh LOD in place (validated
  write-back that keeps skeleton, slots and the other LODs), read a placed or PIE character as posed and
  fit garments against the body (measure / push in / push out / hide), skin static accessories to the
  surface under them and remap bones a leader drops at lower LODs, and turn an image-to-3D GLB into a
  game-ready StaticMesh in one call (size, pivot, decimation, texture cap, LODs, collision, report).
- **Skill `/Skills/Vibe3D/modeling`** (`Skills/modeling/SKILL.md`): the session model, frame semantics
  for sweeps and lofts, the texturing pipeline (islands → unwrap → pack → verify → masks → pixels →
  detail normals), rigging control surfaces, skeletal LOD round trips, garment fit, accessory re-skin,
  generated-prop intake, a GeometryScript Python-name cheat sheet, gotchas. UE 5.9 discovers `<Plugin>/Skills/**/SKILL.md`
  natively and re-reads it on every `GetSkills` call, so edits need no reload or restart. Its sub-pages
  are linked from the skill's **Reference pages** section.
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

- Automation: `Automation RunTests Vibe3D.Modeling` (20 suites, self-provisioning; they write only under
  `/Game/Developers/Vibe3DModelingTests` and `Saved/Vibe3DTests`, and delete what they made). In an
  interactive editor it must be in the foreground — the automation framework waits for an interactive
  frame rate. Headless works without that: `UnrealEditor-Cmd.exe <Project>.uproject
  -ExecCmds="Automation RunTests Vibe3D.Modeling; Quit" -unattended -nopause -nullrhi`.
- Coverage: run `Skills/modeling/scripts/exercise_all.txt` as editor Python — it calls every
  public function and ends with `RESULT: PASS`.
- Agent prompts: `test_prompts/modeling/modeling_tests.md`.

## Layout

```
Vibe3D.uplugin
Source/Vibe3D/            module (registers UToolsetDefinition subclasses with the ToolsetRegistry)
  Public/Vibe3D/UModelingService.h
  Private/Vibe3D/UModelingService.cpp, ModelingServiceTests.cpp
Skills/modeling/          SKILL.md + sub-pages + scripts/exercise_all.txt (UE 5.9 native skill discovery)
Content/samples/          AGENTS.md.sample (the agent guide Vibe3D.GenerateAgentConfig writes)
test_prompts/modeling/    sequential agent prompts covering every function
```

## License

MIT — see [LICENSE](LICENSE).
