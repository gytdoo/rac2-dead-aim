# rac2 "aim with movement": the game's aim mode, its first-person mode, and how a mod can let Ratchet move while aiming

Sources: rac2-decomp v1.01 C (`src/levels/shared`, `src/game`; v2.00 C in `src/us2/...` where it differs), checked
against the v2.00 disc's L00 code with a throwaway disassembler (not kept; the v2 L00 data dump
gave initial values). rac3-decomp `config/names.tsv` for UYA. Nothing was run.
Confidence: **H** read in C and (where it matters) seen in v2 asm, **M** read/inferred from C only, **L** guess.
Hero = 0x189E20 on both discs. "Pad" = g_pad_state 0x138180. L00 function addresses: v1 / v2.

## 0. Answer in one paragraph
In the aim state (29, also 1) Ratchet **cannot move**: those states share the standing handler, which lets the
speed only fall and clears the velocity, and the decider has no exit from 29 except releasing L1. The aim camera
reads the **left** stick first, then the right. GC has no strafe-aim state, but it does have the **first-person
mode**, the menu toggle. When it is on, the left stick walks (strafing, relative to the view), the right stick
looks, shots go to the screen centre, Ratchet is hidden and the held gun is drawn as a view model. That is exactly
"aim with movement". A mod can switch it on and off at will: it is a plain s32 read every tick, with no unlock
check, and the camera cuts at once both ways. **Recommendation: (a) turn the first-person mode on while the
aim button is held.** The mod hands the direction over both ways, and must not pass L1/L2 to the game while it
is on, because in first person L1 and L2 fire. Option (c), an over-the-shoulder zoom with lock-strafe, is the
low-risk alternative. Option (b) is not worth it.

## 1. The aim mode (states 1 and 29, aim camera type 4)

### 1a. In and out (decider, hero_l00_decide_state; v2 copy src/us2/levels/shared/L00_002CB650-sw.c, same logic) H
- Into state 1 (look) from idle 0, walk/run 2/0x2E/0x70 (with an extra +0x1492 / speed condition), 3, 4 (crouch),
  0x3C and others, when **L1 held** (pad+0x1A0 & 4) **and hero+0x1500 == 0** and a per-level check:
  `L00_002CB3A8-decider.c:220, 372, 790, 1030, 1072, 2073` (v2 `...CB650-sw.c:205, 357, 775, 1015, 1057, 2083`).
- Most weapon updates turn 1 into 29 (`hero_l00_set_action(29,1)` when state == 1), e.g. Blitz
  `L00_003A53E8-g16.c:112-113`, Lancer `L00_003A6C30-g8.c:204`; Chopper and Tesla Claw do not.
- Out: state 1 `decider.c:558-597` (L1 released, or 31 ticks without the aim camera flag +0x22B5) -> 0 (4 if R1);
  state 29 `decider.c:2420-2437` (v2 `:2430-2448`): only **R1 picks the crouch animation 0xD** and **L1 released ->
  state 0**. No walk, jump or roll transition. The generic part before the switch still applies to every state
  (fall/ledge/transition checks `decider.c:182`, item input `:179`), so falling off an edge ends the aim.

### 1b. Why he stands still (hero_l00_update_state_machine) H
`L00_002BA7D8.c:563-686` (v2 `src/us2/levels/shared/L00_002BA8E0-sw.c:561-672`): states 0, 1, 3, 4, 0x1C,
**0x1D**, 0x1E, 0x39, 0x6D, 0x6F, 0x7D share one standing handler:
- `hero_resolve_stick_direction(1,0)` still reads the stick copy (hero+0x1FB0/+0x1FB4) into +0x1A0 (move angle),
  +0x190 (move vector), +0x1B0 (wanted speed) (`L00_002AFBC0-input.c:40-71`), but then
- `hero_approach_speed(0.0, 0.004916667)` (`:661`). The rise rate is 0, so the current speed +0x1B4 can only fall
  (`L00_002AFBC0.c:863-868`).
- `vec_clear_128(hero+0xF0)` (`:662`). +0xF0 is the per-tick velocity that ApplyHeroMotionAndResolveCollisions
  adds to the position with collision (`L00_002B5B60-motion.c:72-118`).
