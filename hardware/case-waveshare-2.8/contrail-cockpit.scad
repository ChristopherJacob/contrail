// Contrail cockpit case for the Waveshare ESP32-S3-Touch-LCD-2.8 (240x320, portrait, USB-C at the bottom).
//
// The screen sits in an instrument panel with a glareshield over it, tilted back on a console that holds the two
// oval 2030 speakers that come with the board behind grilles, with two decorative gauges between them.
//
// Parts (set `part`, or use the Makefile/README commands):
//   "assembly"  everything in place, for looking at
//   "front"     the panel's shell: prints face down
//   "back"      the panel's back cover: prints outside face down
//   "hood"      the glareshield: prints top up
//   "base"      the console: prints standing on its bottom
//
// Board figures are from Waveshare's dimension drawing (2D PDF and product page), not a board in hand: print the
// front and back first and check the fit. The figures to check are in the first block below.
// Hardware: 4 x M2.5 x 8 screws (self-tapping into plastic), 2 short pieces of 1.75 mm filament as hood pins, glue.

part = "assembly";
$fn = 48;

// ---- The board (mm). Check these against the board. ----
glass_w = 50.54;      // touch glass, portrait width
glass_h = 73.06;      // touch glass, portrait height
glass_t = 3.6;        // touch panel + LCD thickness
aa_w = 43.2;          // LCD active area
aa_h = 57.6;
aa_top = 5.03;        // glass top edge to active area (the wide border is at the USB end)
pcb_w = 49.9;
pcb_h = 69.0;
pcb_back = 6.0;       // glass front to the PCB's back face
hole_dx = 41.0;       // mounting holes, centre to centre
hole_dy = 60.0;
usb_z = 7.6;          // glass front to the USB-C socket's centre
side_slot = [8, 47];  // BOOT / RESET / PWR buttons and the TF slot: mm below the PCB's top edge, right side (front view)
spk_w = 30.6;         // 2030 oval speaker, with clearance
spk_h = 20.6;
spk_t = 6.0;

// ---- The case ----
fit = 0.3;            // clearance around the glass
face_t = 1.6;         // bezel in front of the glass
bezel = 7;            // panel border beyond the glass (holds the screw bosses)
cav = 17;             // glass front to the back cover (room for the plugs on the board's back)
cover_t = 2.4;
tilt = 15;            // panel lean, degrees
base_w = 112;
base_d = 64;
base_h = 40;
sock = 8;             // panel depth sunk into the console
panel_y = 18;         // console front to the panel's front face

gw = glass_w + 2 * fit;
gh = glass_h + 2 * fit;
pw = gw + 2 * bezel;          // panel width
ph = gh + 2 * bezel;          // panel height
pd = face_t + cav;            // shell depth
ptot = pd + cover_t;          // panel depth with the cover
boss = [pw / 2 - bezel / 2, ph / 2 - bezel / 2];
holes = [for (sx = [-1, 1], sy = [-1, 1]) [sx * hole_dx / 2, sy * hole_dy / 2]];

// Panel-local axes: x, y up (0 = glass centre), z back (0 = front face). Seen from the front, +x is on the LEFT
// (you look along +z), so the right-hand side is -x.
glass_top = gh / 2 - fit;

module rounded_rect(w, h, r) {
    offset(r) square([w - 2 * r, h - 2 * r], center = true);
}

module stadium(w, h) {
    hull() for (s = [-1, 1]) translate([s * (w - h) / 2, 0]) circle(d = h);
}

