# rac2: every weapon's first-person reticle, and what a third-person mod has to read to redraw it

Sources: rac2-decomp v1.01 C (`src/levels/shared`, `src/game`), the v2.00 disc's L00 code disassembled with
throwaway disassembly scripts (not kept), the v2 boot image. Nothing was run.
Confidence: **H** read in C and checked in the v2 asm, **M** read in C only or inferred, **L** guess.
Addresses are **L00**; each level has its own copy of these functions: resolve other levels by name in
`config/levels/functions.tsv` (v1) / `config/us2/levels/functions.tsv` (v2). Hero = 0x189E20 on both discs.
Data addresses are as the L00 code reads them in the v2 dump. Pass level and boot data through the
mod's relocate calls, as the mod already does for the marker list (v2 0x1B7580 -> 0x1BBC40).
Builds on rac2-lobbed-aim-reticle.md (section B) and rac2-weapon-aim.md.

## 0. Method: the list of every marker call is complete
- In level code, `hud_add_moby_marker` is a level-local copy: v1 L00 0x2DB0D0, v2 L00 0x2DB498
  (boot v1 0x273318 / v2 0x273328 is only called from boot copies of Tesla Claw, Swingshot and Dynamo).
  A scan of every `jal` in v2 L00 found **30 call sites in 21 functions** (H). The weapons among them are
  in the table. The rest are the wrench, the Swingshot, the Dynamo and the Hypnomatic (gadgets).
- The only other HUD reticle drawing is `DrawTargetReticle` (boot 0x272CB0, L00 copy v2 0x2DAE30). It is
  called only from `hud_draw_reticle_and_markers`, every frame, in all modes, and draws the
  **hero+0x1450 (L2+R2) lock** with a spinning ring and a health arc (fields hero +0x1450 requested,
  +0x1480 current, +0x1484 fade, +0x1488 spin; src/game/0026EA00-target.c:6-13,83-99). It already shows in
  third person. `gfx_draw_screen_sprite` and `AppendRotatedSpriteQuad` have no other callers in L00 (H).
- Weapons with no call in that list have **no reticle in first person either** (H for "no HUD marker").
  Their world-space aim effects (lob landing markers etc.) are drawn every tick in every mode (section 4).

## 1. The marker entry, corrected field meanings (src/game/00273318-g8.c:27-62, src/game/002729A0-rc2.c:33-80)
Call ABI: `a0 moby, a1 colour, a2 pos, a3 type, t0 arg, t1 (4, unused), f12 scale, f13 rot, f14 (90, unused)`.
Every weapon passes `arg = -1`, `rot = 0`, and a type in 31..39. For those types the function stores
+0x2C = 2, +0x28 = pi/2, +0x26 = 0 (H, also v2 boot asm 0x2733B0..).
Entry (0x30): +0x00 pos, +0x10 f32 scale (sprite = 40*scale square), +0x14 colour 0xAABBGGRR,
+0x18 type (effect texture), +0x1C f32 **sprite rotation in radians** (the "size" argument; 0 for every
weapon), +0x20 moby (skipped if 0 or moby+0x20 is 0xFE/0xFD), +0x24 s16 hasPos, +0x26 s16 **copy count**
(draw mode 0 only), +0x28 f32 **rotation step between copies** (mode 0 only), +0x2C **draw mode**
(0 = +0x26 copies, each rotated +0x28 further; 1 = 2x2 tile set; 2 = one sprite centred on the point).
With mode 2, only +0x10/+0x14/+0x18/+0x1C matter. H for the stores; M for "rotation" (the 5th float of
AppendRotatedSpriteQuad, by its name).

**Alpha cap**: the function caps alpha to `g_moby_marker_max_alpha` = **0x40** (v2 L00 reads gp-0x6670 =
0x1A8980, initial value 0x40; boot 0x1A8630 = 0x40 too). It has no writer in L00 or boot code (scan: one
reader each). So the game stores 0x400000FF / 0x400FFF0F for every weapon colour (0x60.. and 0xFF.. alike).
A mod that writes entries directly must apply the cap itself to match (M-H).

