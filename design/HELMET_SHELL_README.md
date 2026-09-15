# MotoMap Helmet Shell

Main editable 3D source: `motomap_helmet_shell.scad`.

## Exporting parts

1. Install/open OpenSCAD.
2. Set `PART` near the top of the file to the component required.
3. Press `F6`, then export STL.
4. Repeat for `front_shell`, `rear_shell`, `spoiler`, `tail_lights`, `diffuser`, `magnetic_base`, `magnet_caps`, and `right_c_clip`.

## Parameters to measure before printing

- `screen_w`, `screen_h`, and `screen_tilt`
- `magnet_d`, `magnet_t`, and `magnet_spacing`
- `lamp_tube_d`
- the real outer clearance required by the ESP32, wiring, and display PCB

## Tinkercad assets

Import these SVGs as separate solids/holes:

- `tinkercad/motomap_helmet_front.svg`: overall front silhouette
- `tinkercad/motomap_screen_hole.svg`: display hole
- `tinkercad/motomap_helmet_decals.svg`: optional two-color decals
- `tinkercad/motomap_cyberpunk_accents.svg`: optional cyberpunk relief

The OpenSCAD file remains the authoritative 3D source; imported STL meshes in Tinkercad are useful for cuts and basic edits but are not parametric.

## Print notes

- Use PETG, ASA, or nylon for the vehicle-mounted shell; avoid PLA.
- Print the shell pieces separately. Use 3 mm walls and M3 heat-set inserts.
- The C/J clip is a replaceable safety part; test its fit statically before riding.
- The two magnets are retained by pockets and separate caps. Do not rely on adhesive alone.
