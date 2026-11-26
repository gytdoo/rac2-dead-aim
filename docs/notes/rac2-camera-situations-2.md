# rac2 camera situations 2: Grelbin leviathan, Joba arena wall, tight spaces, Clank and the Glider

Sources:
- rac2-decomp at `dev/reference/rac2-decomp`. Paths are relative to its `src/`.
- A rabbitizer disassembly of the USA v2.00 disc (ntsc2): levels 0, 3, 8, 11, 14 and 19, read with the
  decomp's `tools/levels.py`.
- The mod, `repos/rac2-dead-aim/src/mod.cpp`: boom_tick, mod_camera_here, follow_live, camera_up,
  frame_about and find_camera. The file was being edited while I read it, so search by name.
- Notes `rac2-spring-arm.md`, `rac2-special-situations.md` (3a, 3b), `rac2-sphere-collision.md` and
  `rac2-follow-camera-tilt.md`.
- Nothing was run in game.

Confidence: **H** read in C or in v2 disassembly. **M** inferred from code. **L** guess.

Conventions:
- "v2" means the ntsc2 address in level 0 (L00). Use `relocate_level(0, addr)` exactly as the mod does for its
  camera slot. I checked that the deltas agree in L08, L11, L14 and L19.
- st = follow state = *(rec+0x70).
- block = camera block g_scalar_transition_state: v2 L00 0x1B8C00 (the mod's slot is block+0x190).
- U = block+0x1C0 (the mod's `camera_up`, slot+0x30). Raw up = block+0x1D0.

---

## 0. Answers in one table

| # | what the game does | detect from memory | what the mod should do |
|---|---|---|---|
| 1 Grelbin leviathan | A camera **override slot** (priority 5) while a leviathan attacks: distance 10, height 0.1, tilt input 0. So the stock camera goes far, low and level. Still the follow camera, class 0. | `sel = *(s32*)R(0x1B1804)` >= 0; slot = R(0x1BB970) + sel*0x40. Here slot+0x00 = 10, +0x04 = 0.1, +0x0C = 0. | Stand aside while the selected slot forces a tilt (+0x0C >= 0), sets steer/lock flags, or asks for height < ~1 or distance > 6. Or honour it: take the lift from the game. Do not hang the boom from h = 0.1. |
| 2 Joba arena (Arachnoid) | The arena's override in states 8/9: distance 8, height 2, look height 2 for the Arachnoid. Steers toward the arena centre in the boss-less wave. **Cylindrical gravity** once Ratchet is above z = 480: his up becomes horizontal, toward the arena's vertical axis. | `*(u32*)0x1A8FF0 != 0 && *(u32*)0x1A8FF4 != 0` (same address in every level and both discs). Or U.z < 0.95. Or the override slot as in 1. | Stand the boom aside under custom gravity. If you keep it: sweep with the point mask, not the line mask; add the game's probe-mask clearance; use a larger minimum length. |
| 3 tight spaces | Goal length is never under **1.5**. The eye follows the goal by springs and gets a 0.25 floor/ceiling push. The hero is never faded by distance. No camera code moves the hero: the stick is camera-relative every tick. Re-aims hit the goal (up to ~0.5 rad/tick in corridors), not the eye. | n/a | kBoomMin 0.3 lets the eye into Ratchet's head and ears. Use ~1.0-1.5, and raise the lift rather than shortening below it. A > 0.5 rad jump of post-tick st+0x170 is always a snap, never a wall re-aim. |
| 4 Clank / Glider | Clank: follow camera with distance 3, height 1, look height 1 and sweep scale 0.6. Prison zones use overrides of distance 2, height 3; Endako uses class-5 trigger poses. Glider: hero state 0x31 with +0x1830 = glider. Follow camera, with distance 4 (k 1) or 7 (override), stiff pivots and an **auto-steer behind the glider 2 s after the right stick was last moved** (up to 6 deg/tick). | form +0x22B4 == 1; vehicle +0x1830, class 0x905 (Glider); state 0x31. | The mod already stands aside (form/vehicle). Anything that turns the orbit in the glider must reset slot+0x30 = slot+0x32 each tick it turns, or the game steers it back. |

---

## 1. The camera override slots: the general "preset" mechanism (H)

The game has **32 slots of 0x40 bytes**:
- Table: g_cam_l00_override_slots, v2 **0x1BB970**, v1 0x1BB9F0.
- Selected slot: s32 g_cam_override_selected_slot, v2 **0x1B1804**, v1 0x1B1884.
- Both are level data, so read them with `relocate_level(0, ...)`.
- Per-level v2 values I checked: L08 0x1BC730/0x1B23C4, L11 0x1BD570/0x1B3004, L14 0x1BC0B0/0x1B1F04,
  L19 0x1BC5F0/0x1B2344.

Two functions manage the slots:
- `cam_alloc_override_slot(kind)` (`levels/shared/L00_002D6FD8.c:87-118`): sets every float to -1, priority = kind,
  angle = 6, reset timer = 120, active = 1.
- `cam_end_override` (`L00_002D7140.c:13-17`): clears active. If +0x10 >= 0, it eases the FOV back to 1.1243.

`cam_update_overrides` (`levels/shared/L00_002D71A0-g8.c:44-111`; v2 0x2D7568) runs **first thing in the follow camera's
tick**: `mode = cam_update_overrides()` (`us2/levels/shared/L00_004239D0-main-g8.c:152`).
1. It picks the active slot with the highest priority. On a tie the lower index wins.
2. It stores that index in the selected global, or -1 when no slot is active.
3. It calls these setters every tick. All of them return at once unless rec+0x86 == 0, so the follow camera stays
   class 0. Setter addresses are from the v2 L00 disassembly.

| slot field | meaning | setter → what it writes (all into st) |
|---|---|---|
| +0x00 f32 | follow distance target (>= 0) | follow_camera_set_distance (0x425F18): st+0x1A0 = d, st+0x1A8 = k 0.001, s16 st+0x19C = 1. **d < 4.64 or > 6.0 also calls cam_set_snap_flag** (s16 st+0x246 = 1; no reader found, L) |
| +0x04 f32 | height h target | follow_camera_set_height (0x425F60): st+0x1AC = h, st+0x1B4 = k 0.001, s16 st+0x19E = 1 |
| +0x08 f32 | look-at height above P (st+0x110) | cam_set_aim_offset_target (0x425F98): st+0x114 = v, st+0x118 = 0.005, s16 st+0x13A = 1 |
| +0x0C f32 | **tilt input, radians** | cam_set_tuning_value (0x426030): **st+0x1E4 = v**. This is the same field as the mod's tilt override |
| +0x10 f32 | FOV, radians | cam_set_value_target |
| +0x14 f32 | extra | gp float v2 0x1A8900 |
| +0x18 u8 | "flag24" | snap + st+0x10 = 0 (no lazy follow) + lock (st+0x136 = 1: the look-at goes to the smoothed pivot) + pivot speeds |
| +0x19 u8 | "flag25" | lock + pivot speeds |
| +0x1A s16 | input flags | **st+0x1D0 \|= 3**: masks the real right stick for yaw and tilt |
| +0x1C u8 / +0x1D u8 | priority / active | |
| +0x1E u8 | steer mode | each tick cam_set_strength_and_steer(+0x20 dir, +0x34 deg) |
| +0x1F u8 | timed steer | the same steer, but only after s16 +0x30 counts down from +0x32 (120). The timer resets whenever \|g_pad_state+0x100\| or \|+0x104\| > 0.01 (the right stick as the game reads it, pad 0x138280/0x138284) |
| +0x20 vec4 | steer direction | |
| +0x34 s32 | steer rate in degrees (6) | goes to st+0x1D4, the maximum yaw step per tick. The steer writes st+0x1DC, which is used while the stick reads 0 |
| +0x38 s32 | expiry ticks (0 = never) | |

How the setters' values behave:
- weapon_reset_view_state clears st+0x19C/0x1A0/0x19E/0x1AC/0x13A/0x114 at the **end** of each tick
  (`levels/shared/L00_0041ABB8-g8.c:196-219`; view = st+0x60, motion = st+0x160). So a mod that runs after the tick
  sees only the eased results:
  - st+0x18C D
  - st+0x190 h
  - st+0x110 look height
- The targets themselves are gone by then. The slot table and the selected index are what stay readable.
- The mod's tilt write st+0x1E4 is **overwritten at the start of the next tick** whenever the selected slot has
  +0x0C >= 0.

Users of the slots found in C (all priority 5 unless marked):

| place | what it sets |
|---|---|
| leviathan (L19, §2) | distance 10, height 0.1, tilt 0 |
| Joba arena guide (§3) | distance/height/look height per boss |
| Joba path platform (`levels/11_joba/L11_00415E70.c:49-62`) | distance 10, input flags (stick masked), flag24, flag60, steer along the platform |
| Tabora/Grelbin/Museum target tracker (`levels/shared/L08_003D3928-g8.c:121-170`) | Glider distance 7 + timed steer; other vehicles distance 6, look 0, tilt 0.1; on foot, a tracked class-2456 object gives distance 7 + timed steer |
| Grelbin carried marker (`levels/shared/L19_00422A98-g8.c:48-71`) | the same vehicle part |
| Aranos-prison camera zones (L14 v2 0x428430) | cuboids: distance 2, height 3 |
| L01/L08/L09 trigger_area_effect_update (`levels/shared/L01_003D4270-unsplit.c:76-110`) | level-authored zones: every field, priority from the level |
| others | Aranos lift, Oozla, Maktar arena, Endako boss and record, Siberius, Boldan, Smolg lifts, Damosel switch, Snivelak, Yeedil (`grep cam_alloc_override_slot`) |

Detection (M on relocation; it follows the mod's own pattern):
```cpp
const bool v2 = mod.version() == "ntsc2";
const gaddr selp = mod.relocate_level(0, v2 ? 0x1B1804 : 0x1B1884);
const gaddr tab  = mod.relocate_level(0, v2 ? 0x1BB970 : 0x1BB9F0);
const int sel = memory.read<std::int32_t>(selp);
if (sel >= 0 && sel < 32) {
    const gaddr o = tab + 0x40 * sel;
    float dist = memory.read<float>(o), height = memory.read<float>(o + 4), look = memory.read<float>(o + 8);
    float tilt = memory.read<float>(o + 0xC);                 // >= 0: the game forces st+0x1E4 each tick
    bool steer = memory.read<std::uint8_t>(o + 0x1E) || memory.read<std::uint8_t>(o + 0x1F);
    bool lock  = memory.read<std::uint8_t>(o + 0x18) || memory.read<std::uint8_t>(o + 0x19);
    bool stick_masked = memory.read<std::int16_t>(o + 0x1A) != 0;
}
```
The selection is made at the start of the tick, so it is current in on_frame.

---

## 2. Grelbin (19): the Arctic Leviathan (class 0xF57)

What the boss code does (H): `levels/19_grelbin/L19_004160A0-g8.c`, `leviathan_update`. In v2 it is
`us2/levels/19_grelbin/L19_004173C0-sw.c`, the same C.

- **AI state 2 (attacking), hero state != 0x31** (`:143-150`): it allocates a slot of kind 5 and sets
  **+0x00 = 10.0, +0x04 = 0.1, +0x0C = 0** (a word store of 0, which reads as 0.0f). Nothing else is set: no
  look-at, no FOV, no steer, no flags.
- **End** (`:101-106`): the slot is released when the AI state is not 2 or 3, when the hero is in state 0x31, when
  the leviathan's health is <= 0, or when it dies or is spawned out.
- The leviathan updates only while it is in the selected zone (`moby_is_in_selected_zone`).
- State 4 (a burst) also starts a short camera shake: block+0x160 = 0.25, +0x168 = 0x26 (`:241-243`). In L19
  g_hero_l19_state_block is the camera block.
- **No other camera mechanism** is used:
  - SetupLevelCameras only zeroes the camera target (block+0x284) and the hook (block+0x3B0) in L19.
  - The live record stays class 0.
  - So the mod's `follow_live` is true throughout.

What that does to the stock camera, per tick (H for the writes, M for the picture):
- D eases from 4.64 to 10. The spring has k = 0.001, so it takes a few seconds (weapon_reset_view_state).
- h eases from 2.0 toward 0.1. The pivot B is then **0.1 above the floor point under Ratchet**:
  `cam_follow_update_collision`, `levels/shared/L00_00421C50.c:146-150`, builds B = floor hit + U·h. Its ceiling
  and floor branches keep B at least 0.95 from a ceiling.
- Tilt input st+0x1E4 is forced to 0 every tick. With the stick idle (it reads 0) or masked, the tilt goal is 0, so
  the goal orbit levels out (rac2-follow-camera-tilt.md).
- Then the probe sphere (r 0.95) lifts the goal off the floor.

The result is the "pans out" shot: a long, low, level camera about a metre off the ground, looking slightly up at the
boss. The look target height is still st+0x110 above P (unchanged here), and build_view aims at it with a smoothed h
(`L00_004224D0-follow-g8.c:69-72`).

What the boom does wrong (H for what it reads and writes, M for the symptom):
1. **It hangs the arm from A = st+0xC0 + U·h**, so as h goes to 0.1 the hang point sinks to Ratchet's feet. It also
   lengthens the arm to D = 10 at the **player's own lift**. The boom never changes lift for collision; it only
   shortens.
   - A 10-unit arm from a pivot at ankle height, swept with r 0.35, catches every snow mound and the floor.
   - So the length collapses toward kBoomMin = 0.3 near his feet. The game's build_view still aims at the look point
     above P, so the view tilts up: the reported "pans up".
   - With a high lift the eye instead goes about 10·sin(lift) up. That is still not the stock framing.
   - Which one the player saw is not determinable from code.
2. Its tilt override (st+0x1E4) is overwritten at the start of every tick by the slot's +0x0C = 0. The game then
   eases its tilt toward level inside the tick (≤ 0.0305 rad on the goal). That is harmless, because the boom
   rewrites the orbit and the eye springs carry only k·Δ. But it means the mod cannot drive the game's tilt while
   this slot is selected.
3. D = 10 > 6 sets the snap flag st+0x246. I found no reader of it in the v2 L00/L19 code or the exe (L).

What the mod should do:
- **Stand aside while a selected slot forces the framing**:
  - `+0x0C >= 0` (forced tilt), `+0x18/+0x19/+0x1A/+0x1E/+0x1F != 0`, or `+0x04 in [0, 1.0)` (a very low pivot),
    or `+0x00 > 6`.
  - Use the game's own special-distance threshold: distance outside [4.64, 6.0] triggers the snap flag.
- Hand back the way the mod already does (`s.boom_owned = false`; the end-of-tick resets restore st+0x10, st+0x23E
  and the masks). Keep only yaw turning, with turn_camera about U.
- Alternative "honour" path:
  - While +0x0C >= 0, set the boom's lift to the game's: el = asin(tan(0.698·G)) with G from the tilt, which is 0
    here, so level.
  - Never hang the arm lower than ~1.0 above P. Use max(h, 1.0), or the game's B = st+0xB0 plus the probe's 0.95
    clearance.
  - Honour D.
  - This is a lot of special-casing for one boss. Standing aside is safer.

---

## 3. Joba (11): the arena's Arachnoid wave, the rounded wall

### 3a. The arena's camera (H)
`arena_update_camera_guide` (`levels/11_joba/L11_0043E1A0-g8.c:402-436`; v2 L11 0x443C48 with its constants read from
the disc) runs while the arena controller's state byte is 8 (a wave) or 9 (the boss wave). It holds one slot of
kind 5, with fields +0x00 distance, +0x04 height, +0x08 look height:

| case | distance | height | look height | steer |
|---|---|---|---|---|
| a boss is set (st+0x180), **Arachnoid 0xB3A** | 8 | 2 | 2 | none |
| boss 0xD4A | 8 | 4 | 3 | none |
| boss 0xD4B, or boss slot set without a boss | 8 | 4 | 1 | none |
| state 9, no boss | 9 | 4 | 3 | **cam_set_strength_and_steer_target(arena centre, 10 deg/tick; 0.5 deg after 500 ticks)** while hero z > centre z + 16 (`:419-427`) |
| state 8, no boss | 8 | 4 | 1 | none |

There is no tilt and no stick mask. The intro and outro use SetCamera5Pose from g_arena_camera_table (class 5) and
cam_set_mode_state(2) (`L11_0043E1A0-main-g64.c:388-429`). `follow_live` already covers those.

### 3b. The gravity (H)
- In the boss wave (`L11_0043E1A0-main-g64.c:724-746`, which activates the Arachnoid), the launch light/lamp throws
  Ratchet up. Once hero z (hero+0x88) **> 480** and hero+0x180 < 0.0333, the arena sets **0x1A8FF0 = 1 and
  0x1A8FF4 = 1** (`:800-806`; v2 L11 0x440F80/0x440F88).
- The flags are cleared again later in the wave, below z = 463 (v2 0x4416C4-0x4416D8), and when the arena resets
  (0x43FA74).
- These are the engine's custom-gravity flags:
  - 0x1A8FF0 = custom gravity on (decomp's "g_hero_locked_flag").
  - 0x1A8FF4 = **cylinder mode** (decomp's "g_enemy_ai_frozen" / "g_hero_action_blocked_flag").
  - They are at the same absolute address in every v2 level I checked (L00, L03, L11 and L19; about 100 refs each)
    and in the v1 C.
- In cylinder mode the hero ground step sets gravity hero+0x2C0 = **unit((hero pos − axis) with z = 0)**
  (`levels/shared/L00_002AFBC0-ground-g8.c:208-210`, `game/002AFF08.c:34-41`, `game/002AFFD8.c:21-36`).
  - The axis point is g_origin_vector 0x1A8CB0, which comes from level data.
  - So gravity points **away from the arena's vertical axis**: Ratchet walks on the inside of a cylinder, the
    rounded wall.
  - His up, and the camera's U (spring-damped toward it, ~0.5 s lag, `game/00271B90.c`), are **horizontal, toward
    the axis**.
- With 0x1A8FF0 set, cam_follow_compute_position also switches to its locked-pivot branch: look-at lerp 15 ticks,
  lock flag (`levels/shared/L00_004212B8.c:65-72`).

### 3c. Why the boom misbehaves there (M/L: what I can rule in, not prove)
- The boom is built in frame_about(U), so the axis itself is handled. What changes is the geometry:
  - The arm is now 8 long (the guide's distance).
  - Ratchet stands on a concave surface that curves up into the arm whenever the yaw points around the
    circumference.
  - Turning sweeps the arm from the straight direction (along the axis) into the curved one. That is the
    "especially while turning".
- The game survives this with two tools the boom turns off or does not use:
  - **its probe**: a sphere of r 0.95 at the goal, mask st+0x238 = 0x14/0x34. That mask has **no 0x80 and no
    material exclusion**, so it hits every face.
  - **its eye push**: r 0.25, mask 0x36, contacts along the view's up row. In U's frame the wall under him is
    "floor", so the push applies.
  - The boom zeroes the springs and rewrites the goal and eye after the tick, so neither reaches the eye.
- **The boom sweeps with the camera line mask** (0x1B17CC: 0xB4 | material<<8). That mask leaves out:
  - **faces flagged 0x80**.
  - **faces of the material the hero last stood on**, if that was a special ground: ground types 0/14 water,
    1 (deadly ground), 3 (deadly liquid), 11 (death fall), 13 (sinking). See `game/00271FD8-g8.c:37-61` and
    `levels/shared/L00_002ACA08-flags.c:79-101`. The material is sticky: it is kept after he leaves.
  - The game's own arm sweep uses the **point mask** (0x1B17D0: 0x94, or 0xB4 in water), which has no material
    exclusion. Its probe uses 0x14/0x34.
  - If the arena wall is 0x80-flagged or of the excluded material, the boom's sphere and centre line both pass
    through it while the game's probe would not. That puts the camera inside the wall. Which of the two applies is
    level data I could not read (L).
- Smaller contributors:
  - U lags the real up by ~0.5 s, so "level" in the boom's frame dips into the curve on one side.
  - The 0.35 arm sphere and the 0.3 minimum are much smaller than the game's 0.95 and 1.5. The PS2 near plane may
    clip a wall 0.35 away (L).

Fix:
- **Stand aside under custom gravity**: `*(u32*)0x1A8FF0 != 0`, or `camera_up(...).z < 0.95` (the mod's
  `on_planetoid()`; cylinder mode gives U.z ≈ 0).
- If the boom is kept:
  - Sweep with the point mask (relocate_level(0, v2 0x1B17D0 / v1 0x1B1850)) instead of the line mask.
  - Add a final clearance step on the eye: CollSphere at the eye with the probe mask
    (relocate_level(0, v2 0x1B17D4 / v1 0x1B1854)) or 0x36, r ≥ 0.5, and pull in on a hit.
  - Raise kBoomMin.
  - Prefer lift over shortening.

---

## 4. Tight spaces: what the stock follow camera does (H unless marked)

- **Minimum distance.**
  - FollowCameraHideObstructions (`levels/shared/L00_004243C0-g8-dbg.c:102-111`): if D − deficit < 1.5
    (g_cam_follow_probe_minimum, v2 0x1B146C), it sets deficit = D − 1.5 and timer 2000. **The goal orbit is never
    shorter than 1.5.**
  - The arm sweep's own clamp is 0.2 (g_cam_follow_min_distance, 0x1B1468), but it is overridden by this.
  - While the stick turns into a wall, the sweep adds 0.075 per tick to the deficit (0x1B1454,
    `L00_004240D8-g8-dbg.c:60-71`).
  - The deficit recovers by a cosine blend.
  - The eye (smoothed orbit st+0x170) follows the goal by springs, so it is about 1.5 or more from A as well.
- **The pivot.**
  - B = floor point under Ratchet + U·h, with h = 2 (`follow_camera_setup`, `levels/shared/L00_004233F8-g8.c:111-112`;
    update_collision).
  - Under a low ceiling, h is lowered so that B stays 0.95 below it (`L00_00421C50.c:127-145`).
  - So in a crawlspace B drops to about head height, and the 1.5 minimum is what keeps the eye out of his head.
  - The game has no explicit hero-avoidance: every camera query skips the hero moby (M).
- **Eye safety.**
  - The only eye-level collision is move_eye's push: r 0.25, mask 0x36, 3 passes, floors and ceilings only
    (|dot| > 0.5 with the view's up row).
  - Walls are handled on the goal: the probe (r 0.95) and the sweep (6 spheres r 0.35-0.85).
- **Hero hiding or fading.**
  - There is **no camera-distance fade**. hero_set_objects_flag_bit (moby +0x34 bit 0 = not drawn;
    `levels/shared/L00_002D1ED8-scan-g8.c:24-70`, v2 0x2D3208) is called only for:
    - cutscene modes 2/6 with hero+0x22B5 (v2 0x2A6EC0)
    - **hero+0x22B5 set or first person allowed** (v2 0x2A6F9C)
    - form 4 (0x2A96CC)
    - SetHeroMode
    - cutscene start, and the vendor menu.
  - The only camera code that writes the hero record is the **aim camera (class 4)**. It sets hero+0x22B5 = 1 once
    the camera blend (block+0x290) is 0 (cam_aligned_update_from_input, v2 0x427C80-0x427C9C). That hides him only
    in that first-person-style aim.
  - Moby alpha (+0x23) writes are for effects, not the hero.
  - So a follow camera that gets into his head shows the inside of his model (M).
- **"The camera pushes Ratchet".**
  - No camera function writes the hero's position. In the v2 L00 camera range, the only store into
    0x189E20..0x18C200 is +0x22B5.
  - Camera collision volumes are used only by camera code (callers of FindIntersectingGridPrimitive /
    coll_is_point_in_grid / coll_find_nearby_shape_points).
  - The hero's collision never tests the camera.
  - The mechanism is **camera-relative input, recomputed every tick**. hero_resolve_stick_direction adds the
    camera's yaw block+0x158 to the stick angle and turns the stick vector by block+0x370
    (`levels/shared/L00_002AFBC0-input.c:62-70`). Airborne steering, the move physics and the decider read
    block+0x158 too (v2 refs at 0x2B738C, 0x2BAA48, 0x2CBF5C).
  - So any camera swing turns his run (or air) direction while the stick is held: the game's wall re-aim, an
    auto-steer, or a mod adopting a yaw. That is how a camera can "push" him along walls or off edges.
  - M that this is the exploit players mean.
- **How much the game re-aims per tick.** These act on the **goal** st+0x160:

  | mechanism | size per tick |
  |---|---|
  | arm sweep (cam_push_point_out_of_walls) | goal re-aimed at the pushed sample. The push is at most 0.75·(0.25 … 0.95), larger at the far samples; samples sit 0.35-3.6 from B. At the first sample, about 0.19 / 0.35, up to **~0.5 rad**; grazing a far sample, 0.05-0.2 rad (M) |
  | probe at the 1.5 minimum | up to atan(0.95/1.5) ≈ **0.56 rad** (M) |
  | moby steering (5 classes: Saur-Beast, Yeti, vendor, …) | ±1 deg |
  | auto-steers | up to st+0x1D4 (6 deg for the overrides, 10 deg for the Joba centre) |
  | LOS blocked 30 ticks | cam_reset_default snap; the mod's st+0x23E = 1 disables it |

  - The **smoothed orbit st+0x170**, which the boom reads, moves only through the springs: k 0.015, c 0.2. With the
    spring velocities zeroed as the boom does, that is 1.5 % of the goal-to-eye angle per tick, under 0.01 rad for a
    0.5 rad goal jump.
  - So a > 0.5 rad change of post-tick st+0x170 in one tick is **never** a wall re-aim. It is a rewrite:
    - cam_reset_default
    - cam_follow_update_orientation(1) (Giant Clank / reposition)
    - cam_follow_init_from_hero (`L00_00423290-qcopy.c`)
    - UpdateVolumeAimCamera, which writes st+0x170 directly (`L00_00429F68.c:91-107`)
    - a scripted rewrite.
  - Adopting it is right. Jumps measured on st+0x160 would be normal in corridors.

---

## 5. Clank and the Glider

### 5a. Clank (form 1: hero+0x22B4 == 1; Endako 3, Aranos prison 14)
- **Follow camera, class 0** (H). Each tick cam_follow_update_us2 (`us2/levels/shared/L00_004239D0-main-g8.c:184-200`)
  applies these, **only when no override slot is selected (mode == -1) and no request owner (st+0x240 == 0)**:
  - follow_camera_set_distance(1, **3.0**, 0.003). Also st+0x1B8 = 3, which is reset to 4.64 at the end of the tick.
  - look height st+0x110 = **1.0**.
  - h → **1.0**.
  - In the hover (move state 5): look 0.25, h 1.5.
  - st+0x22C = 0.6 (sweep radii 0.25 + 0.06·i, so the largest is about 0.67), set regardless of the override.
  - update_collision uses a head point 0.4 above him, against 0.5 for Ratchet (`L00_00421C50.c:101-108`).
- Other camera changes in those levels:
  - Aranos prison has cuboid camera zones (override **distance 2, height 3**; L14 v2 0x428430, constants 0x1B1758/C).
  - Endako's camera_trigger_area_update uses SetCamera5Pose (class 5) while he is inside a path area
    (`levels/03_endako/L03_00399230-g8.c:297-340`). follow_live is false there.
- Nothing steers the yaw for Clank specifically. Rotating st+0x160/+0x170 and the eyes about U is compatible, as long
  as st+0x00 is rotated too: update_reach lerps the goal 85 % toward st+0x00 − B. turn_camera does rotate st+0x00
  and st+0x210.
- What still turns the camera, as for Ratchet:
  - the lazy follow (update_reach, g_follow_cam_lerp_factor)
  - held lock-on: hero+0x1450 → steer target with a ramp, plus lock and pivot speeds (`:277-283`)
  - block+0x284 target steer (`:256-276`)
  - the timed steers of any override.

### 5b. The Glider (Tabora 8, Dobbo 9, Grelbin 19)
- **Hero state** (H): 0x31 (group 9, camera request 0, hero+0x22BD = 1; `levels/00_aranos_tutorial/L00_002C42E0-state-g8.c:1172-1176`),
  with **hero+0x1830 = the glider moby (class 0x905)**, set by UpdateHeroNpc (`levels/shared/L08_003CDF98-g8.c:231-236, 300`).
- **Camera: the follow camera, class 0.** The glider calls its own cam_update_aim_values every riding tick
  (L08 v2 0x3CE8D8; L19 0x3BD3C8):
  - st+0x250 = 2. That skips the block+0x284 target steer, the Giant Clank branch and the stick-reset branch
    (`state[4] < 2` tests). The hero+0x1450 held-target steer is not gated by it.
  - look height → 0.25 (or the default + 0.25 when the level word at gp+0x10B4 is non-zero), k 0.1.
  - D → **4** with k **1.0**, effectively at once.
  - h → 1.0, at k 0.003.
  - Inside one cuboid (moby vars +0xC8), **tilt input 30 deg** (st+0x1E4 = 0.5236).
  - lock flag (st+0x136).
  - Pivot springs ramp to k = c = 1 over 90 ticks (st+0x13C/0x140), with the vertical pair 0.015 → 0.06. The pivot
    becomes rigid.
  - snap flag.
- **The target tracker also holds an override for it** (`levels/shared/L08_003D3928-g8.c:121-139`; L19 copy
  `levels/shared/L19_00422A98-g8.c:48-71`):
  - distance **7** (k 0.001), no tilt, no look height.
  - **timed steer** toward a point 10 ahead of the glider along its yaw.
  - 6 deg per tick, once the right stick as the game reads it (pad +0x100/+0x104) has been still for 120 ticks.
    The timer starts at 0, so it steers at once.
  - Whether D settles near 4 or near 7 depends on whether the glider moby updates before or after the camera tick
    (both write st+0x1A0/0x1A8; the last write wins). L.
- **What a mod turning the orbit would fight**:
  - The timed steer. The mouse (OpenRAC's built-in, or the mod's turn_camera) does not move pad +0x100/+0x104, so
    about 2 s after the last real stick input the game turns the camera back behind the glider at up to
    0.105 rad/tick.
  - In the 30-degree cuboid, the forced tilt.
  - To allow mouse look there, write slot+0x30 = *(s16*)(slot+0x32) (that is, 120) on every tick the mouse turns.
    The stick does the same.
- Other vehicles in state 0x31 on these levels (the Mining Vehicle and others) get distance 6, look 0, **tilt 0.1**,
  plus the same timed steer.
- The mod today already returns false from mod_camera_here for the glider (vehicle != 0 and group 9) and zeroes its
  own mouse and gyro there. Only OpenRAC's built-in mouse would fight the steer.

---

## 6. Ranked changes for mod.cpp

1. **Override-slot gate** (H mechanism, M thresholds). Add `override_framing(mod, memory)` (§1 snippet). It is true
   when the selected slot has any of:
   - +0x0C >= 0
   - +0x18 / +0x19 / +0x1A / +0x1E / +0x1F != 0
   - 0 <= +0x04 < 1.0
   - +0x00 > 6.0

   Make mod_camera_here false (or only boom_tick's game_has_it true) while it holds. This covers the Grelbin
   leviathan, the Joba platform and the vehicle trackers. Pure distance/height zones (Aranos prison 2/3, the Joba
   guide 8/2/2) keep the boom, which honours D and h already.
2. **Custom gravity gate** (H). Stand aside when `*(u32*)0x1A8FF0 != 0` or `camera_up().z < 0.95`. This covers the
   Joba wall and the spheres.
3. **Sweep mask** (H that the game's sweep uses it). Use the point mask (v2 0x1B17D0 / v1 0x1B1850) for
   sphere_sweep and push_out, not the line mask 0x1B17CC. Keep the line mask only for the "behind a face" line.
4. **Clearance like the game's probe** (M). After choosing L, run CollSphere at the eye with st+0x238 (or 0x36),
   r 0.5-0.95. On a hit, shorten or raise the lift.
5. **kBoomMin 0.3 → ~1.0-1.5** (M). The game never goes under 1.5. With h lowered under a roof, A is at head height,
   and 0.3 puts the eye in his head or ears. When there is no room, raise the lift instead.
6. **Yaw adoption** (H). Keep adopting only big jumps of post-tick st+0x170. They are rewrites, not wall re-aims, so
   the threshold could also apply outside the teleport window. Never adopt from st+0x160.

## 7. Could not determine
- What "pans up" looks like exactly in the leviathan fight. Two possible outcomes of the low pivot and the 10-unit
  arm (§2.1).
- Whether the Joba wall faces are 0x80-flagged, of an excluded material, or a moby. That is level data.
- The meaning of hero+0x180 in the z > 480 test. The PS2 near-plane distance.
- Whether the snap flag st+0x246 has a reader I missed (for example through a struct copy).
- In the glider, which of D 4 and D 7 wins (moby and camera update order).
- What objects class 2456 (the on-foot target tracker's feed: Tabora, Grelbin, Museum) are in play.
- Whether `relocate_level(0, ·)` maps the slot table and selected index. The per-level deltas match those of the
  mod's camera slot and masks.
