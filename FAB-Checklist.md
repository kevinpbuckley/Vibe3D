<!-- Copyright Buckley Builds LLC 2026 All Rights Reserved. -->

# Vibe3D — Fab submission checklist

Status of the plugin against Epic's Fab requirements for a **code plugin**. Mirrors the structure of
`../VibeUE/FAB-Checklist.md`. `[x]` = done in the repo, `[ ]` = still owed before submitting.

## Product page

- [ ] Title, description and all text fields have an English version — draft in `FAB-DESCRIPTION.md`
- [ ] Media accurately displays the plugin's functionality (no bait imagery)
- [ ] 3–5 screenshots (1920×1080) — the showcase level `/Game/Vibe3D/Showcase/L_Vibe3D_Showcase`
      in the Proteus project renders the five demo assets built with the toolset
- [ ] 1–2 minute demo video showing an agent driving the toolset end to end
- [x] Technical Information fields prepared — `FAB_Tech_Details.md`
- [x] Distribution method: code plugin, source only
- [x] Each project file link hosts exactly one plugin folder with the proper structure
      (`MakePlugin.ps1` enforces the layout and fails if anything required is missing)
- [x] Documentation is linked and in-editor — `README.md`, plus the `modeling` agent skill
      registered at editor start (`Content/Skills/modeling/SKILL.md`)
- [x] No Megascans or other re-distributed third-party content

## Code plugin

### .uplugin configuration
- [x] `EngineVersion` = **"5.8.0"**
- [x] `PlatformAllowList` = Win64 + Linux + Mac (matches the supported target platforms)
- [x] `EnabledByDefault` = **false** — a Fab plugin must not force-enable itself in every project
- [x] `VersionName` = **"1.0"**, `IsBetaVersion` = false
- [x] `CreatedBy` / `CreatedByURL` / `DocsURL` / `SupportURL` populated and consistent with the
      copyright headers (`Buckley Builds LLC`)
- [ ] `FabURL` set with the listing id once the listing exists:
      `"FabURL": "com.epicgames.launcher://ue/Fab/product/<listing-guid>"`
- [x] All engine plugins the module depends on are declared in `Plugins`, including
      `GeometryProcessing` (owner of the `DynamicMesh` module) and `MeshModelingToolset`
- [x] `Resources/Icon128.png` present (128×128) — placeholder lettermark; swap for final art

### Copyright & source code
- [x] All source/header files carry `// Copyright Buckley Builds LLC 2026 All Rights Reserved.`
- [x] Shipped Content files carry the same notice (in `SKILL.md` it sits **after** the YAML
      frontmatter — a header above it breaks skill registration in `init_unreal.py`)
- [x] `LICENSE` present in the repo. Fab distributes under the Fab licence only, so
      `MakePlugin.ps1` may be told to strip it from the package if you follow VibeUE's convention

### File structure
- [x] No unused or local-only folders in the packaged distribution (`MakePlugin.ps1` excludes
      `Binaries`, `Intermediate`, `Saved`, `.git`, `.github`, `.vs`, `__pycache__`, `.venv`)
- [x] `Config/FilterPlugin.ini` lists the extra files to ship. UAT's `BuildPlugin` only includes
      the `.uplugin`, `/Resources/...`, `/Content/...`, `/Source/...`, `/Shaders/...` and
      `/Binaries/ThirdParty/...`; **`/Config` itself and anything at the plugin root are dropped**
      unless listed there
- [x] All paths from the plugin folder are ≤ 170 characters (`MakePlugin.ps1` asserts this)

### Build & dependencies
- [x] `bWarningsAsErrors = false` — Epic compiles this source on their toolchain across engine
      patch releases; a new engine-header deprecation under `/WX` becomes a rejected submission
      (VibeUE issue #569 was exactly this)
- [x] No C++ third-party code or libraries — Third Party Software form is N/A
- [x] Shipped Python (`Content/Python/init_unreal.py`) imports only engine modules
      (`unreal`, `toolset_registry`) — no third-party packages
- [x] No binaries committed; Epic builds from source
- [x] `RunUAT BuildPlugin` clean on **Win64** (2026-08-30): BUILD SUCCESSFUL, zero warnings, zero errors;
      FilterPlugin.ini honoured (README.md + test_prompts shipped, Icon128.png included)
- [ ] Same for **Linux** and **Mac** (needs the cross-compile toolchains):
      ```
      RunUAT.bat BuildPlugin -Plugin="<path>\Vibe3D.uplugin" -Package="<out>" -TargetPlatforms=Win64+Linux+Mac
      ```
- [ ] Packaged plugin installed into a clean UE 5.8 project (not Proteus) and verified:
      the toolset appears in `list_toolsets` as `Vibe3D.ModelingService` and the `modeling`
      skill registers

## Known caveats to disclose on the listing

- Vibe3D depends on two engine plugins that ship as **Experimental** in 5.8 and are not enabled by
  default: `ToolsetRegistry` and `PythonScriptPlugin`. Both are pulled in automatically as plugin
  dependencies, but the listing should say so.
- The toolset is editor-only (module `Type: Editor`); nothing is available in a packaged game.