So no weapon reticle rotates, spins or scales over time. The only "animations" are: colour flips
(cooldown), the Pulse Rifle / Vaporizer blinking overlay (section 3.7), and target markers whose scale
follows the camera distance.

## 2. Summary
Item ids and gun classes come from the weapon table (row = g_itemEquippedSlot[item]; +0x14 moby class,
+0x4A next variant row; read from the v2 boot image at 0x239B20 and the L00 data at 0x2624F0, same values).
Class 3310 (HK-22) is the **Seeker Gun's upgraded gun** (item 24, row 67). weapon_visuals.h does not list it.
"State" = the gun moby's vars block, `*(gun + 0x68)`, gun = held weapon moby `*(hero + 0x1220)` (slot 0 moby).
"red" = 0x600000FF, "green" = 0x600FFF0F (both stored as alpha 0x40). Every timer is float seconds,
-1/60 per tick, clamped at 0 (vec_fast_helper_b, nonmatching/functional/func_002829D8.c).

| weapon (item: classes) | update fn v1 / v2 L00 | game draws centre reticle when | type / scale | colour rule (state field) | target markers (already shown in 3rd person?) | verdict |
|---|---|---|---|---|---|---|
| Lancer / Heavy Lancer (30: 1490 / 3077) | lancer_update 0x3A6C30 / 0x3A8060; Heavy via wrapper 0x3E4948 / 0x3E5DC8 | state 29 or +0x1500 | 38 / 1.0 | red if state+0x0C > 0 and ammo | auto-aim lock (state+0x00), searched only while +0x1500 == 0: **yes** | port |
| Blitz Gun / Blitz Cannon (26: 1489 / 2801) | blitz_gun_update 0x3A53E8 / 0x3A6818; UpdateSpreadWeapon 0x3D1D28 / 0x3D3178 | state 29 | 33 / 1.3 | red if state+0x00 > 0 and ammo | none | port |
| Lava Gun / Meteor Gun (29: 1481 / 3161) | 0x419C98 / 0x41B198; 0x3EA540 / 0x3EB9C0 | never | - | - | none | no reticle |
| Plasma Coil (28: 3146) | UpdateChargedWeapon 0x3E8188 / 0x3E9608 | state 29 and gun state 1 | 38 / 1.0 | red if state+0x00 != 0 and ammo | cone target while ready: **yes** | port |
| Plasma Storm (28: 1482) | charged_beam_weapon_update 0x3A0F30 / 0x3A2360 | state 29 and gun state 1 | 38 / 1.0 | red if state+0x00 != 0 and ammo | none | port |
| Minirocket / Megarocket (27: 1487 / 4100) | minirocket_tube_update 0x3A48D0 / 0x3A5D00; megarocket_cannon_update 0x40F6A0 / 0x410BA0 | state 29 **and hero moby hidden** | 39 / 1.25 | red if cooldown > 0 and ammo (Mini +0x00, Mega +0x1C) | lock (75 u): **yes**, wider cone | dynamic, leave alone (the centre reticle itself is static) |
| Seeker Gun / its upgrade HK-22 (24: 1486 / 3310) | seeker_gun_update 0x3A3F30 / 0x3A5360; hk22_update 0x3F3D70 / 0x3F51F0 | state 29 | 39 / 2.0 | red if state+0x00 > 0 and ammo | none (homing uses the L2+R2 lock, drawn by DrawTargetReticle) | **port** (no target-spawned reticle) |
| Pulse Rifle / Vaporizer (23: 1483 / 3076) | UpdateScopedWeapon 0x3A2EE0 / 0x3A4310; vaporizer_update 0x3E3A18 / 0x3E4E98 | **+0x1500** and hero+0x2534 == 0 (Pulse: and gun state != 0) | 39 / 2.0, plus blinking 32 / 1.5 overlay | red if state+0x04 > 0 and ammo; overlay 0x400000FF | crosshair-over-target test: **aim mode only** | port, mod computes the blink |
| Hoverbomb / Tetrabomb (25: 2651 / 3631) | hoverbomb_gun_update 0x3C3418 / 0x3C4860; tetrabomb_gun_update 0x3FE4C8 / 0x3FF9C8 | state 29 and hero+0x2534 timer expired | 31 / 1.0 | red if timer > 0 **or no ammo or gun state != 1** (Hover +0x08, Tetra +0x00) | none | port |
| Chopper / Multi-Star (22: 2169 / 2806) | chopper_update 0x3B5BF8 / 0x3B7038 | +0x1500 or state 29 or **state 1** | 38 / 1.0 | red if state+0x0C > 0 and ammo | weapon_update_target_lock (state+0x14), only while +0x1500 == 0: **yes** | dynamic, leave alone (centre is static) |
| RYNO II (44: 2663) | UpdateLockOnWeapon 0x3C8A90 / 0x3C9ED8 | **never** | - | - | one marker on every target in the cone, when hero+0x1450 == 0: **yes** | dynamic, leave alone (nothing to port) |
| Tesla Claw (18: 177) | tesla_claw_update 0x392530 / 0x393960 | **state 1** and hero+0x2534 timer expired | 31 / 1.0 | red only if no ammo (no cooldown) | none | port |
| Visibomb (14: 163) | visibomb_gun_update 0x38F8B8 / 0x390CB8 | never | - | - | none | no reticle |
| Zodiac (43: 3784) | charging_weapon_update 0x408E58 / 0x40A358 | never | - | - | none | no reticle |
| Spiderbot / Tankbot Glove (32: 2311 / 3706) | 0x3B7D48 / 0x3B9190; placement_object_update 0x4029E8 / 0x403EC8 | never | - | - | world landing marker, always | no reticle |
| Miniturret / Megaturret Glove (41: 3162 / 2661) | 0x3EAA58 / 0x3EBED8; 0x3C8828 / 0x3C9C70 | never | - | - | world landing marker, always | no reticle |
| Bouncer / Heavy (37: 3604 / 3742) | bouncer_update 0x3FDE48 / 0x3FF2E8 | never | - | - | world effect, always | no reticle |
| Gravity Bomb / Mini-Nuke (42: 3299 / 2650) | 0x3EE4C0 / 0x3EF940; 0x3C2FC8 / 0x3C4410 | never | - | - | world landing marker, always | no reticle |
| Bomb Glove (12: 192) | UpdateAimedProjectileLauncher 0x397E60 / 0x399290 | never | - | - | world landing marker, always | no reticle |
| Decoy Glove (17: 562) | UpdateProjectileLauncher 0x39D110 / 0x39E540 | never | - | - | world landing marker, always | no reticle |
| Shield Charger / Tesla Barrier (45: 2652 / 4498) | tesla_barrier_update 0x3C3D90 / 0x3C51D8 | never | - | - | - | no reticle |
| Synthenoids (31: 3250 / 2818) | synthenoid_update 0x3EC140 / 0x3ED5C0 (2818 "Kilonoid": v1 0x3D4A20) | never | - | - | - | no reticle |
| Sheepinator / Black (16: 2653 / 3757) | sheepinator_update 0x3C6298 / 0x3C76E0; MorphorayUpdate 0x405808 / 0x406CF8 | never | - | - | - | no reticle |
| Clank Zapper (9: 3800) | UpdateBeamDamageAndParticles 0x40BA30 / 0x40CF30 | never | - | - | - | no reticle |
| Walloper (53: 180) | UpdateLightningArcs 0x395428 / 0x396858 | never | - | - | - | no reticle |
| (Wrench, item 10, not a weapon) | wrench_update 0x38BF18 / 0x38D318 | state 1 or 29 and hero moby hidden | 37 / 1.0 | constant 0xFF917267 | - | optional |

