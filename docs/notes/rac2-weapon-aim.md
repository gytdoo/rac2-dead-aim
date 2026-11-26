# rac2 weapon aim: how shots pick direction/target, and how a third-person mod aims them at the crosshair

Sources: rac2-decomp v1.01 C (`src/levels/shared`, `src/game`), rabbitizer disassembly of the v2.00 disc's
level 0 (with throwaway disassembly scripts, not kept). Nothing was run.
Confidence: **H** read in matched C and checked in v2 asm, **M** inferred from C/asm, **L** guess.
Function addresses are **L00** (each level has its own copy: resolve per level via
`config/(us2/)levels/functions.tsv`). Hero = 0x189E20 on both discs. Camera/collision data below were read
from L00's code only (prior notes treat them as level-independent; not re-checked per level).

## Data
| what | v1.01 | v2.00 |
|---|---|---|
| camera block base (g_scalar_transition_state) | 0x1B8C80 | 0x1B8C00 |
| camera position (block+0x140) | 0x1B8DC0 | 0x1B8D40 |
| camera euler (block+0x150; +0x158 yaw) | 0x1B8DD0 | 0x1B8D50 |
| camera matrix (block+0x370), row0 = view forward | 0x1B8FF0 | 0x1B8F70 |
| CollLine result (g_collision_result; +0x18 moby, +0x20 hit point) | 0x1BF200 | 0x1BF180 |
| hero body-manipulator block (g_hero_item_attach_records) | 0x25BE10 | 0x25BD90 |
| HUD marker list (6 x 0x30, count +0x120) | 0x1B7600 | 0x1B7580 |
| pad held-edge flags used by lobbed aim (bit 4 = L1) | 0x138320 | 0x138320 |

Hero fields (H = verified in code):
- +0x730/+0x740/+0x750: "weapon basis" rows (fwd, left, up). +0x760: **weapon aim forward** (vec4).
  +0x770: euler (x, +0x774 pitch, +0x778 yaw) used as the auto-aim cone axis.
  Third person: written every tick by update_hero_held_items_and_orientation (v1 0x2AF860 / v2 0x2AF8D8;
  L00_002AE388.c:270-287): basis = hero moby rows (+0xC0..0xE0), +0x760 = basis*(1,0,0) (horizontal facing),
  +0x770 = hero rotation (+0x90). First person/aim: UpdateHeroHeldObject (v1 0x2D5940 / v2 0x2D5CE0;
  L00_002D3798-g8.c:123,184-186) writes the **camera** rows, +0x760 = camera forward, +0x770 = camera euler. H
- +0x1500 "aim active": owned by UpdateHeroAimAndTransitions (v1 0x2D5158 / v2 0x2D54F8;
  L00_002D3798-aim-g8.c:163-189): 1 while hero_is_action_allowed() (menu first-person mode) or hero state
  1/29 (L1 look) with timer >= min; else cleared. H
- +0x22B5: set by the aim camera (L00_004266E8-g8-hazard.c:43). H
- +0x1450: L2+R2 held lock target (UpdateHeroHeldTarget v1 0x2CB038 / v2 0x2CB2E0, L00_002CAD90.c:38-73;
  25 u, 35/70 deg around the camera, then the hero). moby_get_or_find_target returns it first if alive
  (L00_00324D18.c:14-19). DrawTargetReticle draws it. H
- +0xBF8: the held weapon's current target (weapons set it; UpdateHeroAutoAimAngles clears it each tick and
  turns the torso to it). +0x24EC: "arm against a wall" flag, set each tick by cam_update_blend_angles
  (L00_002A6420.c:480-541); when 1, shots go along the gun's own -Y row. H

## 1. How a shot picks its direction (3rd person vs aim)
Per tick (hero_l00_update_frame_2, L00_002A6DD0.c:97-130): update_hero_item_mobys (gun on the hand joint) ->
UpdateHeroSlotCallbacks (v1 0x2B1F90 / v2 0x2B2060): **the held weapon's update runs here in 3rd person**; it
is skipped while +0x1500 is set (L00_002AFBC0.c:575) -> hero_update_items_frame (rewrites +0x730..+0x77C)
-> ... -> UpdateHeroAimAndTransitions (in aim mode it places the gun in **camera space** and runs its update
there). So the weapon reads +0x760/+0x774/+0x778 written by the *previous* tick (or by a mod after it). H