- `hero_shrink_vector_by_amount(0.004917)` (`:663`, `L00_002B91C0.c:170-186`): the leftover slide vector
  **+0x160** (0x189F80) shrinks by 0.0049 per tick and is added to +0xF0. This is the only displacement left, plus
  gravity (hero_apply_mode_transform z -= 0.015).
- States 1/29 with +0x22B5 (the aim camera is in): +0x190/+0x1A0 = the camera's forward and yaw, and
  `hero_steer_heading(0, 1.0, 0, 2.909)`: his facing snaps to the camera yaw (`:581-588`).
So no code path in state 29 turns the stick into motion.

### 1c. The left stick in aim: cam_aligned_update_from_input (v1 0x4266E8 / v2 0x427BF8) H
`L00_004266E8-g8-hazard.c:44-58`. Pad floats (`src/game/002B7E28-pad.c:62-75`): +0x100 right X, +0x104 right Y,
**+0x108 left X, +0x10C left Y** (report bytes 2..5). Each is exactly 0.0 inside |raw-0x7F| < 0x30 and 1.0 from
0x7C on.
- yaw input = -left X; **if exactly 0**, -right X; if 0, the held bits 0x8000/0x2000 (d-pad left/right, which the
  **left stick also sets**, pad.c:96-103).
- pitch input = -left Y, else -right Y, else bits 0x1000/0x4000. It is flipped by the invert option (v1 0x1A7BA4 /
  v2 0x1A7BAC, also seen in v2 asm 0x427E58), and zeroed while the camera blends in with +0x22A0 == 2.
- Eased with a spring (0.03 / 0.4) and turned at up to 0.02618 rad/tick (90 deg/s) (v2 gp 0x1B156C..0x1B1574).
So the left stick has priority and the right stick only looks while the left one is centred. Swapping or zeroing
the left stick in the report changes which stick looks, but it never makes Ratchet walk: the walk input is the same
left-stick floats, copied to +0x1FB0 every tick (`L00_002B36E0-hero.c:71-90`), and 1b ignores it.

### 1d. Is there a moving aim variant? H (for what was searched)
No hero state aims freely and moves. Searched: every case of the decider and the state machine that touches 1/29,
and every caller of hero_is_action_allowed and reader of +0x1500. The shared level code is all C now (one
INCLUDE_ASM left in `src/levels/shared`). Moving while aiming exists only as:
- **lock-strafe** (L2/R2 held: the s16 timer hero+0x1454 >= 1, `L00_002B36E0-hero.c:61-66`), which aims through
  auto-aim/targets in third person;
- the **first-person mode** (section 2), which sets the same strafe timer to 2 every tick.
The level-specific deciders (L13/L17 `L13_002D5ED8-decider.c`, L22/L23 `L22_002D0C98-decider.c`) have the same
gate (L1 and +0x1500 == 0).

## 2. The first-person mode (menu toggle)

### 2a. The switch H
- `g_menu_toggle_option`, an **s32 word**: v1 0x1A7908, **v2 0x1A790C** (v2 `lw` at L00 0x2D67BC). Boot data
  (relocate).
- All v2 readers and writers (L00 + boot scan): hero_is_action_allowed (read); the menu toggle handler (boot
  0x2D37E8, L00 copy 0x34D058); the menu text draw; UpdateAranosVerticalLift (the L00 tutorial lift skips its
  scripted camera pose when on, `L00_003CD8E0-lift-g8.c:133`); save_reset_slot_ids (v2 boot 0x29C418) writes 0
  after a memory-card load or operation (`src/game/0029B628-rc2.c:41`, callers `00298838.c:262`, `00298F00.c:871`).
- Not in the save data (M): its neighbours are the frame-rate globals, and the save block starts at 0x1A79F0.
- The hero code has **no unlock check**: the unlock only decides whether the menu offers it (that path was not
  traced). Writing 0/1 to the whole word is safe.