"ammo" = `get_ammo(-1)` (v1 0x2D4040 / v2 0x2D43E0, L00_002D3798.c:468-482): item = s32 hero+0x1248
(0x18B068); row = u8 g_itemEquippedSlot[item] (0x139568); if u16 weaponTable[row]+0x8E (ammo capacity,
table v1 0x262570 / v2 0x2624F0, stride 0xE0) == 0 it returns 1, else s32 ammo[item] (0x139688 + 4*item).
Every weapon with a reticle has a capacity, so in practice "ammo" = ammo[item] != 0. H (v2 asm 0x2D43E0).

Common hero fields: +0x2294 hero state (29 = aim, 1 = L1 pre-aim; most weapon updates turn 1 into 29 via
hero_l00_set_action, Chopper and Tesla Claw do not); +0x1500 aim flag (rac2-weapon-aim.md); +0x2534 s32
aim-entry delay: weapons write 40 every tick outside aim and count it down in aim (math_fast_float_d,
nonmatching/functional/func_00282948.c: 0 -> "expired"); +0x1450 L2+R2 lock; +0xBF8 the weapon's target;
hero moby (hero+0x2290 = hero+0xC18) +0x34 bit 0 = **hidden**, set by hero_set_objects_flag_bit
(L00_002D1ED8-scan-g8.c:24-38) only while +0x22B5 (aim camera) or hero_is_action_allowed() (menu
first-person mode) (L00_002A6DD0.c:114). H.

