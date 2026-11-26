# rac2: lobbed/thrown weapons at the crosshair point, and the game's own aim reticle

Sources: rac2-decomp v1.01 C, rabbitizer disassembly of the v2.00 disc's L00 (throwaway scripts, not kept).
Nothing was run. Confidence: **H** read in C and checked in v2 asm, **M** read in C only / inferred, **L** guess.
Level code is per level: addresses below are **L00** unless marked; resolve other levels by name in
`config/levels/functions.tsv` (v1) / `config/us2/levels/functions.tsv` (v2). Hero = 0x189E20 on both discs.
Builds on rac2-weapon-aim.md (hero +0x730 basis rows, +0x760 aim fwd, +0x774/+0x778 assist axis).

## A1. CalculateAimedLaunchVelocity (CALV) — the shared lob solver
v1 L00 0x37CEE8 / v2 L00 0x37E328 (per level, e.g. v2 L01 0x374598); src/game/002FDF40-g8.c. H
Signature (EE ABI): `CALV(a0 out_vel, a1 origin, a2 targetMoby, a3 sourceMoby, f12 min, f13 max, f14 speed, f15 grav)`.
All hero weapons pass sourceMoby = 0, so:
- S = hero moby position (*(u32*)(hero+0x2290) +0x10); basis = hero +0x730/+0x740/+0x750.
- n = local "down" (cam_compute_view_direction(S, n, 0); with hero +0x348 != 0, -hero-moby-up).
- Landing target T:
  - targetMoby != 0: T = target+0x10 (only +0x10 and class +0xAA are read; Swamp-monster arm/eye adds a height).
  - else if (pad held 0x138320 & 4 = L1) && hero+0x22BC == 0: direction = camera forward flattened onto the
    ground plane; reach = horizontal distance from the **camera eye** to where the camera ray meets the plane
    through S, clamped [min,max], then max*ease_out_quad(reach/max). T = S + flatdir*reach (at S's height).
    Not the crosshair hit point even in vanilla.
  - else: **T = S + normalize(+0x730 row0, 3-D) * max** (matrix_mul_vec3 with (1,0,0) = row0, VU0 code;
    vec_scale_to_length normalizes). No min clamp here. H
- Velocity: horizontal (wrt n) = speed * unit(horiz(T - origin)); D = |horiz|/speed ticks;
  vertical = vert(T - origin)/D + up*grav*D/2. A plain ballistic arc from origin through T, speed per tick,
  grav per tick^2. H

Each weapon calls CALV **every tick at the top of its update** (also feeding coll_trace_projectile_to_impact,
the predicted landing point at state+0x10 that weapons draw as their landing marker), and the spawn uses the
velocity of the tick it spawns on. H

## A2. Per weapon (all sourceMoby = 0; L00 addresses)
| weapon (class) | update v1 / v2 | min, max (state 29) | speed, grav | target | spawn |
|---|---|---|---|---|---|
| Mini-Nuke (2650) | 0x3C2FC8 / 0x3C4410 | sdata: v1 0x1AFEC4/0x1AFEC0, v2 0x1AFE74/0x1AFE70 = 1e-5, 12 | raw 15/60 = 0.25 (v1 0x1AFECC, v2 0x1AFE7C); 12/3600 (v1 0x1AFEC8, v2 0x1AFE78) | hero+0x1450 lock, else (not state 1/29) SelectBestTargetInCone(muzzle, +0x760, 22.5/45 deg, 11.1 u) | same tick: weapon_spend_ammo then mini_nuke_projectile_spawn (v1 0x3E7650 / v2 0x3E8AD0), class 3079 |
| Gravity Bomb (3299) | 0x3EE4C0 / 0x3EF940 | sdata: v1 0x1B0804/0x1B0800, v2 0x1B07B4/0x1B07B0 = 1e-5, 12 | v1 0x1B080C / v2 0x1B07BC = 15 (/60); v1 0x1B0808 / v2 0x1B07B8 = 12 (/3600) | as Mini-Nuke | same tick, gravity_bomb_spawn_projectile (v1 0x3EED10 / v2 0x3F0190), class 3300 |
| Bomb Glove (192) | 0x397E60 / 0x399290 | 1.5, 10 (11) immediates | 1/6, 0.0030556 | SelectBestTargetInCone(glove pos, +0x760, 22.5/45 deg, 11.1 u) always | spend, **20 ticks later** part_l00_spawn_spinning_debris (v1 0x38D530 / v2 0x38E930), class 121 |
| Decoy Glove (562) | 0x39D110 / 0x39E540 | 1.5, 5 (11) | 1/6, 0.0030556 | none | +20 ticks, decoy_create_projectile (v1 0x398810 / v2 0x399C40) |
| Miniturret Glove (3162) | 0x3EAA58 / 0x3EBED8 | 1.5, 4.5 (11) | 0.25, 0.0030556 | none | +20 ticks, miniturret_spawn_shot (v1 0x387CA8 / v2 0x389078) |
| Megaturret Glove (2661) | 0x3C8828 / 0x3C9C70 | 1.5, 4.5 (11) | 0.25, 0.0030556 | none | +20 ticks, megaturret_spawn_shot (v1 0x38A748 / v2 0x38BB30) |
| Spiderbot Glove (2311) | 0x3B7D48 / 0x3B9190 | 1.6, 4.5 (11) | 0.13333, 0.0030556 | none | +12 ticks, spider_bot_spawn (v1 0x3DE088 / v2 0x3DF4D8); refuses unless the predicted landing is a floor (normal . up >= 0.75) |
| Tankbot Glove (3706) | 0x4029E8 / 0x403EC8 | 1.6, 4.5 (11) | 0.13333, 0.0030556 | none | as Spiderbot |
| Bouncer / Heavy (3604/3742) | 0x3FDE48 / 0x3FF2E8 | 1.6, 10 | 0.25, 0.0040556 | hero+0x1450 only | same tick, bouncer_spawn_debris (v1 0x3FE2D0 / v2 0x3FF7D0) |
Files: L00_003C2FC8-g8.c:58-89, L00_003EE4C0-g8.c:57-79, L00_0038F178.c:400-437 (Bomb) / :455-487 (Decoy),
L00_003EAA58.c:33-73, L00_003C8828-input.c:35-75, L00_003B3158-pickup-g8.c:12-95, L00_003FFA70-g8.c:118-,
L00_003FDE48-g8.c:70-125. Barlow (L04) has its own Mini-Nuke variant (mini_l04_nuke_update v1 L04 0x3EE7C0,
globals D_L04_001B01A0..AC). Decomp prototypes of CALV in these files are often wrong (float/GPR order); the
register mapping above is from the v2 asm (H for Nuke, Gravity Bomb, Bomb Glove; M for the rest).
min/max/speed/grav "sdata" values are level data (gp-relative), so the addresses differ per level; only L00
is known.

Not CALV:
- Chopper / Multi-Star (2169/2806): chopper_update v1 0x3B5BF8 / v2 0x3B7038 (L00_003B5BF8-dbg.c:48-126).
  3rd person throws after an 8-tick wind-up through guided_rocket_create (v1 0x41C2D0 / v2 0x41D7D0,
  L00_0041C2D0.c:78-237) with mode 0: blade class 2170 at the hero + offsets, velocity = horizontal along the
  hero moby yaw (+0xF8) * speed (cam_offset_point_by_side_direction), aim point vars+0x40 = pos + hero moby
  row0*64, a -25 deg yaw curl, homing on the weapon_update_target_lock target (axis +0x778/+0x774, which the
  mod already writes). Aim mode (mode 1) uses the camera euler / camera fwd*64 instead. M
- Hoverbomb / Tetrabomb: moby_fire_shot (reads +0x730/+0x760 like straight shots); player-steered. M
- Synthenoid, Visibomb: no hero aim fields read (no hits in the v2 +0x730..+0x77C reader list); nothing to
  redirect. M (not traced further)

## A3. What does NOT work
- L1 bit (0x138320 & 4): the pad block is rebuilt from the controller early each tick, so an on_frame write is
  gone before the weapon runs; injecting L1 via on_pad puts the hero in state 1/29 (aim). H
- Wrapping rac2.weapon.take_ammo: CALV has already run earlier in the same update (Nuke, Gravity Bomb,
  Bouncer), or the spawn happens 12-20 ticks later on a tick whose update recomputes CALV at its top (gloves).
  The velocity is a stack local of the caller: not reachable cleanly. H
- And the L1 branch would not land on the crosshair point anyway (reach from the camera eye, eased, at the
  hero's height). H
- Faking a target: CALV would take any "moby" (+0x10, +0xAA), but the weapons get it from hero+0x1450 (also
  drawn as a lock reticle, used by the torso turn, cleared/rewritten by UpdateHeroHeldTarget) or from their own
  cone search. Not recommended. M

## A4. Recommended: steer the non-target branch through hero +0x730 row0 (on_frame)
Weapons read +0x730 written in the previous tick (their update runs in hero_update_items_and_flags before
hero_update_items_frame rewrites +0x730; L00_002A6DD0.c:108-112). The only other +0x730 readers are weapon and
muzzle-effect code (v2 reader list), so a pitched row0 has little side effect beyond tilting muzzle effects of
the weapon in hand. M-H

Each on_frame while a CALV weapon is held (and no lock target):
1. P = crosshair hit (rac2-weapon-aim.md 4). S = hero moby +0x10. O = gun muzzle estimate (held gun moby
   *(hero+0x1220) +0x10, or call moby_transform_joint_to_world(gun, 0, out): v1 L00 0x311580 / v2 0x3127C8).
   Optionally predict one tick of motion for S and O.
2. Arc through P with the weapon's speed v and grav g: h = horiz(P-O), dh = max(|h|, min); u = h/|h|;
   tP = dh/v; vz = (P.z-O.z)/tP + g*tP/2; X(t) = O + u*v*t + up*(vz*t - g*t^2/2).
3. If |P-S| < max: bisect t in [tP, tP+600] for |X(t)-S| = max (f(tP) < 0, f grows without bound);
   row0 = normalize(X(t) - S). The game's own arc from O to S+max*row0 = X(t) is then the same parabola, so it
   passes through P (and the projectile hits the surface there). Else row0 = normalize(P-S) (falls short on the
   line, like vanilla max reach).
4. Write hero+0x730 = (row0, 0). Leave +0x740/+0x750.
For Mini-Nuke / Gravity Bomb only, simpler and exact: row0 = normalize(P-S) and write the max global
= |P-S| (L00 addresses above; per-level addresses unknown), restoring 12 when not aiming.
Effects: the game's own landing marker (state+0x10) moves to P; auto-aim (+0x760 cone, +0x1450 lock) still
wins when it finds a target (centred on the crosshair, since the mod writes +0x760). Error ~ one tick of hero
motion. Verify once: log +0x730 inside a rac2.weapon.take_ammo hook (Nuke/Gravity Bomb) — it should be the
mod's value.

## A5. Fallback / precision: rewrite the projectile's velocity after spawn
All of these keep the launch velocity at *(moby+0x68)+0x00 (vec4): Mini-Nuke projectile 3079, Gravity bomb
3300, Bomb Glove bomb 121 (owner at moby+0xB8), (Chopper blade 2170: +0x00 velocity, +0x40 aim point, +0x14
target). H for 121/3079/3300 (spawn C read), M for the others' integrators using the same grav. Find the new
moby (on_moby_update before its first update, or on_frame), re-solve from its current position with the arc
formula of A4 step 2 and write the velocity once. Exact, but the game's landing marker keeps the old point.

## B. The game's aim crosshair (from a sub-agent's read; spot-checked against the chopper/hoverbomb calls)
- No dedicated draw: each weapon's update queues a HUD marker **without a world point** while aiming, and
  hud_draw_reticle_and_markers (boot v1 0x273418 / v2 0x273428, src/game/0026EA00-qcopy.c:88; called only from
  RenderFrame v1 0x27BD70 / v2 0x27C3D0 while the DMA packet is built) draws it at g_reticle_screen_params[0..1]
  (0x1A7348 both discs: block 0x1A7340 = W, H, W/2, H/2, GS offsets; written by gfx_init_view_context /
  gfx_configure_projection / SetPalMode, src/game/00274128.c:754,790,883), then clears the list. H
- hud_add_moby_marker (boot v1 0x273318 / v2 0x273328, src/game/00273318-g8.c:27):
  a0 moby, a1 colour (0xAABBGGRR, A 0x80 = opaque), a2 world pos (0 = screen centre), a3 type (effect texture
  index), t0 -1, t1 4, f12 scale, f13 angle 0, f14 90. Ignored when game mode 0x1A8BB0 == 2 or 6 entries
  queued. List: v1 0x1B7600 / v2 0x1B7580, 6 x 0x30, count +0x120. Entries with moby 0 or moby+0x20 = 0xFE/0xFD
  are skipped. H
- Condition: per weapon, in its update: state 29 (most weapons turn state 1 into 29 via hero_l00_set_action),
  or +0x1500 too (Chopper L00_003B5BF8-dbg.c:57-63, Lancer L00_003A6C30-g8.c:188). No global flag. H
- Look: one sprite, 64x64 effect texture `type`, drawn 40*scale px square at the centre. Type 38 scale 1.0
  (Chopper, Lancer, lock weapons); 39 scale 1.25 (minirocket/megarocket) or 2.0 (scoped, seeker); 33 scale 1.3
  (blitz, spread); 31 (tesla, hoverbomb). Colour 0x600FFF0F green ready, 0x600000FF red while cooling. H
  Texture image: not determined (needs extracting effect textures 31-39).
- Screen: NTSC 512x416 frame (SetupFS_AA_buffer(0x200,0x1A0)), centre (256,208); PAL 512x448. M
- Mod recipe (M-H): each on_frame while a weapon is held and the hero is not in 29/+0x1500, call
  hud_add_moby_marker(gun = *(hero+0x1220), 0x600FFF0F, 0, 38, -1, 4; 1.0, 0, 90), or write an entry
  (+0x10 f32 scale 1.0, +0x14 colour, +0x18 type 38, +0x1C 0, +0x20 moby, +0x24 s16 has-pos 0, +0x26 0,
  +0x28 f32 pi/2, +0x2C mode 2) and bump the count; skip if count >= 6, mode == 2, or an entry for the same
  moby without a point already exists (the list is not cleared while the render layer is off: pause/menus).
  Takes one of 6 shared slots (a weapon's lock markers can be dropped). Don't call the draw functions from
  on_frame (DMA packet outside RenderFrame); don't force state 29 / +0x1500.
- Rect fallback: ~40 px square at the centre of a 512x416 picture, green (15,255,15) ~75% alpha; shape is a
  guess until the texture is seen. Note the overlay API's picture is 512x448 per the earlier note: map by
  proportion.