Straight shots, common pattern (Lancer CreateTargetedMovingEffect v1 0x3E2180 / v2 0x3E3600,
L00_003E2180-g8.c:50-66; Heavy Lancer SpawnAimedShot v1 0x41EB38 / v2 0x420038, L00_0041ABB8-shot-g8.c:75-95;
projectile_spawn_targeted v1 0x3AB9C0 / v2 0x3ACE00, L00_003AB9C0-g8.c:125-145): H
1. +0x24EC == 1: along -(gun +0xD0).
2. else target: (target pos + up*its height) - muzzle.
3. else +0x1500: math_compute_aim_direction (v1 0x37FA90 / v2 0x380EF8, L00_0037FA90-qcopy.c:40-62):
   ray from the camera position along camera forward, length 30 (75 for projectile_spawn_targeted), CollLine
   mask 0 skipping the hero moby; aim point = hit or ray end; dir = normalize(point - muzzle), clamped to
   20 deg (30 deg) from +0x760. **This is UYA's aim_bend_to_point already in rac2**, only in aim mode.
4. else along +0x760.
Others: Seeker (hero_fire_effect_from_facing v1 0x3AB8B8 / v2 0x3ACCF0): state 29 camera forward, else +0x760,
then homes on +0xBF8; the seeker projectile also searches 360 deg / 8 u around itself. Blitz (v1 0x3A53E8 /
v2 0x3A6818, L00_003A53E8-g16.c:188-197): fires along +0x760, overwriting +0x760/+0x730 toward +0xBF8.
Many more read +0x760 (minirocket, spread, lock-on, megarocket, SpawnAimedProjectile...; full v2 reader list in
a scan of the references (not kept),
which may be cleaned up). H

Auto-aim (cone) selection: SelectBestTargetInCone (boot 0x2AF580; L00 v1 0x324528 / v2 0x3257A0;
src/game/002AF580-qcopy.c): a yaw box and a pitch box around `direction` (not a true cone), limit eased from
the near value at 0 u to the far value at `range` by 1-(1-t)^3; score 3*dyaw + 0.2*dpitch*d + 0.1*d; LOS CollLine
mask 18. Callers: H
- Lancer (v1 0x3A6C30 / v2 0x3A8060, L00_003A6C30-g8.c:149-186): axis = spherical(yaw +0x778, pitch +0x774)
  from hero pos + up*0.75; yaw 35 -> 6 deg, pitch 45 -> 6 deg over 20 u (Heavy 30 u); a candidate must persist a
  few ticks to lock; **no search while +0x1500** (first person shoots at the crosshair, no assist).
- weapon_update_target_lock (v1 0x3B6158 / v2 0x3B7598): same axis, 35 -> 10 deg yaw, 45 -> 20 deg pitch, 30 u.
- Seeker/charged-beam style (L00_003A0F30-lock.c:121-140): axis = +0x760, 18/17 deg at 75 u (4 deg in state 29).
- Bomb Glove (v1 0x397E60 / v2 0x399290, L00_0038F178.c:412): axis = +0x760, 22.5/45 deg, 11.1 u.
Note the convention: vec_build_spherical_offset is (cos y cos p, sin y cos p, sin p): **+pitch = up**.
In 3rd person +0x774 is the hero's pitch (~0), so the vertical box is centred on the horizon.

Lobbed (CalculateAimedLaunchVelocity, v1 L00 0x37CEE8 / v2 0x37E328, src/game/002FDF40-g8.c:56-90; Bomb Glove,
Decoy, Gravity Bomb, mini nuke...): target object if any; else **if L1 is held** (0x138320 & 4) the landing is
where the camera ray meets the hero's ground plane (reach clamped [min,max], eased); else src + basis row0
(+0x730) * max reach (always the max). H

## 2. Making 3rd-person shots follow the crosshair
Do NOT set +0x1500 or +0x22B5 (H for the mechanism, M for how bad it looks):
- +0x1500 outside states 1/29 is cleared by UpdateHeroAimAndTransitions the next tick, but first the weapon's
  normal update is skipped and the exit path calls UpdateHeroHeldObject: the gun is moved to the
  first-person view-model spot in front of the **camera**, fires from there, and +0x14F8 blend starts lerping it
  back to the hand. Lancer also drops its auto-aim. It would glitch every tick.
- +0x22B5 hides Ratchet and his items (hero_set_objects_flag_bit, L00_002A6DD0.c:114) and, in states 1/29,
  snaps his yaw to the camera (L00_002BA7D8.c:585-590). It is first-person only.
Neither pitches the upper body. Vanilla 3rd person has no free upper-body aim: only UpdateHeroAutoAimAngles
turning the torso to +0xBF8 (yaw +-59, pitch -25..+35 deg; attach block +0x1C8 yaw / +0x114 pitch) and the head.

