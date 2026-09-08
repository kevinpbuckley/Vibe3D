---
description: Light modeled assets, capture reliable high-resolution Unreal screenshots, and export reviewable standalone asset packages.
---

# Showcase images and delivery

## Studio and capture

These behaviors were observed with native Unreal MCP in UE 5.8; discover signatures on a different
build rather than assuming viewport helpers are interchangeable.

- Use a dedicated showcase map, preserving unsaved work before switching levels. Movable RectLights,
  a floor, and fixed exposure in an unbound PostProcessVolume can produce a complete dark studio.
  SkyAtmosphere is optional for this setup. Cyan and violet rim lights worked well with a neutral
  key on the Vibe3D metallic wordmark; tune the palette and intensity to the subject.
- Setting `force_no_precomputed_lighting` through `WorldSettings.set_editor_property` triggered a
  blocking editor dialog in this session. Avoid that setter during unattended capture. For a new
  standalone project intended to use dynamic lighting, configure `r.AllowStaticLighting=False`
  in its `Config/DefaultEngine.ini` before opening it. This does not require changing the source
  project's lighting settings. If MCP stalls, check for a modal dialog rather than repeatedly
  sending the same mutation or terminating the editor automatically.
- `LevelEditorSubsystem.set_level_viewport_camera_info(location, rotation, viewport_key)` takes the
  active viewport key from `get_active_viewport_config_key()`; set FOV with
  `set_level_viewport_fov(fov, viewport_key)`. The similarly named UnrealEditorSubsystem camera
  setter uses only location and rotation. Use `MathLibrary.find_look_at_rotation` for framing.
- Enable game view to hide editor overlays. A background viewport did not reliably advance on a
  `HighResShot` command alone: native MCP `capture_image` with `source="editor"` triggered the draw.
  Capture once after camera changes, allow lighting to settle, issue the high-resolution command,
  then capture again. Poll for a newly written nonempty output with a bounded timeout; an existing
  file from a prior run is not proof this shot succeeded. Prefer unique filenames per attempt.

Example command, executed through Unreal Python after framing:

```python
unreal.SystemLibrary.execute_console_command(
    None, 'HighResShot 3840x2160 filename="E:/Exports/Hero_attempt_01.png"')
```

Follow with the available native MCP `capture_image` tool outside that Python call. A small tool
preview can trigger the draw; it is the separately written HighResShot file that is the deliverable.
Avoid toggling viewport realtime as a retry strategy; it caused an ensure in this session.

- Restore temporarily hidden actors and the intended camera after alternate or icon shots, then
  save the showcase map. Verify the actual exported image, not only the tool preview: readability,
  clipping, reflections, aliasing, dimensions, and byte size. Rendering at twice the delivery
  resolution and downsampling improved the hero lettering edges. Keep the full-resolution master
  separately from the upload image.
- For FAB submission preparation, check the current official media and project requirements rather
  than freezing changing limits in this skill. Clearly identify the intended main image; a square
  emblem render and a large master may serve different purposes from the listing gallery.

## Asset package

- Save assets and the overview map through Unreal before exporting. Use native
  `AssetTools.migrate_packages` into a standalone project's `Content` folder so references are
  preserved. Pass package names (strip the `.ObjectName` suffix), and use noninteractive migration
  options for a fresh destination. Overwrite conflicts only within a known task-owned export.
- FBX export uses `AssetExportTask`, `StaticMeshExporterFBX`, and `FbxExportOption` with
  `automated=True` and `prompt=False`. Enable collision and LOD export when those are part of the
  product. Check the exporter result and output files. Preserve Unreal material graphs and lights
  in the Unreal project; FBX alone does not reproduce that setup.
- For a distributable game asset pack, validate in the standalone project: load every asset and
  overview map, inspect hard and soft package dependencies, and confirm promised materials, LOD
  counts, and collision. Engine content is expected, but `/Script/` dependencies also need review
  for plugin modules. Loading successfully in the authoring project does not prove independence.
- Package `.uproject`, `Config`, and `Content`, excluding generated caches, logs, and Intermediate
  files. Keep screenshots and optional FBX/source scripts clearly organized. Verify archive
  integrity and record actual mesh statistics instead of inferring them from successful calls.
- Describe what is delivered: static versus rigged, material/texture setup, measured geometry,
  and tested engine version. Preparing files does not itself publish or modify a FAB listing.
