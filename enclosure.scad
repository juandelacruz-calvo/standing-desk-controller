// Standing Desk Controller - Box + angled front panel
// Box screws under desk (hidden), angled panel sticks out with flex buttons

/* [General] */
wall = 2.0;
corner_r = 3.0;

/* [Arduino Nano] */
nano_l = 45.0;
nano_w = 18.0;
nano_h = 7.0;
nano_usb_w = 8.0;
nano_usb_h = 4.0;

/* [HC-SR04] */
sr04_l = 45.0;
sr04_w = 20.0;
sr04_h = 15.0;
sr04_eye_d = 16.0;
sr04_eye_spacing = 26.0;

/* [Buttons] */
btn_d = 12.0;
btn_slit_w = 1.0;
btn_hinge_angle = 40;
btn_spacing = 18.0;
btn_count = 4;

/* [Rail] */
rail_h = 1.5;
rail_w = 1.2;
rail_gap = 2.0;
rail_margin = 6.0;

/* [Angled Panel] */
panel_extend = 18.0;    // how far forward the bottom edge sticks out
panel_thickness = 3.0;  // wall thickness of the panel face

/* [Mounting] */
screw_d = 3.2;

/* [Wiring] */
wire_slot_w = 8.0;
wire_slot_h = 5.0;

/* [Computed] */
inner_l = nano_l + sr04_w + wall + 4;
inner_w = max(nano_w, sr04_l) + 4;
inner_h = max(nano_h, sr04_h) + 4;

outer_l = inner_l + wall * 2;
outer_w = inner_w + wall * 2;
outer_h = inner_h + wall * 2;

$fn = 60;

module nano_standoff(h) {
    difference() {
        cylinder(d = 5, h = h);
        translate([0, 0, h - 4])
            cylinder(d = 1.8, h = 4.1);
    }
}

module enclosure() {
    // Face angle: the panel goes from (Y=0, Z=outer_h) to (Y=-panel_extend, Z=0)
    face_angle = atan2(panel_extend, outer_h);
    face_len = sqrt(panel_extend * panel_extend + outer_h * outer_h);

