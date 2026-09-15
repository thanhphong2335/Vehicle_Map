# MotoMap files for Tinkercad

Tinkercad does not edit OpenSCAD parametrically. These SVG files are the editable/import-friendly version of the MotoMap cyberpunk front:

1. Import `motomap_front_base.svg` as a **solid** and set its height to about 2.5–3 mm.
2. Import `motomap_screen_hole.svg`, set it as a **hole**, centre it on the base, and group it with the base.
3. Import `motomap_cyberpunk_accents.svg` as a **solid**, set its height to 0.6–1 mm, and place it on the front. Keep it separate for a contrasting filament color, or group it into the base for one-color printing.

All SVGs use a 190 × 110 mm reference face. Scale the three imports together in Tinkercad if the real screen needs a different size.

The `.scad` file one directory above is still useful as a parametric reference, but the SVG files are the intended files for direct Tinkercad work.
