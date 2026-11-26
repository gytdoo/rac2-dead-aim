# rac2 special situations: where Dead Aim must stand aside

Sources:
- rac2-decomp at `dev/reference/rac2-decomp`. Paths below are relative to its `src/` unless they start with `config/`.
  `dec` is `levels/shared/L00_002CB3A8-decider.c` (hero_l00_decide_state). `entry` is
  `levels/00_aranos_tutorial/L00_002C42E0-state-g8.c` (hero_l00_set_action, the state entry that writes the
  group). `sm` is `levels/shared/L00_002BA7D8.c` (the state machine). `item` is `levels/shared/L00_002C9448-g8.c`.
- The mod, `src/mod.cpp`. Line numbers are from the copy saved 2026-11-25 20:53. The file was being
  edited while I read it, so search by function name.
- Notes `rac2-aim-movement.md`, `rac2-spring-arm.md` and `rac2-follow-camera-tilt.md`.
- Nothing was run in game.

Confidence: **H** read in C. **M** inferred from C. **L** guess or common game knowledge.

Addresses:
- Hero = 0x189E20 on both USA discs.
- "block" = the level camera block g_scalar_transition_state (v1 0x1B8C80, v2 0x1B8C00). It is level data: use
  `relocate_level(0, ...)`, like the mod's kFollowCamera slot. That slot is block+0x190.

---

## 0. The three findings that matter most