## 3. Per weapon
In third person every weapon update below runs every tick (UpdateHeroSlotCallbacks), so **every cooldown
field is maintained in third person**: each decrement sits outside the aim checks (cited per weapon).
Only the gates and the Pulse/Vaporizer target test are aim-only.

### 3.1 Lancer / Heavy Lancer (item 30; classes 1490, 3077)
- lancer_update v1 0x3A6C30 / v2 0x3A8060 (L00_003A6C30-g8.c:85-339). The Heavy Lancer's dispatch entry
  v1 0x3E4948 / v2 0x3E5DC8 (L00_003E3258.c:266-269) is a 2-instruction wrapper that `jal`s lancer_update
  (v2 asm 0x3E5DD0). H. In-code split: class 1490 uses joint 0 as the muzzle and a 20 u lock range, else 30 u.
- State = *(gun+0x68): +0x00 locked target, +0x04 s16 its class (-1 = none), +0x06 s16 relock delay,
  +0x08 f32 target height, **+0x0C f32 cooldown** (= 1/rate at each shot, :247, :282; decremented every tick
  in gun state 1, :214 (first operand), and 2, :303), +0x10 s16 anim flag, +0x14 muzzle, +0x24 light,
  +0x28 light ticks, +0x30 lock candidate, +0x34 candidate delay, +0x3C sound slot. H.
- Centre: `state 29 || +0x1500` (:188) -> type 38 (0x26), scale 1.0, red if +0x0C > 0 && ammo else green (:189). H.
  Table rates 7.0 / 10.0 (Heavy): red for about 9 / 6 ticks after each shot. M.
- Target marker (:191-194): when +0x00 != 0, at P = target+0x10 + target up row (+0xE0) * info+0x10
  (info = private_value_0, see 3.7), type 38, colour 0xFF0FFF0F, scale = |0.9 - dist(camera pos, P) *
  0.006666667 * 0.65|. The lock search (:149-187) runs only while **+0x1500 == 0** and gun state 1, axis =
  spherical(+0x778 yaw, +0x774 pitch) from the hero + up*0.75 (rac2-weapon-aim.md). While +0x1500 is set the lock
  is cleared (:185-186). So this lock and its marker are **third-person behaviour**: already shown, and they follow the mod's
  +0x774/+0x778 writes. H.
- Verdict: **port** the centre reticle; leave the lock marker to the game. H.

### 3.2 Blitz Gun / Blitz Cannon (item 26; 1489, 2801)
- blitz_gun_update v1 0x3A53E8 / v2 0x3A6818 (L00_003A53E8-g16.c:86-); UpdateSpreadWeapon v1 0x3D1D28 /
  v2 0x3D3178 (L00_003D1D28-g64.c:112-).
- State +0x00 f32 cooldown: decremented every tick unconditionally (Blitz :114, Cannon :135); set to 1/rate
  at the shot (Blitz :283, Cannon :352 via the alias `shot_state = state`, :211). H (v2 reads +0x0 before both calls).
