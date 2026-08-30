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
- Requires these engine plugins, all declared as dependencies and enabled automatically:
  `GeometryScripting`, `GeometryProcessing`, `MeshModelingToolset`, `ToolsetRegistry`,
  `PythonScriptPlugin`, `EditorScriptingUtilities`.
- `ToolsetRegistry` and `PythonScriptPlugin` ship as **Experimental** in UE 5.8 and are not enabled
  by default in a fresh project; Vibe3D enables them through its dependency list.
- No third-party libraries, no external services, no network calls. The plugin ships no binaries.
- Content is non-asset only (the agent skill, a Python bootstrap and a sample guide) — there are no
  `.uasset` or `.umap` files, so nothing to cook.

**Prerequisites:** Unreal Engine 5.8 with the C++ toolchain installed (the plugin is a code plugin).
An MCP-capable agent client is optional — every function is equally usable from the editor's Python
console or from Blueprint-free editor scripting.