1. **The mod's "follow camera" pointer is a cached snapshot of the active camera** (H). It is the main camera bug.
   - block+0x190 (v2 0x1B8D90, the mod's kFollowCameraV200 slot) is the camera manager's **current** camera.
     block+0x194 is the **previous** one. Evidence:
     - cam_switch_active writes `previous = prev; current = next` (`game/00270598.c:119,122`).
     - cam_update_state copies the view from `*(cam+0x190)` (`us2/game/00272378-qcopy.c:34-46`).
   - `find_camera` (mod.cpp:454) reads that slot **once per level load** and caches the record. After that,
     `memory.read<int16>(camera + kCamType)` reads the **cached** record's class, which is always 0. It never sees
     the camera that is live now.
   - So `boom_view` (mod.cpp:2633), `third_person`/`stick_owned` (:2652), `aim_follow_now` (:2028) and
     boom_tick's `game_has_it` (:1207) all believe the follow camera is up while another camera is live:
     - swingshot camera (class 7)
     - scripted pose (5): vendors, bolt crank, level scripts
     - Visibomb (6)
     - path cameras (3, 19, 20)
     - hoverbike (26)
     - trigger camera (27)
     - fixed camera (23)
   - In all of these the mod zeroes the right stick and drives a hidden record. Only `aiming_now` and
     `first_person_on` re-read the slot, so only they are correct.
   - Fix: "follow camera live" = `*(slot) == s.camera && *(s16*)(s.camera+0x86) == 0`. Also cache only a record
     whose class is 0, because the current `camera_shaped` would accept a scripted record at load.
2. **Busy still remaps the shoulders** (H, `controls.hpp` rewrite: only `Gate::Off` returns early). Every
   non-foot group goes to Busy (`gate_now`, mod.cpp:326-344). So swimming, turrets, cranks, vendor/scripted holds,
   the respawn freeze and so on all get the remapped shoulders. Section 2 gives the groups that must be Off.
3. **The non-Ratchet forms are only partly excluded** (H).
   - The hero form byte +0x22B4 (`kHeroBusy`) is:
     - 0: Ratchet
     - 1: Clank (`SetHeroMode(1, 0x3F, ...)`, `levels/03_endako/L03_003D2018.c:34`,
       `levels/14_aranos_prison/L14_003F4F98.c:588`)
     - 2: Giant Clank (`hero_restart_in_mode(2, 0x56, moby)`, `levels/shared/L22_003A5018.c:169`)
     - 4: Hypnomatic-controlled robot / Receiver Bot (`hero_attach_to_object(4, 0x8D, moby)`,
       `levels/shared/L18_00428F08-g8.c:311`)
     - 3: unknown
   - All forms reuse groups 0, 1, 2 and 4: Clank 0x3F, 0x40, 0x41, 0x45; Giant Clank 0x56, 0x57, 0x58; robot 0x8D,
     0x8E, 0x8F (section 2 table).
   - The mod catches Clank only by moby class 87 and Giant Clank only by level 22/23. **The Hypnomatic robot on
     Damosel, Grelbin and Yeedil (18/19/20) gates Active today.**
   - Fix: `+0x22B4 != 0` means Off.

---

## 1. GC levels (index = play_status().level = g_current_map_index = directory number)

The code has no table of level names. Directory names come from `config/programs.tsv`. Index 21 is absent (the
museum, 30, is stored as LEVEL21.WAD, `docs/build/levels.md:14`), and 27-29 do not exist. Gadgets with one global
handler in every level cannot be placed by code: Dynamo, Electrolyzer, Hypnomatic, Tractor Beam, Charge Boots,
Spiderbot/Visibomb, Thermanator and Gravity Bomb. Planet names are directory names; area descriptions are L.

| # | level | mechanics (evidence; confidence) |
|---|---|---|
| 0 | Aranos (tutorial) | tutorial prompts, lifts (`UpdateAranosVerticalLift`, which skips its camera pose when first person is on), `three_part_turret` enemy turret; `hero_steer_along_rail_paths` (00/01) M |
| 1 | Oozla | swamp water sheet, vendor, spline camera class 20 (`UpdateSplinePathCamera`); hero copy has no grind, ledge or swing states H |
| 2 | Maktar Nebula | **arena challenges** (`UpdateArenaController`, names.tsv:811); **custom-gravity zone**: hero z > 420 and within 150 of the arena anchor sets the zone flag (`levels/02_maktar_nebula/L02_003F5430-g8.c:70-95`) M; swing platforms |
| 3 | Endako | **Clank-only section** (form 1, class 87; Gadgebot/Bridgebot `UpdateCommandedMoby`) H; remote camera console; Infiltrator (cls M); swingshot, versa-targets; grind compiled in |
| 4 | Barlow | **hoverbike races** (`UpdateHoverbikeRace` 04/11, rail-grind tricks), camera class 26 H; vendor; swingshot |
| 5 | Feltzin System | **ship space combat** (`UpdateShipWeapons` 05/10/15/25), chase camera class 25 H |
| 6 | Notak | **bolt crank** (`UpdateBoltCrank`, `levels/06_notak/L06_00419CA8-g8.c:115-127`): hero state 0x37 + `SetCamera5Pose` H; Thermanator water (cls M); volume camera class 17 |
| 7 | Siberius | scripted ship sequence; enemy (not manned) turrets H |
| 8 | Tabora | glider and glider pads (08/09/19), rideable Mining Vehicle (08/19) H; swing target; frozen waterfalls (Thermanator) M |
| 9 | Dobbo | Giant Clank pad (09/18/22) H; glider; hover tank enemy |
| 10 | Hrugis Cloud | **ship combat** H |
| 11 | Joba | **manned turret** (`TurretUpdate`, `levels/11_joba/L11_00411D40-g8.c:78,138`) H; **arena** (`arena_controller_update`), custom gravity when the hero is above z = 480 (`L11_0043E1A0-main-g64.c:800-803`) M; hoverbike races; Levitator (11/17) H; Infiltrator |
| 12 | Todano | tourists, trader; volume camera; no grind H |
| 13 | Boldan | Levitator pads (states 0x74/0x82 special cases), path camera class 3, warp rings H |
| 14 | Aranos (prison) | **Clank section** (form 1), Gadgebot/Bridgebot; Levitator H; Infiltrator |
| 15 | Gorn | **ship combat** H |
| 16 | Snivelak | **manned turret** (`snivelak_turret_update`, class 4334; `cam_update_turret_follow`) H; path camera class 3 (grind rails) M |
| 17 | Smolg | Levitator; ride and path platforms; shares the hero copy with 13 H |
| 18 | Damosel | Giant Clank pad; train; **Hypnomatic robot** (form 4, states 0x8D-0x94) H; path camera class 3 |
| 19 | Grelbin | glider, Mining Vehicle, Infiltrator; Hypnomatic robot (Receiver Bot class 0xEE1, `L19_002CAAB0-fn.c:1659`) H |
| 20 | Yeedil | Hypnomatic robot / vehicle modes (`SetYeedilHeroVehicleMode`) H; boss camera; Infiltrator |
| 22 | Dobbo Orbit | **Giant Clank** (form 2, states 0x56-0x5F); `g_planet_radius` from a gravity centre (`levels/shared/L22_003A15D0-g8.c:257`) H; planetoid/space setting L |
| 23 | Damosel Orbit | Giant Clank, as 22; mothership boss H |
| 24 | Ship Shack | shop/showroom (ship attachments) L |
| 25 | Wupash Nebula | **ship combat** H |
| 26 | Jamming Array | portals, orbiting mobys, zone camera, `BlendCameraPose` H |
| 30 | Insomniac Museum | hover vehicle (`MuseumVehicleUpdate`) H; exhibits |

More placements by moby class (cls, M):
- swingshot nodes: 3, 4, 7, 8, 9, 11, 13, 16, 17, 18, 20, 30
- versa-targets: 3, 4, 11, 16, 20
- Infiltrator: 3, 11, 12, 14, 17, 19, 20, 30
- vendor: 1-4, 6-9, 11-14, 16-20

**Custom gravity (spheres, gravity walkways), H for the mechanism:**
- The level header can carry a sphere entry. It sets **0x1A8FF0 = 1** (the decomp's "g_hero_locked_flag" is a
  misnomer) and the centre (`levels/shared/L00_002FC7A0-00300618-g8.c:391-399`). Which levels have one is level
  data, not code.
- Per tick, the hero ground step rewrites the hero gravity vector **hero+0x2C0 (0x18A0E0, points down)** from the
  mode byte hero+0x22C3 (M offset):
  - 0: flat, (0, 0, -1)
  - 1: point/axis mode (0x1A8FF4)
  - 2: centre at hero+0xB0, sign from hero+0xBC
  - Code: `levels/shared/L00_002AFBC0-ground-g8.c:201-223`, `game/002AFFD8.c:21-36`.
- I could not tell which arena or level is "the planetoid with the switches". Detecting by the up vector
  (section 3a) covers every case.

---

## 2. Hero groups (+0x229C) and states (+0x2294)

The group comes from the state entry, which every level copies with different state sets (family F3). I took the
union over the 15 C copies (L00 `entry`, L01, L03, L04, L07, L08, L09, L11, L12, L13, L16, L18, L19, L20, L22).
Group assignments are H from `entry`. Meanings are H where a decider case or call site shows them, M otherwise.
The meanings below correct `rac2-aim-movement.md`: group 0x14 is **death**, not swimming, and swimming is groups
17/18.

| group | states | meaning | game buttons there | should be |
|---|---|---|---|---|
| 0 | 0 idle, 1 look (L1), 0x1D aim, 0x64, 0x3F (Clank idle), 0x56 (Giant Clank), 0x8D (robot) | idle / aim | L1 aim, R1 crouch, Cross jump, L2/R2 strafe | **Active** if form == 0 and state != 0x64 |
| 1 | 2 walk, 3 stop, 0x2E slide, 0x70 wading, 0x62/0x63 scripted walk-to-point, 0x40 (Clank), 0x57, 0x8E | walk / run | as idle | **Active** if form == 0 and state is not 0x62 or 0x63 |
| 2 | 6 fall, 0x2C fall after swingshot, 0x76 slope slide, 0x41/0x44/0x4E (Clank), 0x58, 0x8F, 0x94 | fall | Cross held = heli hover | **Active** (form == 0) |
| 4 | 7, 9, 0xA-0x12, 0x1B, 0x38, 0x45/0x48 (Clank), 0x5B, 0x6E, 0x81, 0x89, 0x92 (`entry`:610-626) | jumps: 0xA/0xF heli, 0xD/0x10 thruster, 0xB long, **0x11 double jump**, **0x12 jump out of water**, 0x1B ledge climb | R1 within 7 ticks = heli/thruster; Square = air attack | **Active** (form == 0). Exceptions inside the block: 0x28/0x29 become group 15 and 0x66 group 25 (`entry`:708,713,724) |
| 12 | 4 | crouch | R1 held; Cross = high/long jump; Square = comet strike | **Active** |
| 3 | 0x17/0x18 ledge grab/hang, 0x19/0x1A shimmy, 0x46/0x47/0x49/0x4A (Clank) | ledges | Cross climb / back-jump; **R1 drop** (dec:1467-1542) | Busy (mapped, no strafe) |
| 5 | 8 heli hover, 0x4B (Clank) | hover/glide | **Cross held** (dec:1691, 1713) | Busy. Must stay mapped, or a held "Jump" shoulder stops hovering |
| 6 | 0x13/0x14/0x15 wrench combo / hyper-strike / comet strike, 0x1F, 0x22, 0x4D, 0x6D, 0x83, 0x93 | melee and item use | Square; R1 + Square | Busy |
| 7 | 0x16, 0x42, 0x72/0x73 (hurt in water), 0x8C (hazard) | hurt | — | Busy |
| 8 | 0x26, 0x2F (hold-to-use item) | item pose | release the item button | Busy |
| 9 | **0x1C** remote Visibomb/Spiderbot (Circle ends it), **0x31 parked in turret/ship** (`L11_00411D40-g8.c:138`), **0x37 bolt crank** (`L06_00419CA8-g8.c:115-127`), **0x6F scripted hold** (vendors, challenge dialogue, lifts), 0x1E, 0x75 | the hero is driven by an object or script; no decider case | turret: Circle fires, Triangle exits; crank: Square in/out; remote: Circle ends | **Off** |
| 10 | 0x77 | bonk off a wall | — | Busy |
| 13 | 0x23/0x24/0x25 | swingshot fire / pull / hang (camera class 7) | release the item button | Busy |
| 14 | 0x2B | swingshot pendulum swing | release the item button | Busy |
| 33 | 0x8A/0x8B | swingshot pulling an object | — | Busy |
| 15 | **0x27 grind**, 0x28 rail jump, 0x29 rail switch, 0x2A grind attack, 0x3E rail landing | grind rails (`FindGrindRail`) | **Cross** jump, stick switches rail, **Square** attack. No shoulders (dec:1186-1451) | Busy (only Cross/Square are read, so a mapped Jump stays Jump) |
| 16 | 0x30 | hazard-surface reaction | — | Busy |
| 17 | **0x32 underwater swim, 0x33 float, 0x34 fast swim** | underwater | **Cross/Square held = pitch** (mask 0xC0); **R1 or R2 held = fast swim** (mask 0xA) (dec:436-489) | **Off** |
| 18 | **0x35 surface swim, 0x36 surface idle** | water surface | Cross = jump out (0x12); **Square, or R1/R2 = dive** (dec:490-539) | **Off** |
| 20 | 0x39, 0x43, 0x5F, 0x67/0x7F (drowning), 0x74 (death-plane fall, `entry`:569-572), 0x79, 0x7A, 0x7C, 0x7D, 0x91 | death (set_action refuses any change except 0x61, `entry`:171) | — | **Off** |
| 24 | 0x60, 0x61 | frozen / respawn | — | **Off** |
| 25 | 0x65 fell into deadly liquid, 0x66 hop out, 0x78 | deadly liquid | **Cross** = hop out, up to 3 times | Busy or Off; Busy keeps a mapped Jump working |
| 26 | 0x71 | path hang / zip-line | none (Square widens the catch) | Busy |
| 28 | 0x7B | **charge boots** | R1 double tap and hold; release R1 to stop (item:366-377, dec:892-937) | Busy (R1 = Crouch stays R1) |
| 30 | 0x82 | **Levitator** float | **Cross held = rise** (dec:1654-1678) | Busy |
| 32 | 0x85 mounted (hero+0x1830 = vehicle), 0x86 hoverbike race | riding | Triangle dismounts; bike: R1/R2 boost, L1/L2 fire | **Off** (vehicle != 0 already) |

How to detect the rest (H unless marked):
- **Form**: +0x22B4. 1 Clank, 2 Giant Clank, 4 Hypnomatic robot, 3 unknown; non-zero means not Ratchet.
  `hero_is_action_allowed` also requires 0 (`levels/shared/L00_002D3798.c:852`).
- **Vehicle**: +0x1830 != 0 (hoverbike 0x85/0x86 read it, `entry`:324-333). Turrets: state 0x31. Whether +0x1830
  is set in a turret is not known.
- **Ship levels**: 5, 10, 15, 25. Giant Clank: 22, 23 and form 2.
- **Cutscene**: game mode word 0x1A8F00 (v2 per rac2-aim-movement.md 2b, M) equal to 2 or 6. set_action refuses
  changes there (`entry`:172).
- **Pad masked**: 0x138180+0x1CC != 0. 1 masks 0x5030 and the left stick; 2 keeps only Start/Select
  (`game/002B7E28-pad.c:30`). The quick-select menu sets 2.
- **Gravity walking**: no group of its own. The hero walks in groups 0/1/2/4 with hero+0x2C0 != (0, 0, -1).
- **Not determined**:
  - the states for magneboot walls, the glider, Infiltrator and Electrolyzer puzzles, and the Snivelak turret.
  - The Electrolyzer handler is a free-fly camera, L3 exits (`levels/shared/L00_003B58B8-g8.c:42-51`, L).
  - Logging `gate_now`'s group and state changes in play is the way to find them.

### What the mod gets wrong today (gate_now / foot_group, mod.cpp:90, 326)
1. **Form 4 (Hypnomatic robot, L18/19/20)**: 0x8D/0x8E/0x8F/0x94 are in groups 0/1/2 and gate **Active**. The
   remap and lock-strafe apply, and Aim: Move can start. H
2. **Forms 1/2** are only caught by class 87 and the level number. Use the form byte. H
3. **Busy remaps** in the groups that must be Off:
   - 9: turret, crank, remote, vendor/scripted hold. A vendor is a 0x6F hold; if play_status still says PLAYING
     there, L1/L2 are remapped to Cross/L1 in the shop. L
   - 17/18: swimming. Physical R2 = Fire becomes Circle, so it never fast-swims; L1 = Jump becomes Cross pitch.
   - 20: death. 0x74 has health > 0.
   - 24: respawn freeze. 0x60; 0x61 is already excluded.
   - any group not listed above.
4. **Double jump 0x11 and jump-out-of-water 0x12** set +0x22B7 (`entry`:780, 685). The `NoMenu == 0` test makes
   them Busy, so lock-strafe drops for the length of a double jump. Those are on-foot jumps. Do not use +0x22B7 as a
   foot test; it is also set in hover, swim, grind-path and other states. M on whether the strafe drop is visible.
5. States 0x62/0x63/0x64 (scripted walk-to-point / wait, groups 1/1/0) gate Active. They are harmless, since the
   decider ignores input there (dec:938-962), but Aim: Move could switch first person on during a scripted walk. M

---

## 3. Cameras per situation

### 3a. The up axis (spheres and gravity walkways). H
- cam_update_follow_hero runs every tick before the camera update (`game/00271B90.c:22-39`;
  `us2/game/00272378-qcopy.c:34-35`):
  - raw up = -normalize(hero+0x2C0), stored at block+0x1D0 (v2 0x1B8DD0 / v1 0x1B8E50).
  - **U = block+0x1C0 (v2 0x1B8DC0 / v1 0x1B8E40)**: spring-damped toward the raw up per component (k 0.015,
    c 0.2) and renormalised, so it lags about 0.5 s.
- The whole follow camera is built from U: pivot B = P + U·h, the eye, the tilt axis (rac2-spring-arm.md).
  Exception: the pivot z is smoothed along world z (`00271B90.c:52`).
- Detection: `U.z < 0.999`. Also useful: hero+0x2C0 != (0, 0, -1), or 0x1A8FF0 != 0 as a level-wide hint (M that it
  is the same address on v1 and v2).
- The mod assumes +Z in several places:
  - boom_tick (`s.boom_lift = atan2(orbit.z, ground)`, mod.cpp:1239; the down line `p.z - 10`)
  - `turn_xy` (yaw about Z)
  - `first_person_tick` (mod.cpp:1577-1586: pitch from +0x14E0.z)
  - `aim_camera_tick` (pitch from rec.z)
  - aim_move_tick's entry (clamps f.z)
- Under a tilted U, the boom fights the game's orbit (it rotates about the wrong axis, and its "level" is not the
  ground), and first person's pitch limit is wrong. OpenRAC's built-in mouse turns st+0x160/+0x170 about the axis
  from `openrac_rac2_follow_axis(up)` (`reference/openrac-kbm/keyboard_mouse.cpp:603-617`). That function's body is
  not in the reference. It almost certainly returns U.

