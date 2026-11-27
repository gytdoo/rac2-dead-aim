# rac2 Hypnomatic robot and manned turrets: what the game reads, and how Dead Aim can drive them

Sources:
- rac2-decomp at `dev/reference/rac2-decomp`. Paths are relative to its `src/` unless they start with `config/` or
  `docs/`. v1.01 C unless marked; `src/us2/` is the v2.00 C.
- The v2.00 disc (SHA-256 `eda31b45...`, the v2.00 hash in the decomp's `docs/build/versions.md`). I read the level programs straight from `G/LEVELn.WAD` with a scratch
  disassembler, to get the v2 data values and to check that the v2 code matches the v1 C.
- Read first: `rac2-special-situations.md` (groups, the button table and turrets) and `rac2-aim-camera.md` (euler
  convention: a camera pitch below 0 looks up).
- Nothing was run in game.

Confidence: **H** = read in C (and in v2 code where it says so). **M** = inferred from C. **L** = guess.

Pad words (`game/002B7E28-pad.c:57-120`):
- held = `((buf0<<8)|buf1) ^ 0xFFFF`. L2 0x1, R2 0x2, L1 0x4, R1 0x8, Triangle 0x10, Circle 0x20, Cross 0x40,
  Square 0x80. Select 0x100, L3 0x200, R3 0x400, Start 0x800. D-pad up/right/down/left are 0x1000/0x2000/0x4000/0x8000,
  and the left stick sets those bits too.
- pad+0x1A0 (0x138320) = **held**, pad+0x1A4 (0x138324) = **pressed** (edge), +0x1B0 = raw held (buttons only).
  The decomp's `g_pad_pressed_flags` is 0x138320, so it is really *held*; its `g_pad_held_flags` is 0x138324, so it
  is really *pressed*.
- `PadPressedWithin(mask, n)` is true if any of the last n polls had a press edge in mask
  (`levels/shared/L00_0032EF10.c:15-30`). It acts as a press buffer.
- Analog: +0x100/+0x104 right X/Y, +0x108/+0x10C left X/Y. The dead zone is |raw-0x7F| < 0x30, which gives exactly
  0.0. Values reach ±1.0 at |raw-0x7F| ≥ 0x7C. Up is negative.
- **The hero's stick copy** is hero+0x1FB0/+0x1FB4 (0x18BDD0/0x18BDD4). It is the **left** stick each tick.
  - When its length is < 0.25, it is replaced by the **d-pad** buttons (raw held), giving -1, 0 or +1 per axis.
  - Code: `levels/shared/L00_002B36E0-hero.c:72-90`; L18/19/20 copy `levels/shared/L18_002C4000.c:107-121`.
  - Both turrets aim from this copy.
- **Strafe flag** hero+0x1454: set to ≥1 while held & 3 (**L2 or R2**). Otherwise it counts down by 1 per tick. This
  happens in every form (`L00_002B36E0-hero.c:62-67`, `L18_002C4000.c:101-104`). H

---

## 1. The Hypnomatic robot (form 4)

### 1a. What it is (H)
- `hero_attach_to_object(4, 0x8D, robot)` (`levels/shared/L18_002CFD68-qcopy.c:19-56`) does the following:
  - The **hero record now drives the robot moby**: +0x2290 = +0xC14 = robot. Ratchet's own moby stays at +0xC18.
  - The hero's position and rotation (+0x80/+0x90) become the robot's.
  - Health +0x24CC = 3. Ratchet's health is saved at +0x24D4.
  - Form +0x22B4 = 4, state 0x8D.
  - **It does not write +0x1830** (vehicle), and nothing in the robot path does. So the vehicle test stays 0 (H for
    attach/detach, M overall).
- Called from receiver_bot_update state 7 (`levels/shared/L18_00428F08-g8.c:300-312`).
  - That state also **detaches first when the hero is already form 4** (:302-305), so a controlled robot can hand
    over to the next one.
- Robot classes, from the level dispatch tables (`docs/game/moby-dispatch.tsv:4828, 4835, 5079, 5087, 5348`):

  | class | name | levels |
  |---|---|---|
  | **0xEC0 (3776)** | unnamed | 18 Damosel, 19 Grelbin |
  | **0xEE1 (3809)** | Receiver Bot | 18, 19, 20 Yeedil |

  - Class 0xED4 (3796) appears in the robot code, but no level dispatches it to receiver_bot_update.
- Read the class at `*(s16*)(*(hero+0x2290)+0xAA)`.

### 1b. Buttons the robot reads (H; the v2.00 decider cases are identical apart from names)
Robot decider cases are the same in all three levels:
- L20 `levels/20_yeedil/L20_002DEBE0-decider.c:2398-2582`. v2: `us2/levels/20_yeedil/L20_002DEE88-sw.c:2417-2600`.
- L18 `levels/18_damosel/L18_002E68B0-decider.c:1934-2110`. v2: `us2/.../L18_002E6B50-sw.c:1948-2124`.
- L19 `levels/19_grelbin/L19_002CAAB0-fn.c:1656-1830`. v2: `us2/.../L19_002CACE8-sw.c:1622-1796`.
- I diffed v1 against v2: only the function names differ.

| action | game button | where / condition | conf. |
|---|---|---|---|
| **Jump** | **Cross**, pressed within 8 ticks | 0x8D idle (:2416) and 0x8E walk (:2554) go to **0x92**. In 0x92, Cross within 4 ticks re-jumps on landing (:2497) | H |
| **Attack / shoot** | **Square**, pressed within 9 ticks | 0x8D (:2420) and 0x8E (:2558) go to **0x93**, **except, by the decompiled check, the Receiver Bot (0xEE1)**. In play the Receiver Bot shoots with Square all the same (Yeedil, v1.01: Square held as the robot fires its orange shots; Circle held grows the beam), so the check is read wrong or the shot comes from another path. Seen in game, H. 0x93 runs `shooter_update_ranged_attack` on the robot (`L20_002CD928.c:3102-3117`). For 0xEC0: each new Square press resets the attack timer vars+0x392, so tapping keeps it firing; at 0 it returns to 0x8D (`levels/shared/L18_00427BE0-burst-g8.c:30-49`). Controlled shots fly along the robot's facing, with no height aim (`L18_00427BE0-shot-g8.c:51-69`) | H (button), M (what fires) |
| **Look / aim ("view")** | **L1 held**, **only from idle 0x8D** | Also needs +0x1500 == 0 and `level_get_saved_value()` == 0 (:2408). This is Ratchet's look state **1** (camera request 4; state entry `L20_002D7730-state-g8.c:335-341`). The Hypnomatic's update turns 1 into **29** (camera request 0x51; `levels/shared/L00_003B3158-lock-g8.c:203`, entry `L20_002D7730-state-g8.c:1201`). The robot stands still. **0x8E (walk) has no L1 test.** Release L1 → state 0, whose entry sends any form != 0 back to 0x8D (`decider.c:584-586, 2590-2593`; `state-g8.c:211`; `L20_002D6488.c:15-30`) | H |
| **Strafe** | **L2 or R2 held** (flag +0x1454) | In 0x8E the robot uses `hero_update_turn_from_anim` (`L20_002CD928.c:3144-3146`). It **faces the camera yaw and moves along the stick**, as Ratchet does (`levels/shared/L00_002B35A0.c:9-40`; push direction +0x1440, `L20_002CD928.c:3186-3190`) | H |
| Circle (Fire) | **Hypnomatic beam** from the robot, when the Hypnomatic is the equipped item | The item's button is slot0 mask = **0x20 (Circle)**; the wrench uses 0x80 (`levels/shared/L00_002AFBC0.c:238-244`). target_lock_on_update has form-4 branches: beam origin at robot joint 5 (:124-127), the equip delay is waived (:205), and Circle held grows the beam (`L00_003B84F0-g8.c:176, 207-215`). The beam can hypnotise the next robot (see 1a) | H (code), M (that it runs in form 4) |
| R1 (Crouch) | **nothing**; R1 is read only for form 0 (state 1/29 animation) | — | H |
| Triangle | **nothing** | The weapon wheel opens only for forms 0/3 (Clank's radial for 1) (`decider.c:177-196`). The HUD Triangle path needs form < 2 (`levels/shared/L00_0036F488-g8.c:108-111`) | H |
| left stick | movement (stick copy +0x1FB0/+0x1FB4) | — | H |
| right stick | the follow camera's own orbit | — | M |
| **exit** | **no button** | See 1d | H for the code paths found |

The robot never reads equipped weapons with Circle. `ProcessHeroEquippedItemInput` returns 0 when the form byte is
set (`levels/shared/L00_002C9448-g8.c:125-141`, field `disabled` = +0x22B4). H

### 1c. States, groups, camera (H unless marked)
State entry is in `L20_002D7730-state-g8.c` (all robot states request camera +0x24A0 = 0):

| state | group | meaning |
|---|---|---|
| 0x8D | 0 | idle (:1137) |
| 0x8E | 1 | walk (:1159) |
| 0x8F | 2 | fall (:1142) |
| 0x94 | 2 | slope slide, when +0x149A is set (:1147) |
| 0x92 | 4 | jump (:600, :725-735; jump heights per class) |
| 0x93 | 6 | attack (:875, :986-989; only 0xEC0 starts animation 10) |
| 0x90 | 7 | hurt (:1151) |
| 0x91 | 20 | self-destruct / control lost (:1166); state machine `L20_002CD928.c:3088-3101` fades out and exits |
| 1, then 29 | 0 | L1 look / aim, as for Ratchet |

- **Camera**: the follow camera (class 0), with Ratchet's presets. The follow-camera preset code has branches for
  forms 1 and 2 only (`levels/shared/L00_004224D0-main-g8.c:177-215`) (M). In states 1/29 it is Ratchet's look/aim
  camera (class 4). The hypnotise hand-over (robot state 7) poses camera record 5 itself
  (`L18_00428F08-g8.c:273-354`).
- **Vehicle** +0x1830 is not set (see 1a).

### 1d. How control ends (H)
- **Timer**: robot vars `*(robot+0x68)` +0x3BC (u16) is reloaded from +0x3BE on attach (`L18_00428F08-g8.c:299`).
  - It counts down in `hero_l18_update_frame` (`levels/shared/L18_002C4000-g8.c:66-74`). At 0, health is set to 0
    and the state goes to 0x91.
  - The HUD timer bar shows it (`L18_002C4000-hud-g8.c`).
- **Health** 0 (3 hits) → 0x91.
- **Falling** below `robot vars+0x3EC` → 0x91 (`decider.c:210-212`).
- **Level triggers**:
  - rotating trigger class 3792 (`L18_00427BE0-trigger-g8.c:62`)
  - platforms/sequences (`levels/19_grelbin/L19_00411890-g8.c:105`, `levels/shared/L19_003DCCC8-platform-g8.c:220`,
    `levels/18_damosel/L18_003F90A0-g8.c:258`)
- **Hypnotising another robot** with the beam.
- When the hero leaves form 4, the robot explodes (`L18_00428F08-g8.c:355-372`).

### 1e. Consequences for the mod
1. Gate the robot by form 4, not as Off. "Robot" mode applies to states 0x8D, 0x8E, 0x8F, 0x92, 0x93, 0x94, 1 and 29.
   0x90 can stay mapped. 0x91 (group 20) is Off.
2. Mapping that matches what the robot reads:
   - Jump → Cross (as now).
   - Aim → L1 (as now).
   - **Crouch shoulder → the strafe toggle**. R1 is dead for the robot, so nothing is lost.
   - Lock strafe holds **L2**; R2 works the same.
   - **Fire**: the robot's gun is **Square** for every class (the Receiver Bot's too, seen in play); its Circle is
     the Hypnomatic beam. So Fire → Square and the Wrench action → Circle, so the beam stays reachable. (Fire →
     Circle for the Receiver Bot, from the decompiled check, put the beam on Fire; changed in 0.1.3.)
3. **Lock strafe blocks the L1 view after the robot has walked** (H).
   - 0x8E returns to idle 0x8D only when the stick is < 0.17 **and +0x1454 == 0** (`decider.c:2566-2568`).
   - Only 0x8D tests L1.
   - So when the Aim shoulder is pressed in robot mode, drop the L2 hold for at least one tick. +0x1454 counts down
     from 1 to 0 in one tick. The player must also not be pushing the stick.
4. The robot's follow camera is class 0 with a +Z up axis, so the mod's boom could own it. Whether the robot's camera
   height suits the boom is not known (L).

---

## 2. Manned turrets

Only two turrets read the pad. I checked the enemy turrets named in L07/L16/L18/L20 (`boss_turret_update`,
`g_turret_*`): none of them reads the pad.

State 0x31 is **not unique to turrets**. It is also used by:
- the hoverbike race start/end, Barlow **and Joba** (`levels/shared/L04_00419918-g8.c:318-322`)
- the glider, 08/09/19 (`L08_003CDF98-g8.c:233, 281`)
- rideable class 2140, 08/19/30 (`L08_003C6AB0-g8.c:360`)
- the museum hover vehicle (`levels/30_insomniac_museum/L30_003B3F70-g8.c:152`)
- the ship levels

So identify a turret by its moby, as below. The 0x31 entry sets group 9, +0x22BC = 1, +0x22B7 = 1 and camera
request 0 (`L11_002D8EF8-state-g8.c:992-996`, `L16_002C0BC8-state-g8.c:915-919`). That blocks the weapon wheel and
item input, so Triangle is free for the exit.

### 2a. Joba turret: `TurretUpdate`, class **0xC20 (3104, "Turret")**
Code: `levels/11_joba/L11_00411D40-g8.c`. v2 address 11:0x4134B0. There is no v2 C, but the v2 disassembly has the
same constants, pad words and field offsets. Class from `docs/game/moby-dispatch.tsv:2949`.

- **Mount** (:101-141), automatic, no button: the hero stands on the turret (+0x33C ground == turret) within 1.2 of
  the seat, with mount_delay 0. The turret sets command (+0xBC) = 1 and the hero enters 0x31.
  - While mounted, the hero is snapped to the seat every tick and his rotation = the turret's.
  - On the first manned tick the turret's +0x20 becomes **5**. Unmanned is 4.
- **Exit**: **Triangle pressed** (pad+0x1A4 & 0x10) (:111-118). This calls `cam_set_mode_state(0)`, sets hero state
  0 and command 2.
  - Re-mount needs the hero to leave (more than 1.2 away, or off the turret), then 60 ticks (:129-132).
  - v2 code reads 0x138324 & 0x10. H
- **Fire**: **Circle** (:245-306).
  - A Circle press starts the barrel animation.
  - While Circle is held, a shot spawns at each animation peak, alternating barrels.
  - Shot direction = spherical(1, yaw, pitch).
  - v2 reads 0x138320 & 0x20 and 0x138324 & 0x20. H
- **Aim input**: the **left stick** through the hero copy +0x1FB0/+0x1FB4, with the d-pad fallback.
  - `x = stickX`; `y = -stickY * invert`. invert = -1, or +1 when the invert option is set (v2 0x1A7BAC, the same
    flag the aim camera uses).
  - (x, y) is clamped to unit length (:207-215).
  - So by default, stick up = aim up. H
- **Dynamics** (:216-243). This is an **acceleration model, not a position map**:
  - `yaw_speed += -x * 0.00087266`; `pitch_speed += -y * 0.00031513` (rad/tick²).
  - An axis whose input is **exactly 0** has its speed zeroed at once.
  - `yaw += yaw_speed` (moby+0xF8, wraps, **no yaw limit**).
  - `pitch += pitch_speed`, then **clamped to ±0.5236 (±30°)** every tick, with the speed zeroed at the limit.
  - Both speeds are multiplied by 0.97 each tick.
  - Terminal rate at full stick ≈ 0.97·a/0.03: **yaw ≈ 97°/s, pitch ≈ 35°/s**, time constant ≈ 33 ticks (0.55 s).
    H (constants), M (derived rates)
- **Angle fields** (v2 offsets checked in the disassembly):

  | field | where |
  |---|---|
  | yaw | turret moby **+0xF8** (rot z) |
  | pitch | **vars+0x44**, vars = `*(moby+0x68)`; pitch > 0 = up |
  | yaw_speed | vars+0x60 |
  | pitch_speed | vars+0x64 |
  | barrel target (= -pitch) | vars+0xB4, eased onto joint 2 by `moby_update_eased_node(…, 0.03, 0.3)`, so the barrel visibly lags |

- **Camera**: **record class 5** (scripted pose).
  - Switched in by `SetCamera5Pose` on the first manned tick (:121).
  - After 2 ticks (dismount_delay), every tick (:308-321):
    - position = joint 2 + spherical(0.5, yaw, pitch) + 0.2 z, so essentially first person at the gun
    - rotation = (0, **-pitch**, yaw)
    - v2 constants 0x1B0C70 = 0.5 and 0x1B0C74 = 0.2.
  - The game never reads the right stick here.
  - How the camera returns to follow after the exit was not traced (`cam_set_mode_state(0)`, `game/0032EB28.c:483`).
    L
- **+0x1830**: not written by TurretUpdate or the 0x31 entry. Expect 0 (M).
- **Finding it**:
  - Best: scan for a moby of class 0xC20 with +0x20 == 5 and +0xBC == 1 (M).
  - Or `*(hero+0x33C)` while state 0x31 on level 11. The hero stands on it, but the code does not require that while
    mounted (M).
  - Do not use state 0x31 on level 11 alone: the hoverbike race also uses it.

### 2b. Snivelak turret: `snivelak_turret_update`, class **0x10EE (4334)**
Code:
- v1 `levels/16_snivelak/L16_004165B0-fx-g8.c`; v2 C `us2/levels/16_snivelak/L16_004179E8-sw-g8.c`, same logic.
- Steering `L16_00417210-g8.c` (v2 16:0x418648).
- Fire `L16_004165B0-aim-g8.c:37-66` (v2 16:0x4183E8).
- Camera `L16_00416EC8-g16-qcopy.c` (v2 16:0x418300).

Moby +0x20 states: 1 or 4 waiting; **2 intro** (auto-aims at a scripted target); **3 player control**; 5/6 destroyed.
- **Mount**, automatic: in state 1, with hero +0x33C == turret and +0x340 (on ground), the game calls
  `hero_reset_position_and_direction(seat, turret rot, 0x31, 0)` (v2 C :89).
  - **The vehicle slot +0x1830 is set to the turret** (`g_hero_state.owner = self`; the offset works out to 0x1830
    in the Global struct, :91). H
  - `SetCamera5Pose(…, mode 3, 60 ticks)` blends into camera 5.
  - Then state 2 turns the turret to the target with `math_approach_target_with_direction_mode` on +0xF8. When
    within 5° it goes to state 3 (v1 :105-133). **Do not drive the aim in state 2**: it would fight the intro or keep
    it from finishing.
- **Exit**: **Triangle pressed** (0x138324 & 0x10, v2 code checked), or the hero leaving 0x31 (v1 :135-147).
  - `turret_reset_camera_state` puts the hero back in a move state and calls `cam_set_mode_state(3)` (the L16 copy
    of 0x3355A8) (`L16_004165B0-g8.c:85-90`).
  - Damage while manned costs health; at health 0 the hero exits (:166-175).
- **Fire**: **Circle or Square held** (0x138320 & 0xA0, v2 checked).
  - One shot per refire timer vars+0x8C = **8 ticks** (v2 0x1B1AA8), alternating muzzles.
  - The shot goes to **where the screen centre hits**: CollLine from the camera, 2 to 120 units along the camera
    forward. If a moby is within 1.5 of the aim point (2, 0, 2) in the turret frame, it targets that moby instead.
  - Shot speed 75/s (v2 0x1B1AA0).
  - **So aiming the camera is aiming the gun.** H
- **Aim input**: the **left stick** copy +0x1FB0/+0x1FB4 (`g_player_state`/`g_pad_stick_y` = 0x18BDD0/0x18BDD4).
  **The invert option is not read.**
  - Each axis is first limited to ±|stick|, so release is instant. It then approaches the stick at 0.15/tick, about
    7 ticks to full.
  - Filter globals: v2 0x1B1AF0 (x), 0x1B1AF4 (y); v1 0x1B1B50/0x1B1B54.
- **Dynamics** (rates in deg/s and deg/s², converted with π/180/60 and π/180/3600):
  - Below full deflection: target rate = **90°/s × stick**, acceleration 90°/s² × |stick|. So it takes about
    1 s to reach speed.
  - **At full deflection (|value| = 1.0, i.e. |raw-0x7F| ≥ 0x7C)**: target **360°/s**, acceleration 360°/s². This is a
    step up ("turbo") at the edge.
  - Deceleration 2000°/s².
  - Yaw and pitch use the same numbers.
  - v2 data 0x1B1AB0..0x1B1AD8 = 90, 360, 90, 360, 2000, (60 unused?), 90, 360, 90, 360, 2000. v1 = v2 + 0x60.
- **Integration**:
  - `yaw(moby+0xF8) += rate(vars+0x80)`, only if the rate != 0. No yaw limit.
  - `pitch(vars+0x90) += rate(vars+0x84)`, only if the rate != 0. Braking (`math_clamp_speed_to_braking_limit`)
    stops it softly at **[-30°, +20°]** (v2 0x1B1AE4 = -30, 0x1B1AE0 = 20). These are the v2-checked offsets.
  - **The clamp runs only when the pitch rate is non-zero.**
- **Pitch sign**: the camera pitch = moby rot.y - 0.1 + vars+0x90. A positive value **looks down**
  (rac2-aim-camera.md). Target rate = speed × (-stickY). So **stick up looks down**: it is inverted, flight style, by
  default. H for the arithmetic, M for how it looks on screen; check in game.
  - So the limits are 30° up and 20° down from the bias. The -0.1 rad bias tilts the camera 5.7° up.
- **Camera**: **record class 5**. Each tick in states 2/3 `cam_update_turret_follow` does:
  - angles = turret rot (+0xF0); angles.y += -0.1 + vars+0x90
  - target = turret pos + turret matrix × (0, 0, 4)
  - eye = target + R(angles) × (-4.5, 0, 0), with eye z ≥ turret z + 1.0
  - it writes the class 5 record's position (state+0x80) and rotation (state+0x90)
  - v2 constants at 0x1B1A40/0x1B1A50/0x1B1A60/0x1B1A64.
  - It is a chase camera 4.5 behind. **The right stick is never read.** H
- **Finding it**: `*(hero+0x1830)` with class 0x10EE (`*(s16*)(m+0xAA)`) and +0x20 == 3. H

### 2c. Recommended way for the mod to drive turret aim from the right stick, mouse and gyro
For both turrets, the game ignores the right stick, and the camera is class 5 posed from the turret's own angles.
The mod's `follow_live` fix (special-situations §5.1) therefore stands the boom aside automatically. H

- **Fire**: in a turret, map the Fire shoulder (and the mouse Fire) to **Circle**. Snivelak also accepts Square, so
  Wrench can stay Square there. Keep Triangle raw for the exit; `opening_menu` already does.
- **Aim, recommended: write the angles directly every tick and zero the game's own input**:
  - Centre the left stick in the report (0x7F/0x80 is inside the dead zone, so exactly 0) and let no d-pad through.
    The game's integrators then add nothing.
  - Joba:
    - yaw += Δyaw at moby+0xF8 (wrap to ±π).
    - pitch += Δpitch at vars+0x44, clamped to ±0.5236.
    - Optionally zero vars+0x60/+0x64. A zero stick already zeroes them that tick.
    - The game's own ±30° clamp still runs.
  - Snivelak, **only in moby state 3**:
    - Write moby+0xF8 and vars+0x90.
    - **Clamp the pitch yourself** to [-0.5236, +0.3491] rad. The game clamps only while its rate is non-zero.
    - Zero vars+0x80/+0x84 and the two filter globals once on entry, or every tick.
    - For "stick up / mouse up = look up", subtract from vars+0x90.
  - Timing: the turret update and its camera pose are computed from these fields in the same tick, after the pad is
    read. Shots use them too: Joba from yaw/pitch, Snivelak from the camera of the previous frame. So there is at most
    one frame of lag (M).
  - Right stick through this path: use the mod's own rate curve, so it feels like the on-foot camera.
- **Fallback, stick emulation**: copy the right-stick bytes (2, 3) into the left-stick bytes (4, 5) and centre the
  right stick. For Snivelak, flip Y if "up looks up" is wanted.
  - This keeps the game's feel: Joba's 0.55 s lag; Snivelak's 1 s ramp and 360°/s turbo.
  - It is poor for mouse and gyro, because input maps to acceleration, not angle.

---

## 3. Could not determine
- What the 0xEC0 robot's attack actually fires: only the button, timer and facing are traced. Its in-game name is not
  known.
- Whether the Hypnomatic's update really runs every tick in form 4. The form-4 branches say it was meant to; the
  per-frame call site of `UpdateHeroSlotCallbacks` was not traced.
- What the beam does when fired from a robot, beyond re-hypnotising a bot.
- What `level_get_saved_value()` (an L1 gate) stands for.
- Whether a record of class 0x51 exists for state 29. It is the same as for Ratchet's aim.
- How camera 5 hands back to the follow camera after either turret exit.
- The v1 addresses of the L11 camera constants. v2 is 0x1B0C70/0x1B0C74; v1 is probably +0x20 (L).
- In game: the Snivelak pitch direction, the Joba invert default, and whether `*(hero+0x33C)` stays the Joba turret
  while mounted.
