# rac2: why some weapons won't aim low in third person, and how the mod can fix each

Sources: rac2-decomp v1.01 C (`src/levels/shared`, `src/game`), plus a rabbitizer disassembly of the v2.00 disc's
L00 (throwaway disassembly scripts and a dump of the L00 image, not kept). Nothing was run in game.
Confidence: **H** = read in the C and checked in the v2 asm, **M** = read in the C only or inferred, **L** = guess.
Addresses are L00 (each level has its own copy of the code: resolve it per level). Hero = 0x189E20 on both discs.
Builds on rac2-weapon-aim.md, rac2-weapon-reticles.md and rac2-lobbed-aim-reticle.md. It also corrects one line in
rac2-weapon-aim.md: the Blitz does **not** fire along +0x760 (see 1).

## 0. Short answer
- A weapon in third person takes its direction from one of three places. The mod writes only the first one:
  1. **+0x760, aim forward.** This covers the straight shots: Lancer, Heavy Lancer, Pulse Rifle, Vaporizer, Seeker, HK-22,
     Plasma Coil, Plasma Storm, Minirocket, Megarocket, RYNO II, Hoverbomb and Tetrabomb. In third person the game has no
     vertical limit on these. The only limit is the mod's own `kAimPitchLimit` (70 deg).
  2. **+0x730..+0x75F, the "weapon basis"** (rows: forward, left, up). In third person,
     update_hero_held_items_and_orientation copies the hero body's own matrix into these rows every tick, so they are
     **flat**: they turn with his yaw only. This group is the **Blitz Gun, Blitz Cannon, Tesla Claw, Sheepinator, Black
     Sheepinator and Meteor Gun**, plus the lobbed weapons, which the mod already handles through row 0.
  3. **The gun moby's own barrel** (gun +0xC0 matrix, -Y row). This follows the hand, so it follows the arm and torso
     pose. This is the **Lava Gun's** stream, and **every** weapon while +0x24EC (the "gun against a wall" flag) is 1.
- So the Blitz's "limit" is not a clamp. Its 20 damage rays go out in a random cone around the flat +0x730, out to 18.75
  deg (Gun) or 30 deg (Cannon). Nothing lower than that, measured from the muzzle, can be hit. Meanwhile its *visuals*
  (the Gun's charge flash, the Cannon's lightning strips) are drawn along +0x760, so they follow the crosshair. That is
  why the shots seem to stop going lower while the crosshair keeps going down.
- Best fix for groups 1 and 2: after each tick, write the whole basis at +0x730/+0x740/+0x750 as an orthonormal frame
  whose row 0 is the mod's aim direction D. Do it for the group-2 classes only. The weapon reads these rows in the next
  tick **before** the game rewrites them (2). H for the mechanism, M for side effects.
- No angle limit on this path is data the mod could patch. The cone widths, the torso clamps and the wall-pose clamps
  are literals in the C, so they are compiled into the native port's code (4).

## 1. Blitz Gun (blitz_gun_update v1 0x3A53E8 / v2 0x3A6818) and Blitz Cannon (UpdateSpreadWeapon v1 0x3D1D28 / v2 0x3D3178)

