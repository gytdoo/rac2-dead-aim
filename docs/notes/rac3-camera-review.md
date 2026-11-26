# UYA's camera, read wide, and a review of the Modern camera

Sources:
- `rac3:` = rac3-decomp (R&C3 **PAL**). Matched C in `src/levels/shared/`,
  `src/frontbin/`; drafts in `nonmatching/functional/` (each says "functional: high/medium");
  names and NTSC-U descriptions in `config/names.tsv`; layouts in `include/structs/camera.h`.
- `rac2:` = rac2-decomp (only to check what our boom inherits).
- `mod.cpp` = repos/rac2-dead-aim/src/mod.cpp at b8f9a75 (clean tree; 4,743 lines).
- Read first: rac3-strafe-camera.md (fps_cam, fps_cam_place, follow-camera collision), rac2-spring-arm.md,
  rac2-sphere-collision.md, rac2-camera-situations-2.md, rac2-follow-camera-tilt.md, rac2-aim-movement.md,
  rac2-weapon-aim.md. This note does not repeat them.
- Nothing was run. There is no UYA disc or data in the repo: constants held in data words are known only from
  names.tsv text.

Confidence: **H** read in matched C or a "high" draft; **M** a "medium" draft, or inferred from code; **L** a guess,
or names.tsv text only.

PAL vs NTSC: the rac3 code is PAL (50 Hz). The free-look code is rescaled (speeds x1.2, accelerations x1.44). In the
follow camera most springs are not rescaled, a few rates are (they say which below). Mode blends store NTSC per-frame
rates and multiply them by 1.2 when they run (`rac3:src/frontbin/382380.c:769-772`), so their seconds are the same
on both. Frame counts written as `(n * 5 + 2) / 6` in the code are NTSC counts turned into PAL ones.

---

## 0. In short

UYA has two cameras. The **follow camera** (mode 0) is a lakitu: a lazy, dragged arm with springs, a 1.5 u floor on
its length, slow pull-in recovery and many per-state rules. The **hero view** (mode 0x20, the lock-strafe and
first-person setting) is fps_cam: the view is pure stick yaw and pitch, the position is eye - forward x L, and
collision only shortens L. Our Modern camera is the second kind built on top of rac2's first kind.

Where we differ from UYA's hero view in ways a player sees:
1. **Our view is not along the boom.** rac2's build_view aims the camera from the eye at its own look point (about
   0.5 below our hang point, horizontally at the unlagged hero). UYA's hero view looks along the boom through the
   eye point. This one difference is the "hat" in close quarters, much of the need for fading, and very likely the
   "camera pushes Ratchet" (the move direction is the view's yaw, and that yaw becomes unstable when the eye gets
   close to the look point). See section 3.
2. **The boom is retaken only on a tilt.** After any stand-aside (a swing, a zone, a scene, Clank) the stock camera
   runs until the player gives pitch input.
3. **The hang point lags sideways** by rac2's look-point springs (about 13 ticks of his motion); UYA's hero view hangs
   from the hero rigidly.
4. The stick lacks UYA's **quick turn** at full deflection and its **camera friction** on targets.
5. **Swingshot**: the auto Swingshot picks by Ratchet's body heading through rac2's finders (45 deg cones), not by
   the camera; standing still his heading never follows the view. Pick by the camera, check the pick with rac2's own
   finders at a written heading, and write that heading on the press tick (section 6).

---

## 1. What UYA does

