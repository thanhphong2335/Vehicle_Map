/*
 MotoMap Helmet Shell — editable 3D enclosure source
 =====================================================
 A parametric enclosure inspired by the supplied multi-view reference, made
 for a horizontal MotoMap display. Dimensions are intentionally generic.

 Open in OpenSCAD. Change PART to export individual components as STL.
 Use F5 preview / F6 render / File > Export > Export as STL.

 Coordinate system: X = left/right, Y = front/rear, Z = bottom/top.
 The display is on the front (negative Y) and tilts upward toward the rider.
*/

$fn = 48;

// ============================== EXPORT ===============================
// "assembly", "front_shell", "rear_shell", "spoiler", "tail_lights",
// "diffuser", "magnetic_base", "magnet_caps", "right_c_clip", "front_decals"
PART = "assembly";
EXPLODED_VIEW = false;

// ============================ MAIN DIMENSIONS ========================
body_w = 190;                 // overall left/right width
body_h = 110;                 // overall bottom/top height
body_d = 60;                  // overall front/rear depth
wall = 3;
seam_y = 4;                   // front/rear clamshell split

// Screen: keep 320:172 aspect ratio by default.
screen_w = 160;
screen_h = 86;
screen_r = 6;
screen_tilt = 14;             // degrees upward, viewed from the rider
bezel = 6;
side_lip_h = 3.5;             // deliberately low for side visibility

// Cosmetic form controls.
visor_overhang = 12;
visor_t = 3;
spoiler_w = 116;
spoiler_d = 15;
spoiler_t = 4;
tail_light_d = 13;
show_reference_decals = true;
show_tail_lights = true;
show_spoiler = true;
show_diffuser = true;

// Two magnetic feet. Change after measuring the actual reservoir screws.
magnet_d = 12;
magnet_t = 3;
magnet_clearance = 0.35;
magnet_cover_t = 0.8;
magnet_spacing = 55;          // centre-to-centre screw spacing
magnet_y = -1;
pad_t = 1.0;                  // TPU/rubber anti-slip pad recess

// Removable right-side C/J safety clip. Change lamp_tube_d after measuring.
lamp_tube_d = 20;
clip_wall = 3.2;
clip_width = 18;
clip_gap = 7;
clip_lip = 1.6;
clip_offset_y = 3;

// Assembly hardware (only the shell halves use screws/heat-set inserts).
m3_clearance = 3.35;
m3_insert_d = 4.6;
m3_insert_h = 5;

// =============================== HELPERS =============================

module rounded_box(w, d, h, r) {
    rr = min(r, min(w, d) / 2);
    linear_extrude(height = h)
        hull() {
            translate([-w/2 + rr, -d/2 + rr]) circle(r = rr);
            translate([ w/2 - rr, -d/2 + rr]) circle(r = rr);
            translate([ w/2 - rr,  d/2 - rr]) circle(r = rr);
            translate([-w/2 + rr,  d/2 - rr]) circle(r = rr);
        }
}

module ellipsoid(sx, sy, sz) {
    scale([sx, sy, sz]) sphere(r = 1);
}

module tube_between(a, b, r) {
    v = b - a;
    len = norm(v);
    axis = cross([0, 0, 1], v);
    angle = acos(v[2] / len);
    translate(a)
        rotate(a = angle, v = axis)
            cylinder(h = len, r = r);
}

// Local coordinate plane for the upward-tilted screen.
module display_plane(z_offset = 0) {
    translate([0, -body_d/2 + 7, body_h * 0.48 + z_offset])
        rotate([screen_tilt, 0, 0])
            children();
}

module display_window(depth = 20) {
    linear_extrude(height = depth, center = true)
        offset(r = screen_r)
            square([screen_w - 2*screen_r, screen_h - 2*screen_r], center = true);
}

module screen_bezel(depth = 4) {
    difference() {
        linear_extrude(height = depth)
            offset(r = screen_r + bezel)
                square([screen_w - 2*screen_r, screen_h - 2*screen_r], center = true);
        translate([0, 0, -1]) display_window(depth + 2);
    }
}

// ============================ OUTER SILHOUETTE =======================

module helmet_envelope() {
    // Rounded, helmet-like upper body. The front is lower and more open.
    hull() {
        translate([0, -10, body_h * 0.53])
            ellipsoid(body_w * 0.51, body_d * 0.60, body_h * 0.48);
        translate([0, 16, body_h * 0.48])
            ellipsoid(body_w * 0.46, body_d * 0.57, body_h * 0.44);
        translate([0, -body_d/2 + 10, body_h * 0.27])
            ellipsoid(body_w * 0.47, 13, body_h * 0.25);
    }
}

