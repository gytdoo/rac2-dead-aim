# UYA (rac3) lock-strafe camera and camera collision: what to copy into a rac2 strafe mod

Source: rac3-decomp (R&C3 **PAL** build; `rac3:` = the rac3 decompilation).
Matched C in `rac3:src/levels/shared/`, near-miss drafts in `rac3:nonmatching/functional/`, names and NTSC-U
descriptions in `rac3:config/names.tsv` (from open-rac-engine, read on NTSC-U).
Nothing was run. Confidence: **H** = read in matched/"functional: high" C, **M** = draft marked medium or inferred,
**L** = guess.

## 0. PAL vs NTSC constants (read this first)
The decomp is PAL (50 Hz). The free-look/lock-strafe code (fps_cam, eye height) is **rescaled for PAL**: speeds x1.2,
accelerations x1.44 relative to NTSC. Check: fps_offset_length ease is 0.008 / 0.4 in PAL C
(L01_00412FE0.c:441) and 0.00556 / 0.333 in names.tsv:118 (NTSC); eye height 0.0064 / 0.64 (L01_00513FD8.c:259) vs
0.00444 / 0.533 (names.tsv:1593). The **follow camera's** springs are NOT rescaled (0.015/0.2 in both), though some of
its rates are (pull-in bump 0.09 PAL vs 0.075 NTSC). rac2 is 60 Hz, so use the NTSC column below.

## 1. Lock-strafe (hero_camera_mode 1) is the first-person camera with a boom, not the follow camera
- hero_camera_mode D_001A6290 (names.tsv:119). Modes 1 and 2 both run **fps_cam** (FpsCamVars at hero+0x1540 =
  0x1A6120, names.tsv:113); mode 1 only adds fps_offset = (-6,0,0) in camera axes (names.tsv:116) and length 6.
  The GameCamera shows it through mode 0x20 (camera_hero_view_update copies fps_cam's position and matrix every frame,
  names.tsv:1171); entering blends at 0.075/frame (~14 frames, names.tsv:1169), leaving at 0.045 (names.tsv:1172). H
- Per frame, near the end of hero_tick: hero_camera_update (func_L01_00512E68,
  `rac3:nonmatching/functional/func_L01_00512E68.c`) -> fps_cam_update (func_L01_00413138,
  `rac3:src/levels/shared/L01_00412FE0.c:105`) -> fps_cam_place (func_L01_00413B68, same file :381) ->
  then hero_eye_update (func_L01_00513FD8, `rac3:src/levels/shared/L01_00513FD8.c`). So the camera is placed from
  **last frame's eye** (1 frame lag, negligible). Then strafing = 2 and the hero's move angle/facing = camera yaw
  (00512E68.c:78-81). H

### 1a. Yaw/pitch from the stick (fps_cam_update; constants from fps_cam_init L01_00412E70.c:12-27)
State: yaw +0x40, yaw speed +0x44, pitch +0x60 (**positive = looking down**), pitch speed +0x64. H
1. Right stick: sx = -rx, sy = ry (L01_00412FE0.c:133-134); invert options flip them (:153-158); then **squared,
   sign kept**: s = s*|s| (:160-161). No deadzone in this function (the pad layer may have one: unknown).
2. Speed targets (all x `scale`):
   | | PAL (code) | NTSC (60 Hz) |
   |---|---|---|
   | yaw max speed, x sx | 0.0314159 rad/f (+0x4C) | 0.02618 (1.5 deg/f = 90 deg/s) |
   | yaw accel, x \|sx\| | 0.0027925 (+0x54) | 0.001939 |
   | quick turn (\|sx\|>0.99) max | 0.1256637 (+0x48) | 0.10472 (6 deg/f = 360 deg/s) |
   | quick turn accel | 0.0083776 (+0x50) | 0.005818 |
   | pitch max speed, x sy | 0.0418879 (+0x68) | 0.0349 (2 deg/f = 120 deg/s) |
   | pitch accel, x \|sy\| | 0.0020944 (+0x6C) | 0.001454 |
   Quick turn engages at once with no target, else after 8 frames at full deflection; with a target its accel x0.333
   (:173-210). H