### 3b. Camera classes (rec+0x86 of the live record *(block+0x190))
Record table: 48 records at block+0x550 (`levels/shared/L00_002D7940-setup-g8.c:109-129`). The hero asks for a
class through hero+0x24A0. When no record of that class exists, the follow camera (0) stays live with other
presets (`game/00270838-g8.c:50-63`). All entries below are H except where marked.

| situation | live camera | mod's boom today (cached record) | must stand aside? |
|---|---|---|---|
| on foot | 0 follow | owns | no |
| L1 look / aim | 4 | `aiming_now` reads fresh → stands aside | yes (already) |
| swingshot 0x23-0x25, 0x2B, 0x2C | **7** (`L22_002C9898-g8.c:462-507`) | **owns (bug 0.1)** | yes |
| vendor, bolt crank (0x37), level scripts | **5** (SetCamera5Pose; crank `L06_00419CA8-g8.c:127`) | **owns (bug)** | yes |
| Visibomb / remote 0x1C | **6** (`L00_0038F178-tether-g8.c:80`) | **owns (bug)** | yes |
| special grind rails (L13/16/18), 0x71 | **3** path, M | **owns (bug)** | yes |
| cinematic / spline / trigger / fixed | 19 / 20 / 27 / 23 | **owns (bug)** | yes |
| hoverbike race (04/11) | **26**, M | **owns (bug)** if 0x86 is not caught by the vehicle test (boom_view has no vehicle test) | yes |
| ship (5/10/15/25) | 25 | `playing` is false (raw_level) | yes (already) |
| other grinding | 0 follow, game steering | owns | optional. Keep the boom but feed it the game's steer (st+0x1C0/+0x194), or stand aside. L |
| swimming 17/18 | 0 follow, distance 0.25, height 0.5 (`us2/levels/shared/L00_004239D0-main-g8.c:178`), water collision mask (`game/00271FD8-g8.c:28`) M | owns | **yes**: the game's swim camera follows the 3-D swim, and the boom's Z-up pitch fights it. M |
| ledges 3 | 0 follow, steered behind the hero (`main-g8.c:170-176`) L | owns | optional |
| heli hover 5 / Levitator 30 / path 16 | 0 with height/distance presets (`main-g8.c:162, 182, 192`) | owns | no. Keep reading st+0x18C/+0x190 so the presets apply |
| turret 0x31 (Joba, Snivelak) | Snivelak has `cam_update_turret_follow`; the class was not determined | owns if class 0 | **yes** (group 9) |
| Clank (form 1) | 0 follow, distance 3, height 1 (`main-g8.c:193-200`) M | owns | yes, to match the pad going Off |
| Giant Clank (form 2) | 0 follow, orbit rewritten and snapped every tick (`main-g8.c:201-249`) M | `busy == 2` → stands aside | yes (already) |
| death 20 / respawn 24 | 0 follow; game death camera behaviour not traced | owns | yes |
| camera blend after a switch | block+0x290 (s16) = 1/2 blend, 3 snapshot (`00272378-qcopy.c:37-44`) | ignored | yes, while it is != 0 |
| first person on | block overwritten from hero+0x14D0/+0x14E0 | handled | (already) |
| cutscene | game mode 2/6; scripted class | play_status | yes (already, plus class != 0 once fixed) |