module front() {
    win_w = aa_w + 1.4;
    win_h = aa_h + 1.4;
    win_cy = glass_top - aa_top - aa_h / 2;
    difference() {
        linear_extrude(pd) rounded_rect(pw, ph, 4);
        // the window, chamfered outward
        hull() {
            translate([0, win_cy, -0.01]) linear_extrude(0.01) square([win_w + 2 * face_t, win_h + 2 * face_t], center = true);
            translate([0, win_cy, face_t]) linear_extrude(0.01) square([win_w, win_h], center = true);
        }
        translate([0, win_cy, 0]) linear_extrude(pd) square([win_w, win_h], center = true);
        // the glass and board drop in from the back
        translate([0, 0, face_t]) linear_extrude(pd) square([gw, gh], center = true);
        // screw bosses for the cover
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * boss[0], sy * boss[1], pd - 10]) cylinder(d = 2.2, h = 10.01);
        // USB-C (and the speaker lead) out of the bottom
        translate([0, -ph / 2, face_t + usb_z]) cube([14, 2 * bezel + 4, 9], center = true);
        // buttons and TF card: a slot, with the wall thinned to 1.5 mm outside it
        pcb_top = pcb_h / 2;
        sl = side_slot[1] - side_slot[0];
        translate([-gw / 2 + 1 - (bezel + 2), pcb_top - side_slot[1], face_t + 2.5]) cube([bezel + 2, sl, 9]);
        translate([-gw / 2 - 1.5 - bezel, pcb_top - side_slot[1] - 3, face_t + 1]) cube([bezel, sl + 6, 12]);
        // instrument-style screw heads on the bezel's corners (decoration, printed on the bed side)
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * boss[0], sy * boss[1], -0.01]) linear_extrude(0.5) {
            difference() { circle(d = 4.2); circle(d = 3.4); }
            rotate(45 + 30 * sx * sy) square([3.4, 0.7], center = true);
        }
        // hood alignment pins (1.75 mm filament)
        for (sx = [-1, 1]) translate([sx * pw / 4, ph / 2 + 0.01, pd / 2]) rotate([90, 0, 0]) cylinder(d = 1.9, h = 5);
    }
}

module back() {
    // modelled outside face at z = 0; the posts reach the PCB's back face
    post = cav - pcb_back - 0.2;
    difference() {
        union() {
            linear_extrude(cover_t) rounded_rect(pw, ph, 4);
            for (h = holes) translate([h[0], h[1], 0]) {
                cylinder(d = 5.5, h = cover_t + post);
                cylinder(d = 2.1, h = cover_t + post + 1.2);   // locates in the board's hole
            }
        }
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * boss[0], sy * boss[1], 0]) {
            translate([0, 0, -0.01]) cylinder(d = 2.9, h = cover_t + 0.02);
            translate([0, 0, -0.01]) cylinder(d = 5.2, h = 1.2);
        }
        // vents
        for (i = [-3:3]) translate([i * 5.5, 8, -0.01]) linear_extrude(cover_t + 0.02) rounded_rect(2.4, 30, 1.19);
    }
}

module hood() {
    // modelled in panel-local axes, sitting on the shell's top; printed top up (see print_hood)
    over = 12;   // reach in front of the face
    t = 2.6;
    lip = 5;
    difference() {
        union() {
            // the top plate: square at the back, rounded forward like a glareshield brow
            translate([0, ph / 2, 0]) rotate([-90, 0, 0]) linear_extrude(t) hull() {
                translate([-pw / 2 - 2, -ptot]) square([pw + 4, 1]);
                translate([0, over - 14]) scale([1, 14 / (pw / 2 + 2)]) circle(r = pw / 2 + 2, $fn = 96);
            }
            // the brow's lip
            translate([0, ph / 2 - lip, 0]) rotate([-90, 0, 0]) linear_extrude(lip) difference() {
                translate([0, over - 14]) scale([1, 14 / (pw / 2 + 2)]) circle(r = pw / 2 + 2, $fn = 96);
                translate([0, over - 14]) scale([1, (14 - t) / (pw / 2 + 2 - t)]) circle(r = pw / 2 + 2 - t, $fn = 96);
                translate([-pw, -40]) square([2 * pw, 40]);
            }
        }
        for (sx = [-1, 1]) translate([sx * pw / 4, ph / 2 - 0.01, pd / 2]) rotate([-90, 0, 0]) cylinder(d = 1.9, h = 2);
        // name on top
        translate([0, ph / 2 + t - 0.6, pd / 2]) rotate([-90, 0, 0]) rotate([0, 0, 180])
            linear_extrude(1) text("CONTRAIL", size = 5, font = "Liberation Sans:style=Bold", halign = "center", valign = "center");
    }
}