3. scale = (fov / 1.11) * lerp(0.25, 1.25, camera_speed_option / 64); option defaults to 50 -> 1.03 (:124-125). H
4. Integrator (:310-321): if |target speed| < |current speed| the speed **drops to the target at once**; otherwise it
   ramps by `accel` per frame (approach). Then angle += speed (wrapped), pitch clamped to [limit_up, limit_down]
   (:322-326). So: linear spin-up (yaw ~13 frames to full, pitch ~24), instant stop, no spring/overshoot. H
5. Aim assist (only with a target from fps_cam_find_target, 30 u ray / 15 deg cone, names.tsv:1231): speed caps cut by
   (1 - 0.5 w) yaw, (1 - 0.6 w) pitch (:231-252), and while the stick is non-zero the camera tracks the hero-vs-target
   relative motion (gains 0.666 yaw / 0.85 pitch, yaw nudge clamped +-0.0314 PAL / 0.02618 NTSC) (:278-307). Optional. H
6. Orientation = euler (roll 0, pitch, yaw) (:336-341); with a non-upright gravity basis (hero+0x1480, walls/planets)
   it is turned through that basis instead (:328-335). rac2 mod can ignore the basis. H

### 1b. Pitch limits
- hero_camera_set(1) (L01_00514378.c:323-331): +0xB4 = +1.2217 (70 deg **down**), +0xB8 = -1.0472 (60 deg **up**),
  offset (-6,0,0), offset length 6 (snapped on entry). H
- hero_camera_update eases +0xB8 toward -60 deg, or -45 deg in hero state types 3 and 20, at 0.0419 rad/f PAL
  (0.0349 NTSC) (00512E68.c:70-74). By the sign convention this is the **up** limit; names.tsv:1589 calls it "the down
  limit" - I trust the code (fps_cam's own entry names.tsv:113 says +0xB4 down / +0xB8 up). M on which states 3/20 are.
- First person: +-89 deg (+-70 with D_001D673C). H

### 1c. Eye / pivot / boom (no shoulder offset)
- **Eye** (hero_eye_update): eye = hero.pos + up * h (L01_00513FD8.c:259-268). h springs (approach_eased, accel=decel
  0.0064, max 0.64 u/f PAL; 0.00444 / 0.533 NTSC) to **2.0** in lock-strafe (1.75 first person, 1.0 crouched; Clank 1.1)
  (:225-236). H
- Jump hold: in lock-strafe, hero state type 4 (jump) while height above ground < 3 u, h is reduced by the hero's vertical
  motion and targets 2.0 - (height gained), so the eye stays level through short jumps; >= 3 u it lets go (:240-255). M
- Looking down (pitch > 0): eye moves forward 0.45 u * pitch / 90 deg (:273-285). H
- Water: eye kept >= 0.75 above the water height in swim states (:269-271). H
- **Pivot** for the eye test: hero.pos + up * 0.7 (00512E68.c:63-67). H
- **Boom**: camera = eye - forward * L, L = fps_offset_length (6). It points straight back along the view axis, so the
  eye point (2 u above the feet) is exactly at screen centre; Ratchet sits below centre. No lateral/shoulder offset in
  lock-strafe (offset y,z = 0; only character 2 uses another length D_L01_001DBD5C). H
- Reticle aim point = hit of a 30 u ray from the camera along forward, or its end (fps_aim_pos, names.tsv:117; guns
  bend shots to it up to 80 deg, names.tsv:1463). H

## 2. How UYA keeps the camera out of geometry

### 2a. Lock-strafe: fps_cam_place (L01_00412FE0.c:381-455) = a real spring arm, computed from scratch each frame. H
1. **Eye guard**: if eye != pivot, sphere-sweep (func_L01_00446E38(from, to, radius, mask, skip); returns fraction,
   sphere centre at contact in 0x1EAA00+0x30) from pivot to eye, radius D_001D6530 (= 0.4 per names.tsv:1230), mask 0x84,
   skipping the hero (+0xBC = hero+0x25C0, set by fps_cam_init in func_L01_004DF840.c:66). On a hit the arm origin
   becomes the sphere centre (:402-410). Keeps the arm's start out of low ceilings.
