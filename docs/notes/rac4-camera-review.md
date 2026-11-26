# Deadlocked (rac4) camera, and what it means for Dead Aim's Modern camera

Sources and method. The rac4 decomp (`dev/reference/rac4-decomp`) has ~10,000 matched C files, but **no
names**, and the camera's big functions are **not** in C: in the camera range only the small helpers are
matched (`config/us/map.tsv`; e.g. `src/overlays/shared/L01_00524EC0.c` = UYA camera_hero_view_init,
`L00_0058A778.c` = camera_aim2_spring, `src/game/0043A068.c` = the GameCamera FOV reset). Some files say which
UYA function they came from (`/* from rac3-decomp's func_... */`), which is how the camera was located.
`research/ps2recomp-pal` holds one unrelated draft. So I read the **retail code** instead: the USA disc's level 1
(Dreadzone Station) program, read with the decomp's own `tools/levels.py` and disassembled with rabbitizer,
with float and address annotation; L01 addresses map to the decomp's `func_` names.
gp = 0x22597F (`tools/audit_relocs.py:63`).

Addresses below are **US L01** (`.text` 0x3C7C80). The decomp name follows in brackets. All Deadlocked constants
are per 60 Hz frame (US build), so they drop straight into rac2. Confidence: **H** read in the code,
**M** read but the meaning inferred, **L** guess. UYA numbers are from `rac3-strafe-camera.md` (NTSC column).

Function map (all H for location, names are mine, by match with UYA's names in `rac3-decomp/config/names.tsv`):

| Deadlocked L01 | decomp name | UYA equivalent | size |
|---|---|---|---|
| 0x3C7B00 `lvl.camvtbl` | - | camera mode table | 0xA0 |
| 0x5617A8 | func_00457A58 | fps_cam_init | 684 |
| 0x5620A0 | func_L00_005C2100 | fps_cam_update | 3332 (UYA 2472) |
| 0x562E40 | func_00457EF0 | fps_cam_place (the boom) | 1892 (UYA 992) |
| 0x563710 | func_L00_005C3770 | fps_cam_find_target | 1500 |
| 0x562DA8 | func_L00_005C2E08 | fps_cam_target_side | 148 |
| 0x68BBA0 | func_L00_006CEE20 | hero_camera_update | 3592 |
| 0x68DF20 | func_00525250 | hero_eye_update | 1020 |
| 0x68E3B0 | func_005256E0 | hero_camera_set | 552 |
| 0x524EC0 / 0x524F58 / 0x524FA0 | L01_00524EC0 / L01_00524F58 / func_L01_00524FA0 | camera mode 0x20 init / update / check | |
| 0x5BDA40 | func_0048FB70 | pad_update (dead zone) | 2180 |
| 0x686BF8 | func_L00_006C9EE8 | hero_find_active_swingshot_node | 768 |
| 0x6868C8 | func_L00_006C9BB8 | hero_find_swingshot_node_in_front | |
| 0x673B78 | func_005157E0 | hero_score_target_by_heading | 780 |

Hero struct offsets in Deadlocked: fps_cam at hero+0x1970 (so fps yaw is hero+0x19B0, pitch +0x19D0, camera
position +0x19A0, boom length +0x1A94), hero_camera_mode +0x1AE0, eye height +0x1AFC, eye point +0x1950.

---

## 1. What Deadlocked does

### 1.1 The camera is UYA's lock-strafe camera, and it is the default (H)
- Camera modes (`lvl.camvtbl`, 5 words per mode: id, ?, init, update, check): 0 follow, 4 look, 5, 7 swing,
  0x13, 0x17 fixed, 0x20 hero view. Same set as UYA.
- The camera option table (core.data 0x1711D0, one word per player) is **1 (lock-strafe) for every player** in a
  new game. The menu calls it "Camera mode: Lock-strafe" (`~/Games/openRAC/deadlocked example.jpg`).
- So normal play is camera mode 0x20, which copies the hero's fps_cam each frame (0x524F58: `lq 0x230(cam)` ->
  update object +0x30, matrix from +0x200). Blend in 0.075 a frame (13 frames, 0x524F10), blend out 0.045 (22
  frames, 0x525020). Same as UYA.
