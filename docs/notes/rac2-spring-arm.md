# rac2 follow camera: a spring-arm eye that never enters geometry

Sources: rac2-decomp v1.01 C (src/levels/shared, src/us2/levels/shared, src/game) and a rabbitizer
disassembly of the v2.00 disc's level 0 (throwaway helper scripts, not kept). Read with
rac2-follow-camera-tilt.md (tick order, SIG block) and rac2-aim-camera.md.
"v2" = USA v2.00 (ntsc2) L00, "v1" = USA v1.01 (ntsc) L00. rec = *(u32*)g_follow_camera_object
(v2 0x1B8D90 / v1 0x1B8E10), st = *(rec+0x70). U = up = *(vec4*)(g_camera_move_state+0x20)
(g_camera_move_state v2 0x1B8DA0 / v1 0x1B8E20, so U at v2 0x1B8DC0 / v1 0x1B8E40; +0x30 is
g_camera_up_vector, the same axis). Hero record 0x189E20 on both USA discs, hero moby = *(u32*)0x18C0B0.

## 1. The collision line the camera uses: CollLine

`int CollLine(vec4 *from, vec4 *to, u32 flags, Moby *ignore, void *hit_event)`
(a0, a1, a2, a3, t0; EE EABI, 5th arg in $t0). Returns 1 for a hit, 0 for a clear segment.
- from/to: 16-byte aligned vec4s in guest memory (it loads them with lqc2) -> use stack_push(0x20).
- ignore: a moby skipped (the camera passes the hero moby, *(0x189E20+0x2290)).
- hit_event: 0 (non-zero posts a damage event to the hit moby). The camera passes 0.
- Hand-written VU0 leaf; internally scaled by 1024 (hit stored * 1/1024). Keeps the nearest hit.
- Results go to the loaded PROGRAM's collision block (not the exe's):
  base+0x18 hit moby (0 = level faces), +0x1C poly info (low 5 bits material, <0 none),
  **+0x20 hit point**, +0x30 push-out point (coll_test_point output, "g_collision_position"),
  **+0x40 face normal**, +0x50/+0x60/+0x70 the triangle's vertices. No t/fraction is stored:
  t = |hit - from| / |to - from|.
  L00 base: v2 0x1BF180 (hit point 0x1BF1A0 = decomp "g_hero_l00_position" D_L00_001BF220 on v1,
  normal 0x1BF1C0) / v1 0x1BF200 (hit 0x1BF220, push 0x1BF230, normal 0x1BF240).
  Robust per level: decode it from the loaded CollLine's code: words at CollLine+0x14/+0x18 are
  `lui t7,hi ; addiu t7,t7,lo` -> base = (hi<<16) + (s16)lo  (verified on all v2 levels below).