module lower_skirt() {
    hull() {
        translate([0, -4, 4]) rounded_box(body_w * 0.94, body_d * 0.86, 10, 9);
        translate([0, 7, 10]) rounded_box(body_w * 0.88, body_d * 0.74, 8, 8);
    }
}

module shell_volume() {
    union() {
        helmet_envelope();
        lower_skirt();
    }
}

module inner_cavity() {
    hull() {
        translate([0, -6, body_h * 0.53])
            ellipsoid(body_w * 0.47, body_d * 0.53, body_h * 0.43);
        translate([0, 15, body_h * 0.47])
            ellipsoid(body_w * 0.41, body_d * 0.50, body_h * 0.39);
    }
}

module front_half_mask() {
    translate([-body_w, -body_d - 35, -5])
        cube([2*body_w, body_d + seam_y + 35, body_h + 35]);
}

module rear_half_mask() {
    translate([-body_w, seam_y, -5])
        cube([2*body_w, body_d + 45, body_h + 35]);
}

// ============================ FRONT SHELL ============================

module front_shell() {
    difference() {
        intersection() {
            shell_volume();
            front_half_mask();
        }

        // Inner volume leaves a printable shell wall.
        translate([0, 0, wall]) inner_cavity();

        // The display opening cuts through the forward face.
        display_plane()
            rotate([90, 0, 0])
                display_window(30);

        // Bottom magnet pockets: magnets insert from inside and remain covered
        // by a thin external skin. Pocket caps are separate modules.
        for (x = [-magnet_spacing/2, magnet_spacing/2]) {
            translate([x, magnet_y, magnet_cover_t])
                cylinder(d = magnet_d + magnet_clearance,
                         h = magnet_t + wall + 2);
            translate([x, magnet_y, 0])
                cylinder(d = magnet_d + 2*pad_t,
                         h = pad_t + 0.1);
        }

        // M3 clearances along the rear seam.
        for (x = [-body_w*0.34, body_w*0.34])
            translate([x, seam_y - 1, body_h*0.42])
                rotate([90, 0, 0]) cylinder(d = m3_clearance, h = 14);
    }

    // Raised bezel and deliberately low side lips. They terminate before the
    // display side edge to preserve left/right viewing angles.
    display_plane(1)
        rotate([90, 0, 0])
            screen_bezel(3.2);

    display_plane(3.3) {
        translate([-screen_w/2 - bezel + 4, 0, 0])
            cube([side_lip_h, screen_h * 0.70, 4], center = true);
        translate([ screen_w/2 + bezel - 4, 0, 0])
            cube([side_lip_h, screen_h * 0.70, 4], center = true);
    }
}

module sun_visor() {
    // Curved upper-only hood, intentionally without tall side walls.
    hull() {
        display_plane(11)
            translate([0, screen_h/2 + 6, 0])
                rounded_box(screen_w + 2*bezel + 18, visor_overhang, visor_t, 6);
        translate([0, -body_d/2 + 7, body_h * 0.78])
            rounded_box(screen_w + 2*bezel + 10, 10, visor_t, 5);
    }
}

module front_shell_with_visor() {
    union() {
        front_shell();
        sun_visor();
    }
}

// ============================= REAR SHELL ============================

module rear_shell() {
    union() {
        difference() {
            intersection() {
                shell_volume();
                rear_half_mask();
            }
            translate([0, 0, wall]) inner_cavity();
        }

        // Solid heat-set insert bosses receive screws from the front half.
        for (x = [-body_w*0.34, body_w*0.34])
            translate([x, seam_y + 3, body_h*0.42])
                rotate([90, 0, 0]) cylinder(d = m3_insert_d, h = m3_insert_h);

        // Layered side armor panels, modeled as shallow relief.
        for (side = [-1, 1]) {
            translate([side*(body_w*0.43), 13, body_h*0.47])
                rotate([0, side*12, side*3])
                    rounded_box(12, body_d*0.55, body_h*0.38, 4);
        }
    }
}

// ============================ REAR DETAILS ===========================

module spoiler() {
    union() {
        // Raised supports.
        for (x = [-spoiler_w*0.38, spoiler_w*0.38])
            hull() {
                translate([x, body_d/2 - 4, body_h*0.68]) cube([5, 5, 6], center = true);
                translate([x, body_d/2 + 11, body_h*0.94]) cube([5, 5, 6], center = true);
            }

        // Broad, printable rear wing.
        translate([0, body_d/2 + 13, body_h*0.94])
            rotate([0, 0, 0])
                rounded_box(spoiler_w, spoiler_d, spoiler_t, 3);
    }
}

module tail_lights() {
    // Decorative red-print inserts; kept separate from the rear shell.
    for (x = [-body_w*0.25, -body_w*0.13, body_w*0.13, body_w*0.25])
        translate([x, body_d/2 + 4, body_h*0.42])
            rotate([90, 0, 0]) cylinder(d = tail_light_d, h = 3);
}