The mod reads the blend phase as **s32** at slot+0x100 (`kPhaseAboveSlot`, mod.cpp:1538, used in
aim_camera_tick). The field is an s16 at block+0x290 (`*(unsigned short*)(cam+0x290)`, `00272378-qcopy.c:37`).
Read it as an s16. M that +0x292 is ever non-zero.

---

## 4. Where the game reads the buttons for something other than their on-foot meaning (H unless marked)

Pad bits as the game reads them (`game/002B7E28-pad.c:57`): L2 0x1, R2 0x2, L1 0x4, R1 0x8, Triangle 0x10,
Circle 0x20, Cross 0x40, Square 0x80. pad+0x1A0 is held and +0x1A4 pressed. The decomp's names
`g_pad_pressed_flags`/`g_pad_held_flags` are swapped relative to these words.

| situation | Cross | Square | Circle | R1 | R2 | L1 | L2 |
|---|---|---|---|---|---|---|---|
| underwater 0x32-0x34 | pitch (held) | pitch (held) | — | **fast swim** | **fast swim** | — | — |
| water surface 0x35/0x36 | jump out | **dive** | — | **dive** | **dive** | — | — |
| deadly liquid 0x65 | hop out | — | — | — | — | — | — |
| grind 0x27/0x28 | rail jump | grind attack | — | — | — | — | — |
| ledge 0x17-0x1A | climb / back-jump | — | — | **drop** | — | — | — |
| heli hover 8 | keep held | — | — | — | — | — | — |
| levitator 0x82 | **held = rise** | — | — | — | — | — | — |
| charge boots 0x7B | — | — | — | **double tap + hold** | — | — | — |
| jumps | — | air attack | — | **heli/thruster within 7 ticks**; held + flick + Cross = long jump | — | — | — |
| crouch 4 | high/long jump | comet strike | — | held | — | — | — |
| bolt crank 0x37 | — | **enter / leave** | — | — | — | — | — |
| Joba turret 0x31 | — | — | **fire** | — | — | — | — |
| remote Visibomb 0x1C | — | — | **end** | — | — | — | — |
| hoverbike | start/jump | — | — | **boost** | **boost** | **fire** | **fire** |
| ship (5/10/15/25) | twin shots | beam | special | roll right | U-turn | roll left | U-turn |
| first person (toggle) | jump | wrench | — | crouch | — | **fire** | **fire** |
| Clank | jump / hover | punch | — | ledge drop | — | aim | — |
| Giant Clank (22/23) | jump | punch | — | — | — | state 1 | — |
| Hypnomatic robot | jump 0x92 | attack 0x93 | — | — | — | state 1 | — |
| Electrolyzer free camera (L) | forward | — | — | — | — | — | — |