### 2b. The gate: hero_is_action_allowed (v1 0x2D6418 / v2 0x2D67B8) H
`L00_002D3798.c:841-861`, v2 asm identical. It returns 1 only if:
- the toggle != 0;
- g_game_l00_mode (v2 L00 gp-0x60F0 = 0x1A8F00) is not 2 or 6;
- the active camera record (`*0x1B8D90` v2) type +0x86 != 5 (scripted camera);
- hero +0x22B4 == 0 (normal Ratchet, no vehicle or other form), +0x229C != 0x14, health +0x24CC != 0;
- state +0x2294 is not 0x6F, 0x31, 0x1C or 0x8C.
When any of these fails the game is plain third person while the toggle stays on, and first person comes back by
itself afterwards. The mod's `first_person_on` (mod.cpp:1726) leaves out the game-mode and the four state checks.
The functions are present in all 27 levels (`config/levels/functions.tsv`, UpdateHeroAimAndTransitions etc.).

### 2c. What it does each tick, in update order (hero_l00_update_frame_2, `L00_002A6DD0.c:77-128`; v2 `src/us2/levels/shared/L00_002A67F8.c:53-141`)
1. **Walking**: UpdateHeroInputAndHistory copies the **left stick** (pad+0x108/+0x10C, or the d-pad under 0.25)
   to +0x1FB0/+0x1FB4 and counts the strafe timer +0x1454 down (`L00_002B36E0-hero.c:61-90`). The decider and
   state machine then run their **normal** states: idle, walk, run, jump, crouch... Movement is relative to the
   camera (hero_resolve_stick_direction uses the camera yaw at block+0x158 and its matrix,
   `L00_002AFBC0-input.c:59-70`), as a **strafe** because +0x1454 != 0 (direction +0x1457, walk handler
   `L00_002BA7D8.c:1846, 1895, 1943-1951`). L1 cannot start state 1, because +0x1500 != 0 (1a). H
2. **Hidden**: hero_set_objects_flag_bit sets the hero moby's +0x34 bit 0 and that of his items and Clank, except
   **slot 0, the held item** (`L00_002D1ED8-scan-g8.c:40-43`). Turned off: hero_clear_objects_flag_bit clears it
   every tick (`L00_002A6DD0.c:114-115`). H
3. **Looking**: UpdateHeroAimAndTransitions (v1 0x2D5158 / v2 0x2D54F8, `L00_002D3798-aim-g8.c:77-158`). H
   - The view is the unit vector **+0x14E0** (world). If it has length 0, it becomes the hero matrix row 0 (his
     facing). Every tick yaw and pitch are **re-derived from it**, through the hero frame +0x40 / +0x00 when
     +0x2C8 > -0.99. So one write of +0x14E0 sets the view; the mod's tests in play confirmed 1:1.
   - Rates: **yaw +0x14F4 from right X** (pad+0x100), **pitch +0x14F0 from right Y** (pad+0x104), pitch flipped
     by the invert option (v2 0x1A7BAC). The C names these swapped (`pitchRate`/`yawRate`). Trust the use: yaw +=
     +0x14F4, pitch += +0x14F0 (`:145-146`).
   - Constants (v2 L00 data, only read by L00 code):

     | quantity | value | address |
     |---|---|---|
     | max look speed | 180 deg/s (yaw and pitch) | 0x1A7F9C / 0x1A7FA0 |
     | accel (yaw / pitch) | 360 / 240 deg/s^2 | 0x1A7FA4 / 0x1A7FA8 |
     | decel | 2880 deg/s^2 | 0x1A7FAC / 0x1A7FB0 |

     The target speed **and** the accel are x0.3 unless the stick is at full deflection (|v| >= 1.0, raw >= 0x7C).
     So most of the stick turns at up to 54 deg/s, and full tilt jumps to 180 deg/s.
   - **Aim friction**: a 30 u camera ray (CollLine mask 1, skipping the hero) that hits a moby of class type
     (+0x24 -> +0x46) 5 caps both scales at 0.6. While the stick is off full deflection, the speed is also blended
     with a tracking term (0.75 at 0x1A7FB4, 0.333 at 0x1A7FBC) (`:98-138`).
   - Pitch is clamped to +-1.48353 rad. A platform's yaw +0x15C is added unless g_hero_locked_flag (v2 0x1A8FF0).
   - hero_update_camera_block (`L00_002D64B8.c:37-57`): eye **+0x14D0** = pos + up(+0x20) x h. The height h at
     **+0x151C** springs (speed +0x1520) to **1.5**, or **0.75** in state 4 (crouch), at 0.00444 / 0.533. It is
     set to 1.5 if 0. In group 25 / state 102 the eye z is kept at least +0x334 + 0.75 (probably the water level, M). The camera block position and rows
     come from the eye and +0x14E0.
   - Unless the state is 17 or 98-100 (hero_get_camera_params gives a scripted look there, `L00_002D64B8.c:10-29`):
     **+0x1454 = 2** (strafe), +0x1A0 = camera yaw, +0x190 = camera forward, and
     `hero_steer_heading(0, 0.037, 0.25, 0.2036)`. Ratchet turns to the view yaw at up to 0.2036 rad/tick
     (`:152-158`).
