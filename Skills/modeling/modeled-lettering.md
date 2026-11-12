---
description: Build extruded logo lettering from font contours with correct counters, winding, bevels, and camera orientation.
---

# Modeled lettering

Use this workflow when the requested text must be actual mesh geometry, such as a logo rendered
with Unreal lights. These notes come from the Vibe3D logo session in UE 5.8.

- Obtain flattened outlines from an installed font, then send the polygon coordinates through
  `ModelingService.append_extrude_polygon`. Windows `System.Drawing.Drawing2D.GraphicsPath.AddString`
  and `Flatten` worked for this. Preserve contour boundaries; do not join every path point into one
  polygon. Remove duplicate closing points and consecutive duplicate vertices before extrusion.
- Classify contour nesting using containment, not winding alone. Even nesting depths are filled
  regions; their immediate odd-depth children are counters (holes). Extrude each filled contour
  into its own handle, subtract the counters, then append the completed part to the wordmark.
  An island inside a counter becomes another filled contour.
- Counter cutters must extend beyond both ends of the text depth. Coplanar cutter caps can leave
  fragments or close the counters. Check letters such as B, D, and e from the front and side.
- Reflections of outline coordinates reverse winding. Run `ensure_outward` separately on each
  extruded outer contour and each cutter before boolean operations or assembly. Recomputing
  shading normals alone does not fix inward triangle orientation.
- Validate the font outlines visually before detailing the whole word. Arial outlines worked in
  this session after another font produced broken joins; this is evidence to inspect contours,
  not a requirement to use Arial for every logo. Record the font used; an outline export does not
  require bundling the font binary.
- Check a simple asymmetric glyph in the intended camera before laying out a whole title.
  Screen horizontal direction depends on the viewing side, and a local XY polygon needs a
  transform into the title plane. Verify bounds and a capture rather than guessing rotation signs.
- Union overlapping glyph pieces only after their counters and orientation are correct. Bevel
  conservatively relative to narrow strokes and counters; the subtitle may need no bevel at all.
  Finish with normals and UVs, and check closedness and `num_unset_triangles == 0` before saving.

Keep lettering, emblem, and display base as separate assets when alternate crops or icon-only
renders are useful. A promotional logo scene does not need gameplay collision, LODs, or rigging
unless the deliverable also calls for those uses.