Electrolyzer: L3 exits; Triangle moves back.

Sources:
- swim: dec:436-539
- grind: dec:1186-1451
- ledge: dec:1467-1542
- hover: dec:1679-1750
- levitator: dec:1654-1678
- charge boots: item:366-377
- crank: `L06_00419CA8-g8.c:115-127`
- turret: `L11_00411D40-g8.c:111-113, 245`
- remote: `L00_003FFBA0-g8.c:208`
- hoverbike: `L04_00402D90.c:712`, `L04_00402D90-g8.c:217-241`
- ship: `L05_00442A58-g8.c:139-178`, `L05_00443D08-g8.c`
- first person: rac2-aim-movement.md 2e
- Clank: dec:2073-2361
- Giant Clank: `L22_002D0C98-decider.c:2416-2640`
- robot: `levels/20_yeedil/L20_002DEBE0-decider.c:2398-2560`

Triangle is in the mod's `opening_menu`, so it is always raw. That already keeps turret exit and dismount working.

---

## 5. Ranked fixes for mod.cpp

1. **Use the live camera, not the cached one** (H; §0.1, §3b). Without this fix, camera classes 7, 5, 6, 3, 19,
   20, 23, 26 and 27 are all mistaken for the follow camera.
   - Add `follow_live(mod, memory, camera)`:
     `*(u32*)slot == camera && *(s16*)(camera+0x86) == 0 && *(s16*)(slot+0x100) == 0`.
     slot = relocate_level(0, kFollowCamera…); slot+0x100 is block+0x290, the blend phase.
   - Use it in `boom_view` and `third_person` (frame), `game_has_it` (boom_tick), `aim_follow_now` and
     `hold_direction`.
   - In `find_camera`, cache only a record whose +0x86 == 0. If the slot holds another class, return the cache
     when one exists, or 0.