4. **The gun**: +0x1500 (aim active) is set on the first tick, with blend +0x14F8 = 0 and the hand position
   captured (`:163-175`). Then, every tick, UpdateHeroHeldObject (v1 0x2D5940 / v2 0x2D5CE0,
   `L00_002D3798-g8.c:94-192`) H:
   - places the gun in **camera space** from the weapon table offsets, with a bob, and blends +0x14F8 from 0 to 1
     (rates 10 and 15 at 0x1A7CE0 / 0x1A7CE4, roughly a third of a second, L);
   - pulls the gun back when something is in front (-1.5..0);
   - writes **+0x730..+0x75C = camera rows, +0x760 = camera forward**;
   - **runs the gun's update right there with the fire mask +0x1230 |= 5 (L2|L1)** (`:186-191`). The normal slot
     callback skips the gun while +0x1500 (`L00_002AFBC0.c:575-578`).
5. **Camera**: the level copy of cam_update_state (v2 L00 0x2D9F38; source `src/game/0026EA00-qcopy.c:137-178`,
   a stub in the boot) runs after the active camera's update. If hero_is_action_allowed(), it overwrites the
   camera block with the hero eye and +0x14E0 (position +0x140, rows +0x370/+0x380/+0x390), whatever record is
   active (v2 asm: `jal hero_is_action_allowed` at 0x2D9FFC). **No blend, no FOV change.** H

### 2d. Weapons and reticles in first person
- Straight shots take the +0x1500 branch: a camera ray (30 or 75 u), bent at most 20-30 deg from +0x760 (the
  view), so **to the crosshair** (rac2-weapon-aim.md section 1). The Lancer does not search for auto-aim
  (+0x1500); the Seeker launches along +0x760 = the view. H
- Lobbed weapons: L1 is not held and +0x730 holds the camera rows, so they fly **along the view at maximum
  reach** (CalculateAimedLaunchVelocity, rac2-weapon-aim.md). A mod cannot change this: +0x730 is written
  inside UpdateHeroHeldObject just before the gun's update reads it. M
- Centre reticles: only the weapons gated on **+0x1500** draw one: Lancer/Heavy Lancer, Pulse Rifle/Vaporizer,
  Chopper/Multi-Star. The **state 29** gated ones (Blitz, Seeker/HK-22, Plasma Coil/Storm, Hover/Tetrabomb,
  Mini/Megarocket) and the **state 1** one (Tesla Claw) draw **nothing**, because first person never enters 1 or
  29. Sources: rac2-weapon-reticles.md table; Blitz checked at `L00_003A53E8-g16.c:112-121`. H. The mod's
  `first_person_crosshair` already queues them when `first_person_on`. Mini/Megarocket are "left to game" there,
  so in first person they have no centre reticle unless ported.
- The Pulse Rifle scope works (+0x1500): R1 zooms in, R2 out (`L00_003A2EE0-g8.c:197-225`). In first person R1
  also crouches (state 4, eye 0.75). M

### 2e. Input side effects (important for the mod) H
- **L1 and L2 fire** (the mask |= 5 above). Pressing L1 or L2 with the wrench out also draws the quick-select
  weapon, like Circle (`L00_002AFBC0-equip.c:112`). So the mod's faked lock-strafe L2, and an aim button that
  presses game L1, would **fire continuously** in first person. Strip game L1/L2 from the report while first
  person is on. Strafing needs no L2 there (+0x1454 = 2).