- Centre: `state 29` only (:115 / :136), type 33 (0x21), scale 1.3, red if +0x00 > 0 && ammo (:117-121 / :138-142). H.
- No target marker (the Blitz Gun points the shot at +0xBF8 without drawing anything). Verdict: **port**. H.

### 3.3 Plasma Coil (item 28; 3146) and Plasma Storm (item 28 upgrade; 1482)
The weapon table settles it: row 28 = class 3146 (Coil), row 62 = 1482 (Storm). H.
- **Coil**: UpdateChargedWeapon v1 0x3E8188 / v2 0x3E9608 (L00_003E8180-g8.c:79-; the same code in the other
  levels is L01_00397110-weapon-g8.c and L30_003E1E78-g8.c). Vars: +0x00 f32 timer (0.5 s at each shot,
  :163/:166; decremented every tick in gun state 1, :156 first operand), +0x04 handle, +0x10 charge, +0x14 angle.
  - Centre, in gun state 1 only (case 1): `state 29` (:129-136): type 38, scale 1.0, red if +0x00 != 0 && ammo. H.
  - Target marker (:137-155), every tick in any mode: if hero+0xBF8 is 0 (it takes +0x1450 when set, :125),
    hit = cone search along **hero+0x760** (part_start_effect_in_front_of_hero(hero+0x760, 1, 10 deg, 10 deg,
    20 u, 45 deg, 45 deg), v2 0x3E99C8; M on which value is which). If timer == 0 && hit && ammo: marker at
    hit+0x10 + view_dir(hit)*info+0x10, type 38, 0xFF0FFF0F, the same distance scale. **Already in 3rd
    person**, and it follows the mod's +0x760. H.
- **Storm**: charged_beam_weapon_update v1 0x3A0F30 / v2 0x3A2360 (L00_003A0F30-eye.c:64-). State +0x00 f32
  cooldown (1/rate on fire :144/:152; decremented every tick in case 1, :130 first operand), +0x0C charge,
  +0x10 phase angle. Centre in case 1: `state 29` (:122-129), type 38, scale 1.0, red if +0x00 != 0 && ammo.
  No target marker (it fires at +0xBF8). H.
- Verdict: **port** both centre reticles. H.

### 3.4 Minirocket Tube / Megarocket Cannon (item 27; 1487, 4100)
- minirocket_tube_update v1 0x3A48D0 / v2 0x3A5D00 (L00_003A0F30-lock.c:95-); megarocket_cannon_update
  v1 0x40F6A0 / v2 0x410BA0 (L00_0040F6A0-g16.c:109-).
- Vars, Mini: +0x00 f32 cooldown (decremented every tick :114; 1/rate at the shot :254), +0x10 target,
  +0x14 lock timer, +0x18 lock height, +0x20 candidate, +0x24 candidate delay, +0x28 aiming. H (v2 asm 0x3A5E00-0x3A5E4C).
  Mega: +0x00 target, +0x04 lock timer, +0x08 height, +0x10 candidate, +0x14 delay, +0x18 s16 aiming,
  **+0x1C f32 cooldown** (decremented every tick :166; set :315). H for +0x1C (v2 0x410CB8), M for the rest.
- Centre: `state 29 && (*(u16*)(hero moby+0x34) & 1)` (Mini :122, Mega :139) = only once the first-person
  view hides Ratchet. Type 39 (0x27), scale 1.25, red if cooldown > 0 && ammo. H.
- Lock marker (Mini :149-189, :171; Mega :169-206, :188): runs **every tick in any mode**. In aim the cone
  is 4 deg around +0x760, otherwise 18 x 17 deg (Mini) / 20 deg (Mega), 75 u, from the hero + up*0.75. A
  candidate must persist 5 ticks. Marker at target+0x10 + up*height, type 38, 0xFF0FFF0F, scale
  |0.9 - dist(camera, target pos)*0.006666667*0.65|. **Already in 3rd person.** H.
- Verdict: **dynamic/auto-aim, leave alone** (as asked). Note that the centre reticle itself is static (39/1.25 + cooldown).