2. **Form byte +0x22B4 != 0 → Off** in `gate_now`'s `hard_off` (H; §0.3). It replaces the class-87 test and
   catches the Hypnomatic robot (form 4) on 18/19/20. Add the same test to `game_has_it` (today only `== 2`),
   `aim_follow_now` and `aim_move_tick`'s `want`. `first_person_on` already requires 0.
3. **Make Busy a whitelist; everything else Off** (H for the groups, M for the Busy/Off policy). In `gate_now`:
   - Active: group in {0, 1, 2, 4, 12}, form 0, state not in {0x62, 0x63, 0x64}.
   - Busy: group in {3, 5, 6, 7, 8, 10, 13, 14, 15, 16, 25, 26, 28, 30, 33}. These are on-foot actions where a
     held mapped shoulder must keep its meaning (heli hover holds Jump, charge boots and comet strike use Crouch/R1,
     grind reads only Cross/Square).
   - Off: groups 9 (turret 0x31, crank 0x37, remote 0x1C, scripted/vendor 0x6F, 0x1E, 0x75), **17/18 (swimming:
     R1/R2 fast swim, Square/R1/R2 dive)**, 20 (death), 24 (respawn), 32 (riding), and any group not listed.
   - Drop the `+0x22B7 == 0` foot test, which wrongly sends double jump 0x11 and jump-out-of-water 0x12 to Busy.
     Keep `state != 0x31` only as a belt; group 9 covers it.