- L1 aim (state 1/29) cannot start (1a). R1 crouches. Cross jumps. Everything else is normal.

### 2f. Entering and leaving
- **Menu on** (`src/game/002CD8C0.c:307-311`): toggle = 1; **+0x14E0 = normalize(hero +0x00 row 0)**, his facing.
  H. Row 0 of the matrix built from his euler +0x90 (`L00_002B36E0-hero.c:106`) is his facing. M
- **Menu off** (`:302-306`): toggle = 0, **+0x14F8 = 0, +0x1500 = 0**. With active already 0 the exit pass does
  not run, so the gun snaps back to the hand. H
- **Toggle off alone** (what a mod gets by writing only the word): the next tick runs the exit pass (`aim-g8.c:176-189`).
  UpdateHeroHeldObject runs once more (the gun at the view-model spot; its update runs there, once, since the slot
  callback skipped it that tick). +0x14A0 = gun position in hero space, blend +0x14F8 = 1, +0x1500 = 0. The
  third-person attach code then eases +0x14F8 back to 0, and the gun flies back to his hand
  (`L00_002AFBC0-attach-g8.c:195-200`). This is the same path as leaving the L1 aim. H
- **Camera**: instant both ways (2c.5). When the mode goes off, cam_update_state copies the active record again.
  That is the follow camera, which **kept updating in the background**: cam_update_active runs every tick, and no
  camera C tests the toggle, +0x1500, +0x22B5 or hero_is_action_allowed (grep of all shared camera files; the
  complete v2 caller list of hero_is_action_allowed has no camera function besides cam_update_state). H for "it
  runs".
  - Its orientation on return is wherever the follow logic left it: it kept chasing Ratchet, and its own
    right-stick turning reads the same right stick as the first-person look, at its own speed. So expect a yaw
    different from the first-person view. M/L
  - With the mod's Modern boom, the mod decides: it owns yaw, pitch and length and rebuilds the record every tick.
- **Hero**: shown again on the next tick, facing the view yaw (turned at up to 0.2036 rad/tick in first person).
  +0x1454 goes 2 -> 1 -> 0 over 2 ticks unless L2/R2 are held. The eye height +0x151C keeps its last value, which
  the next entry starts from (it is set to 1.5 only when it is 0). H
- Flipping at will: no debounce, no transition state, read every tick. The cost of a quick on/off is the gun
  raise/lower blend and the camera cuts. H
- Pause menu: while it is on, the options page shows "On". Toggling it there clears +0x1500/+0x14F8, and the mod
  would set it again. The Aranos lift skips its camera pose if the mode is on at that moment.

## 3. Options for "aim with movement"

### (a) First-person mode while the aim button is held. Feasibility H, feel good (recommended)
The game's own FPS: strafing, jumping, crouching (eye 0.75), shots to the centre, view model, aim friction, all
levels.

Recipe:
- **Entry** (in the per-tick callback before the hero update, e.g. on_pad):
  1. Preconditions: on foot and the 2b gate true apart from the toggle. Remember the player's own toggle value; if
     it was already 1, leave everything as it is.
  2. Optionally, the UYA-like blend: ease the boom length in to about 1.2-1.5 u over 6-10 ticks first.
  3. Write **+0x14E0** = the current view forward (camera block +0x370 row 0, v2 0x1B8F70, or the boom's yaw and
     pitch): (cos p cos y, cos p sin y, sin p), p clamped to +-1.4835.
  4. Write **+0x14F0 = +0x14F4 = 0** (stale rates from the last session would carry on).
  5. Write **+0x151C = 1.5** (0.75 if state 4) and +0x1520 = 0.
  6. Write the toggle word = 1 (s32).
