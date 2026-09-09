<!-- Copyright Buckley Builds LLC 2026 All Rights Reserved. -->

# Vibe3D — Fab "Technical Information" form

Copy/paste answers for the Fab listing form.

**Distribution Method:** Code plugin (source only — Epic compiles)

**Modules**

| Name | Type | Loading Phase |
|---|---|---|
| Vibe3D | Editor | Default |

**Supported Engine Versions:** 5.8

**Supported Target Build Platforms:** Win64, Linux, Mac

**Engine Plugin Dependencies:** `GeometryScripting`, `GeometryProcessing`, `MeshModelingToolset`,
`ToolsetRegistry`, `PythonScriptPlugin`, and `EditorScriptingUtilities`. All are declared in
`Vibe3D.uplugin` and enabled automatically. `ToolsetRegistry` and `PythonScriptPlugin` are
Experimental in Unreal Engine 5.8.

**Number of C++ Classes / source files:** 1 service class (`UModelingService`) plus the module
implementation — 4 source files (2 `.cpp`, 1 `.h`, 1 test `.cpp`), ~4,800 lines.

**Number of exposed functions:** 120 `AICallable` `UFUNCTION`s on `UModelingService`, all also
callable from editor Python as `unreal.ModelingService.<snake_case>(...)`.

**Network Replicated:** No (editor-only tooling)

**Documentation:** `README.md` in the plugin, plus an in-editor agent skill
(`Content/Skills/modeling/SKILL.md`) registered with the engine's toolset registry at editor start
and re-loadable with the `Vibe3D.ReloadSkills` console command. Agent-guide generator:
`Vibe3D.GenerateAgentConfig [ClaudeCode|Gemini|Codex|Hermes|Cursor|Copilot|All] [import]`.

**Important / Additional Notes**

- Editor-only. The module is `Type: Editor`; nothing is exposed in a packaged game.
- No third-party libraries, no external services, no network calls, no telemetry. The plugin ships no binaries.
- Content is non-asset only (the agent skill, a Python bootstrap and a sample guide) — there are no
  `.uasset` or `.umap` files, so nothing to cook.

**Prerequisites:** Unreal Engine 5.8 with the C++ toolchain installed (the plugin is a code plugin).
An MCP-capable agent client is optional — every function is equally usable from the editor's Python
console or from Blueprint-free editor scripting.

## Technical notes for the listing body

- Editor-only plugin (module type `Editor`) for Unreal Engine 5.8, Win64 / Linux / Mac.
- Depends on the engine plugins GeometryScripting, GeometryProcessing, MeshModelingToolset,
  ToolsetRegistry, PythonScriptPlugin and EditorScriptingUtilities — all enabled automatically.
  ToolsetRegistry and PythonScriptPlugin are Experimental in 5.8.
- No third-party libraries, no network calls, no telemetry.