- It is in the executable AND copied into every level program. The camera calls the LEVEL copy.
  The exe copy (v2 0x276208, v1 0x2761F8, PAL 0x276098) writes the exe result block at
  v2 0x1BAF00 (v1 0x1BAF80), which is inside a level's bss -> do not call the exe body raw in a level.
  v2 level copies (functions.tsv "func_002761F8", checked on the disc) and their blocks:
  L00 2DE878 1BF180 | L01 2D3FE8 1BF900 | L02 2E1E10 1C0940 | L03 2DF4F8 1BFDC0 | L04 2FF080 1BFB00
  L05 2F7018 1BFA80 | L06 31E060 1BFC80 | L07 2D4778 1BF5C0 | L08 2EA670 1BFF40 | L09 2DA8A8 1BFDC0
  L10 2F9518 1BFA40 | L11 2F0678 1C0D80 | L12 2E1E20 1C01C0 | L13 2E93E0 1BFC80 | L14 2E61F8 1BF8C0
  L15 2F7618 1BFB80 | L16 2D7B50 1BFEC0 | L17 2DF9B0 1C0000 | L18 2F9110 1BFB80 | L19 2DC1B8 1BFE00
  L20 2F2878 1C0380 | L22 2E4AD0 1C0040 | L23 2E91B0 1C0A00 | L24 2DD898 1BEA80 | L25 2F1998 1BF5C0
  L26 2DEF98 1BEF40 | L30 2E5630 (not decoded)
  v1 level copies: L00 2DE4B0, L01 2D3C80, L02 2E19C8, L03 2DF038, L04 2FED10, L05 2F6C50,
  L06 31DB98, L07 2D4510, L08 2EA300, L09 2DA5C0, L10 2F9150, L11 2F0318, L12 2E1BC8, L13 2E8FA0,
  L14 2E5DB0, L15 2F7250, L16 2D7790, L17 2DF570, L18 2F8D60, L19 2DBF58, L20 2F24B8, L22 2E4708,
  L23 2E8DE8, L24 2DD4D0, L25 2F15D0, L26 2DEB50, L30 2E5368.
  Callable: openrac_api.function_at(level address) (certain to be the loaded copy). By name: the
  rac2 mods use func_<this disc's address> on ntsc2 ("func_00276208"; ntsc "func_002761F8";
  PAL "func_00276098"); keyboard-mouse shows function(name) == function_at(level copy) for a
  name in a level on rac1. UNVERIFIED for rac2: check function(name) == function_at(addr) at runtime.
- Mask: g_cam_collision_line_mask (v2 0x1B17CC / v1 0x1B184C, bss, relocate_level(0, .)),
  rewritten every tick by cam_select_coll_masks (exe func_00271FD8; v2 L00 copy 0x2D9BA8;
  src/game/00271FD8-g8.c:39-62): 0xB4, or 0x1B4/0xBB4/0x3B4/0xDB4 by what the hero stands on.
  Bits, from the CollLine disassembly (medium confidence): 0x1 skip level faces; 0x9 skip mobys
  (unless moby flag 0x4000); 0x80 skip faces flagged 0x80; 0x20 skip faces whose material ==
  (flags>>8)&0x1F; 0x10 face-side test; 0x4/0x2 moby mesh selection. So the camera line hits
  level faces and mobys (not the hero) and ignores camera-transparent (0x80) faces.
  Read the live word each tick and pass it unchanged.
- Uses in the camera: cam_follow_update_orientation (v2 0x424548 / v1 0x423038,
  L00_004224D0.c:71-120; reposition only, eye put 0.5 short of the hit), cam_follow_update_collision
  (v2 0x423150 / v1 0x421C50, L00_00421C50.c:116-150; floor/ceiling lines through the hero, +-20 up),
  FollowCameraHideObstructions (v2 0x4258D0 / v1 0x4243C0, L00_004243C0-g8-dbg.c:133-138:
  LOS from st+0xB0 to st+0xB0+st+0x160; 30 blocked ticks in a row -> cam_reset_default snap).
  Probe/constrain/sweep do NOT use CollLine: they use coll_find_nearby_shape_points and
  coll_test_point (sphere tests, exe func_00277258, v2 L00 copy 0x2DF8D8).
- SDK alternative (api >= 1.17): openrac_api.collision_line(from, to, OPENRAC_LINE_LEVEL|MOBYS,
  hero_slot, &hit): point + normal, no stack/relocation. Its mask is the shots' (unknown), so it
  may stop on faces the camera ignores (0x80, ground material). Use as a fallback.

## 2. What the tick does to the orbit (and what fights a mod)