- **While held**:
  - Strip game **L1 and L2** from the report (byte 1 bits 0x04 and 0x01 set: active-low = released), and do not
    fake L2.
  - Mouse and gyro: the existing `first_person_tick`.
  - Right stick, either way:
    - let the game read it: its 54/180 deg/s two-speed look plus aim friction (GC's own tuning); or
    - own it for the same feel as Modern third person: neutral bytes 2..3, rotate +0x14E0 with the Modern stick
      model, keep +0x14F0/+0x14F4 at 0. Aim friction is then lost unless re-made.
  - Reticles: the existing `first_person_crosshair`.
  - Optional ADS zoom: the FOV write in (c).
- **Exit**:
  1. Take yaw and pitch from +0x14E0.
  2. Write the toggle = 0, then either:
     - +0x1500 = 0 and +0x14F8 = 0 (menu style: the gun snaps to the hand); or
     - nothing more (game style: the gun lowers to the hand over about 0.3 s).
  3. Boom: yaw = view yaw, pitch = view pitch clamped to the boom's range. Start the length at about 1.2-1.5 u (not
     0: Ratchet is visible again at once and a shorter boom sits inside his head), then use the existing ease-out
     (UYA 0.00556 / 0.333).
  4. Make sure the follow record holds the boom pose on the first third-person frame. Either keep computing it
     during first person (the game ignores the record while the mode is on), or write it on the exit tick.
     Otherwise expect a one-frame pop. Then give L2 back to lock-strafe.
  5. Classic camera: turn the follow orbit to the view yaw (the mod's turn_camera), or accept the cut.
- **Fields** (hero + offset = absolute):

  | field | offset | absolute |
  |---|---|---|
  | aim vector | +0x14E0 | 0x18B300 |
  | pitch rate | +0x14F0 | 0x18B310 |
  | yaw rate | +0x14F4 | 0x18B314 |
  | gun blend | +0x14F8 | 0x18B318 |
  | aim active | +0x1500 | 0x18B320 |
  | eye | +0x14D0 | 0x18B2F0 |
  | eye height, speed | +0x151C, +0x1520 | 0x18B33C, 0x18B340 |
  | strafe timer (s16) | +0x1454 | 0x18B274 |
  | fire mask | +0x1230 | 0x18B050 |

  Toggle v1 0x1A7908 / v2 0x1A790C. Camera block v1 0x1B8C80 / v2 0x1B8C00 (level, relocate_level):
  +0x140 position, +0x158 yaw, +0x190 the active record pointer, +0x370 rows.
- **Risks**:
  - L1/L2 fire (above).
  - First person drops out by itself in the 2b states (swimming group 0x14, scripted camera 5, states 0x1C, 0x31,
    0x6F, 0x8C, forms and vehicles, game modes 2/6, death). Treat "toggle on but `first_person_on` false" as not
    aiming and keep the boom in step.
  - Grind rails, ladders and swingshot are **not** excluded: allow the hold only in on-foot ground/air states.
  - Lobbed weapons go to maximum reach along the view.
  - Reticles of the state-29 weapons must come from the mod (they already do; Mini/Megarocket do not).
  - The camera cuts.
  - The Pulse Rifle's R1 both crouches and zooms.
  - The player's own menu setting must be restored, never left on.
  - Exact frame order (hero update before cam_update_state) is M; if reversed, the first frame lags one tick.

### (b) Keep the game's aim mode and move him. Feasibility M, feel poor (not recommended)
- Nothing in state 29 moves him (1b), so the mod would have to do all of it:
  - zero the left stick in the report (bytes 4..5 = 0x80, and the d-pad bits), so the aim camera falls back to the
    right stick (1c; the mod's mouse/gyro `aim_camera_tick` keeps working);
  - drive the body itself, from the real left stick.
- **Saner lever**: write the leftover slide vector **+0x160** (0x189F80) every tick before the hero update, as the
  wanted per-tick displacement plus 0.0049 along it. The standing handler shrinks it by 0.0049, adds it to +0xF0,
  and ApplyHeroMotionAndResolveCollisions integrates that **with collision** and ground contact
  (`L00_002B5B60-motion.c:94-118`; speed capped at +0x264 - 0.02).
  - Collision H for +0xF0. Whether anything between the mod's write and the handler rewrites +0x160 is M: only
    the state-0 entry copies +0x150 there.
  - Writing the position (+0x80) skips collision: not sane.
  - +0xA60 (move impulse) is rebuilt from +0xA70..+0xA78 by hero_build_move_impulse every tick, and its writers
    were not traced (L).
- **Costs**:
  - the mod must make up acceleration and walk speeds;
  - no jump (the 0x1D decider ignores Cross);
  - no crouch-walk;
  - edges kick him out of aim through the fall check;
  - no strafe animation or footsteps (he is hidden in aim, so mostly unseen);
  - his facing stays locked to the camera.
- Fragile and worse than (a), which is the designed version of the same thing.

### (c) No game aim mode: zoom the boom over the shoulder, keep lock-strafe. Feasibility H, lowest risk, different feel (TPS-style ADS)
- **Everything is already in the mod**:
  - lock-strafe through game L2 (pad held & 3 -> +0x1454, `L00_002B36E0-hero.c:61-66`);
  - shots to the crosshair (+0x760/+0x774/+0x778, lobs through +0x730; rac2-weapon-aim.md section 2);
  - per-weapon reticles (rac2-weapon-reticles.md);
  - the boom.
- **Aim held**:
  - shorten the boom (for example 6 -> 2.5 u);
  - add a right-shoulder offset (sweep pivot -> offset point -> camera so it cannot clip);
  - slow the look;
  - optionally narrow the FOV.
- **FOV**: camera block **+0x3D4 target, +0x3D8 current, +0x3E8 mode (u8), +0x3E9 active (u8)**, spring
  +0x3DC/+0x3E0/+0x3E4 (v2 0x1B8FD4.., asm 0x2D7B70).
  - cam_update_fov_transition (`src/game/0026EA00.c:500-528`) recomputes g_fov_tangent only while active == 1.
    Mode 0 snaps (current = target), 3 springs (cam_set_value_target, `src/game/0026FF08-rc2.c`).
  - Default 1.1243411 rad (64.4 deg), spring 0.005/0.2 (`L00_002D7140.c:28-37`).
  - Mod: each tick write the eased value to +0x3D4 with mode 0 and active 1. Restore the value read at the start.
    Do not fight the game's own FOV moves (cutscenes, the Pulse Rifle scope).
- **Risks**:
  - Ratchet covers part of the view;
  - near walls the boom pulls into his head, and he cannot be hidden in third person (the game clears +0x34 bit 0
    every tick, `L00_002A6DD0.c:115`);
  - weapon auto-aim cones stay active (the mod already centres them on the crosshair).

### UYA patterns worth copying (rac3 config/names.tsv)
- hero_camera_update (names.tsv:1589): in follow mode it "keeps fps_cam in step with the follow camera", so a
  switch never changes direction. For (a), keep a would-be +0x14E0 equal to the boom forward and write it at entry.
- Entering and leaving the hero view blends (0.075 and 0.045 per frame, names.tsv:1169-1172). The lock-strafe ->
  first-person boom collapse is an ease of the boom length to 0 (rac3-strafe-camera.md 1/4).
- camera_setting (names.tsv:23) moves R1 -> R2 and L1 -> L2 in its strafe/first-person settings: UYA also remaps
  shoulders when the camera mode changes.
- The strafing flag is 2 in first person and lock-strafe (names.tsv:102), the same as GC's +0x1454 = 2.

## 4. Not determined
- How long the gun raise/lower takes. The rates are known (10, 15); the exact semantics of
  math_step_value_toward_target's arguments are not.
- Where the follow camera ends up after a first-person session in Classic: its right-stick read was not located.
- Whether the 0x1A8F00 game-mode word is boot or level data for OpenRAC's relocation (the mod found the marker list
  at 0x1B7580 to be boot data despite its address).
- What hero states 0x31, 0x6F, 0x8C, 17 and 98-100 are. 0x1C (28) is entered when a remote-controlled Visibomb
  or Spiderbot is launched (`L00_0038F178-tether-g8.c:82`, `L00_003FFBA0-g8.c:182`), M.
- The relative order of the hero update and cam_update_state within a frame (assumed hero first).
- Whether the menu hides the option until it is unlocked (not traced). The hero code does not care.
- Nothing here was run in game: the first test of (a) should check the entry direction, the exit pop, that L1/L2
  are stripped (no auto-fire), and walking/strafing speed.