### 3.5 Seeker Gun / HK-22 (item 24; 1486 and its upgrade 3310)
- seeker_gun_update v1 0x3A3F30 / v2 0x3A5360 (L00_003A3F30-g8.c:31-82); hk22_update v1 0x3F3D70 /
  v2 0x3F51F0 (catalogue name func_US2_L00_003F51EC; L00_003F3D70-g8.c:39-111, v2 C
  src/us2/levels/shared/L00_003F51EC-g8.c, identical reticle code).
- State +0x00 f32 cooldown: decremented every tick (Seeker :62, HK-22 :68, first operand); 1/rate at the
  shot (:79 / :106). H (v2 reads +0x0).
- Centre: `state 29` only (:46 / :56), type 39, scale 2.0, red if +0x00 > 0 && ammo. H.
- No target marker. Homing takes hero+0x1450 -> +0xBF8 (:54-55 / :62-63) and the seeker's own 360 deg / 8 u search;
  the L2+R2 lock is DrawTargetReticle's. Verdict: **port**. Nothing spawns on targets here. H.

### 3.6 Hoverbomb / Tetrabomb (item 25; 2651, 3631)
- hoverbomb_gun_update v1 0x3C3418 / v2 0x3C4860 (L00_003C3418-g16.c:53-); tetrabomb_gun_update v1 0x3FE4C8
  / v2 0x3FF9C8 (L00_003FE4C8-g16.c:58-).
- State, Hover: +0x00 the bomb moby, **+0x08 f32 timer**, +0x0C s16 fire delay, +0x0E s16 repeat. Tetra:
  **+0x00 f32 timer**, +0x04/+0x06 delays, +0x08 shot index, +0x10 bomb list. The timer is decremented every
  tick (:70 / :81) and set to 1/rate when fire is pressed in gun state 1 (:99 / :111). H (v2 asm 0x3C4938,
  0x3FFAC4).
- Centre: `state 29` and the +0x2534 delay has expired (:72-80 / :83-91), type 31 (0x1F), scale 1.0,
  colour **red if timer > 0 || ammo == 0 || gun state (+0x20) != 1** (gun state 2 = launch delay, 3 = a bomb
  is alive/steered), else green. H.
- No target marker. Verdict: **port**. H.

### 3.7 Pulse Rifle / Vaporizer (item 23; 1483, 3076): the reticle that "animates on a target"
- UpdateScopedWeapon v1 0x3A2EE0 / v2 0x3A4310 (L00_003A2EE0-g8.c:91-340); vaporizer_update v1 0x3E3A18 /
  v2 0x3E4E98 (L00_003E3A18-g8.c:100-302).
- State, Pulse: +0x00 s16 zoomed, +0x02 s16 fire delay, **+0x04 f32 reload**, +0x08 max fov, +0x0C fov,
  +0x10 light, +0x14 flash, ..., +0x28 shots, **+0x2C s16 locked, +0x2E s16 blink, +0x30 target**. Vaporizer:
  same up to +0x1C, then +0x20 resource base, **+0x28 s16 targeting, +0x2A s16 targetFrames, +0x2C target**
  (different layout). H (v2 asm 0x3A452C-0x3A462C and 0x3E50A4-0x3E51D4).
  Reload: 1/rate when the beam fires (Pulse :297, Vapor :268), decremented only in gun state 3 (:305 / :273),
  which follows every shot in any mode. Maintained in 3rd person. H.
- Gate: **+0x1500** (not state 29) (Pulse :135 also needs gun state != 0) and **hero+0x2534 == 0** (the
  40-tick zoom-in delay) (Pulse :154, Vapor :164). H.