Fields (st = follow state; v2 = v1 offsets):
 +0x00 goal eye (= B + goal, written at the start of move_eye) | +0x10 s16 update_reach enable
 +0x60 pivot P | +0xB0 **B = P + U*h**, kept 0.95 under ceilings/above floors (update_collision)
 +0xC0 **smoothed pivot** (springs st+0x13C..0x148, vel st+0xD0..0xE8) | +0xA0 look-at
 +0x160 **goal orbit, relative to B** | +0x170 **smoothed orbit, relative to A = st+0xC0 + U*h**
 +0x180/+0x184/+0x188 eye yaw/elevation/length spring velocities
 +0x18C **D distance** (eased at END of tick toward +0x1A0 if s16 +0x19C, else the mode default)
 +0x190 **h height** (eased toward +0x1AC if +0x19E) (weapon_reset_view_state, L00_0041ABB8-g8.c)
 +0x194 yaw rotation applied this tick (= +0x1D4 * +0x1C0) | +0x198 tilt step
 +0x1C0 yaw input, +0x1C8/+0x1CC tilt, +0x1D0 input mask, +0x1DC steer input (the game's own
  auto-steer: lock-on, ladders, state 123 writes it via cam_steer_toward_direction), +0x1E4 tilt input
 +0x210 goal eye copy | +0x220 **deficit: goal length = D - deficit** | +0x224 s16 recovery timer
 +0x228 s16 probe class | +0x22A s16 | +0x22C/+0x230/+0x234 sweep params (end-of-tick reset
  1.0, 8.0, 0.1 -> sphere radius 0.25+0.1*1*(8-1) = 0.95 for probe, sweep end and B's clearance)
 +0x23C s16 LOS-blocked counter | **+0x23E s16 lock**: skips the sweep, cam_check_noop and the
  LOS counter/reset (reset to 0 at end of tick; game sets it in hero states 120/124)
Eye: rec+0x30 = st+0xC0 + U*st+0x190 + st+0x170 (move_eye v2 0x4271E0, L00_004224D0-g8.c:69-72).

Goal modifiers inside the next tick, in order:
1. TranslateObjectAndView: moves the eyes/pivots with the hero's platform (orbits untouched).
2. cam_follow_update_us2 (0x4239D0): mode setters; hero+0x22B4==2 rewrites the whole state (snap).
3. update_collision: new P, B, smoothed pivot A.
4. update_reach (v2 0x4269A0, L00_004224D0.c:450-476): goal = lerp(goal, st+0x00 - B, 0.85) (the lazy
   follow), length clamped to [D - deficit, D]. Skipped when s16 st+0x10 <= 0 (only reader).
5. cam_vehicle_update_turn: yaw by +0x194 (stick X or +0x1DC), tilt step <= SIG+0x14 turn_limit.
6. HideObstructions: moby steering (+-1 deg for a few classes); [sweep: 7 growing spheres from B,
   on contact pushes out and RE-AIMS the goal]; probe+constrain (sphere r 0.95 at B+goal, rewrites
   goal from the pushed point, may raise the deficit); deficit >= D-1.5 clamp (probe minimum,
   SIG+0x20 = 1.5); deficit recovery (cosine blend, timer 2000); goal length = D - deficit;
   LOS CollLine B -> B+goal.
7. move_eye: smoothed orbit springs toward the goal: length k=0.02/c=0.2 (ease speeds SIG+0x34/0x38),
   yaw k/c = SIG+0x3C/0x40 (0.015/0.2), elevation SIG+0x44/0x48 (0.015/0.2), spring step
   v = v + k*diff - c*v clamped to |diff| (src/game/0026EA00.c). Then the push-out: coll_test_point
   (eye, mask 0x36, no ignore, r 0.25 immediate in code; L13 under z 120: r 0.3) up to 3x, applied
   only when the contact is along the view's up row (|dot| > 0.5, i.e. floors/ceilings, not walls);
   the push also shifts st+0x60/+0xC0/+0xF0/+0x90 by the same delta.
8. build_view; then weapon_reset_view_state: eases D/h, resets masks, sweep params, lock, st+0x10=1.

Consequences:
- Only the springs (step 7) carry goal changes into the eye. With the game's default springs and the
  spring velocities zeroed, a goal that differs from st+0x170 by delta moves the eye by only
  k*delta (1.5-2 %) per tick. **The tilt mod's patch SIG+0x44/0x48 = 1 must be undone** for the
  spring arm: it copies every probe/sweep/turn push of the goal into the eye in the same tick
  (that is the fight). turn_limit: restore 0.0305433 or set 0 while owning (0 = no tilt rotation).
- Probe/constrain/sweep shorten or re-aim the GOAL only; the game's eye safety is just the 0.25
  vertical push. Do not rely on them; neutralise the sweep with st+0x23E = 1.
- The game itself casts its LOS from B (st+0xB0); the eye hangs from A (st+0xC0 + U*h). A lags B
  (pivot springs) in jumps/falls; B is the collision-maintained point.

## 3. Spring-arm algorithm (on_frame, i.e. for the next tick)

Own only when: rec+0x86 == 0, *(v2 0x1B8D94 / v1 0x1B8E14) (the camera manager's active camera,
g_scalar_transition_state+0x194) == rec, play phase PLAYING, no camera script (on_camera_script),
hero+0x22B4 != 2, and the real right stick was idle (on_pad, |axis| < 0.3) for N ticks.
Per tick:
  D = st+0x18C (game zoom/distance, incl. scripts and the face-the-camera pull-out), h = st+0x190
  B = st+0xB0 ; A = st+0xC0 + U*h ; A_pred = A + (A - A_prev)   (reset A_prev on jumps > 2 units)
  yaw += mouse ; yaw += (st+0x1C0 != 0 ? st+0x194 : 0)   (keeps the game's auto-steer/lock-on)
  pitch = commanded ; dir = unit(yaw, pitch) in the U frame
  O = A_pred ; if CollLine(B, A_pred) hits: O = B and write st+0xC0 = B - U*h (A coincides with B)
  E_far = O + dir*(D + M/0.25) ; if CollLine(O, E_far, mask, hero_moby):
      t = |hit - O| ; c = max(|dot(dir, normal)|, 0.25) ; L_tgt = t - M/c   else L_tgt = D
  L_tgt = clamp(L_tgt, L_min 0.3, D)
  L = (L_tgt < L) ? L_tgt : L + (L_tgt - L)*(1 - exp(-dt/0.3))    (instant in, ~0.3 s out;
      optional 0.15 s hold after a pull-in)
  M = 0.35 (> the 0.25 push radius + one tick of pivot motion).
Writes (v = dir*L):
  st+0x160 = v ; st+0x170 = v ; st+0x00 = B + v ; st+0x210 = B + v ; rec+0x30 = A + v
  st+0x180 = st+0x184 = st+0x188 = 0
  st+0x220 = D - L ; s16 st+0x224 = 2000 ; s16 st+0x22A = 0
  s16 st+0x23E = 1 (no sweep, no LOS reset) ; s16 st+0x10 = 0 (no lazy-follow lerp; leave it
     alone instead if the game's auto-follow yaw should stay: then take yaw from post-tick st+0x160)
  st+0x1D0 |= 3 ; st+0x1DC = 0 ; T = clamp(atan2(v.U, |v|)/0.6981318, -1, 1) ; st+0x1C8 = T ;
     st+0x1CC = 0 ; st+0x1E4 = 0.6981318*sgn(T)*(0.3+0.7|T|) (G == T, no tilt step); or turn_limit 0.
  SIG (per-level block): eye springs at the defaults (0.015/0.2), turn_limit default or 0.
Remaining 1-tick error: the eye lands at A_next + v (pivot motion since on_frame), covered by M;
goal-vs-eye differences move the eye by k*delta; L < 1.5 is clamped back to 1.5 in the goal only.
Hand back: stop writing (end-of-tick resets restore st+0x10, +0x23E, masks), restore SIG. The goal
is left at v with deficit D-L, so the game eases out by itself. While handed back, resync
yaw/pitch/L from post-tick st+0x170 each tick so retaking is seamless; resync too when the
post-tick st+0x170 differs a lot from what was written (a game snap: cam_reset_default,
update_orientation(1)).
Alternative without fighting at all: SDK camera_set (1.12) with the same arm math (game camera
underneath; one mod at a time; the game's logic reads the set camera).