### Call order in one tick (hero_l00_update_frame_2, L00_002A6DD0.c, v2 0x2A6DD0) H
- :100 cam_update_blend_angles: clears +0x24EC, then sets it to 1 if a wall is within 1.3 u (L00_002A6420.c:485-542).
- :102 UpdateHeroAutoAimAngles: sets the torso and head targets (section 3).
- :107 attachment_update_all: eases the joint manipulators.
- :110 hero_update_items_and_flags (v2 call at 0x2A6F60): this runs update_hero_item_mobys (the gun goes onto the hand
  joint, using the hero's joint cache at +0xC50), then UpdateHeroSlotCallbacks, which **runs the held weapon's update**.
- :113 hero_update_items_frame (v2 call at 0x2A6F78): this runs update_hero_held_items_and_orientation (v2 0x2AF8D8,
  L00_002AE388.c:274-287). It rewrites +0x730/+0x740/+0x750 = hero moby rows +0xC0/+0xD0/+0xE0 (the hero's own matrix,
  copied by update_hero_moby, L00_002A9460.c:48), +0x760 = row 0, and +0x770 = hero rotation.
- :121 UpdateHeroAimAndTransitions. Only in aim mode (+0x1500) does it call UpdateHeroHeldObject, which writes the
  **camera** rows into +0x730..+0x75F and the camera forward into +0x760 (L00_002D3798-g8.c:184-186). This is why the
  Blitz aims correctly in first person.
- Within L00 code, the scan found no other writer of +0x730..+0x75F (a reference scan, not kept). So a write made
  after the tick is what the next tick's weapon update reads. H for the order. M that no writer exists anywhere else
  (the scan follows lui chains only and is known to miss some readers, for example the Plasma Storm's).

### Blitz Gun (class 1489 = 0x5D1, item 26), L00_003A53E8-g16.c H
- Trigger (:134-169): spends the ammo and sets stage 2 with a startup count (state+6). The rays fire one or more ticks
  later (:186-187), so the direction is read **on the firing tick, not the trigger tick**.
- Target (:126-127, :189-192): if hero +0x1450 (the L2+R2 lock) is set, +0xBF8 = +0x1450. If +0xBF8 is set, +0x760 is
  set to the direction from the muzzle (state+0x10) to the target and **copied into +0x730**. This is the only use of
  +0x760 for aiming. v2 0x3A6C30-0x3A6C58.
- Stage 2 (:193-204): state+0x30 = **+0x760** (the flash and ring visual axis, beam_draw_ring_and_core). v2 0x3A6C94.
- Fire (:220-283):
  - The 1.5 u close-hit sphere sits at the gun's -1.35 * +0xD0 (:220-232).
  - The origin is gun joint 0 (:235).
  - With +0x24EC == 1, the rays use the **gun's own rows**: forward = -gun+0xD0, axis = -hero+0xC0 (:236-240).
    Otherwise forward = **hero +0x730**, spin axis = **+0x740**, bias = -0.1 * **+0x750** (:241-245).
    v2 0x3A6EF0-0x3A6F70.
  - 20 rays (:257-276). Each is turned about +0x740 by `math_random_float_range(0.01, 0.3272492588)` rad (0.6 to
    18.75 deg, v2 0x3A7018 `lui 0x3EA7`), then rolled about forward by a running random angle (step 60 deg +-50%).
    Each ray is 32 u long (v2 0x3A70C4 `lui 0x4200`), and beam_trace_and_apply_damage traces it at once.
  - state+0x30 = forward (:247).
- Unused: `frame` = basis * euler(g_blitz_gun_euler_angles) is computed but never read.

### Blitz Cannon (class 2801 = 0xAF1, item 26 upgrade), L00_003D1D28-g64.c H
- It has the same structure as the Gun. Stage 2: q30 = g_strip_axis = **hero +0x760** (:215, v2 0x3D3610).
- Fire (:307-347):
  - +0x24EC == 1: gun rows. Otherwise forward = **+0x730**, axis = +0x740, bias = -0.1 * +0x750 (:310-320, v2
    0x3D3B48-0x3D3BD8).
  - 20 rays (:329-347), turned 0.01 to 0.52359885 rad (0.6 to 30 deg, v2 0x3D3C70 `lui 0x3F06`), 38 u long (v2
    0x3D3D1C `lui 0x4218`), traced by weapon_fire_damage_ray.
- Visual only: InitEffectStrips draws the lightning strips along g_strip_axis (+0x760), turned about +0x740 by -(10..24)
  deg and about +0x750 (L00_003C6298-strip.c:58-65; v2 refs 0x3D40E8/0x3D41D0/0x3D4210). lightning_update_arcs and
  RenderRadialRingAndQuad read the same rows.

### Every vertical limit on the Blitz's path in third person
| limit | where | what it reads | patchable? |
|---|---|---|---|
| direction = +0x730 row 0, which is flat in 3P | blitz :242, cannon :316; written at L00_002AE388.c:278-282 | hero moby rows (the hero matrix, yaw only; small tilt only in special states, hero_update_model_tilt) | mod can write the field (fix A) |
| random cone 18.75 / 30 deg around it | blitz :259, cannon :330 | C literal | no (code) |
| ray length 32 / 38 u | blitz :267, cannon :337 | C literal | no |
| wall case: gun's own rows | blitz :236, cannon :310 | +0x24EC, set in-tick at L00_002A6420.c:542 | no (it is recomputed each tick before the weapon runs) |
| lock: straight at the target | blitz :189-192 | +0x1450 -> +0xBF8 | (vanilla behaviour) |
| mod's kAimPitchLimit 70 deg | mod.cpp aim_frame | applies to +0x760, which the Blitz rays ignore | mod constant |

The Blitz does **not** read the camera, +0x774/+0x778, the torso/arm manipulator angles, or any +0x14xx field, in
third person. The torso pose moves only the rays' origin (gun joint 0) and, in the wall case, the gun rows. H.

## 2. The hero's torso, head and arm aiming (body manipulators)
- Block: g_hero_item_attach_records = g_attachment_records, **v1 0x25BE10 / v2 0x25BD90** (level data, L00; v2
  attachment_update_all loads it at 0x2A5378). It holds 40 records of 0xB0 bytes. H.
  - Record fields: +0x40/+0x44/+0x48 current euler (x, y = pitch, z = yaw); +0x50.. angular velocity; **+0x60/+0x64/+0x68
    target euler**; +0x70 position offset, +0x80 its velocity, +0x90 its target; +0xA0 s16 joint; +0xA2 s16 owner kind
    (0 = hero); +0xA4/+0xA8 ease parameters; +0xAC scale.
  - attachment_update_record (L00_002A5048-qcopy.c:52-73) eases current toward target with
    math_approach_with_velocity(.., a = +0xA4, b = +0xA8). It has **no clamp**, and it **clears the targets
    (+0x60, +0x90) after every update**. H.
  - Hero records, as the v2 data image initialises them:
    - rec 1 (v2 0x25BE40), joint 10: torso pitch. The target is block +0x114, in radians, **+ = down**.
    - rec 2 (v2 0x25BEF0), joint 11: torso yaw. The target is block +0x1C8.
    - rec 3 (v2 0x25BFA0), joint 4: head, pitch +0x274 and yaw +0x278.
    - rec 8 (v2 0x25C310, joint 13, targets +0x5E0/+0x5E4) and rec 9 (v2 0x25C3C0, joint 12, targets +0x690/+0x694):
      the gun arm's wall pose.
    - The joint numbers are H. The body-part names are M.
- Writers and their clamps:
  - **UpdateHeroAutoAimAngles** (v1 0x2ABBA0 / v2 0x2ABC18, L00_002A6420-target-g8.c:50-168). H.
    - Every 7 ticks it picks a look target into +0x2490: within info+0x28 u, at most 110 deg off his heading, at most 60
      deg up or down, with line of sight (:71-115).
    - It aims at +0xBF8 (only when +0x1450 is 0), or else at that look target. It clamps yaw to +-59 deg (1.0297) and
      pitch to **-25..+35 deg** (-0.4363..0.6109, + = down) (:139-146). When hero s16 +0x1DC >= 28 it clamps to +-17
      deg yaw and pitch 0 instead (:147-153).
    - With a weapon target it writes torso pitch rec 1 and torso yaw rec 2, at ease rates 0.037/0.3. Otherwise it writes
      head pitch, head yaw * 0.7, and torso yaw * 0.385 (:154-165).
    - All of these constants are `lui` immediates in v2 (0x2AC02C-0x2AC0EC).
  - UpdateHeroPitchAndYawEasingTargets (L00_002AB420-g8.c:79-88): with a weapon out (+0x22B8) or a target (+0xBF8) it
    only sets the ease rates (0.03/0.2) and returns, so it writes **no** torso target.
  - cam_update_blend_angles (L00_002A6420.c:485-542): wall pose. Arm pitch is scale * -70 deg, clamped to -70..-30 deg
    (it raises the gun). Arm yaw goes up to 40 deg for items 27 and 44. Sets +0x24EC = 1.
- **Do shots read these eased values? No.** In third person no shot reads the records, +0x774/+0x778, or any "current
  aim" field. The pose matters only through the gun moby: its muzzle (joint 0) for every weapon, its barrel for the Lava
  Gun, and its rows in the wall case. In third person nothing pitches the torso unless the weapon has a target (+0xBF8).
  So the Lava Gun's vertical range is "whatever the held animation does". H for "not read", M for the pose part.
- +0x14xx: +0x1450 is the L2+R2 lock. +0x1480.. is its reticle. +0x14E0 is the menu first-person mode's aim vector
  (rates +0x14F0/+0x14F4). +0x14F8 is the view-model blend. +0x1500 is the aim flag. None of them is read by
  third-person shots. H (rac2-weapon-aim.md, rac2-aim-camera.md).
- Code or data? The clamps (-25/+35, +-59, +-17, -70/-30) and the Blitz cones are C literals, so they are compiled into
  the native port: **a mod cannot change them**. The record targets and rates are data in game memory: a mod can write
  them, but the game rewrites the rates every tick. Rule of thumb from the follow-camera work: a value the C reads through
  an extern symbol (D_/g_) lives in game memory and can be patched; a C literal cannot. M.

## 3. Survey: what each held weapon aims with in third person
"3P" means hero state != 29 and +0x1500 == 0, which is where the mod's Modern aim works. Each weapon is listed with its
update function, then its gun class, then where it aims from (source, file:line), then its vertical limit and the fix.
"Basis rows" means the full +0x730/+0x740/+0x750 frame (fix A).

**Straight shots that read +0x760.** None of them is limited by the game in 3P.
- **Lancer**: lancer_update v1 0x3A6C30 / v2 0x3A8060 -> CreateTargetedMovingEffect, class 1490. Aims along +0x760
  (L00_003E2180-g8.c:51-66, v2 ref 0x3E3790). Shot moby 3070 (0xBFE), velocity at vars+0x20. Limit: only the mod's 70
  deg. Fix: nothing (widen kAimPitchLimit if wanted). H.
- **Heavy Lancer**: same update (via a wrapper) -> SpawnAimedShot, class 3077. Aims along +0x760
  (L00_0041ABB8-shot-g8.c:74-93, v2 0x4201BC). Shot 3209 (0xC89), velocity at vars+0x20. Same limit and fix. H.
- **Pulse Rifle** (UpdateScopedWeapon -> beam_fire_with_muzzle_flash, class 1483) and **Vaporizer** (vaporizer_update
  -> AimAndSpawnBeamWithMuzzleFlashes, class 3076): +0x760 (L00_003A3B58-g8.c:37-51; L00_003E4508-g8.c:43-53).
  Same limit and fix. H.
- **Seeker Gun** (seeker_gun_update -> hero_fire_effect_from_facing, class 1486): +0x760 (L00_003AB8B8.c:38-44). Its
  seeker (3329) homes afterwards. Same limit and fix. H.
- **HK-22** (hk22_update, class 3310): +0x760 (L00_003F3D70-g8.c:74-82). Same limit and fix. H.
- **Plasma Storm** (charged_beam_weapon_update, class 1482): +0x760 (L00_003A0F30-eye.c:186-192; v2 0x3A2884). Same
  limit and fix. H.
- **Plasma Coil** (UpdateChargedWeapon, class 3146): +0x760, then a nearby moby or a 10 deg cone hit along +0x760
  (L00_003E8180-g8.c:183-208). Same limit and fix. M.
- **Minirocket** (minirocket_tube_update -> weapon_spawn_muzzle_effect -> projectile_spawn_targeted, class 1487):
  +0x760 (L00_003AB9C0-g8.c:136-143). Rocket 1498: basis at vars+0x30, moby rows +0xC0/+0xD0/+0xE0. Same limit and
  fix. H.
- **Megarocket** (megarocket_cannon_update -> projectile_spawn_targeted via charge_shot_start_effect /
  sequenced_child_spawn, class 4100): +0x760. Same limit and fix. M.
- **RYNO II** (UpdateLockOnWeapon -> SpawnAimedProjectile, class 2663): +0x760 (L00_004096B0-g8.c:269-272). Rocket 3799,
  orientation in its moby rows. Same limit and fix. H.
- **Hoverbomb** (hoverbomb_gun_update -> moby_fire_shot, class 2651): +0x760 at launch (L00_003B9CB8-g8.c:144-147).
  The bomb is then steered by the player. Same limit and fix. H.
- **Tetrabomb** (tetrabomb_gun_update -> weapon_compute_spread_shot_velocity, class 3631): +0x760, spread axis +0x750
  (L00_003F8E08-e920-g8.c:54-64). Same limit and fix. H.

**Weapons that read the flat basis (+0x730).** These are the limited ones.
- **Blitz Gun** (blitz_gun_update, class 1489) and **Blitz Cannon** (UpdateSpreadWeapon, class 2801): +0x730/+0x740/
  +0x750 (section 1). **Limited**: flat, with an 18.75 / 30 deg cone. Fix A (basis rows). H.
- **Tesla Claw** (tesla_claw_update v2 0x393960, class 177): beam end = joint 0 + **+0x730** * 12 or 18 u
  (L00_00392530.c:125-136; v2 0x39425C). **Limited: flat.** Fix A. H.
- **Sheepinator** (sheepinator_update v2 0x3C76E0, class 2653): (1,0,0) * **+0x730 basis**, which gives the yaw and pitch
  of its target search (L00_003C6298-gun-g8.c:133-148; v2 0x3C7914). **Limited: flat.** Fix A. H.
- **Black Sheepinator** (MorphorayUpdate, class 3757): (1,0,0) * **+0x730 basis** (L00_00405808-g8.c:121-128).
  **Limited: flat.** Fix A. M (the v2 copy was not matched).
- **Meteor Gun** (meteor_gun_update v1 0x3EA540 / v2 0x3EB9C0, class 3161): each meteor (class 2862 = 0xB2E) comes from
  projectile_spawn_small (L00_003D37E8-g8.c:117-168).
  - With +0xBF8 set it goes straight at that target.
  - Otherwise weapon_throw_along_camera_arc (v2 0x3DD610, L00_003DC1C0-rc2.c:45-50) takes over. First it looks for a
    target within 8 u and 22.5 x 22.5 deg of his facing. Then it calls **CalculateAimedLaunchVelocity** with no source
    object, which means **+0x730 row 0**, at reach rand(8, 15.5) u, speed 15/60 u a tick and gravity 11/3600. With no
    target it adds a random +-11.25 deg about the hero's up. The result goes to the meteor's vars+0x00.
  - The barrel vector (-gun Y) is used only for the target case.
  - **Limited**: a flat lob 8 to 15.5 u ahead. Fix: the lob solver (fix C1) or a velocity rewrite after the spawn (C2).
    M.

**Other aim sources.**
- **Lava Gun** (lava_l00_gun_update v1 0x419C98 / v2 0x41B198, class 1481): the stream vector = the **gun's -Y row**
  (gun +0xC0 matrix) + 0.02 z. When hero state != 0 it is turned -5.6 deg about the hero's +0xD0. It points straight at
  +0x1450 when that is set (L00_00419C98-g8.c:121-134). It is appended to the ring: AppendVectorRingEntry, entry +0x10 =
  vector. **Limited by the arm/torso pose**: no pitch in 3P. Fix B (ring rewrite), or the torso write (L). H for the
  source.
- **Lobbed weapons** (Bomb Glove, Decoy, Gravity Bomb, Mini-Nuke, Mini/Megaturret, Spiderbot/Tankbot, Bouncer):
  CalculateAimedLaunchVelocity uses +0x730 row 0 * max reach, or the L1 ground-plane branch
  (src/game/002FDF40-g8.c:44-53, 89-93). The horizontal speed is fixed and the vertical part is solved, with **no
  vertical clamp**. The only danger is a point straight below, where the time is 0 and the code divides by it. The mod
  already handles these (lob_row). H.
- **Chopper / Multi-Star** (chopper_update, classes 2169 / 2806): guided_rocket_create (L00_0041C2D0.c:131-139). In aim
  it uses the camera euler; otherwise it uses cam_offset_point_by_side_direction and the hero moby rows, plus a -25 deg
  curve. Blade class 2170, velocity at vars+0x00. The mod already rewrites it (aim_blades).
- **Wall case, all weapons**: while +0x24EC == 1 most weapons fire along -gun+0xD0 (the raised gun). It is recomputed in
  the tick, so the mod cannot override it (vanilla behaviour).
- Not aimed: Visibomb (camera-guided), Synthenoids, Zodiac, Shield Charger, Walloper (melee) and Clank Zapper (mounted
  on Clank). Not studied further.

The bend clamps of 20, 30 and 40 deg in math_compute_aim_direction (v2 0x380EF8) apply **only** while +0x1500 is set,
that is, in first person. In third person the straight shots take +0x760 as it is. H.

## 4. Fixes, ranked

### A. Basis rows after the tick (Blitz Gun, Blitz Cannon, Tesla Claw, Sheepinator, Black Sheepinator). Best. H mechanism, M side effects
In aim_frame, after D (the clamped unit aim direction, the one already written to +0x760) is known, and only while
the held gun's class is 0x5D1, 0xAF1, 0x0B1, 0xA5D or 0xEAD:
```cpp
constexpr gaddr kHeroWeaponBasis = kHero + 0x730;   // rows: forward, left, up (vec4 each, w = 0)
V3 f = D;                                           // == what goes to +0x760
float l = std::sqrt(f.x * f.x + f.y * f.y);         // = cos(lift) > 0 while |lift| < 90 deg
V3 left{-f.y / l, f.x / l, 0.0f};                   // horizontal, perpendicular to f
V3 up{-f.z * left.y, f.z * left.x, f.x * left.y - f.y * left.x};   // f x left
write_v3(memory, kHeroWeaponBasis, f);        memory.write<float>(kHeroWeaponBasis + 0x0C, 0.0f);
write_v3(memory, kHeroWeaponBasis + 0x10, left); memory.write<float>(kHeroWeaponBasis + 0x1C, 0.0f);
write_v3(memory, kHeroWeaponBasis + 0x20, up);   memory.write<float>(kHeroWeaponBasis + 0x2C, 0.0f);
```
- Write all three rows. The Blitz turns its rays about +0x740, so a left row that is not perpendicular to f (the
  mod's 80 deg yaw bend) narrows and skews the cone. It also offsets the rays by +0x750.
- Result: the rays and the visuals both centre on the crosshair, with vanilla first-person spread. A lock (+0x1450)
  still wins, as in vanilla.
- The same write window as the mod's lob row: after on_frame, read by the next tick's weapon update, replaced by
  hero_update_items_frame afterwards.
- Side effects:
  - The muzzle effects that read these rows turn with the aim (SpawnLayeredParticleBurst,
    part_spawn_fragment_fan_and_smoke, weapon_spawn_muzzle_burst). That is good.
  - The Lancer's point light sits at basis * (2,0,2) (L00_003A6C30-g8.c:316). It is harmless, and outside the gated
    classes anyway.
  - The reader scan may be incomplete, which is why the write is gated by class. Writing it for every non-lob weapon
    would probably also be fine, but has not been checked.
- Keep the lob classes on lob_row (row 0 only).

### B. Lava Gun: steer the newest stream blob after the tick. M
- Gun vars = *(gun+0x68): +0x00 muzzle (joint 0, rewritten each tick), +0x3C head, +0x40 full, +0x44 count, **+0x48 the
  newest entry's index**, +0x50 stream strength (springs up to 10; the vector length = strength * 0.05, about 0.5 u a
  tick), +0x60 tick count.
- Ring: 35 entries of 0x40 bytes at `*(u32*)` **v2 L00 0x1B13EC** (sda g_lava_gun_ring_entries = gp+0x23FC; neighbours
  0x1B13E8 particle slots, 0x1B13F0 beam slots). The v1 address was not determined (L: probably +0x80). Entry: +0x00
  position, **+0x10 velocity**, +0x30 life (40), +0x34 landed flag, +0x38 effect handle.
- AppendVectorRingEntry: L00_0041ABB8-ring-g8.c:38-58. lava_gun_update_blobs moves each entry by pos += vel, sticks it on
  a CollLine hit and damages there (L00_00412928-shots-g8.c:48-).
- Each on_frame while class 0x5C9 is firing (+0x50 != 0) and +0x48 changed: take entry[+0x48] and set velocity =
  |velocity| * normalize(P - muzzle) (+0.02 z if wanted) and position = muzzle + velocity. That entry has already moved
  once this tick.
- The older blobs keep their paths, so the stream bends over a few ticks. Untested.

### C. Meteor Gun (class 0xC59)
- **C1.** Add it to kLobs: {0x0C59, 0.25, 0.0030556, 1.5, reach 11.75}. The real reach is random, between 8 and 15.5 u
  per shot, so this is approximate. The 8 u / 22.5 deg auto-target and the +-11.25 deg scatter stay. M.
- **C2.** On first sight of each new class 2862 moby, rewrite vars+0x00 (its launch velocity, the output of
  CalculateAimedLaunchVelocity) so its arc passes through P. Keep the horizontal speed at 0.25 and solve vz with
  g = 0.0030556, as aim_blades does for the Chopper. M on vars+0x00 being the live velocity (the meteor's own update was
  not read).

### D. Upper-body pitch (cosmetic; might help the Lava Gun). L
- Each on_frame, write torso pitch: rec 1 target, block+0x114 (v2 0x25BEA4 / v1 0x25BF24, + = down), for example
  clamp(-lift * 0.6, -0.6, 0.6). Optionally write the arm pitch at rec 9 +0x694 (v2 0x25C424 / v1 0x25C4A4).
- The game writes these only for a weapon target (UpdateHeroAutoAimAngles) or a wall (cam_update_blend_angles), so the
  mod's value survives until attachment_update_all, which runs before the weapon in the next tick and then clears it.
  This is level data: relocate per level.
- Unknowns: whether the hero joint cache (+0xC50) that places the gun is rebuilt after the manipulators in the same tick
  (else one tick of lag), and how far one spine joint can bend before it looks wrong.

### E. Widen the mod's own clamp (straight shots)
kAimPitchLimit = 70 deg is the only vertical limit for the +0x760 weapons. It also matters close up: a point near his
feet needs more than 70 deg from the muzzle, so the shot passes above the crosshair. 85 deg is safe for +0x760 and for
fix A (cos 85 = 0.087 still normalises). Keep about 70 for lob_row, which divides by the horizontal distance.

### Not possible / not useful
- Data patching: there is no vertical-limit data on these paths (see 2).
- Setting +0x1500 or +0x22B5 to get the first-person path: rejected in rac2-weapon-aim.md 2.
- Clearing +0x24EC: it is recomputed in-tick before the weapon runs.

## 5. OpenRAC SDK on_weapon_fire / on_shot_end (1.29+, openrac_mod.h:970-1033, 2908-2939)
- `fn(user, const openrac_weapon_shot*)` is called once for each shot, **after the game tick that made it**, before the
  mods' tick callbacks. The struct is read-only: item, slot, oclass, owner (gun slot), target slot, serial, flags,
  position[3], direction[3] (unit), speed. The hook itself **cannot set a direction**.
- Moby shots (Minirocket, RYNO II, Seeker, Hoverbomb, Bomb Glove, Gravity Bomb, Bouncer, Decoy, Miniturret) get a
  slot and a serial. With those the mod can:
  1. Write the shot moby's motion fields itself: moby = table first (*(relocate(v2 0x1B1ADC))) + slot * 0x100.
     M: this assumes the slot is the table index. Examples: Lancer 3070 vars+0x20; Heavy Lancer 3209 vars+0x20;
     Minirocket 1498 vars+0x30 basis and moby rows; RYNO 3799 moby rows; blade 2170 vars+0x00; meteor 2862 vars+0x00.
  2. Call `weapon_refire(serial, yaw, pitch)` for a free **copy** turned by yaw/pitch. The original still flies, so it
     is no redirect.
  Whether a new shot has already moved once in its spawn tick depends on the moby update order (L).
- **Blitz, Lancer, Lava Gun** and every non-moby weapon are reported once a tick with OPENRAC_WEAPON_SHOT_ROUNDS and
  slot -1, after the damage is done. The Blitz's rays are traced inside its update, so **no hook after the fact can aim
  them**. It has to be fix A (pre-write).
- Every weapon that needs a fix reads its direction before the hook runs, so fixes A-C are better than the hook. The
  hook only helps as an alternative for C2 (it says when a meteor appears; but 2862 is not in its moby-shot list, so it
  is likely reported as ROUNDS: L).
- `rac2.weapon.take_ammo` (an in-tick hook) is too early for the Blitz: ammo is spent on the trigger tick, and the rays
  fire one or more ticks later.

## Not determined
- Whether some code outside the scanned L00 lui chains reads +0x730..+0x75F between the tick start and the weapon update.
  This is the risk in fix A, which is why it is gated by class.
- The meteor (2862) and lava blob behaviour after a rewrite, the Lancer/rocket shot updates (whether anything re-steers
  them vertically), and the visual result of manipulator writes.
- v1 addresses of the Lava Gun ring pointer and the Meteor gp constants. The decomp's sheepinator/Morphoray v2 copy was
  not matched.
- Why the SDK lists the Lancer as having "no shot moby" when CreateTargetedMovingEffect spawns class 3070.