- Centre: type 39, scale 2.0, red if reload > 0 && ammo (Pulse :162, Vapor :170-172). H.
- **Blinking overlay**, queued *before* the centre reticle:
  1. Target test, only inside the +0x1500 block and only on ticks where `g_frame_l00_counter & 4`
     (4 ticks of 8; v2 dump gp 0x1B1948): clear target/locked; CollLine(camera pos, camera pos +
     128*camera fwd, mask 0x10, skip the hero moby) (v2: pos 0x1B8D40, fwd 0x1B8F70, result moby 0x1BF198).
     Hit moby m is a target if m+0x31 != 0 and (class m+0xAA in 500..540 inside the moby table
     [moby_is_in_special_class_range, src/game/002AA800.c:457-468], or info != 0 && info+0x00 health > 0
     && *(s16*)(*(m+0x24) + 0x46) == 5). info = private_value_0(m) = (m+0x34 & 0x20) ? *(u32*)*(m+0x68) : 0
     (src/game/002AA800.c:268-272). (Pulse :136-153, Vapor :147-163.) H.
  2. If locked: blink++ ; blink > 20 -> 0. If reload <= 0 && ammo && blink >= 11: marker type 32 (0x20),
     scale 1.5, colour 0x400000FF (red), at the centre (Pulse :155-161, Vapor :165-169). It shows for 10 ticks
     of every 21 while the crosshair rests on a target and the gun is ready. H.
- **None of the target logic runs in 3rd person.** The mod must do the ray test and the counter itself.
  Verdict: **port** (centre + overlay). H.

### 3.8 Chopper / Multi-Star (item 22; 2169, 2806)
- chopper_update v1 0x3B5BF8 / v2 0x3B7038 (L00_003B5BF8-dbg.c:48-126). State: +0x00 shot slot data,
  **+0x0C f32 cooldown** (decremented in gun state 1, :97 first operand; 1/rate at :101/:116), +0x10 secondary
  delay, +0x14 locked target, +0x18 candidate, +0x1C s16 acquire delay (4), +0x1E s16 relock hold-off (2).
- Centre (:58-63): `+0x1500 || state 29 || state 1`, type 38, scale 1.0, red if +0x0C > 0 && ammo. H (v2 0x3B707C-0x3B7098).
- Lock: weapon_update_target_lock v1 0x3B6158 / v2 0x3B7598 (L00_003B6158.c:22-54), first in the update (:57).
  Search only when +0x1500 == 0 (field `blocked`), gun state 1, not +0x24EC, and the hold-off ended; cleared
  while +0x1500 is set. Axis spherical(+0x778, +0x774), 10 deg (17.5 deg for the Oozla swamp-monster arms),
  30 u. Marker at the target + up*height, type 38, 0xFF0FFF0F, distance scale. **Already in 3rd person.** H.
- Verdict: **dynamic, leave alone** (as asked).

### 3.9 RYNO II (item 44; 2663)
- UpdateLockOnWeapon v1 0x3C8A90 / v2 0x3C9ED8 (L00_003C6298-charge-g8.c:108-). Vars +0x00 f32 cooldown (:128,
  :259), +0x10 target.
- **No centre reticle in any mode.** Every tick: SelectBestTargetInCone(gun pos, +0x760, 25 x 25 deg, up
  to 32 targets, 48 u) (:156); if hero+0x1450 == 0, one marker per target (:162-176), type 38, 0xFF0FFF0F,
  distance scale. **Already in 3rd person**, following the mod's +0x760. Verdict: **dynamic, leave alone.** H.

### 3.10 Tesla Claw (item 18; 177)
- tesla_claw_update v1 0x392530 / v2 0x393960 (L00_00392530.c:48-). Gate (:64-67): **hero state == 1** (it never
  turns 1 into 29) and the +0x2534 delay expired. Type 31, scale 1.0, **red only if ammo == 0**, else green.
  There is no cooldown. H (v2 0x393A10-0x393A24). (The boot copy, v1 0x309718 / v2 0x30A528 (catalogue name func_US2_0030A510), calls a stub for
  ammo and would always be red, but the level copy is the one the class dispatch uses.)
- Verdict: **port**. H.

