# MotoMap 3D concept files

`motomap_cyberpunk_faceplate.scad` is an editable OpenSCAD concept for a horizontal MotoMap display enclosure.

It deliberately uses generic dimensions. Edit the parameter block at the top before printing:

- `box_w`, `box_h`, `box_d`
- `screen_w`, `screen_h`
- `corner_r`, `wall`, and `bezel_w`

The front uses raised cyberpunk traces. If a clean face is needed, set `show_cyberpunk_relief = false`.

This is not yet a finished motorcycle mount. After measuring the actual display, reservoir-screw spacing, magnet size, and right lamp-stem diameter, add the magnetic feet and removable right-side C/J safety clip as separate modules.