2. If offset length is 0 (first person): camera = eye, done (:411-414).
3. Desired length: probe = eye_orig + back * (6 + 0.5). coll_line(eye_guarded -> probe, mask 4, skip hero)
   (func_003D3CB0, names.tsv:668: hit point 0x1EAAA0 (=0x1EAA20 PAL)). On a hit:
   - moby hit (not class 0xD54) **and** a 0.51 u moby query around the current camera finds something -> L = |hit - eye_orig| - 0.5;
   - otherwise re-cast with mask 2; on a hit L = |hit - eye_orig| - 0.5; on a miss L stays 6 (:418-428).
   So **margin 0.5 u back from the hit, measured along the ray**; mobys only shorten the arm when the camera is already
   touching one (crates/enemies crossing the line do not pop it). If L < 0 the camera goes to the eye (:429-432).
4. Length state +0x124 (speed +0x128):
   - L < current -> current = L, speed = 0 (**instant pull-in**) (:434-436);
   - L > current -> speed = max(speed, 0); approach_eased to L with accel = decel 0.008, max 0.4 u/f PAL
     (**0.00556 u/f^2, 0.333 u/f NTSC**) (:437-442). From 1.8 to 6 u that is ~55 frames (~0.9 s), smooth S-curve.
   - flag 4 in +0xE0 instead eases to 0 (0.032/0.064/1.6 PAL; going first person) (:415-417). Who sets it: not traced.
5. camera = eye_orig + back * current. Final **0.2 u sphere sweep** (D_001D6534, names.tsv:1230) over the last 1 u,
   from eye_guarded + back*(current-1) to camera, mask 0x104; on a hit camera = sphere centre (:443-451).
6. Matrix times an extra euler (+0xD0, normally zero) (:452-453).
- **Direction is never changed by collision.** Yaw/pitch are pure player input; collision only shortens the boom.
  There is no stored camera position, so fast pitch cannot tunnel: the cast starts at the eye every frame.
- **Floor while looking up**: the boom swings under the eye, the ray hits the floor, the camera slides in along the view
  ray toward the head. It does not rise and the look-at (eye) does not move. Worked example: eye 2.0 up, pitch 60 deg up:
  floor hit at 2.31 u, L = 1.81, camera 0.43 u above the floor ~0.9 u behind the eye. The 60 deg up limit (45 in some
  states) is what bounds this. (Computed from the code, not observed.)
- **Not pushing the hero**: the camera is never a body. All tests skip the hero moby; the hero is never moved.

### 2b. Follow camera (mode 0) for comparison: still lakitu, with a lot of patching
camera_follow_update (func_L01_003EA6E8) -> platform ride, params, motion, then func_L01_003EA648
(L01_003E5700.c:1628-1645): arm_drag -> stick input -> manual input -> camera_obstacles -> camera_place -> camera_orient.
- arm_drag: arm = lerp(arm, last_camera - pivot, 0.85), length clamped to [distance - pull_in, distance]
  (func_L01_003E9908.c, names.tsv:1136). The camera's last position feeds back: this is the lakitu part. H