// The panel (shell, cover, hood) in panel-local axes.
module panel() {
    color("#2b3036") front();
    color("#3a4048") translate([0, 0, ptot]) mirror([0, 0, 1]) back();
    color("#20252b") hood();
}

// Panel-local -> console axes (X right, Y back, Z up), leaning back by `tilt`.
// (x, y, z) -> (-x, z, y): a rotation, not a mirror, so the assembly shows the parts as printed. The panel's centre
// is lifted by ph/2 so its bottom sits `sock` below the console top before the lean.
module on_console() {
    translate([0, panel_y, base_h - sock + ph / 2]) rotate([-tilt, 0, 0]) rotate([0, 0, 180]) rotate([90, 0, 0])
        children();
}

module gauge(d) {
    // engraved ring, ticks and a needle, cut into the console's front face
    difference() { circle(d = d); circle(d = d - 1.6); }
    for (a = [-120:30:120]) rotate(a) translate([0, d / 2 - 2.8]) square([0.8, 2], center = true);
    rotate(35) translate([0, d / 6]) square([0.9, d / 3], center = true);
    circle(d = 2);
}

module base() {
    spk_x = 34;
    spk_z = 21;
    difference() {
        hull() for (x = [-1, 1], y = [0, 1]) translate([x * (base_w / 2 - 6), 6 + y * (base_d - 12), 0]) cylinder(r = 6, h = base_h);
        // the panel's socket: everything the panel occupies
        on_console() translate([-pw / 2 - fit, -ph / 2 - fit, -fit]) cube([pw + 2 * fit, ph + 2 * fit, ptot + 2 * fit]);
        // the USB-C plug and leads go down a shaft and out under the console
        on_console() translate([-8, -ph / 2 - 60, face_t + usb_z - 5.5]) cube([16, 61, 11]);
        translate([-3.5, 10, -0.01]) cube([7, base_d, 5]);
        // speakers: pockets behind the front wall, loaded from underneath
        for (s = [-1, 1]) translate([s * spk_x, 0, 0]) {
            translate([0, 2.0, 0]) rotate([90, 0, 0]) mirror([0, 0, 1]) linear_extrude(spk_t)
                hull() { translate([0, spk_z]) stadium(spk_w, spk_h); translate([0, -1]) square([spk_w, 1], center = true); }
            // grille
            r = spk_h / 2;
            for (row = [-2:2]) let(dz = row * 3.6, half = (spk_w - spk_h) / 2 + sqrt(r * r - dz * dz) - 2.5)
                translate([0, -0.01, spk_z + dz]) rotate([-90, 0, 0]) linear_extrude(2.1) stadium(2 * half, 1.8);
            // lead to the shaft
            translate([s > 0 ? 0 : -spk_x, 4, -0.01]) cube([spk_x, 4, 3.5]);
        }
        translate([-3.5, 4, -0.01]) cube([7, 12, 3.5]);
        // gauges
        for (s = [-1, 1]) translate([s * 9.5, -0.01, spk_z]) rotate([-90, 0, 0]) linear_extrude(0.8) gauge(15);
        // console look: chamfer the front top edge
        translate([0, 0, base_h]) rotate([45, 0, 0]) cube([base_w + 2, 6 * sqrt(2), 6 * sqrt(2)], center = true);
        // feet
        for (x = [-1, 1], y = [0, 1]) translate([x * (base_w / 2 - 12), 12 + y * (base_d - 24), -0.01]) cylinder(d = 10.5, h = 1);
    }
}

// A stand-in for the glass and screen, for the assembly view only.
module screen() {
    color("#0d1117") translate([0, 0, face_t]) linear_extrude(glass_t) square([glass_w, glass_h], center = true);
    color("#1f4e79") translate([0, glass_top - aa_top - aa_h / 2, face_t - 0.05]) linear_extrude(0.1) square([aa_w, aa_h], center = true);
}

if (part == "front") front();
else if (part == "back") back();
else if (part == "hood") translate([0, 0, ph / 2 + 2.6]) rotate([-90, 0, 0]) hood();   // top down on the bed
else if (part == "base") base();
else {
    color("#3a4048") base();
    on_console() { panel(); screen(); }
}