Recommended write set, each on_frame (after the tick; the next tick's weapon update reads it, then
hero_update_items_frame overwrites it): M-H
- M = muzzle estimate: held gun moby (*(hero+0x1220)) +0x10, or hero pos + up*~1.0.
- P = crosshair point (section 4). D = normalize(P - M) (fallback camera forward).
- hero+0x760 = (D, 0): straight shots, Blitz, seekers' launch, and the assist axis of Seeker/beam/Bomb Glove.
- hero+0x778 = atan2(D.y, D.x), hero+0x774 = asin(D.z) (+ = up): Lancer and weapon_update_target_lock assist
  cones now follow the crosshair, vertically too (keeps auto-aim, centred on the reticle, like UYA).
- Leave +0x730..+0x750 as the game wrote them (they also orient muzzle effects); the mod's strafe already turns
  the hero to the camera yaw, so lobbed weapons go the right way at max reach. Writing a pitched row0 would aim
  the lob at src + max*D (beyond P when P is near); crosshair-accurate lobbing needs the L1 branch, which can't
  be faked without entering hero state 1.
- Just-in-time alternative: `rac2.weapon.take_ammo` (weapon_spend_ammo v1 0x2D3F30 / v2 0x2D42D0) runs inside
  most weapons' fire path right before the shot spawns, after the gun is on the hand: write +0x760 there with
  the exact gun position. Not every weapon spends before spawning (Bomb Glove spends, spawns 20 ticks later).
- Exceptions: +0x24EC == 1 (gun against a wall) ignores +0x760; a locked target overrides D.
Auto-aim off (optional): no global switch. Per weapon: clear the lock fields each tick (Lancer state =
*(gun+0x68): +0x00 target, +0x30 candidate, +0x34 timer) and clear hero+0x1450 to stop the L2+R2 lock. Pointing
+0x774 away (e.g. -1.5) kills the Lancer/target_lock searches but not the +0x760-axis ones. M
Optional upper-body pitch: the arm block +0x694 pitch / +0x690 yaw (attach block, used by the wall pose,
-30..-70 deg raises the gun) or torso +0x114: untested whether a mod write survives; L.

## 3. Up Your Arsenal (rac3) for reference
fps_aim_pos (NTSC-U 0x1A6230) = hit of a 30 u ray from the camera along its forward, or the end; guns call
aim_bend_to_point in lock-strafe and first person: velocity = speed*dir(muzzle -> fps_aim_pos), or
hero_fire_dir turned toward it by at most 80 deg (20 for the scoped Flux; a tenth within 4 u)
(rac3 config/names.tsv:117, 1463). Assist is camera friction toward the fps_cam_find_target target (30 u,
15 deg) plus lock_target; shots are not auto-bent to enemies in lock-strafe. Lock-strafe camera: boom 6 u
straight back from an eye 2 u above the feet, no shoulder offset (rac3-strafe-camera.md 1c). H (from names.tsv)

## 4. Crosshair placement and convergence
- Put it at the **exact screen centre** when the camera's forward (camera matrix row0, v2 0x1B8F70) is the
  view axis: any point on that ray projects to the centre. The game's own marker list draws a marker without
  a world point at g_reticle_screen_params[0..1] (0x1A7348, runtime-set; presumably the centre)
  (src/game/0026EA00-qcopy.c:99-113); the overlay API's 512x448 picture centre is (256, 224). M
- If the mod adds a shoulder offset, the centre ray no longer passes over the hero; convergence still works.
- P: cast from the camera C along f, ignore hits before t0 = dot(heroEye - C, f) (the hero/near clutter
  between camera and hero; the game's own version starts at C and skips only the hero moby), out to
  t0 + R (R = 30 like rac2/UYA guns; 75 like projectile_spawn_targeted). P = first hit, else the ray's end.
  Use the SDK's collision_line (1.17) if rac2 answers it (check for OPENRAC_HOOK_UNKNOWN), mask both level and
  mobys, ignore the hero's slot. Aim from the muzzle at P (converge); clamp the bend (rac2 20-30 deg, UYA 80).
- Optional: collision_line(M -> P); if it hits short of P, draw a second dot at project(hit) (blocked shot).

## Not determined
- Data addresses in levels other than L00; whether the overlay/collision_line calls are present for rac2.
- Exact on_frame vs game-tick ordering beyond the prior notes' claim (writes after the tick are read next tick).
- Visual result of the arm/torso manipulator writes; g_reticle_screen_params values; meaning of the per-item
  flag 0x20 that gates the L2+R2 lock (v1 symbol g_movie_seen_flags, probably misnamed).