### 3.11 No reticle (no marker call; H)
Lava Gun/Meteor, Visibomb, Zodiac, Spiderbot/Tankbot, Miniturret/Megaturret, Bouncer, Gravity Bomb/Mini-Nuke,
Bomb Glove, Decoy Glove, Shield Charger/Tesla Barrier, Synthenoids, Sheepinator/Black Sheepinator, Clank
Zapper, Walloper (addresses in the table). None of them draws a HUD reticle in first person. The lobbed ones register a
world-space landing effect every tick in all modes (Gravity Bomb weapon_draw_aim_effect when state+0x24
!= 0, L00_003EE4C0-g8.c:115 / L00_003EE100.c:195-200; Bomb/Decoy L00_0038F178.c:446/:505; Miniturret
L00_003EAA58.c:79; Nuke L00_003C2FC8-g8.c:116; Spiderbot L00_003B3158-pickup-g8.c:132; Bouncer
L00_003FDE48-g8.c:133). Only their reach changes in state 29 (11 u, rac2-lobbed-aim-reticle.md). M on
"landing marker" (each callback was not opened, only seen to be registered unconditionally).

### 3.12 Wrench (item 10, bonus)
wrench_update v1 0x38BF18 / v2 0x38D318 (L00_0038BF18-g8.c:151-156; v2 C src/us2/levels/shared/
L00_0038D318-g8.c:132): gun state 0, `(state 1 || state 29) && hero hidden`: type 37 (0x25), scale 1.0,
constant 0xFF917267. H.

## 4. Things that only exist in aim mode (the mod must compute them itself)
1. **Pulse Rifle / Vaporizer target blink** (3.7): the camera ray, the validity test and the blink counter
   run only with +0x1500. Use the mod's own crosshair ray (camera eye along camera forward). The game's
   ray starts at the camera position and is 128 u long. Apply the 3.7 test to the hit moby and keep a
   0..20 counter. Show the overlay at counter >= 11 while reload <= 0 && ammo.
2. **The gates** themselves: +0x1500 (Pulse/Vapor), state 29 (most), state 1 (Tesla Claw, Chopper), hero
   hidden (Mini/Megarocket, wrench), +0x2534 == 0 (Pulse, Vapor, Hover, Tetra, Tesla). In 3rd person every
   weapon rewrites +0x2534 = 40 each tick, so the game's own delay never runs out. The mod should skip that
   delay, or keep its own 40-tick counter after the aim starts if it wants the vanilla fade-in.
3. Everything else the reticles read is maintained in 3rd person: the cooldowns (cited above), the
   ammo counts, and the gun state byte +0x20 (Hover/Tetra red while a bomb is out).

## 5. Mod recipe (per frame, after the game tick, held gun = *(hero+0x1220), class = gun+0xAA)
- Skip unless the class is one of: 1490/3077, 1489/2801, 3146, 1482, 1486/3310, 1483/3076, 2651/3631, 177
  (plus 1487/4100 and 2169/2806 only if the centre reticle is wanted; RYNO has none).
- st = *(gun+0x68); ammo = ammo[item] != 0 (or capacity 0). Colour per the table, then **alpha = 0x40**.
- Pulse/Vaporizer: first queue the overlay {type 32, scale 1.5, colour 0x400000FF} when 3.7 says so.
- Entry: +0x10 scale, +0x14 colour, +0x18 type, +0x1C 0.0, +0x20 gun, +0x24 0, +0x26 0, +0x28 pi/2,
  +0x2C 2; count++ (skip when count >= 6, game mode 2, or when the game itself queued an entry for the gun
  this frame (aim mode)). The target markers are queued by the game in 3rd person and share the 6 slots.
- Vanilla rule for "red": cooldown > 0 **and** ammo (empty = green) for every weapon except
  Hover/Tetra (red when empty or a bomb is out) and Tesla Claw (red only when empty).

## Not determined
- What effect textures 31/32/33/37/38/39 look like (the mod has exported them).
- Per-level data addresses other than L00 (the camera/collision/frame-counter values are L00 dump
  addresses); the function addresses for other levels are in the tsv tables.
- Rates quoted (Lancer 7, Heavy 10, Seeker 2, Pulse/Vapor 1, Hover 3, Blitz 2, Cannon 2.6, Mini 1.7,
  Mega 4, Plasma Storm 1, Chopper 1.8) are the static table values (L00 data 0x2624F0 / boot 0x239B20).
  Upgrades and Mega rows may change them at run time. M.