### 1.1 The two cameras and their order (H)
- camera_manager_update runs per frame: camera_select (the current mode's check, then every other mode's eligibility,
  then camera_switch, then the current mode's update; `rac3:src/frontbin/382380.c:541-585`), the blend, then the
  matrix, angles and position (func_00384D88 at `382380.c:1200`; names.tsv:440, 447).
- Mode 0 follow update (camera_follow_update, `rac3:src/levels/shared/L01_003E5700.c:1714-1773`):
  orbit speed from the option; the platform carry (its z rate-limited); camera_platform_ride; camera_follow_params
  (asm only: per-state overrides and "the sharp-turn recentring", names.tsv:1114); camera_follow_motion;
  then arm_drag -> stick input -> manual input -> camera_obstacles -> camera_place -> camera_orient
  (`L01_003E5700.c:1636-1645`); camera_end_of_frame (springs D, h, look height; resets the per-frame overrides;
  `L01_003E5700.c:444-532`).
- The hero view (0x20) copies fps_cam's position and rows each frame (names.tsv:1171). fps_cam runs near the end of
  hero_tick (hero_camera_update, `rac3:nonmatching/functional/func_L01_00512E68.c`).
- **While the follow camera is current, fps_cam's yaw, pitch and rows are copied from it every frame**
  (func_L01_00512E68.c, the `else if (!option)` branch: +0x15A0, +0x1580, +0x1630 from the camera). So switching to
  lock-strafe or the look never changes the direction. H.

### 1.2 Follow-camera geometry (H unless marked)
Layout: `rac3:include/structs/camera.h:122-217` (CameraFollow).
- **Follow point** (+0x60/+0x70): the hero horizontally, unlagged; its height set by the vertical mode (1.6).
- **Look point** (+0xC0): springs to the follow point, 0.015/0.2 across and 0.0125/0.2 up (camera_follow_motion,
  `L01_003E5700.c:236-244`; names.tsv:1112). Not rescaled for PAL.
- **Pivot/clearance point** (+0xB0): follow point + up x height, or lowered by two 20 u lines up and down: a gap
  under 2r puts it in the middle, a ceiling within height + r puts it r under the ceiling; both also set the height
  target (gain 0.006) (`L01_003E5700.c:289-327`). r = 0.35 + sweep growth.
- **Aim** (+0xA0): lerp(follow point, look point, n/90) + look height. n counts up only while "aim blend" is on
  (strafing, lock-on, bosses, volumes set it); otherwise **the view aims at the unlagged hero** while the body lags
  (`L01_003E5700.c:254-264`; camera.h:139, 157).
- **Dragged arm** (+0x180): arm = lerp(arm, last camera - pivot, 0.85), length clamped to [D - pull-in, D]
  (`rac3:nonmatching/functional/func_L01_003E9908.c`). The camera stays where it was in the world; the arm is then
  forced back to length D. This is the lazy follow: running sideways swings it behind, running at it pushes it back.
  A volume can replace the 0.85 (CameraVolumeSlot +0x14 drag).
- **Actual arm** (+0x190): its yaw and elevation spring 0.015/0.2 toward the dragged arm's; its length springs with
  gain/damping from camera_approach_distance (`L01_003E5700.c:1543-1582`).
- **Camera** = look point + up x height + actual arm, then up to 3 pushes of a 0.25 sphere (mask 0x36); on a push
  the look point, pivot and follow point move by the same delta (`L01_003E5700.c:1591-1624`).
- **View** (camera_orient, `rac3:nonmatching/functional/func_L01_003E99E8.c`): yaw toward the aim (held if the aim
  is under 0.05 u away horizontally), plus an extra yaw (+0x48, "zero in every trace"); pitch toward the aim using
  view heights that spring at 0.004/0.2; minus the look-up angle; clamped to +-70 deg (1.2217). rac2's
  follow_camera_build_view is the same code (`rac2:src/levels/shared/L00_004224D0-follow-g8.c:41-120`).

### 1.3 Collision and its easing (follow camera; the hero view is in rac3-strafe-camera.md 2a)
- **Pull-in** (camera_obstacles, `rac3:nonmatching/functional/func_L01_003E8360.c`, medium):
  - Arm sweep and camera-end sphere as in rac3-strafe-camera.md 2b. A contact sets the pull-in.
  - **Minimum arm 1.5**: pull-in clamped to D - 1.5, timer set to 0x683 (1667 PAL frames = 2000 NTSC).
  - Timer **held while blocked**, and also held at its reset value **while the right stick is pushed (>= 0.3)**:
    the release waits for the stick to rest (the test is on the signed axes, so only one way of each counts; M). Then each frame pull-in = cosine_ease(0, pull-in, timer/1664).
    Applied to the already-eased value each frame, so the release is slow at first: half gone about 2.7 s after
    the release starts (my estimate, M).
  - In the look camera (mode 4 / 0x51) within 8 frames of a switch the timer is set to 0x589 (shorter hold).
  - A stick turn into a wall: +0.09 PAL (0.075 NTSC) pull-in and timer 0x719 (2180 NTSC)
    (`L01_003E5700.c:612-648`).
  - Line of sight pivot -> camera blocked **25 frames** (PAL; 30 NTSC) -> camera_reset_follow; camera over 30 u away
    -> reset; within 20 u but blocked while the hero faces it -> reset.
  - Mobys near the arm have their collision pointer cleared for the step (the game passes through them), except ten
    classes it steers round by 1 deg a frame.
- Hysteresis in one line: **instant in, a long hold, a slow cosine out, no out while the player turns.**

### 1.4 Auto-follow and recentring
- **Lazy follow** is the arm drag (1.2). It needs movement; it does not happen while standing. H.
- **Virtual stick steer** (camera_steer_to, `L01_003E5700.c:1187-1222`): the arm's angle off the wanted direction,
  x = angle / 90 deg, gives a virtual right stick 2x - x^2 (signed), times the turn speed (+0x1F4). So the steer
  slows smoothly to zero as it arrives. A real stick push restores the normal speed and overrides it
  (camera_stick_input, `L01_003E5700.c:1271-1320`). H.
- **Lock-on**: turn speed (t/400) x pi/2 rad a frame, t the frames locked (to 400), then steer_to the target
  (names.tsv:1133). L (names only).
- **Before the look** (mode 4, L1 aim): the follow camera is steered behind the hero at 0.0698 rad (4 deg) a frame
  for up to 6 frames, then the switch. **After it**, 25 frames of steering at 0.1396 rad (8 deg) a frame
  (`rac3:nonmatching/functional/func_L01_003EAC70.c`, medium). M.
- **Volumes**: "steer" every frame, or "steer when idle" once the right stick rested 120 frames (any push > 0.01
  restarts the count), at 6 deg a frame by default (`rac3:src/levels/shared/L01_003F29F8.c:207-217`). H. Same as
  rac2's override slots.
- **Sharp-turn recentring**: in camera_follow_params, which is assembly only. Not read. L.
- No general "recentre after N seconds idle" was found. The hero view has no auto camera at all (the hero turns to
  the camera instead: hero_strafe_turn, rac3-strafe-camera.md 3.7).

### 1.5 Lag and lead
- Lag: the look point's springs (1.2). Steady-state lag of a spring v += k e - c v is e = (c/k) x speed, so
  0.2/0.015 = **13 frames of his motion across** and 0.2/0.0125 = 16 frames up. The camera body hangs from that
  point; the aim does not (it uses the follow point unless aim blend is on). H for the formula.
- **Run-at-camera pull-back** (camera_approach_distance, `L01_003E5700.c:1429-1477`): moving with
  move . camera_axis <= -0.3, the distance target rises from the rest distance (4.64) toward 6.0 by speed (full at
  0.07 u/frame), kept at the highest reached, with a stiffer length spring (gain 0.02 -> 0.04, damping 0.2 -> 0.3).
  It stays while he stands facing the camera. Volumes outside 4.64-6.0 turn it off. H. rac2 has a face-the-camera
  pull-out too (rac2-spring-arm.md 3); our boom inherits it through D (st+0x18C). M.
- No lateral look-ahead was found.
- Platform carry: added to every point at once; its z is rate-limited (`L01_003E5700.c:1732-1748`). H.

### 1.6 Vertical follow: jumps, falls, slopes
camera_vertical_modes and camera_follow_vertical (`rac3:src/levels/shared/L01_003E3930.c:675-865`, `879-1068`). H.
- **0 ground**: follow point = hero height.
- **2 jump hold** (state type 4): the follow point keeps the take-off height. It follows the ground below if that
  rises above (landing on a ledge). It lets go into mode 1 when he is past the apex, more than 0.2 above the ground,
  and his head has left the view (camera_head_angle < -0.01 or > pi/2), or on a wall jump (state 0x11)
  (`L01_003E3930.c:949-985`). So **no bob in ordinary jumps**; the "dead zone" is the whole jump while he stays in
  view.
- **1 fall** (type 2 or 30): follows him exactly, and the look point's vertical spring stiffens from 0.01/0.2 to
  0.06/0.4 over a 10-frame blend (camera_aim2_spring, `L01_003E5700.c:143-167`).
- **Timed rises** to "ground + h": heli jump 3.0 u over 71 frames, double/rocket jump 3.5 u over 37, flip 1.0 u over
  29, jump attack 1.5 u over 25 (PAL frame counts). Linear, stopped by a 0.2 sphere ceiling test at the camera
  (`L01_003E3930.c:688-752`, `990-997`). Then hold (mode 4) until landing or falling, with 17-frame blends back.
- The jump attack ends with a 75-frame ease of the view direction back (camera_orient's blend).
- **Hero view**: eye = hero + up x h, h eased to 2.0; in a jump under 3 u the eye holds its height
  (rac3-strafe-camera.md 1c). So both cameras ignore ordinary jumps.
- **Auto-pitch on slopes**: none as such. The follow camera's arm drag keeps the camera's world height, so uphill it
  ends lower and looks up. The look-up angle adds up to 15 deg on good ground with the arm below level
  (camera_orient). Volumes can set a tilt target while the stick's y rests. The hero view has none.

### 1.7 Pitch limits (H)
- Follow: tilt target up to +-40 deg (0.698) past a 0.3 dead zone, stepping 0.02 a frame pushed and 0.01 back, the arm
  turning at most 0.0305 rad a frame; **it springs back when the stick is let go**; view clamp +-70 deg
  (`L01_003E5700.c:1337-1427`; camera_orient).
- Hero view: 70 deg down, 60 deg up; the up limit eases to 45 deg in hero state types 3 (ledge) and 20 at 0.0419 rad
  a frame PAL (func_L01_00512E68.c; rac3-strafe-camera.md 1b).

### 1.8 FOV (H)
- 1.1100293 rad (63.6 deg) by default; set by camera_fov_set (`rac3:src/frontbin/382380.c:148-189`): mode 0 jumps,
  1-2 blend over frames, 3 springs (0.005/0.2 everywhere it is called).
- Gameplay changes only through camera volumes (+0x10) and the Flux Rifle scope. Leaving a volume that set it springs
  back to 1.11 (`L01_003F29F8.c:146-151`). No FOV kick for running, strafing or aiming.
- fps_cam's turn speed is scaled by fov / 1.11 (`L01_00412FE0.c:124`), so a zoomed view turns slower.

### 1.9 Close quarters (H unless marked)
- Hero view (fps_cam_place, rac3-strafe-camera.md 2a): the camera slides in **along the view ray** toward the eye
  point. L = (hit distance along the ray) - 0.5; below 0 the camera sits at the (guarded) eye. **No minimum length.
  The eye does not drop, rise or swing; the view's direction never changes.** The eye is 2.0 above his position, 0.25
  above his first-person eye (1.75), so at L = 0 he is mostly under the bottom of the frame.
- Looking down, the eye moves forward 0.45 u x pitch/90 deg (rac3-strafe-camera.md 1c).
- Follow camera: never under 1.5 u; floors and ceilings keep the arm's length and slide/raise the camera; walls
  shorten it to the 1.5 floor; if that still does not fit, the camera presses into the wall and after 25 blocked
  frames it is reset.
- **No hero fade** was found: no hero alpha write in names.tsv, and no camera code writes the hero (L for absence;
  names cover only part of the code). Deadlocked's fade is not in any decomp we have.

### 1.10 Aim, assist and lock-on
- L1 look (mode 4) and the hero view (0x20) take fps_cam's position and rows. Blend in 0.075 a frame (about 13 NTSC
  frames, 0.22 s); leave the look from 25 % done at 0.018 a frame (0.9 s); leave the hero view at 0.045 (0.37 s), or
  cut into the look (names.tsv:1143-1144, 1169-1172; `L01_003E5700.c:2052-2093`). H.
- Lock-strafe to first person: offset length eased to 0 (flag 4: 0.032/0.064/1.6 PAL). Who sets it: not traced.
- fps_cam assist (`rac3:src/levels/shared/L01_00412FE0.c:105-330`), only with a target (fps_cam_find_target: a
  category-5 moby on a 30 u ray, w = 1; else the best in-sight one within 30 u and 15 deg x zoom,
  w = (cos off - cos 15) / (1 - cos 15)) H:
  - **friction**: yaw speed cap x (1 - 0.5 w), pitch x (1 - 0.6 w) (`:231-252`);
  - **tracking**: while any stick (right or left) is off centre, yaw += -(target's relative motion . left / dist) x
    0.666 w (clamped +-0.0314 PAL / 0.0262 NTSC) and pitch += (... . up / dist) x 0.85 w with a distance fall-off
    (`:278-307`);
  - **flick**: a push past 0.85 toward a target's side is kept for 6 frames after release and the turn stops exactly
    on the target (`:188-198`, `:255-270`);
  - quick turn (below) waits 8 frames and accelerates at a third.
- Guns: in lock-strafe there is no auto-aim; gun_target is the lock-on target, else none with the lock-strafe camera
  (names.tsv:1347). Shots bend up to 80 deg to fps_aim_pos (30 u camera ray). Lock-on itself needs camera mode != 1
  (names.tsv:1561). L/M (names).

### 1.11 Blends between modes and handing control back
- camera_switch (`rac3:src/frontbin/382380.c:369-458`) H:
  - the old camera's leave kind (+0x7E) 4 = cut;
  - record blend 1 = lerp with the record's rate or 0.018 a frame; 5 = orbit blend over the record's frames or 40;
  - **kind 3 or 5 = copy**: the new camera starts at the old one's exact position and rows.
- Lerp blend: cosine-eased progress, position lerp whose start moves with the hero, rotation slerp
  (`382380.c:733-775`). Orbit blend: yaw, pitch and length about the hero (`382380.c:795-882`), so a blend never cuts
  through walls or the hero.
- The follow camera's init rebuilds its arm from the camera's current position (names.tsv:1115). **Hand-back is
  therefore continuous**: after a copy-leave, the follow camera simply continues from where the other camera was,
  and its drag and springs settle.
- no_look_timer: 2 frames after a scripted camera, 10 after the fixed camera, 4 for Clank (names.tsv:46).
- **Swing and grapple** (mode 7, `rac3:src/levels/shared/L01_003EB9C8.c:232-625`) H:
  - in: an orbit blend of 50 frames (37 when re-latching to another node);
  - distance 3.65 + t and height 2t, t = how far he hangs below the node (0..1), approached at 0.16 and 0.08 u a
    frame PAL (0.1333 / 0.0667 NTSC); otherwise 4.64 and 2.0;
  - yaw: a soft angle spring 0.00125/0.175 toward his swing heading, or toward the next swing node ahead of it;
  - pitch toward the hero's look height, spring 0.02/0.175 (the grapple ramps 0.017 a frame);
  - out: when his state changes, leave kind 3 (the follow camera copies the swing camera's pose).
- Grind: no grind camera mode was found (the follow camera, with its drag, trails along the rail). L.

### 1.12 The right stick
- Hero view: rac3-strafe-camera.md 1a, plus details read now (`L01_00412FE0.c:133-325`) H:
  - the stick is squared after the pad; no dead zone in fps_cam itself;
  - **quick turn**: |sx| > 0.99 (after squaring: the stick pinned) switches the yaw target to 0.1257 rad/frame PAL
    (0.1047 NTSC, 360 deg/s) with accel 0.0084 PAL (0.0058 NTSC); at once with no target, after 8 frames with one;
    off when <= 0.99;
  - a speed below the current one is taken at once (instant stop and reversal), a higher one ramps;
  - scale = fov/1.11 x lerp(0.25, 1.25, option/64).
- Follow camera: yaw = orbit speed x stick, **linear, no squaring**; the gain-2/damping-4 spring with its
  no-overshoot clamp lands in one frame, so the response is instant. Orbit speed = option/100 x 0.0567 + 0.0218
  rad/frame (PAL code, `L01_003E5700.c:1729-1730`; 0.050 rad/frame at the default 50). Pitch as 1.7.

---

## 2. Our camera, as it behaves (facts the review uses)

From mod.cpp:2281-2545 (boom_tick), 2638-2679, 3054-3145, 3147-3206, 3364-3448, 3486-3624. H for the reading.
- The boom writes the goal and smoothed orbits (v = dir x L), the eyes, rec+0x30 = anchor + v, zeroes the eye springs,
  sets the shortfall, the sweep lock and the tilt. It **does not write the view**: rac2's build_view builds the
  rows from the eye toward its look point (horizontally the unlagged hero, vertically about 0.5 under the hang point)
  minus its look-up angle (`rac2:L00_004224D0-follow-g8.c:41-120`). The aim ray (aim_frame) and the move direction
  use those rows.
- The move direction in rac2 is the stick angle plus the camera block's yaw (block+0x158) and the stick turned by the
  block's rows (+0x370) (`rac2:src/levels/shared/L00_002AFBC0-input.c:59-70`). UYA's hero_stick_move does the same with
  GameCamera +0x18 and +0x380 (`rac3:src/levels/shared/L01_004E8EE0.c`, func_L01_004EE850). H. (hero +0xA60..+0xA78
  is a separate push added to the speed only when +0xA70 > 0, `rac2:L00_002AFBC0.c:895-906` and its per-level
  readers; it is not the stick. M.)
- The anchor is st+0xC0 + U x h: rac2's look point, which lags as UYA's does (1.5). The pivot st+0xB0 is rac2's
  clearance point (unlagged, kept off ceilings by the game, 1.2).
- rac2's vertical modes are UYA's code (jump hold, timed rises, fall follow: `rac2:src/levels/shared/L00_004212B8.c:99-205`),
  and the anchor follows them, so the boom already ignores ordinary jumps and does not bob. Nothing to add there. H.
- The run-at-camera pull-back reaches the boom through D (st+0x18C). Collision timers are the boom's own.
- The anchor's room and push corrections change v, but the mod never writes st+0xC0. rac2's move_eye rebuilds the
  eye next tick as st+0xC0 + U x h + st+0x170. If the picture is built in the tick (before on_frame), the eye on
  screen is the game's anchor + v, not ours + v (see open question 1). M.
- The boom is taken only when the player gives pitch input (`if (pitch == 0.0f) return;`, mod.cpp:2303). After any
  stand-aside the stock camera (lazy follow, spring-back tilt, 1.5 floor) runs until then.
- Swings (groups 13, 14, 33) and grinds (15, 16) are move_group, so the boom runs there and holds its direction.
- modern_stick: per-axis dead zone 0.12, rescaled, squared; top yaw speed = the game's rotation option (st+0x1D4),
  pitch 4/3 of it, UYA's ramp and instant stop; no quick turn, no outer dead zone, no friction.

---

## 3. Close quarters: backing into a wall with the camera between

The player backs Ratchet into a wall. The boom shortens to kBoomMin 0.25 from the anchor (about 1.64 above his feet).
The eye ends just behind and above his head, looking down at it ("like a hat"). He fades under 1.3 u. The player also
feels the camera "push" him along the wall.

### 3a. What UYA does there (H, from fps_cam_place and 1.9)
- The camera slides in **along the view ray** to the eye point. There is no minimum: at L <= 0 it sits at the eye.
- The eye (the hang point) does **not** drop, rise or move sideways. Only a ceiling lowers it (the 0.4 sphere from
  hero + 0.7).
- The view's yaw and pitch **do not change**: they are the stick's. The eye is 2.0 above his position, 0.25 above his
  first-person eye (1.75), so at L = 0 he is mostly under the frame. Looking down, the eye moves forward up to 0.35 u
  at 70 deg.
- No fade was found.
- Recovery: L eases out (0.00556 u/tick^2, at most 0.333 u/tick, NTSC), so about 1 s to full length.
- The move direction is the camera's yaw, which is the stick yaw: stable at any length.
- The follow camera (not the hero view) never gets there: its arm floor is 1.5, so its eye-to-aim geometry stays well
  conditioned.

### 3b. Why ours looks like a hat and pushes (H mechanism, M magnitude)
- build_view aims at the look point, height minus look height = 2.0 - 1.5 = 0.5 under the anchor (rac2's defaults,
  `rac2:src/levels/shared/L00_004233F8-g8.c:90, 111`). Seen from 0.25 behind the anchor that point is steeply
  below: the view looks down at the top of his head. At L = 0.25 and lift 20 deg the view pitch is about 68 deg down
  (the 70 deg clamp is near); at L = 4.64 about 26 deg.
- Its yaw is the direction from the eye to the unlagged hero, horizontally. At L = 0.25 that baseline is about 0.25 u
  long. Any sideways offset of the eye from the hero turns the yaw by atan(offset / 0.25): 0.1 u is 22 deg. Offsets
  come from the anchor's spring lag (13 ticks of his motion; while he backs up, the lagging anchor can be in front
  of him and the yaw can turn round entirely) and from our anchor_push off the wall. build_view only holds the
  previous yaw when the baseline is under 0.05 u (`rac2:L00_004224D0-follow-g8.c:61`).
- The stick's move direction is that yaw (section 2). So the swinging view steers his run: he slides along the wall.
  No camera code writes his position (rac2-camera-situations-2.md 4). M that this is the push the player feels.

### 3c. What our boom should do there (recommendation)
1. **Own the view's orientation** (recommendation R1): forward = -dir (along the boom, through the anchor), yaw and
   pitch the boom's. This alone removes the hat (the view stays at the player's pitch) and the push (the move yaw is
   the boom yaw).
2. Keep the length logic. Let it reach about 0.1 (or 0); UYA has no floor. kBoomMin 0.25 is fine once the view is
   along the boom.
3. Fade by length: start at about 1.0 and reach alpha 0 (if alpha 0 draws nothing; test) by about 0.35. With the view
   along the boom he fills less of the frame, so the fade can start later than 1.3.
4. Hang point: see the judgement below. Do not push it sideways off the wall while short (anchor_push): with an owned
   view that only slides the picture.
5. Recovery: as now (the 30-tick hold, then the UYA ease).

### 3d. The candidate idea, judged
"Below about 1 u of arm, ease the hang point down to head height (~1.3) along the view, so at minimum the eye is in his
head looking along the view, with him fully faded."
- UYA does not move its hang point for walls. Its eye stays where it is (0.25 above its first-person eye) and the
  camera slides along the view to it.
- **Without R1 it fixes only half.** The view would still aim at rac2's look point, which is always 0.5 under the
  anchor. Lowering the eye 0.34 brings it to 0.16 above that point: the hat gets milder. But the eye is then a few
  tenths from the look point both across and down, so the yaw stays unstable (the push stays) and the pitch becomes
  unstable too; lowered by more than 0.5 the view tips up.
- With R1 it is reasonable, a first-person-like end state, but:
  - in code terms our anchor is 2.0 above the follow point (st+0xC0 + U x st+0x190), 0.5 above rac2's first-person
    eye (1.5 above his position, hero +0x151C, rac2-aim-movement.md 2c.3). The "1.64 above his feet" and "head
    height 1.3" figures do not match those numbers (open question 9). End the drop at **the first-person eye height,
    1.5 above his position** (a drop of 0.5), which is where the game's own first person looks from, and no lower;
  - drive it by length (h = lerp(1.5, anchor height, clamp(L / 1.0, 0, 1))), not by time, so it is continuous and
    reverses by itself;
  - move it straight down along up, not along the view, so the crosshair's column does not shift;
  - it is optional: with R1 and a fade, UYA's choice (keep the hang point) also works; at L = 0 the eye is 0.5 above
    his first-person eye, looking along the view, and he is faded (UYA's is 0.25 above, unfaded).
- My preference: R1 first, keep the hang point, test; add the lowering only if his faded head still bothers.

---

## 4. Ranked recommendations

**R1. Own the view: look along the boom.** (Player sees: no hat in tight spots; no view tipping down as the boom
shortens; the run direction no longer wanders near walls; Ratchet sits at a fixed place in the frame like UYA's
lock-strafe.)
- UYA: fps_cam builds the rows from yaw/pitch; the position is eye - forward x L (`L01_00412FE0.c:336-341`,
  `:381-455`).
- Change: in boom_tick, make the rendered camera's rows forward = -dir, left = unit(up x forward), up = forward x left,
  and the eye = anchor + dir x L. Options, in order:
  1. `mod.camera_set()` (SDK 1.12; rac2 support is "part": test which part). The docs say the picture and the game's
     logic both read it while the game's camera runs underneath. If it covers rac2's view build and block+0x158,
     this is the cleanest: the boom would then not need to write the orbits, eyes, springs, shortfall or tilt at all.
  2. If on_frame writes reach the picture (open question 1): write the record's rows (rec+0x00..+0x2F) and the
     camera block's rows (+0x370) and yaw (+0x158) after the tick, with the eye.
  3. Fallback through build_view's own inputs: the look angle st+0x34 with st+0x3C = 0 and st+0x38 = 0 (pitch), and the
     extra yaw st+0x48 (yaw) (`rac2:L00_004224D0-follow-g8.c:57-59, 106-113`). They need a prediction of next tick's
     eye-to-look-point vector, which is ill-conditioned below about 1 u; there freeze the last good values.
- Effort: small (1) to medium (3). Risk: medium; test that the aim ray, the reticles and the HUD markers follow (they
  read the rows), and the 70 deg clamp.

**R2. Retake the boom at once on hand-back.** (Player sees: after a swing, a zone, a scene or Clank, the camera is the
Modern one straight away, not the stock lazy camera until the first tilt.)
- UYA: hand-back is continuous; the next camera starts from the last pose (1.11).
- Change: in boom_tick, drop `if (pitch == 0.0f) return;` once the boom has been owned in this level (or always), and
  adopt yaw, lift and length from the game's smoothed orbit as it does now. Keep the start-of-level behaviour if it is
  wanted.
- Effort: small. Risk: low (adoption already exists; check the first tick after a scripted camera for a pop).

**R3. Hang the boom from the unlagged point horizontally.** (Player sees: Ratchet stays put on screen while strafing
and running sideways; the view does not drift and settle back when he stops.)
- UYA: the hero view's eye is hero + up x h (no horizontal lag); the follow camera lags its body but aims at the
  unlagged hero.
- Change: anchor = pivot B (st+0xB0) horizontally + the vertical part of (st+0xC0 + U x h). B is the game's own
  clearance point, so the B -> A room sweep becomes a short vertical guard. Write st+0xC0 to match (R4) or use R1's
  owned eye.
- Effort: small. Risk: medium; landings and steps then show through unsmoothed horizontally (none expected) and the
  jitter fixes must be retested. Measure the lag first (log |A - B| horizontally while running).

**R4. Make the anchor corrections reach the screen, or drop them.** (Player sees: fewer clips into walls and ceilings
near the hang point; maybe the curved-wall case.)
- Change: first answer open question 1. If the eye on screen is the game's st+0xC0 + U x h + v, write
  st+0xC0 = anchor_corrected - U x h (and zero its spring speeds st+0xD0..+0xE8), as rac2-spring-arm.md's recipe did;
  or with R1 option 1 the corrections are used directly.
- Effort: small. Risk: low to medium (st+0xC0 is also the spring state of the look point).

**R5. Quick turn and an outer dead zone.** (Player sees: a pinned stick swings the camera fast, as in UYA.)
- UYA: |sx| > 0.99 -> yaw target 0.1047 rad/tick NTSC (4x normal), accel 0.0058 (3x normal); off when released
  (1.12).
- Change: in stick_shape, add an outer dead zone (map |u| >= 0.95 to 1.0). In modern_stick, when |sx| (shaped) > 0.99
  use target = 4 x top, accel = 3 x the normal accel (kStickYawRamp x 3); keep the instant drop. Optionally a radial
  inner dead zone instead of per-axis.
- Effort: small. Risk: low; make it a setting if players dislike it.

**R6. Camera friction on targets (and optionally tracking).** (Player sees: the camera slows over an enemy under the
crosshair and follows it a little while he moves.)
- UYA: 1.10. Only with a target, so never felt in empty spaces.
- Change: in modern_stick, w = 1 when crosshair_on_target (mod.cpp:2819) is true with a weapon in hand, else 0 (or a
  15 deg cone fall-off); scale the yaw and pitch targets by (1 - 0.5 w) and (1 - 0.6 w). Tracking (yaw += the
  target's lateral motion / dist x 0.666, clamped 0.026 rad/tick) only while a stick is off centre. Skip the flick.
- Effort: small (friction) / medium (tracking). Risk: low; cost one more line cast a tick (aim_frame casts one already).

**R7. Soft follow on rails and swings.** (Player sees: on a curving rail or a chain of swings the camera turns with
him instead of holding still.)
- UYA: the swing camera springs its yaw 0.00125/0.175 toward his heading or the next node; grinds use the dragged
  follow camera.
- Change: in groups 13, 14, 33, 15, 16, once the player's own turning has rested about 1 s (UYA volumes use 120
  frames), add to boom_turn a steer toward his travel heading with UYA's virtual-stick shape
  (rate = speed x (2x - x^2), x = angle off / 90 deg, speed about 2-3 deg/tick). Player input overrides at once.
- Effort: small. Risk: low; it is an auto camera, so keep it to these groups (or a setting).

**R8. A short blend into first person (Aim: Move).** (Player sees: holding Aim zooms quickly into his head instead
of cutting.)
- UYA: hero view in 0.22 s; lock-strafe to first person eases the boom to 0.
- Change: in aim_move_tick, on the press ease s.boom_length to 0 over about 6-8 ticks (with R1 the view does not move),
  then turn first person on. Exit stays as boom_behind_view (1.3, then the UYA ease).
- Effort: small. Risk: low; it delays the first shot by those ticks.

**R9. Small UYA touches.** (Low value.)
- Scale the stick's speeds by FOV / default (only matters under the Pulse Rifle scope and override FOVs).
- Ease the up limit to 45 deg on ledges.
- Move the hang point forward 0.45 u x pitch/90 deg when looking down (needs R1).

---

## 5. What looks unnecessary next to UYA, and what we do that UYA does not

Probably unnecessary or over-complicated (H for what the code does; M for "unnecessary"):
- The **five-line fallback** in boom_tick (offsets, kBoomRadius, the facing factor, kBoomMargin / 0.25 reach) runs only
  when sphere_sweep cannot be asked. Keep one line (UYA's: margin 0.5 along the ray) or none.
- **kBoomSlack and kBoomClearTicks** add a hold on top of the 30-tick min ring, which already holds the nearest want
  for 0.5 s. One hold is enough (keep the ring: the player confirmed it fixed the jitter).
- **The behind-a-moby placement** (moby_side, moby_in, kMobyRoom, the second reverse line). UYA: mobys only shorten
  the arm when the camera already touches one (a 0.51 u query at the camera); otherwise the arm passes through them.
  The sphere sweep already stamps mobys out. Simpler: pull in in front of a moby only when the camera point is inside
  or touching it.
- **anchor_room + push_out + anchor_push + its line of sight**: three guards for one point, and per section 2 they may
  not reach the screen. With R3 the room sweep becomes UYA's short vertical guard; the push-out is doubtful since
  first_root ignores starting contacts that move away.
- Dead code: `hx, hy, hz` in level_line, `want_now`.
- boom_yaw, boom_yaw_written, boom_heading and boom_heading_written store the same direction twice (as angles and as
  vectors). The vectors alone suffice; derive the yaw in the frame when needed.
- With R1 option 1, the whole write set (orbits, eyes, springs, shortfall, timer, lock, tilt) and the rescue filters
  (kStuckLength, kAutoJump) would no longer be needed: the game's camera could do what it likes underneath. Only a
  real reset (a respawn) needs detecting, from the hero's position jumping.

What we do that UYA does not:
- **Fade Ratchet.** UYA does not need to (its eye is above his head). Good to keep; with R1 it can start later.
- **The 30-tick min ring.** UYA's hero view has none (instant in, eased out). Justified here by rac2's busier geometry
  and by the player's report.
- **A smooth pull-in (0.08 s) unless the camera is behind a face.** UYA always pulls in at once. Smoother; good.
- **The crosshair ray starts at Ratchet's depth.** UYA's starts at the camera. Better in third person; good.
- **Auto-aim cones kept and centred on the crosshair.** UYA's lock-strafe turns auto-aim off and uses camera friction.
  Fine for a pad; R6 adds UYA's friction on top.
- **The heading kept as a world vector for planetoids.** UYA turns fps_cam through the gravity basis; same aim. Good.
- **hold_direction taking back the game's turns.** UYA does not need it because its view is its own; R1 option 1
  would retire it for Modern.

---

## 6. Swingshot targeting

The player's report: auto Swingshot works standing, but needs very precise aim. This section is about selection.
rac2 paths are under `rac2:src/levels/shared/`, UYA's under `rac3:src/levels/shared/`. I re-read the cone and scorer
lines myself; the rest comes from a delegated read of the same files.

### 6a. Why it feels precise today (rac2 + our mod)
1. **The finders look along his body's heading (+0x98), not the camera.** mod.cpp's swing_target (:646-659) runs
   rac2's three finders as they are. Standing, nothing turns +0x98: the idle states do not steer it, and lock-strafe
   turns him to the camera only while he walks (`L00_002BA7D8.c:563-612`, `L00_002B35A0.c:31-35`). So with Modern's
   free camera, turning the view at a node does nothing; he has to be turned. H for the code, M that this is the
   whole feel.
2. **Node class 0x323 has a 45 deg vertical window from his feet**, range 16, and must be no more than 3 below him
   (`L00_002C3BB0.c:42-53`). A node high above a near ledge is refused until he backs off or jumps. H.
3. **One pick per class, and line of sight only on that pick.** If the best-scored node is blocked, nothing is ready:
   no fallback to the next (`L00_002C3BB0.c:58-69`, `L00_002C3E18.c:63-78`). A nearer blocked node inside the cone
   hides the one he wants. H.
4. **Aiming (state 0x1D) or looking (1) narrows the cone to 5 deg in both axes, measured from the camera**
   (`L00_002C3BB0.c:49-50`; `L00_002AE388-range.c:31-36`). With our Aim held it becomes very precise. H.
5. The mod checks at the press and again after the equip (mod.cpp:998, :1117), accepting any ready class, not the
   same node. A heading change during the equip can drop the target. M.

### 6b. The two games' selection
| | rac2 | UYA |
|---|---|---|
| 0x2F6 node | 30 deg across, no vertical limit, range 30 (or the node's), horizontal distance >= 4 (`L00_002C3E18.c:29-81`) | the same, range capped 30 (`L01_004FE668.c:507-552`) |
| 0x323 node | **45 deg across, 45 deg up/down**, range 16, <= 3 below; 70/65 deg in state 0x2C (`L00_002C3BB0.c:42-52`) | **70 deg across, 80 deg up/down**, range 16, <= 3 below; 90 deg across in state 44 (`L01_004FE668.c:394-410`) |
| blue 0x99D | 30 deg, range 30 (`L00_002C2DE8-select-g8.c:27-68`) | 30 deg, range 30 |
| axis | +0x98 (his heading; +0x64C on a grind); the camera only in states 1/0x1D, 5 deg | +0x98; the camera only in states 1/29, 5 deg |
| score | distance x (1 + angle across), +10 if not on screen (+0x31 == 0); lowest wins (`L00_002AE388-range.c:18-43`) | the same (`L01_004E8EE0.c:594-640`) |
| sticky | no: picked again every tick; cached only during the swing | no |
| line of sight | his position + 0.5 z to the node, CollLine mask 2, the winner only | the same |
| turned to it | yes, eased: +0x1A0 set at the swing's start, then a spring on +0x98 capped 6 deg a tick (`L00_002C42E0-state-g8.c:452, 463`; `L00_002BA7D8.c:1172, 1190`) | entry the same (M) |
| reticle | only in states 1/0x1D | only in 1/0x1D (look is forced to 0x1D with it in hand) |
| auto-equip | none | **in the air only**: the finders run while falling or near a drop; a found node sets +0x25E7 and the Swingshot is put in hand; Circle still fires it (`L01_004DF428.c:1404-1465`, M for the swap) |

Neither game selects by the camera in third person. UYA's answer to "too precise" was a much wider 0x323 cone
(70/80 deg) and an air-only auto-equip. H for the cones, M for the auto-equip details.

### 6c. The idea, judged: pick by the camera with a wider cone, LOS from higher, turn +0x98 on the press tick
- **Turning +0x98 to the node makes rac2's own finder accept it across.** The finders read only +0x98 (+0x64C in
  group 15), his position, the state and, in states 1/0x1D, the camera. Nothing in case 13 or the swing's start tests
  facing again (`L00_002C9448-g8.c:287-312`; `state-g8.c:170-176`). Written in on_pad it meets one tick of the state
  machine first: no steering when idle, a small spring step when walking. H/M.
- **It does not get past the rest:**
  - the 45 deg vertical window on 0x323, the range, the <= 3 below rule, the >= 4 across rule on 0x2F6 and blue;
  - **line of sight from his feet + 0.5. Yes, it still refuses a node above a ledge or behind a low wall**: the
    heading plays no part in it;
  - the score: at the new heading our node scores its distance, but a nearer node within 45 deg can still score lower
    and win, and if that one is blocked, nothing fires;
  - case 13's choice between 0x2F6 and 0x323 (the lower score).
- **A higher line-of-sight origin in our preview would promise nodes the game then refuses.** The game tests from the
  feet + 0.5. Use the higher test only for a "jump to reach" hint, never to decide.
- Side effects of writing +0x98: his model turns for a tick (the swing turns him that way anyway); it enters his yaw
  history; the strafe heading +0x1458 copies it when not strafing; his run direction comes from +0x1A0/+0x1458, not
  +0x98. M.

### 6d. Recommendation for our preview and press
Preview (each tick, Swingshot tier on):
1. Candidates: the moby list's 0x2F6, 0x323 and 0x99D nodes with the game's static filters (flags, range, <= 3 below,
   >= 4 across, the exclusion cuboid).
2. Pick by the **camera's forward** (the view; with R1 the boom's): a cone of about 30 deg across and 30-35 deg up and
   down, scored like the game, distance x (1 + angle). UYA's own 0x323 cone (70/80) shows a wide cone is fine.
3. **Sticky**: keep the current pick until it leaves its cone by about 10 deg or fails the check; switch only for a
   clearly better node (about 25 % lower score).
4. **Check with rac2 itself**: inside KeepCollision, save +0x98, write the yaw from him to the node (and +0x64C in
   group 15), run the finders, and accept only if the game's pick (+0xAD0, +0xAA4 or +0xB10) is our node, ready, and
   would win case 13's choice. Restore +0x98. This predicts every refusal in 6c exactly. Skip it in groups 13 and 33
   (cached results) and in states 1/0x1D (leave them to the game).
5. Mark the checked node in third person with the node marker gadget_crosshair already draws in first person
   (mod.cpp:2959-2987). Optionally a dim marker for a node that only fails line of sight from the feet ("jump").

Press (the replay tick, in on_pad, before set_circle):
1. Check again; write +0x98 = the yaw to the node, +0x1A4 = 0 (no carried turn), +0x1A0 the same; +0x64C in group 15.
2. In auto_frame: if his state is now 0x23, 0x2B or 0x8A, the game has it and eases him to the node. If not, put
   +0x98 back and cancel.
3. Never write in states 1 and 0x1D.
4. Re-check the same node after the equip, not "any class ready".

Effort: medium (a node walk, a checked pick, a marker). Risk: low to medium; the vertical window and line of sight
from the feet remain, but the check keeps the preview honest. Forcing past the finders (hiding other nodes for a tick,
or starting state 43/35 ourselves) is possible but invasive; not first.

---

## 7. Open questions

1. **Render order.** Does the picture use what on_frame writes (rec+0x30, rows), or is it built inside the tick from the
   game's own st+0xC0 + U x h + st+0x170? Test: in on_frame, log the camera block's position (block+0x140) and compare
   it with the previous on_frame's anchor + v. This decides R1's option and R4.
2. What "part" means for camera_set on rac2 (SDK by-area table). Test whether the move direction (block+0x158) follows.
3. Whether st+0x48 (extra yaw) survives rac2's end-of-tick reset (the reset list in rac2-follow-camera-tilt.md does not
   name it).
4. Ratchet's run and back-up speeds in rac2 (to size the anchor's lag: 13 ticks x speed).
5. Who sets rac2's aim-blend flag (st+0x136) in lock-strafe; with it on, build_view aims at the lagged point.
6. UYA's camera_follow_params (assembly): the sharp-turn recentring and the strafe aim blend.
7. UYA's pad dead zones (fps_cam has none of its own); hero state type 20.
8. Whether alpha 0 on the hero moby draws nothing in rac2 (for a full fade).
9. Where the anchor really is: the code gives follow point + 2.0 (rac2's default height), the player's measure gives
   1.64 above his feet. Log st+0x60, st+0xC0, st+0x190 and hero +0x80 standing still to settle it.