4. **Stand the boom aside outside plain on-foot play** (M). In boom_tick's `game_has_it`, and the same condition in
   `boom_view`/`third_person` so the right stick goes back to the game, add any of:
   - group in {9, 17, 18, 20, 24, 32}
   - vehicle +0x1830 != 0
   - form != 0
   - game mode 0x1A8F00 in {2, 6}
   - the not-follow-live test from fix 1
   For grind (15) and ledges (3): either stand aside, or keep the boom and add the game's own auto-steer
   (`yaw += st+0x194` when st+0x1C0 != 0, rac2-spring-arm.md §3). Decide by feel. L
5. **Up axis** (H for detection, M for the fix).
   - Read U = *(vec3*)(block+0x1C0) (slot+0x30; v2 0x1B8DC0, v1 0x1B8E40).
   - When U.z < 0.999, either:
     - (a) stand the boom aside and let OpenRAC/the game turn the orbit, which is quickest and safest; or
     - (b) rebuild the boom in U's frame. Yaw about U, lift = asin(dot(orbit, U)/|orbit|), the down line along -U,
       and `turn_xy` replaced by a rotation about U, as keyboard_mouse.cpp's `kbm::turn_orbit(…, up, …)` does.
   - In the same condition, keep `aim_move_tick` from entering first person, or make `first_person_tick` pitch
     about U. Also skip the third-person aim writes in `aim_follow_now`, unless their basis is built from U (the
     +0x730 basis rows need a real up).