- camera_obstacles (func_L01_003E8360.c, M): arm sweep (func_L01_003E7FA8, L01_003E5700.c:799): 7 spheres stepping out
  along the arm, radius 0.35 + 0.1 per step (0.45..0.95), coll_sphere_push; hits push the arm **sideways** until the
  arm line clears the contact by 0.75 x (0.35 + 0.7 t) (camera_arm_push :713); a right-stick turn into a wall adds
  0.09 PAL (0.075 NTSC) pull-in (:870-878). Then camera_end_sphere (func_L01_003E77B0.c): a line from **last frame's
  camera** to the desired spot (hit + 0.005 normal), then up to 3 push-outs of a clearance sphere (0.35 + ..., "1.05 at
  rest") against world (coll_sphere_push), camera_collider_push and the collision file's camera groups; the first
  contact normal is classed by dot with a camera axis: > 0.75 / < -0.5 -> floor-or-ceiling (+-1), else wall (0) (:41-62).
- camera_set_pull_in (L01_003E5700.c:604-648): floor/ceiling contact or none: arm points at the pushed spot but **keeps
  its length** (so on a floor the camera slides along it and rises); wall: the arm shortens, pull_in = distance - length,
  arm at least 0.2; stick turning > 0.05 into it: pull-in timer 0x719 and 0.09 extra. camera_obstacles then floors
  the arm at 1.5 u (8360.c:100-103). H/M
- Pull-in recovery: pull_in = cosine_ease(0, pull_in, timer/1664) every frame (8360.c:124; names.tsv:620 says /2000 on
  NTSC), the timer (1667 PAL, ~2000 NTSC) held while blocked and counting down otherwise, so pull-in holds then eases out
  over a few seconds (my estimate: half gone ~2.5 s after clearing; depends on timer_tick's step, not confirmed). M
- camera_place (L01_003E5700.c:1479-1626): the actual arm chases the dragged one with angle springs 0.015/0.2 for yaw and
  tilt (:1570, :1582); camera = look point + height + arm; then up to 3 coll_sphere_push of **0.25** (mask 0x36)
  (:1598-1605), and if pushed, the look point and pivots are shifted by the same delta (:1606-1624) so the rig does not
  spring back into the wall. Same idea as rac2's eye push (rac2-follow-camera-tilt.md).
- Tilt: right stick past a 0.3 dead zone -> tilt target up to 40 deg, step 0.01/0.02 (L01_003E5700.c:1358-1373);
  camera_orient caps the view pitch at 70 deg (names.tsv:1137). Same numbers as rac2's follow camera.
- Blocked line of sight for 25 frames, or the camera > 30 u away -> camera_reset_follow (8360.c:137-160). M

### 2c. camera_collider_at (func_L01_003F7350) is not terrain collision
`rac3:nonmatching/functional/func_L01_003F7350.c`: a point-in-shape test against level-authored **camera collision
volumes**: a 64 x 64 grid of 16 u cells (D_L01_001DF840), each cell a list of shapes (cuboid 3, sphere 5, cylinder 6,
pill 7; camera_collider_test names.tsv:1186), bounding circle tested in xy (vec_length_xy) + margin. Returns the shape.
Used by camera_collider_push (names.tsv:1103, sphere push-out from such a shape) inside camera_end_sphere and by
point_in_camera_collider (names.tsv:1104) to skip arm-sweep and line-of-sight tests inside them. Only the follow camera
uses it; fps_cam_place does not (it gets camera geometry through sweep mask bit 0x100 instead, M). H for the function,
M for the mask reading. rac2 has no known equivalent; skip it.

## 3. Differences from rac2's follow camera that matter for feel
1. rac2 follow: goal orbit -> pushed out of geometry -> tilt re-derived from the pushed orbit -> second-order springs
   (0.015/0.2, ~0.5 s) on the eye -> eye push again; pitch rate capped at 0.0305 rad/tick. Collision fights the input and
   the eye has memory, so a fast commanded pitch can put it inside the floor before the push catches it.
   UYA lock-strafe: view = input yaw/pitch exactly; position = eye - forward * L, rebuilt each frame from the eye;
   collision touches only L. No tunnelling, no fighting.
2. Response: rate control with linear spin-up and instant stop, squared stick (UYA) vs the follow camera's springs.
3. Distance 6 behind an eye 2.0 up (UYA lock-strafe) vs the follow camera's 4.64 / height 2.0 / look height 1.5
   (camera_follow_init, names.tsv:1115). The lock-strafe view looks *through* a point above the head; no shoulder offset.
4. Pull-in: instant in, ~1 s eased out (UYA lock-strafe) vs push-out + slow cosine release (follow cameras).
5. Floors when looking up: lock-strafe pulls in toward the head; follow camera keeps length and rides up the floor.
6. Pitch window 70 down / 60 up vs rac2 follow's 40 deg tilt target / 70 deg view clamp.
7. Hero: in lock-strafe he turns to the camera yaw on hero_strafe_turn's spring (gain 0.042, damping 0.25, cap 0.32
   rad/f, names.tsv:1520) and moves relative to the camera; eye height ignores short jumps.

## 4. Distilled algorithm for a rac2 mod (NTSC numbers; per 60 Hz tick)
```
state: yaw, pitch (+down), yawSpd, pitchSpd, L = 6, Lspd = 0, eyeH = 2.0, eyeHspd = 0

input (stick, or mouse/gyro):
  sx = stick_x*|stick_x| ; sy = stick_y*|stick_y|          // or skip and add mouse deltas to yaw/pitch directly
  k  = sens (UYA: fov/1.11 * lerp(0.25,1.25,opt/64) ~ 1.03)
  tY = 0.02618*sx*k ; aY = 0.001939*|sx|*k               // |sx|>0.99: tY = +-0.10472*k, aY = 0.005818*k
  tP = 0.0349*sy*k  ; aP = 0.001454*|sy|*k
  yawSpd   = |tY| < |yawSpd|   ? tY : approach(yawSpd, tY, aY)
  pitchSpd = |tP| < |pitchSpd| ? tP : approach(pitchSpd, tP, aP)
  yaw = wrap(yaw + yawSpd) ; pitch = clamp(pitch + pitchSpd, -60deg, +70deg)
  // mouse/gyro: yaw += dx*s; pitch = clamp(pitch + dy*s, ...) - no smoothing needed, collision cannot tunnel.

eye:
  eyeH  = approach_eased(eyeH, eyeHspd, 2.0, acc 0.00444, dec 0.00444, max 0.533)
  eye   = hero.pos + up*eyeH  (+ fwd_flat * 0.45*max(pitch,0)/90deg)
  pivot = hero.pos + up*0.7
  g = eye ; if line(pivot -> eye, terrain, skip hero) hits: g = hit - dir(pivot->eye)*0.4   // stand-in for the 0.4 sweep
fwd  = (cos p cos y, cos p sin y, -sin p)   // p = pitch, +down
back = -fwd
boom:
  want = 6
  if line(g -> eye + back*6.5, terrain mask, skip hero) hits: want = |hit - eye| - 0.5
  if want < 0: cam = g ; goto out
  if want < L: L = want ; Lspd = 0
  elif want > L: Lspd = max(Lspd,0) ; L = approach_eased(L, Lspd, want, acc 0.00556, dec 0.00556, max 0.333)
  cam = eye + back*L
  backstop (UYA: 0.2 sphere sweep from g + back*(L-1) to cam): line(g + back*max(L-1,0) -> cam) hit -> cam = hit - back*0.2
          (or rac2's 0.25 point push at cam, without translating anything else)
out:
  write camera pos = cam, forward = fwd, rows from (yaw, pitch, roll 0); hero facing/strafe heading = yaw
```
approach(v, t, a): move v toward t by at most a.
approach_eased(v, spd, t, acc, dec, max) (names.tsv:625): spd moves by acc toward sign(t-v)*min(max, sqrt(2*dec*|t-v|));
v += spd, no overshoot (snap and zero spd when it would pass t).
Which rac2 fields to write each tick: see rac2-spring-arm.md (other agent) / rac2-aim-camera.md (rec+0x00 forward,
+0x30 eye). The point of the UYA design for that write set: never feed the game's pushed eye back into the orbit;
own yaw, pitch and L, and recompute the position from the eye every tick after the game's own update.

## 5. What the decomp does not settle
- Collision mask meanings (line masks 4 and 2; sweep masks 0x84 / 0x104 beyond bit 0x100 = camera groups, bit 0x80 =
  some extra list via func_L01_003FDCA0). Map rac2's terrain-only mask yourself.
- D_001D6530 = 0.4 and D_001D6534 = 0.2 come from names.tsv text; the data words are not in the repo (no asm/ROM).
- func_003D4D00 is named ball_damage but is used here and in camera_obstacles as "mobys within radius of a point"
  (returns a count). M.
- Who sets fps_cam flag 4 (boom collapse); hero state types 3 and 20 (45 deg up limit); D_L01_001DBD5C (char 2 length);
  the pad layer's deadzone; timer_tick_short's step (pull-in release timing); which camera axis camera_end_sphere's
  floor/ceiling test uses (sign of +-1 class).
- Feel in game: none of this was run.

## rac4 (Deadlocked) reference
the rac4 decompilation: src/ is mostly game/net/overlay matches, research/ps2recomp-pal
holds drafts and packing evidence; grep finds no camera names or camera code in src, nonmatching, research or docs
(the only hits are "campaign"/"camvtbl" in config). Nothing usable for this.