    difference() {
        union() {
            // --- Main box (hidden under desk) ---
            hull() {
                for (x = [corner_r, outer_l - corner_r])
                    for (y = [corner_r, outer_w - corner_r])
                        translate([x, y, 0])
                            cylinder(r = corner_r, h = outer_h);
            }

            // --- Angled front panel (visible, sticks out from desk edge) ---
            // Hull from the front face of the box down to the protruding bottom edge
            hull() {
                // Top front edge of box
                for (x = [corner_r, outer_l - corner_r])
                    translate([x, 0, outer_h - corner_r])
                        sphere(r = corner_r);

                // Bottom front edge, pushed forward
                for (x = [corner_r, outer_l - corner_r])
                    translate([x, -panel_extend, corner_r])
                        sphere(r = corner_r);

                // Tie back into box front face
                for (x = [corner_r, outer_l - corner_r])
                    translate([x, corner_r, corner_r])
                        sphere(r = corner_r);
                for (x = [corner_r, outer_l - corner_r])
                    translate([x, corner_r, outer_h - corner_r])
                        sphere(r = corner_r);
            }
        }

        // --- Hollow main box ---
        translate([wall, wall, wall])
            cube([inner_l, inner_w, inner_h + wall + 1]);

        // --- Hollow angled panel section ---
        hull() {
            translate([wall + 1, wall, outer_h - wall])
                cube([outer_l - (wall + 1) * 2, 0.1, 0.1]);
            translate([wall + 1, wall, wall])
                cube([outer_l - (wall + 1) * 2, 0.1, 0.1]);
            translate([wall + 1, -panel_extend + panel_thickness, wall + panel_thickness])
                cube([outer_l - (wall + 1) * 2, 0.1, 0.1]);
        }

        // --- Flex button slits (living hinge) on angled face ---
        for (i = [0 : btn_count - 1]) {
            bx = (outer_l - (btn_count - 1) * btn_spacing) / 2 + i * btn_spacing;
            // Center of the angled face
            cy = -panel_extend / 2;
            cz = outer_h / 2;

            translate([bx, cy, cz])
                rotate([-face_angle, 0, 0])
                translate([0, 0, -wall * 3])
                linear_extrude(wall * 6)
                difference() {
                    circle(d = btn_d + btn_slit_w);
                    circle(d = btn_d - btn_slit_w);
                    // Hinge bridge (uncut arc at top)
                    rotate([0, 0, 90 - btn_hinge_angle / 2])
                        polygon([
                            [0, 0],
                            [btn_d, 0],
                            [btn_d * cos(btn_hinge_angle), btn_d * sin(btn_hinge_angle)],
                        ]);
                }
        }

        // --- Button labels embossed on angled face ---
        labels = ["M1", "M2", "\u25B2", "\u25BC"];
        label_depth = 0.6;
        for (i = [0 : btn_count - 1]) {
            bx = (outer_l - (btn_count - 1) * btn_spacing) / 2 + i * btn_spacing;
            cy = -panel_extend / 2;
            cz = outer_h / 2;

            translate([bx, cy, cz])
                rotate([-face_angle, 0, 0])
                translate([0, 0, panel_thickness - label_depth])
                linear_extrude(label_depth + 0.1)
                text(labels[i], size = 4, halign = "center", valign = "center",
                     font = "Liberation Sans:style=Bold");
        }

        // --- HC-SR04 sensor holes (bottom face) ---
        sr04_cx = wall + 2 + sr04_w / 2;
        sr04_cy = wall + (inner_w - sr04_l) / 2 + sr04_l / 2;
        translate([sr04_cx, sr04_cy - sr04_eye_spacing / 2, -0.1])
            cylinder(d = sr04_eye_d + 1, h = wall + 0.2);
        translate([sr04_cx, sr04_cy + sr04_eye_spacing / 2, -0.1])
            cylinder(d = sr04_eye_d + 1, h = wall + 0.2);

        // --- USB port (back face) ---
        translate([wall + sr04_w + wall + 2, outer_w - wall - 0.1, wall + 2])
            cube([nano_usb_w, wall + 0.2, nano_usb_h]);

        // --- Wire exit slot (right side) ---
        translate([outer_l - wall - 0.1, outer_w * 0.4, wall + 2])
            cube([wall + 0.2, wire_slot_w, wire_slot_h]);

        // --- Screw holes through top face ---
        for (x = [wall + 6, outer_l - wall - 6])
            for (y = [wall + 6, outer_w - wall - 6])
                translate([x, y, outer_h - wall - 0.1])
                    cylinder(d = screw_d, h = wall + 0.2);
    }

    // --- Button PCB slide rails (inside angled panel) ---
    rail_total_l = (btn_count - 1) * btn_spacing + btn_d + rail_margin * 2;
    rail_start_x = (outer_l - rail_total_l) / 2;
    btn_cy = -panel_extend / 2;
    btn_cz = outer_h / 2;

    translate([rail_start_x, btn_cy, btn_cz])
        rotate([-face_angle, 0, 0])
        translate([0, 0, -panel_thickness / 2 - rail_h]) {
            translate([0, rail_gap / 2, 0])
                cube([rail_total_l, rail_w, rail_h]);
            translate([0, -rail_gap / 2 - rail_w, 0])
                cube([rail_total_l, rail_w, rail_h]);
        }

    // --- Nano standoffs ---
    nano_ox = wall + sr04_w + wall + 2;
    nano_oy = wall + (inner_w - nano_w) / 2;
    standoff_h = 3;
    translate([nano_ox + 2, nano_oy + 2, wall])
        nano_standoff(standoff_h);
    translate([nano_ox + nano_l - 2, nano_oy + 2, wall])
        nano_standoff(standoff_h);
    translate([nano_ox + 2, nano_oy + nano_w - 2, wall])
        nano_standoff(standoff_h);
    translate([nano_ox + nano_l - 2, nano_oy + nano_w - 2, wall])
        nano_standoff(standoff_h);

    // --- SR04 ledge supports ---
    sr04_ox = wall + 2;
    sr04_oy = wall + (inner_w - sr04_l) / 2;
    translate([sr04_ox - 1, sr04_oy - 1, wall])
        cube([2, sr04_l + 2, 2]);
    translate([sr04_ox + sr04_w - 1, sr04_oy - 1, wall])
        cube([2, sr04_l + 2, 2]);
}

// --- Render ---
enclosure();