6. **`aim_follow_now`** (M): besides fix 1, require form 0, a foot or Busy-whitelist group except 13/14/33
   (swingshot), and not 15 (grind). The game's lock-on/aim there is its own. Also require U.z >= 0.999 unless the
   basis follows U. Today it checks only class (cached), aim/first person, moby, vehicle, health and weapon.
7. **`aim_move_tick`** (M):
   - `want` already requires `gate == Active` (via fp_want).
   - With fix 2/3 it no longer fires for the robot or for scripted walks.
   - Also refuse when U.z < 0.999 (5).
   - Give up first person when the gate leaves Active while held. `first_person_on` already treats group 0x14
     (death), form != 0 and class 5 as off. hero_is_action_allowed also refuses states 0x6F, 0x31, 0x1C and 0x8C and
     game modes 2/6 (rac2-aim-movement.md 2b). Add those to `first_person_on` so the mod does not count a refused
     tick as "first person on".
8. **Blend phase type** (M): read block+0x290 as s16 (`kPhaseAboveSlot`), not s32.
9. **raw_level** (H): 5, 10, 15, 25, 22, 23 is right for space combat and Giant Clank. With fix 2, 22/23 are also
   covered by form 2. Levels 24 (Ship Shack), 26 (Jamming Array) and 30 (Museum) are hero levels. The museum's hover
   vehicle should set +0x1830; that is not verified.

## 6. Could not determine
- The states for magneboot/gravity-boot walls, the glider, Infiltrator and Electrolyzer puzzles, the Snivelak
  turret (probably 0x31), and states 0x1E, 0x26, 0x75 and 0x6D.
- Which levels carry a sphere entry (0x1A8FF0), and which arena is "the planetoid with switches". The Maktar
  (z > 420) and Joba (z > 480) arena gravity zones are proven in code.
- Whether +0x1830 is set in turrets, the glider, the Mining Vehicle and the museum vehicle.
- Whether play_status leaves PLAYING in vendor menus. Fix 3 makes it irrelevant.
- The camera class used by turrets and by death.
- Form 3.
- The body of `openrac_rac2_follow_axis`.
- The v1 address of the game-mode word, and whether 0x1A8FF0 relocates.
- How these look in play: log gate_now's level, class, group and state at a swim, a
  grind, a crank, a turret, a Hypnomatic robot and an arena gravity zone.