- hero_camera_set(1) (0x68E3B0): offset (-6, 0, 0), boom length 6 (+0x1A94), eye height 2.0 (+0x1AFC), pitch
  limits +70 deg down / -70 deg up (+0x1A24 / +0x1A28). First person (2): eye 1.15, +-89 deg (+-70 with a global
  flag at .lit 0x21E878). Mode 0: eye 1.15, +-89.
- Movement is camera-relative through the fps camera's own yaw and matrix: hero_update (0x690F18, 18 KB)
  copies fps_cam's matrix (+0x1970) and yaw (+0x19B0) into hero +0x1790/+0x17A0 when the setting is not 0
  (0x691814-0x691894), else the follow camera's (+0x2CF0/+0x2CE8). The eye position is never used for the
  move direction. **H** for the copy, **M** that +0x1790/+0x17A0 is the move frame (read by the walk code).

### 1.2 Right stick (H unless marked)
- Pad (0x5BDA40): per axis, dead zone 48 of 127 (0x30), then (|raw-127| - 48) / 76, clamped to 1. Linear.
  Same as UYA's pad.
- fps_cam_update (0x5620A0) reads the right stick at pad +0x100 / +0x104, applies the invert tables
  (core.data 0x171E00 vertical, 0x171E78 horizontal) and **does not square it**. UYA squares it
  (`s = s*|s|`). I looked for the squaring between the stick read (0x562370) and the speed targets (0x5625E4):
  none. **This is the biggest feel difference from UYA.**
- Constants (fps_cam_init 0x5617A8, stored at 0x56191C-0x56198C):

  | | Deadlocked | UYA NTSC |
  |---|---|---|
  | yaw top speed (+0x4C), x stick | **0.05236 rad/f (3 deg/f, 180 deg/s)** | 0.02618 (90 deg/s) |
  | yaw ramp (+0x54), x \|stick\| | 0.001939 rad/f^2 | 0.001939 |
  | quick turn top (+0x48) | 0.10472 (360 deg/s) | 0.10472 |
  | quick turn ramp (+0x50) | 0.00349 | 0.005818 |
  | quick turn starts | \|stick\| > 0.99 for **2 frames**, target or not (0x562498-0x5624D4) | at once without a target, 8 frames with one |
  | pitch top (+0x68), x stick | 0.0349 (120 deg/s) | 0.0349 |
  | pitch ramp (+0x6C), x \|stick\| | 0.001454 | 0.001454 |

- Scale on all of them: (fov / 1.11) x lerp(0.25, 1.25, option / 64), option default 50, so 1.03
  (0x562184-0x5621F4; option table core.data 0x171EF0). In zoom states (hero state 1 or 0x1D, or hero bytes
  +0x2ECE / +0x2ED0, the same test that switches to first person) the scale is x0.4 more (0x5621EC-0x56225C).
- Integrator as UYA: a target smaller than the current speed is taken at once (instant stop), else the speed
  ramps by the ramp per frame. Yaw wraps, pitch clamps to the limits (0x562AA8-0x562B58).
- Ramp time at full stick: 0.0524 / 0.002 = 26 frames (0.43 s) to 180 deg/s; holding it fully over, the quick
  turn then takes it on to 360 deg/s at 0.0036 a frame (about 15 more frames).
- Effective curve. With the 37.8 % dead zone and a linear map, Deadlocked's speed fraction against raw stick
  deflection r is (r - 0.378) / 0.598. Our `stick_shape` is ((r - 0.12) / 0.88)^2. They are close:
  r 0.45: 0.12 vs 0.14; r 0.6: 0.37 vs 0.30; r 0.8: 0.71 vs 0.60. **So the shape is not the problem, the top
  speed is**: Deadlocked's full speed is twice UYA's, plus the 360 deg/s quick turn at full deflection. (M:
  arithmetic on H constants.)
- Turn assist toward a target (as UYA): a push of 0.85 or more toward the side the target is on keeps a virtual
  stick (+0x88) going for **7** frames (UYA 6) (0x5624F8-0x562514).

### 1.3 Pitch limits, FOV (H)
- Down limit +70 deg. Up limit eased each frame toward **-70 deg** (UYA -60), **-45** in hero state type 3,
  **+10** (must look at least 10 deg down) in state type 0x14 or state 0x7A; rate 2.0944 rad/s x dt
  (2 deg/frame) (hero_camera_update 0x68C24C-0x68C2D0).
