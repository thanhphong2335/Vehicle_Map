/*
  MotoMap — Cyberpunk front enclosure concept
  ------------------------------------------------
  Editable OpenSCAD starting point for a horizontal motorcycle display box.
  This file intentionally does NOT lock in the real screen, magnet, or clip
  dimensions. Edit the values in the PARAMETERS section before exporting STL.

  Suggested workflow:
    1. Open this file in OpenSCAD.
    2. Change outer dimensions and screen opening.
    3. Press F5 for preview, F6 to render, then export STL.
    4. Add the rear magnetic feet and right-side C/J clip after measuring the
       motorcycle reservoir screws and lamp stem.

  The generated object is a front-face concept, not a final road-safe mount.
*/

$fn = 56;

// ============================ PARAMETERS ============================

// Overall front housing. All units are millimetres.
box_w = 190;
box_h = 110;
box_d = 30;
corner_r = 15;
wall = 3;

// Generic 320 x 172 aspect-ratio display opening. Measure the real module.
screen_w = 160;
screen_h = 86;
screen_corner_r = 6;
bezel_w = 7;

// Cyberpunk visual relief.
faceplate_d = 3.2;
ridge_h = 0.8;
trace_w = 2.0;
accent_tab_w = 14;
accent_tab_h = 5;

// Visual switches.
show_shell = true;             // Basic rear-open body for reference.
show_cyberpunk_relief = true;  // Raised decorative traces and corner fins.
show_screen_placeholder = true;

// ============================== HELPERS =============================

module rounded_rect_2d(w, h, r) {
    rr = min(r, min(w, h) / 2);
    hull() {
        translate([-w/2 + rr, -h/2 + rr]) circle(r = rr);
        translate([ w/2 - rr, -h/2 + rr]) circle(r = rr);
        translate([ w/2 - rr,  h/2 - rr]) circle(r = rr);
        translate([-w/2 + rr,  h/2 - rr]) circle(r = rr);
    }
}

module rounded_prism(w, h, r, d) {
    linear_extrude(height = d) rounded_rect_2d(w, h, r);
}

module angled_trace(points, z = faceplate_d, height = ridge_h) {
    translate([0, 0, z]) linear_extrude(height = height) polygon(points);
}

module screen_window(d = faceplate_d + ridge_h + 1) {
    rounded_prism(screen_w, screen_h, screen_corner_r, d);
}

// ============================ BASE ENCLOSURE ========================

module rear_open_shell() {
    difference() {
        rounded_prism(box_w, box_h, corner_r, box_d);

        // Open rear cavity. This is deliberately plain so it is easy to alter.
        translate([0, 0, wall])
            rounded_prism(box_w - 2*wall, box_h - 2*wall,
                          max(2, corner_r - wall), box_d);

        // Window through the front.
        translate([0, 0, -1]) screen_window(box_d + 2);
    }
}

module front_bezel() {
    difference() {
        rounded_prism(screen_w + 2*bezel_w,
                      screen_h + 2*bezel_w,
                      screen_corner_r + bezel_w,
                      faceplate_d);
        translate([0, 0, -1]) screen_window(faceplate_d + 2);
    }
}

// ========================== CYBERPUNK DETAILS =======================

module left_lower_trace() {
    angled_trace([
        [-box_w/2 + 13, -box_h/2 + 20],
        [-box_w/2 + 48, -box_h/2 + 20],
        [-box_w/2 + 61, -box_h/2 + 31],
        [-box_w/2 + 57, -box_h/2 + 35],
        [-box_w/2 + 45, -box_h/2 + 26],
        [-box_w/2 + 13, -box_h/2 + 26]
    ]);
}

module right_lower_trace() {
    mirror([1, 0, 0]) left_lower_trace();
}

module upper_corner_fins() {
    angled_trace([
        [-box_w/2 + 16, box_h/2 - 17],
        [-box_w/2 + 47, box_h/2 - 17],
        [-box_w/2 + 55, box_h/2 - 25],
        [-box_w/2 + 49, box_h/2 - 29],
        [-box_w/2 + 43, box_h/2 - 23],
        [-box_w/2 + 16, box_h/2 - 23]
    ]);
    mirror([1, 0, 0]) children();
}

module bezel_side_accents() {
    // Four short raised tabs that can later be printed in a contrasting color.
    for (side = [-1, 1]) {
        translate([side * (screen_w/2 + bezel_w - accent_tab_w/2),
                   screen_h/2 + bezel_w - accent_tab_h/2,
                   faceplate_d])
            cube([accent_tab_w, accent_tab_h, ridge_h], center = true);
        translate([side * (screen_w/2 + bezel_w - accent_tab_w/2),
                   -screen_h/2 - bezel_w + accent_tab_h/2,
                   faceplate_d])
            cube([accent_tab_w, accent_tab_h, ridge_h], center = true);
    }
}

module lower_tech_vents() {
    // Decorative shallow bars only. Remove if the front needs to be watertight.
    for (side = [-1, 1]) {
        for (i = [0:2]) {
            translate([side * (box_w/2 - 25), -box_h/2 + 36 + i*5, faceplate_d])
                rotate([0, 0, side * 18])
                    cube([14, trace_w, ridge_h], center = true);
        }
    }
}

module cyberpunk_relief() {
    left_lower_trace();
    right_lower_trace();
    upper_corner_fins();
    bezel_side_accents();
    lower_tech_vents();
}

// =============================== ASSEMBLY ===========================

module motomap_front_concept() {
    if (show_shell) rear_open_shell();

    // The faceplate is placed on the front face of the shell.
    translate([0, 0, box_d - faceplate_d]) front_bezel();

    if (show_cyberpunk_relief) {
        translate([0, 0, box_d - faceplate_d]) cyberpunk_relief();
    }

    // Preview-only screen. Disable before exporting a printable shell.
    if (show_screen_placeholder) {
        color([0.05, 0.2, 0.28, 0.45])
            translate([0, 0, box_d - 0.6])
                rounded_prism(screen_w - 2, screen_h - 2,
                              max(1, screen_corner_r - 1), 0.5);
    }
}

motomap_front_concept();