module rear_light_panel() {
    translate([0, body_d/2 + 1, body_h*0.42])
        rounded_box(body_w*0.69, 4, body_h*0.19, 3);
}

module diffuser() {
    union() {
        translate([0, body_d/2 + 1, 7])
            rounded_box(body_w*0.75, 9, 12, 3);
        for (x = [-body_w*0.25, -body_w*0.12, 0, body_w*0.12, body_w*0.25])
            translate([x, body_d/2 + 8, 1])
                cube([7, 9, 14], center = true);
    }
}

// ========================== MAGNETIC UNDERSIDE =======================

module magnetic_base() {
    // Thin plate resting on the reservoir. It is a visual/fit starting point,
    // not a final reservoir contour.
    difference() {
        translate([0, magnet_y, 0]) rounded_box(body_w*0.61, body_d*0.46, 7, 6);
        for (x = [-magnet_spacing/2, magnet_spacing/2])
            translate([x, magnet_y, magnet_cover_t])
                cylinder(d = magnet_d + magnet_clearance,
                         h = magnet_t + 7);
    }
}

module magnet_caps() {
    // Print separately and glue/screw mechanically after inserting magnets.
    for (x = [-magnet_spacing/2, magnet_spacing/2])
        translate([x, magnet_y, 0])
            difference() {
                cylinder(d = magnet_d + 1.2, h = magnet_cover_t);
                translate([0, 0, -0.1]) cylinder(d = magnet_d - 1, h = magnet_cover_t + 0.2);
            }
}

// =========================== RIGHT C/J CLIP ==========================

module right_c_clip() {
    // Replaceable module. The opening faces rearward; a small lip resists
    // wind-induced release while deliberate flexing removes it.
    outer_r = lamp_tube_d/2 + clip_wall;
    inner_r = lamp_tube_d/2 + 0.5;
    translate([body_w/2 + clip_wall + 5, clip_offset_y, body_h*0.22])
        rotate([90, 0, 0])
            difference() {
                linear_extrude(height = clip_width, center = true)
                    difference() {
                        circle(r = outer_r);
                        circle(r = inner_r);
                        translate([-outer_r - 1, -clip_gap/2])
                            square([outer_r + inner_r + 3, clip_gap]);
                    }
                // Keep the opening clear after extrusion.
                translate([-outer_r - 2, -clip_width, -clip_gap/2])
                    cube([outer_r + inner_r + 5, 2*clip_width, clip_gap]);
            }

    // J-shaped retaining lip at the open edge.
    translate([body_w/2 + clip_wall + 5 - inner_r + 1,
               clip_offset_y,
               body_h*0.22 - clip_gap/2])
        cube([clip_lip, clip_width, clip_gap], center = true);
}

// ============================= DECALS ================================

module front_decals() {
    if (show_reference_decals) {
        // Separate shallow relief: remove or replace after importing custom SVG.
        translate([body_w*0.38, 9, body_h*0.72])
            rotate([90, 0, 0])
                linear_extrude(height = 0.8)
                    text("LB", size = 18, halign = "center", valign = "center",
                         font = "Liberation Serif:style=Bold");
        translate([-body_w*0.38, -2, body_h*0.45])
            rotate([90, 0, 0])
                linear_extrude(height = 0.8)
                    text("LIBERTY\nWALK", size = 8, halign = "center", valign = "center",
                         spacing = 1.05, font = "Liberation Sans:style=Bold");
    }
}

// ============================= ASSEMBLY ==============================

module full_assembly() {
    if (EXPLODED_VIEW) {
        translate([-body_w*0.30, 0, 0]) front_shell_with_visor();
        translate([ body_w*0.30, 0, 0]) rear_shell();
        translate([0, 25, 30]) magnetic_base();
        translate([0, 45, 30]) spoiler();
        translate([0, 55, 30]) tail_lights();
        translate([0, 70, 30]) diffuser();
        translate([45, 0, 20]) right_c_clip();
    } else {
        front_shell_with_visor();
        rear_shell();
        magnetic_base();
        if (show_spoiler) spoiler();
        if (show_tail_lights) {
            rear_light_panel();
            tail_lights();
        }
        if (show_diffuser) diffuser();
        right_c_clip();
        front_decals();
    }
}

if (PART == "assembly") full_assembly();
else if (PART == "front_shell") front_shell_with_visor();
else if (PART == "rear_shell") rear_shell();
else if (PART == "spoiler") spoiler();
else if (PART == "tail_lights") tail_lights();
else if (PART == "diffuser") diffuser();
else if (PART == "magnetic_base") magnetic_base();
else if (PART == "magnet_caps") magnet_caps();
else if (PART == "right_c_clip") right_c_clip();
else if (PART == "front_decals") front_decals();
else full_assembly();