- FOV 1.11 rad (63.6 deg) (0x534E80, func_0043A068). camera_fov_set (0x534F30, func_0043A118): mode 3 springs
  to a target with gain/damping (weapons and vehicles reset with 0.005 / 0.2). No speed or sprint FOV.
- The turn speed scales with fov / 1.11, so a zoom slows the look.

### 1.4 Eye point, vertical follow (H unless marked)
- hero_eye_update (0x68DF20): eye = hero position (+0xA0) + up x eye height. No horizontal smoothing at all:
  the camera is welded to the hero sideways and forward. Only the height is sprung.
- Height spring (approach_eased): ramp 32/3600 = 0.00889 u/f^2, top 64/60 = 1.067 u/f, x dt
  (0x68DF68-0x68DFA4). **Twice UYA's** (0.00444 / 0.533).
- Targets: 2.0 lock-strafe; 1.15 first person; 0.6 in states 4 / 0x15 with mode 2 or byte +0x2ECD (crouch, M);
  1.75 in state type 3.
- Jump hold: lock-strafe, state type 4 (jump), not states 0x83 / 0x1B, height above ground (+0x2CC)
  **< 3.5 u** (UYA 3.0), and not released (+0x1B04): the eye's target drops by the height gained, so the eye
  stays level through jumps (0x68E030-0x68E170). Over 3.5 u it lets go. **M** on the exact release rule (it
  also compares the velocity's horizontal and vertical parts).
- Looking down (pitch > 0): eye moves forward along the view's yaw by 0.45 u x pitch / 90 deg
  (0x68E248-0x68E2FC). Same as UYA.
- Water: in state type 0x19 or state 0x69, eye z >= water height (+0x2E4) + 0.75 (0x68E208-0x68E244). And
  after fps_cam_update, the **camera** z is kept >= a global plane (.bss 0x222BFC) + 0.3 (0x68C320-0x68C37C;
  M that the plane is the water).
- Pivot for the eye guard: hero position + up x 0.7 (0x68C2D4-0x68C300).

### 1.5 The boom, collision, easing (fps_cam_place 0x562E40; H unless marked)
1. **Eye guard only when the eye is low**: if eye height < 1.15 (crouch, Clank height) and eye != pivot,
   sweep a 0.4 sphere (func_00474818, mask 0x10004) from pivot to eye; on a hit the arm starts from the hit
   sphere's centre (0x562EC0-0x562F7C). With the normal 2.0 eye **there is no eye guard** (UYA always did one).
2. Offset length 0 (first person): camera = eye.
3. Flag 4 in +0xE0: boom collapses with approach_eased 0.0222 / 0.0444 / 1.333 (0x562FA0-0x562FD8).
4. Probe = eye + boom direction x (length + 0.5). CollLine (func_00441088) from the (guarded) eye, mask 4, skip
   the owner. On a hit: if the hit is a moby that is not class 0xD54, 0x268B or 0x2667 **and** a 0.51 query
   round the current camera (func_L00_005A7230) finds a moby, L = |hit - eye| - 0.5. Otherwise re-cast with
   mask 2 (class 0x2667 ignored there too); a hit gives L = |hit - eye| - 0.5 (0x563068-0x5631B0).
5. **New in Deadlocked**: a third line test, func_00444C98 (a CollLine variant on another collision set, the
   pointer at .bss 0x222BE4), same rule L = |hit - eye| - 0.5 (0x5631CC-0x56322C). M: it is probably the
   camera-only collision or water.
6. L < 0: camera = guarded eye (no minimum length; 0x563230-0x563248).
7. L < current: current = L, speed 0 (**instant in**). L > current: speed = max(speed, 0), approach_eased to L
   with ramp 0.005556 and top 0.3333 u/f (**eased out, S-curve**, same as UYA NTSC) (0x56324C-0x5632B0).
   There is no hysteresis, no hold, no "clear for N frames": that is the whole length logic.
8. Camera = eye + direction x current (0x5632B4-0x5633A0).
9. The final 0.2 sphere sweep over the last 1 u (mask 0x20004) runs **only** in hero state type 0xD with a
   flag (0x5633AC-0x563408). UYA always ran it.
10. Direction is never changed by collision. Yaw and pitch are input only.

### 1.6 Close quarters (H for the code, M for the picture)
- The camera slides along the view ray into the eye point. It never rises, never swings aside, never
  lowers the hang point. Minimum length 0 (the camera is then at the eye).
- The eye is 2.0 above the feet, above Ratchet's head. Looking level from there, he is below the frame;
  looking down, the eye has moved forward up to 0.35 u (70 deg x 0.45/90), so the lens is ahead of his head,
  not over it. The view orientation never changes with the length.
- No fade of the hero by camera distance in hero_camera_update, fps_cam_place or hero_eye_update. The only hero
  opacity writer I found near the hero code (0x684EF0, writes hero moby +0x23 from his speed, 64 x
  (speed/0.1333)^2) is gated by multiplayer settings flags: a stealth/cloak effect, not a camera fade. First
  person hides the hero and his item by moby flags +0x34 |= 0x41 (0x68C83C-0x68C884). **M** that Deadlocked
  has no camera fade at all (I did not search every draw path).
- Recovery: the S-curve ease out, 0.00556 u/f^2 up to 0.333 u/f: from 0 to 6 u in about 52 frames (0.9 s).
- Movement uses the camera yaw (1.1), so a squeeze cannot turn his run.

### 1.7 Auto-follow, recentring, lag (H)
- **None.** fps_cam_update changes yaw only from: the stick, the target turn assist (1.2), target tracking
  (1.8), and the frame of a moving basis (+0xE8/+0xEC/+0xF0: the facing is kept in a platform's or gravity
  basis's frame, so a turning platform carries the view; 0x5622C0-0x562358 and 0x562B5C-0x562C18). In swing
  states 0x2B/0x2C the basis carry is switched off (flag 2, func_L00_005C1C90 0x561C30).
- No lag and no lead horizontally: the camera is rebuilt from the hero's position every frame.

### 1.8 Aim, aim assist, lock-on (H unless marked)
- The crosshair point is fps_cam +0x110: the end or hit of a ray from the camera along the view, length
  +0x120 = **45 u** (UYA 30) (fps_cam_find_target 0x563710).
- Target for assist: cone +0x7C = **20 deg** (UYA 15) x fov/1.11, range 45, LOS test from 1.15 above
  (func_L00_005C1DA8 0x561D48, 12 tries). Weight +0xB0.
- With a target: top speeds cut by (1 - 0.5 w) yaw and (1 - 0.6 w) pitch (0x56263C-0x562688).
- Tracking, only while **any stick is moved** (left or right; 0x5627A4-0x5627F8): the camera follows the
  target's motion relative to the hero, yaw gain 0.666 x w, the yaw nudge capped at +-0.02618 rad/f
  (+-0.01309 when the global .lit 0x21E824 == 1, M: a multiplayer or difficulty switch), pitch gain 0.85 x w
  faded out between 12 u and 4 u of distance ((d - 4)/8). Halved with flag 0x20 (0x5629A8-0x562AA4).
- No shoulder offset. No separate aim camera in lock-strafe: zoom weapons switch to first person (mode 2,
  0x68BD60-0x68BDE8) and slow the turn x0.4.

### 1.9 Mode blends and hand-back (M)
- Hero view mode 0x20: in 0.075/frame, out 0.045/frame (H). Its check (0x524FA0) leaves for requests 4 (look)
  and 0x51 (targeting) and some game-mode globals.
- Vehicles and turrets reuse fps_cam_update with their own fps_cam (callers 0x4D6A58, 0x4E90D0), and reset the
  FOV with springs 0.005 / 0.2 on exit (0x4D6B80, 0x4E93B4).
- The fps_cam keeps its yaw/pitch across modes, so returning to mode 0x20 lands where the player left it.

### 1.10 Deadlocked versus UYA, in one list
1. Linear stick, not squared; yaw top 180 deg/s (UYA 90). Quick turn after 2 frames at full, always.
2. Up limit 70 deg (UYA 60).
3. Eye height spring twice as fast; jump hold to 3.5 u (UYA 3.0).
4. Boom: one more line test (func_00444C98), more moby classes passed through, eye guard only for a low eye,
   final 0.2 sweep only in one state.
5. Aim assist: 20 deg cone, 45 u range; pitch tracking faded close in.
6. Turn x0.4 in zoom states.
7. Camera kept 0.3 above a global plane (water, M).

---

## 2. Recommendations, ranked

### R1. Make the view look along the boom, whatever its length (fixes "hat", the corner push, part of the spins)
- **Player sees**: backed into a wall, the view stays level and pointed where he was looking; the camera just
  slides in. No looking down at his scalp. His run direction no longer turns in a squeeze.
- **Deadlocked**: camera = eye - forward x L; forward is input only (1.5, 1.6). Movement uses that yaw (1.1).
- **Why ours goes wrong (M, from the code plus rac2 notes)**: `boom_tick` writes only positions (orbits, eyes,
  camera eye `camera + 0x30`, mod.cpp:2520-2524). The game's follow_camera_build_view then aims the view from
  the eye at **its** look point (P + U x look height, `rac2-follow-camera-tilt.md` "build_view",
  `rac2-camera-situations-2.md`:145). Our hang point is a different point: A (+0xC0, the smoothed pivot) + U x h,
  then lowered by `anchor_room` and moved by `anchor_push` (mod.cpp:2351-2399). At L = 3-5 u the difference is a
  few degrees. At L = 0.25 a 0.15 u height difference is 31 deg of pitch (the "hat") and a 0.3 u sideways push
  is 50 deg of yaw. rac2's movement reads the camera's yaw block+0x158 (`rac2-camera-situations-2.md`:296-302),
  so that yaw swing turns his run: the "push" along the wall.
- **Change**: in `boom_tick`, give the game a look point on the boom line. Options, cheapest first:
  1. Write the game's look-at target (the point build_view aims at) to `anchor - dir x K` each tick
     (K = 2-4 u, any value works as long as it is on the line), or set the look height equal to the hang
     height and apply the same `anchor_push` to it. Need to confirm which field build_view reads (st+0xA0 or
     P + st+0x110) and that it is not reset inside the tick.
  2. Or overwrite the camera rows (forward = -dir) after the tick and before the draw (SDK camera_set, api
     1.12, `rac2-spring-arm.md` last line), and set the yaw rac2's movement reads to atan2 of -dir.
- Constants: none new. Check with a log: at L < 1, the camera forward's pitch and yaw vs `s.boom_lift` and
  `s.boom_yaw` (they should agree within 1 deg).
- **Effort**: small to medium (one field to find and write, or one SDK call). **Risk**: medium, the look point
  may also feed the game's own LOS and probe; with st+0x23E = 1 those are off.

### R2. Hang the boom from the hero, not from the smoothed pivot (camera welded sideways; fewer wall problems)
- **Player sees**: Ratchet stays fixed in the frame while running and turning; no camera catching up after a
  stop; the boom origin never cuts a corner into a wall (likely part of "stuck in the curved arena wall").
- **Deadlocked**: eye = hero position + up x eye height, height sprung only (1.4). No horizontal lag.
- **Ours**: anchor = st+0xC0 (A) + U x h (mod.cpp:2351). A follows P through the game's pivot springs
  (0.015 / 0.2, `rac2-spring-arm.md` +0xC0). For a spring v += k(x_t - x) - c v the steady lag at constant speed
  V is (c / k) V = 13 V, so about 1-2 u at a run (M: assumes those are the horizontal pivot gains; measure it).
- **Change**: anchor = hero position (kHeroPosition, or the game's P st+0x60) + U x h_eye, where h_eye is our
  own approach_eased height (ramp 0.0089, top 1.07 u/tick, Deadlocked's) toward 1.64 (or the game's h). Keep
  B (st+0xB0) only as the ceiling-safe start of the hang sweep. Add Deadlocked's jump hold: in jumps, while
  height above ground < 3.5, target h_eye = h - height gained (eye level), released beyond 3.5 or on landing.
  Then `anchor_room` and `anchor_push` can probably go (section 3).
- **Effort**: medium. **Risk**: medium; on moving platforms use the hero's position (he is carried), and keep
  the planetoid frame (`camera_up`) for the up vector.

### R3. Stick model: Deadlocked's speeds (feel; also helps "it turns slowly")
- **Player sees**: a much quicker camera at partial stick, and a fast spin-around at full deflection.
- **Deadlocked**: 1.2. Top yaw 0.05236 rad/tick, quick turn 0.10472 rad/tick when |s| > 0.99 for 2 ticks,
  ramp 0.00349 to it; pitch 0.0349.
- **Change** in `modern_stick` (mod.cpp:3179): keep `stick_shape` (its curve matches Deadlocked's dead zone
  plus linear map closely, 1.2) and `stick_rate`. Double the default top: `kStickYaw = 0.05236f` (keep the
  ramp `kStickYawAccel = 0.001939f`, so `kStickYawRamp` becomes 0.037), and scale the game's option so its
  default lands at 0.05236. Add the quick turn: a counter of ticks with |stick_x| raw > 0.97 (our report
  scale; Deadlocked's 0.99 is after a 37.8 % dead zone); from the 2nd tick the target is
  sign x 0.10472 x scale with ramp 0.00349 x scale; leave it when the stick drops below.
- Pitch stays 0.0349 (do not derive it as 4/3 of the yaw any more: Deadlocked's pitch did not double).
- **Effort**: small. **Risk**: low (it is a tuning change); offer it behind the existing speed option.

### R4. One length rule: instant in, Deadlocked's S-curve out (fixes residual pumping; less code)
- **Player sees**: the camera snaps in when something comes behind it and glides out; no half-second
  "stuck short" after a pillar passes.
- **Deadlocked**: 1.5 step 7: L < current snaps, L > current eases (0.00556, 0.333). Nothing else.
- **Ours**: 30-tick minimum ring (`kWantHold`, mod.cpp:2484-2490), 6 clear ticks (`kBoomClearTicks`), a slack
  of 0.15 (`kBoomSlack`), a 0.08 s in-ease unless "behind a face" (`kBoomInSeconds`, the extra centre line
  for `boom_inside`, mod.cpp:2409-2423, 2493-2497).
- **Change**: in = snap always. Out = the existing approach_eased branch (mod.cpp:2499-2509) with no slack and
  no clear count. Keep a short hold only if testing shows flicker: a 6-8 tick minimum instead of 30. The
  in-ease exists because the 5 side lines flickered; with the sphere sweep the want is stable (the sweep was
  the jitter fix).
- **Effort**: small. **Risk**: low-medium; re-test the old jitter spots.

### R5. Mobys: Deadlocked's rule (pass through, unless the camera already sits in one)
- **Player sees**: crates and enemies crossing the boom do not yank the camera; the camera never ends up
  inside a crate.
- **Deadlocked**: a moby hit counts only if a 0.51 query round the **current** camera finds a moby
  (1.5 step 4); otherwise the line is re-cast without mobys.
- **Change**: replace the front/back moby block (mod.cpp:2440-2482, `kMobyRoom`, `moby_in`/`moby_side`) by: if
  the level-only want is longer than the moby line hit, run the moby query (CollSphere's moby query, CollLine +
  0x2C38, already wrapped) at the current camera position with r 0.5; if it touches the same moby, use the moby
  hit - 0.5.
- **Effort**: small-medium. **Risk**: medium (the current code was written for real cases: the ship overhead,
  crates). Keep the "ship overhead" case: Deadlocked passes it too, since the camera is not inside it.

### R6. Close quarters: what to do (answers the "hat" question)
- Do R1 first. With the view along the boom, the "hat" goes away by itself.
- Let the minimum go to 0 (Deadlocked has none): `kBoomMin` 0.25 -> 0. The sphere margin keeps the eye off
  the wall.
- Add Deadlocked's look-down shift: eye forward by 0.45 x max(pitch_down, 0) / 90 deg along the flat view
  (in `boom_tick`, on the anchor before the arm). The lens then sits ahead of his head when looking down.
- Keep the fade as a safety net (Deadlocked's eye at 2.0 does not need one; our 1.64 is closer to his head):
  start nearer, e.g. `kFadeFar` 1.0, `kFadeNear` 0.4, and fade only while the camera is within ~0.6 u of his
  head point, not by arm length alone.
- **The candidate idea (lower the hang point to ~1.3 below 1 u of arm, eye in his head, fully faded)**:
  Deadlocked does the opposite: the eye stays high (2.0) and the camera goes to it. Lowering into the head
  needs a full fade (with the gun and Clank faded too), shows the near plane cutting his model, and moves the
  view down as you back up, which reads as motion. It also changes where the crosshair ray starts
  (`aim_frame`). I would not do it. If anything, raise the hang point toward 1.9-2.0 when the arm is short and
  the ceiling allows (the hang sweep already tests the room), so the camera is above the ears, as in
  Deadlocked.
- **Effort**: small after R1. **Risk**: low.

### R7. Yaw ownership without the yaw-jump heuristic (spins and flips in corridors)
- **Player sees**: no 180 deg flips when the stock camera "rescues" itself.
- **Deadlocked**: the yaw is never read back from anything (1.7).
- **Ours**: each tick `game_yaw` is compared with what we wrote; a jump > 0.5 rad is adopted unless
  `last_want < kStuckLength` (mod.cpp:2319-2334). That guard is a patch for the game's own reset.
- **Change**: adopt the game's yaw only on events: a hero teleport (position jump > ~4 u in a tick), a level
  load, `fp_grace`, leaving a scripted/override camera. Never from the orbit alone.
- **Effort**: small. **Risk**: low-medium (doors that re-aim the camera; check the doors listed in
  `rac2-camera-situations-2.md`).

### R8. Up limit 70 deg and pitch top as Deadlocked (small)
- `kBoomLowest = -60 deg` -> -70 deg (mod.cpp:1892). Floors are already handled by the length (Deadlocked
  pulls in along the view, the same).
- **Effort**: trivial. **Risk**: low.

### R9. Camera friction near targets (optional, very "modern")
- **Player sees**: the view slows a little and drifts with an enemy while he moves.
- **Deadlocked**: 1.8 (20 deg, 45 u, x(1 - 0.5 w) yaw, x(1 - 0.6 w) pitch; tracking only while a stick moves).
- **Change**: in `modern_stick`, find the best enemy in a 20 deg cone (rac2's own target finder, as the
  Hypnomatic code does), weight w = 1 on the crosshair ray else (cos off - cos 20)/(1 - cos 20), and scale
  `top` by (1 - 0.5 w). Leave the tracking out at first.
- **Effort**: medium. **Risk**: medium (taste; make it an option).

### R10. Aim ray 45 u (trivial)
- `kAimReach` 30 -> 45 (mod.cpp:2683), as Deadlocked's +0x120. Check that rac2's guns accept far points
  (the bend limit still applies).

---

## 3. Unnecessary or over-complicated next to Deadlocked
1. **Hysteresis stack** (R4): `kWantHold` ring of 30, `kBoomClearTicks`, `kBoomSlack`, `kBoomInSeconds`,
   `boom_inside` and its extra CollLine. Deadlocked: snap in, S-curve out, nothing else.
2. **Five-line fallback** (mod.cpp:2425-2436, `kBoomRadius`): only runs when `sphere_sweep` fails. If
   `coll_for_level` works on every level, delete it; if not, one centre line (Deadlocked uses exactly one line
   and a 0.5 margin) is enough as the fallback.
3. **Three hang-point mechanisms**: `anchor_room` (sweep, with a level_line fallback), `anchor_push`
   (push_out + LOS), both eased. Deadlocked: one 0.4 sweep from pivot to eye, and only for a low eye. With R2
   (anchor from the hero) keep one 0.4 sweep from a low pivot (hero + 0.7) to the hang point and drop the push.
4. **Moby front/back sides** (R5): ~40 lines; Deadlocked's rule is one query.
5. **Yaw-jump adoption** with `kStuckLength` (R7).
6. **Pitch top derived as 4/3 of yaw** (`kStickPitchToYaw`): Deadlocked keeps pitch at 120 deg/s while yaw
   doubled; use two constants.
7. Keep (needed for rac2, not over-complicated): the writes that quiet the game's camera (shortfall, timer,
   st+0x23E, tilt override), `camera_overridden`, `hold_direction` off outside `mod_camera_here`.

---

## 4. Swingshot targeting

### What Deadlocked does (H for the code; it is rac2's finder, almost line for line)
- hero_find_active_swingshot_node (0x686BF8) and rac2's (`rac2-decomp src/levels/shared/L00_002C3E18.c:29-81`)
  are the same algorithm:
  - In swing (state group 0xD) it returns the cached result: the target is sticky during the swing.
  - Nodes come from a registry of up to 32 (.bss 0x247F40) instead of a class scan. Skipped if moby flags +0x34
    bit 1 set, bit 0x1000 clear, or vars+0 != 0.
  - Range 30, or the node's own (vars+0x20); xy distance < 4 rejected; a node in its exclusion cuboid
    (vars+0x1C) rejected.
  - Cone: **30 deg horizontal** off a heading, **no vertical limit** (vlimit -1); in hero states 1 / 0x1D
    (and, new in Deadlocked, while hero byte +0x2ECE is set) **5 deg in both** axes, measured **from the
    camera's position, yaw and pitch** instead (hero +0x2CD0 / +0x2CE8 / +0x2CE4) (scorer 0x673B78; rac2:
    `L00_002AE388-range.c:18-45`).
  - Score = distance x (1 + angle off) (+ the vertical angle in the camera case); +10 if the node's byte +0x31
    is 0 (meaning unknown, L: maybe "seen last frame"). Lowest score wins.
  - Then CollLine from **hero position + 0.5 z** to the node, mask 2: ready only if clear.
- The heading is hero +0xB8, his facing (M: +0xA0 is his position, +0xB0 his rotation). In Deadlocked's default
  lock-strafe, Ratchet faces the camera yaw, so in practice the 30 deg cone follows the camera (M, from UYA's
  lock-strafe; I did not trace Deadlocked's strafe turn).
- The "node in front" finder (0x6868C8, rac2's hero_find_swingshot_node_in_front): 45 / 45 deg, **85 / 85
  deg** in its special state (rac2 70 / 65), 5 deg in states 1 / 0x1D, range 16.
- Not found in Deadlocked: any widening, aim assist or camera turn toward the node; any pre-press turning of
  the hero (not traced, L). Ground versus air: the finder itself does not check it.

### Why ours feels like it needs precise aim (M)
- rac2's finder switches to **5 deg from the camera** in hero states 1 and 29 (29 = our `kAimState`, the aim
  state; `L00_002AE388-range.c:31-37`). If the press happens while Ratchet is in the aim state (Aim held, or the
  strafe/aim the mod drives), the cone is 5 deg: "look right at the node".
- Otherwise the cone is 30 deg off his **facing** (+0x98), which follows the camera only when lock-strafe has
  turned him (with a weapon, `rac2-auto-gadgets.md`:201). Standing after running another way, the camera can
  point at a node he does not face.

### Recommendation for preview and press
1. **Preview**: choose the node ourselves the Deadlocked way, but by the **camera** yaw (Deadlocked's facing =
   camera): horizontal off-angle h from the camera's flat forward, from the hero position; accept h <= 30 deg
   (35 if testing wants a little slack, not more: the score already prefers the centred node), no vertical
   limit, range vars+0x20 or 30, xy >= 4, skip flags as the game, score d x (1 + h) + 10 if byte +0x31 == 0,
   and the exclusion cuboid. LOS **from hero + 0.5 z, mask 2**, exactly as the game's: case 13 re-runs the
   game's own finder with that origin, so a higher origin in the preview would show nodes the press then
   refuses. (The player says standing works; LOS is not the issue.)
2. **Press tick**: before the tick that sees Circle, write his heading +0x98 = atan2 to the chosen node (in
   the camera-up frame on planetoids), and make sure he is not in state 1 or 29 (else the 5 deg camera rule
   applies: either delay the press until he leaves it, or also turn the camera at the node). Then require the
   game's own +0xAA8 (or +0xADE / +0xB14) to read 1 before letting Circle through; otherwise retry next tick.
   M: the hero update may rewrite +0x98 from the stick before case 13 runs; the +0xAA8 check catches that.
3. The idea as proposed: camera selection yes; a wider cone only modestly (30-35 deg); a higher LOS origin no
   (it disagrees with the game's own check); turning the heading on the press tick yes.

---

## 5. Open questions
1. Which field rac2's build_view aims at (st+0xA0, or P + U x st+0x110), whether it is rewritten inside the tick,
   and whether block+0x158 (the yaw movement reads) is built from it. Decides R1's option 1 vs 2.
2. The horizontal gains of rac2's pivot springs (st+0x13C/+0x140) and the real lag of A at a run (R2): log
   |A - P| while running.
3. Deadlocked's func_00444C98 collision set (.bss 0x222BE4): camera-only geometry or water? Only matters if rac2
   has camera-only faces we should also test.
4. Deadlocked's jump-hold release rule (the velocity comparison at 0x68E084-0x68E0D0) in detail.
5. Whether Deadlocked fades the player anywhere in the draw code (I found none in the camera path).
6. Deadlocked's strafe turn: does Ratchet face the camera yaw at all times in lock-strafe, or only when moving
   or firing? (Affects how literally "facing = camera" holds for the swingshot cone.)
7. What moby byte +0x31 is (the swingshot score's +10).
8. rac2's camera rotation option values (g_hero_linked_object_values, `L00_00426340-g8.c:111-115`), to map the
   default option to Deadlocked's 0.05236 in R3.
9. Nothing here was run in the game.
