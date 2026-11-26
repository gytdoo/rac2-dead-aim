# rac2 sphere collision: CollSphere, the moby-overlap query, and how to sweep a sphere

Sources: rac2-decomp at 4ea93cea (2026-11-17, the commit OpenRAC 1.1.1-dev's games/rac2/game.json pins),
paths below relative to dev/reference/rac2-decomp; a rabbitizer disassembly of the v2.00 disc (the ISO in
~/Games/openRAC/discs) for L00 and the code words of all v2 levels; and an x86-64 disassembly of the dev
AppImage's port binaries (openrac/port/build/release/openrac-rac2-ntsc2 and -ntsc), which have symbols.
"v2" = USA v2.00 (ntsc2), "v1" = USA v1.01 (ntsc). Confidence: H high, M medium, L low.
Read with rac2-spring-arm.md (CollLine, follow-state fields) and rac3-strafe-camera.md (UYA).

## 0. Short answer

- **rac2 has no continuous sphere sweep** (H). The engine's one sphere primitive is **CollSphere**
  (`func_00277258`). It finds the nearest level or moby face within r of a point and returns that
  point and the pushed-out centre. So it is an overlap test with a push-out, not a sweep. Every "sweep" in
  rac2 samples it at points along the path: the follow camera's arm sweep (6 growing spheres), the
  16-sphere `cam_clamp_segment_by_collision`, and `CollideSweptSphere` (an end sphere plus a line).
- UYA's true sweep `func_L01_00446E38` (rac3 src/levels/shared/L01_00446E38.c:25-86) works in two steps.
  It lists every triangle near the segment with the same engine primitive in **list mode** (flags
  `|= 0x50`), then runs an exact swept-sphere test on each triangle. rac2's CollSphere **has the same
  list mode** (flag 0x40). So a mod can rebuild UYA's sweep: one or two game calls, plus the
  per-triangle test in C++ (§7b; that C++ code is tested).
- Overlap and push: `CollSphere(P, mask, skip, r)` returns 1 and leaves the pushed centre at block+0x30
  (§1, §7a).
- Recommended call, per level (same lookup as CollLine): **CollSphere = CollLine + 0x1060** in every level
  program of both discs. The results block is the same one CollLine uses.
  `using CollSphereFn = int (*)(gaddr pos, std::uint32_t flags, gaddr skip, float r);`

## 1. CollSphere = func_00277258 (H)

Names: config/names.tsv:1925 `coll_test_point` (descriptive). nonmatching/functional/func_00277258.c
calls it CollSphere, and docs/port/port-handoff.md:141 does too. Original: hand-written VU0/MMI
assembly, `ASM_FUNC` at src/game/00274128.c:455 (its comment, "returns a hit handle", is wrong: it
returns 1). The port runs the functional C: nonmatching/functional.tsv:139 lists it, and the dev binary
has its helpers `game_tris_00277258_hc2` and others.

**C prototype** (nonmatching/functional/func_00277258.c:324):
`int func_00277258(void *pos, int flags, char *skip, float r)`. Registers: a0 = pos, a1 = flags,
a2 = skip, f12 = r (v2 L00 disassembly at 0x2DF8D8: `lqc2 vf31,0(a0)`, `mfc1 v1,f12`,
`addi s3,a1`, `addi s2,a2`). The C callers in the decomp declare it in four different argument orders
(for example `(float, float*, int, void*)` at src/game/002FC8D0.c:359, `(void*, float, void*, void*)`
at src/levels/shared/L00_004239A8-g8.c:17). On the EE these all land in the same registers, so they are
the same call.

**Native signature in the port** (H, from the x86-64 code of `func_00277268` in openrac-rac2-ntsc2 and
`func_00277258` in -ntsc): edi = pos, esi = flags, edx = skip, xmm0 = r, eax = result. So:
`int (gaddr pos, std::uint32_t flags, gaddr skip, float r)`. Keep this order: it does not matter on
SysV Linux, where integer and float arguments are counted separately, but it matters on Windows x64.

Inputs:
- `pos`: guest address of a vec4, **16-byte aligned** (loaded with lqc2; xyz used, w ignored). Put it on
  the game stack (StackFrame), as for CollLine.
- `flags`: the collision mask (§3).
- `skip`: guest address of one moby to leave out (0 = none). It is compared with the moby pointer only.
- `r`: radius in world units. It is clamped to **at most 10** (`pminw` against 10.0f; :334) and must be
  > 0. **The whole box pos ± r must lie inside the world cube (0, 1024) on every axis**, or the function
  returns 0 without testing anything (:352-361).

Behaviour (:314-323, H): the level part is skipped when flags & 1. It looks up every 4-unit cell of the
level collision octree (block+0x00) whose box is within r. Each cell's chunk is DMA'd to the scratchpad,
its packed vertices are unpacked, and its triangles (quads as two triangles) are tested for the
**closest point to pos**. Then mobys: every moby in the 16-unit moby grid cells under the box that has
collision (moby+0x98 != 0), whose bounding sphere meets the query sphere, and that is not `skip` and not
already stamped this query (moby+0xA0 == stamp) gets its triangle mesh tested (if moby+0x9C >= 0) and
then its primitives, spheres and capsules (unless flags & 2). Only the **single closest** contact is
kept; the squared distance of a triangle hit is shrunk by 0.9995.

Return value and outputs:
- **0 = no contact.** The results block is **not written** and keeps stale data, so always test the
  return value. 0 also comes back when the stamp block+0x10 < 0 (collision disabled), the box leaves
  the world cube, or r <= 0.
- **1 = contact.** It writes the results block (the same block as CollLine's; addresses in §2):
  - +0x18 u32: the moby hit (0 = level faces).
  - +0x1C: for a triangle, its type byte + 0x1000 (low 5 bits = material; GetCollHitMaterial reads
    this); for a moby primitive, minus the primitive's address.
  - **+0x20 the closest point** on the nearest face. The decomp's misnamed `g_hero_l00_position`
    (D_L00_001BF220 on v1) is this field.
  - **+0x30 the push-out centre**: closest + unit(pos − closest) · r · ~1.00025 (the 0.9995 shrink
    gives a slight over-push). The decomp calls it `g_collision_position` (D_L00_001BF230 on v1).
    The game copies it back into the probe point and calls again, up to 3 times
    (L00_004239A8-g8.c:45-64; move_eye).
  - +0x40 for a triangle: N = cross(V2−V0, V1−V0), **unnormalised** and in 1024²-scaled units. It points
    to the face's front side: without flag 0x10, faces whose front is away from pos are ignored. For a
    primitive: the push vector from its centre, in 1024 units. CollideSweptSphere multiplies it by
    1/1024 and normalises it (src/game/002FC8D0.c:411-412). For the push direction, prefer
    unit(+0x30 − +0x20). With flag 0x10 (two-sided), the sign of +0x40 does not tell you which side
    the sphere is on.
  - +0x50/+0x60/+0x70: the triangle's vertices in world units (left stale for a primitive hit).
  - All w words are 0 in the port (VU leftovers on the PS2).
- **flags & 0x40 = list mode** (:283-310, :841-842, asm 0x2E0558-0x2E05C4). Every triangle within R
  (best is reset to R² after each hit, so they are not progressively closer) is appended to the
  **triangle list = block + 0x5280**: 64 bytes each, +0x00 V0, +0x10 V1, +0x20 V2 (world units), +0x30
  N (unnormalised). Ignore the w words (the port writes vertex outcodes there). The return value is
  the **count of listed triangles, at most 63** (the write pointer is capped at list+0xFC0; later hits
  keep overwriting entry 63). Moby primitives are never listed, and the results block is not written.
  Moby mesh triangles **are** listed, with nothing to say they came from a moby (§3d).
- Corner case: if pos lies exactly on a face, the push divides by zero (FLT_MAX). And if the centre is
  already **behind** a thin face, +0x30 pushes it out on that same far side: the push cannot tell a
  centre that went through. Guard with a CollLine from a known-clear point (the game's LOS line does
  this job).

Side effects (H from the code): block+0x10 stamp += 1. moby+0xA0 is set to the stamp on every moby
tested. The 8-entry moby collision cache is refreshed (block+0x04 buffers of 8×2 KB, +0x08 key table,
+0x0C round-robin index, func_002A3110 called to rebuild an entry). The results block, or the list in
list mode. Scratchpad 0x70000000-0x70003FFF is used as temp (chunk buffers 0x70000000/0x70001000,
vertices 0x70002000, cell list 0x70003800), plus toSPR DMA register writes. No hit events, no hero or
camera state.

**Calling it from the mod's on_frame callback after the tick (M-H):** its memory footprint is
CollLine's (same block, scratchpad and DMA path), and the mod already calls CollLine there without
trouble. The writes are transient (game code reads the block straight after its own query), and the
cache only affects performance. Guards before calling: a level is loaded and you are in play,
`*(u32*)block != 0` (the octree pointer; it is read without a check), `*(s32*)(block+0x10) >= 0`.
Call only on the game thread (on_frame), not from on_pad. Not verified at runtime.

## 2. Where it lives: boot exe plus a copy in every level program (H)

It is a boot function, copied into every level program, like CollLine. Inside a level, call the
**level copy** through `function_at`, the same way the mod calls CollLine. Derivation: the decomp's
catalogue lists every place of a boot routine per level. config/us2/levels/functions.tsv (v2) and
config/levels/functions.tsv (v1) have a row for `func_00277258` whose 6th column is `LL:address`. The
kCollLineV200/V101 tables came from the same files (row `func_002761F8`). In the catalogue, **on both
discs and every level, CollSphere = CollLine + 0x1060, the moby query (§5) = CollLine + 0x2C38, and
func_00276158 (the octree cell lookup) = CollLine − 0xA0.** On the v2 disc I checked every level L00-L26:
the copy starts with the VU word 0x4A2004D8, and its `lui t7 / addiu t7` at **+0x04/+0x08** decode to the
same block as CollLine's +0x14/+0x18, which equals kCollBlockV200. L30 is not in the ISO's level
archives that tools/levels.py reads, so it was not checked.

```cpp
constexpr gaddr kCollSphereV200[31] = {   // = kCollLineV200[i] + 0x1060
    0x2DF8D8, 0x2D5048, 0x2E2E70, 0x2E0558, 0x3000E0, 0x2F8078, 0x31F0C0, 0x2D57D8, 0x2EB6D0, 0x2DB908,
    0x2FA578, 0x2F16D8, 0x2E2E80, 0x2EA440, 0x2E7258, 0x2F8678, 0x2D8BB0, 0x2E0A10, 0x2FA170, 0x2DD218,
    0x2F38D8, 0, 0x2E5B30, 0x2EA210, 0x2DE8F8, 0x2F29F8, 0x2DFFF8, 0, 0, 0, 0x2E6690};
constexpr gaddr kCollSphereV101[31] = {   // = kCollLineV101[i] + 0x1060
    0x2DF510, 0x2D4CE0, 0x2E2A28, 0x2E0098, 0x2FFD70, 0x2F7CB0, 0x31EBF8, 0x2D5570, 0x2EB360, 0x2DB620,
    0x2FA1B0, 0x2F1378, 0x2E2C28, 0x2EA000, 0x2E6E10, 0x2F82B0, 0x2D87F0, 0x2E05D0, 0x2F9DC0, 0x2DCFB8,
    0x2F3518, 0, 0x2E5768, 0x2E9E48, 0x2DE530, 0x2F2630, 0x2DFBB0, 0, 0, 0, 0x2E63C8};
constexpr gaddr kCollHitV200[31] = {      // = kCollLineV200[i] + 0x2C38  (moby query, §5)
    0x2E14B0, 0x2D6C20, 0x2E4A48, 0x2E2130, 0x301CB8, 0x2F9C50, 0x320C98, 0x2D73B0, 0x2ED2A8, 0x2DD4E0,
    0x2FC150, 0x2F32B0, 0x2E4A58, 0x2EC018, 0x2E8E30, 0x2FA250, 0x2DA788, 0x2E25E8, 0x2FBD48, 0x2DEDF0,
    0x2F54B0, 0, 0x2E7708, 0x2EBDE8, 0x2E04D0, 0x2F45D0, 0x2E1BD0, 0, 0, 0, 0x2E8268};
constexpr gaddr kCollHitV101[31] = {      // = kCollLineV101[i] + 0x2C38
    0x2E10E8, 0x2D68B8, 0x2E4600, 0x2E1C70, 0x301948, 0x2F9888, 0x3207D0, 0x2D7148, 0x2ECF38, 0x2DD1F8,
    0x2FBD88, 0x2F2F50, 0x2E4800, 0x2EBBD8, 0x2E89E8, 0x2F9E88, 0x2DA3C8, 0x2E21A8, 0x2FB998, 0x2DEB90,
    0x2F50F0, 0, 0x2E7340, 0x2EBA20, 0x2E0108, 0x2F4208, 0x2E1788, 0, 0, 0, 0x2E7FA0};
```
Results block, three equivalent ways:
1. Decode the `lui/addiu` at CollSphere+0x04/+0x08 (opcodes 0x0F/0x09).
2. Reuse kCollBlockV200 or CollLine's decode.
3. `mod.relocate(v2 ? 0x1BAF00 : 0x1BAF80)`. The exe addresses are relocated (H): the native body
   computes its block exactly this way (`openrac_relocate_data(0x1baf00)` in func_00277268,
   `0x1baf80` in the ntsc build), and the API's relocate_data is the same range check plus call.

Every v2 level then has: **triangle list = block + 0x5280**, **moby hit list = block + 0x4080**, hit-event
ring = block + 0x4280 (checked L00-L26; the v1 and v2 exe addresses have the same offsets:
v1 0x1BAF80/0x1C0200/0x1BF000/0x1BF200, v2 0x1BAF00/0x1C0180/0x1BEF80).

By name (M): the port has one native body per boot routine that relocates its data to the loaded level.
So `mod.function("func_00277268")` on ntsc2 or `"func_00277258"` on ntsc should behave the same as the
level copy. That is not verified; `function_at(level copy)` is the path already proven by CollLine.

## 3. Masks (bit meanings H from the code; meanings of the flags themselves M/L)

CollSphere and CollLine share the face filters (func_00277258.c:184-207, func_002761F8.c:187-217):

| bit | effect |
|---|---|
| 0x01 | no level faces (mobys only) |
| 0x02 | no moby primitives (spheres and capsules); mobys without a triangle mesh (col+0xC == 0) are skipped entirely |
| 0x04 | choose the moby collision set: with 4, count at col+0 and primitives tagged 1; without, col+2 and tag 2. Every camera mask sets it; gameplay masks (0x24, 0x22, 2) mostly do not (M: "camera" set) |
| 0x08 | **CollLine only**: with 0x8 (or 0x1), mobys are tested only if u16 moby+0x34 & 0x4000 (func_002761F8.c:627). 0x4000 marks mobys that get hit-event records (M). CollSphere has no such test |
| 0x10 | two-sided faces (without it, a face whose front is away from the query is ignored) |
| 0x20 | leave out faces whose material (type byte & 0x1F) == (flags >> 8) & 0x1F |
| 0x40 | CollSphere list mode (§1) |
| 0x80 | leave out faces whose type byte has bit 0x80 (the SDK only says "a face the queries with flag 0x80 leave out"; the camera's line and sweep set it, its probe and eye push do not; L as to what those faces are) |
| 8-12 | the material for 0x20 |

**Which masks the follow camera uses.** cam_select_coll_masks (src/game/00271FD8-g8.c:23-62; v2 L00 copy
0x2D9BA8, checked) rewrites them every tick. All are L00 bss, so read each tick with
`relocate_level(0, addr)`:

| global (v2 / v1 L00) | value | used by |
|---|---|---|
| g_cam_collision_line_mask 0x1B17CC / 0x1B184C | 0xB4, or 0x1B4 / 0x3B4 / 0xBB4 / 0xDB4 | LOS CollLine (HideObstructions), update_collision, update_orientation |
| g_cam_collision_point_mask 0x1B17D0 / 0x1B1850 | 0x94, or 0xB4 in hero states 17/18 or mode 112 (water), or in map 6's box | arm-sweep CollSphere and cam_push_point_out_of_walls |
| g_camera_default_tracker_word (the saved mask) 0x1B17D4 / 0x1B1854 | 0x14 or 0x34 | copied every tick to st+0x238 (L00_0041ABB8-g8.c:250) = the probe's CollSphere mask |
| immediate 0x36 | 0x36 | move_eye's eye push-out (CollSphere r 0.25) |

0xB4 = 0x80 | 0x20 | 0x10 | 0x04: no 0x80 faces, no faces of material (mask>>8), two-sided, camera set.
The material follows the hero's special-ground flags: +0x1494 → 0, +0x1495 → 1, +0x1496 → 3,
+0x149B → 11, +0x149C → 13. When none is set, the last material is kept at v2 0x1B8E60. Barlow's
UpdateHeroGroundMaterialFlags (src/levels/04_barlow/L04_002D38E8.c:53-100) sets +0x1494 for ground
type 0 (computes water depth and wading) and +0x149C for type 13 (sinking ground). So **material 0 is
water** (M): by default the camera line passes through water surfaces. The arm sweep (0x94) does
collide with water, except when swimming (0xB4).

### 3d. Leaving mobys out (the camera wants level only)
- No mask bit makes **CollSphere** level-only. `skip` removes one moby. 0x2 removes primitive-only mobys
  (the hero and Clank are probably among those, M) but not mesh mobys (crates, platforms, the ship).
- **The game's own trick** (HideObstructions, src/levels/shared/L00_004243C0-g8-dbg.c:58-73, 139-145):
  1. It asks for the mobys overlapping a sphere round the arm (§5).
  2. It sets their **moby+0x98 (collision pointer) to 0**. The decomp calls this field `visible`, but
     +0x98 is the `col` pointer CollSphere reads (func_00277258.c:573).
  3. It runs its sweep, probe and LOS, then restores the pointer from the class (moby+0x24)->+0x10.
  Classes 2234, 0xD07 (Jobian Saur-Beast), 0x10D0 (Yeti), 0x477 and 0xB (Megacorp vendor and logo) are
  not cleared; the arm is steered ±1°/tick around them instead (:68-92). That is why the game camera
  passes over crates and the ship.
- **Less invasive trick (M-H, from the code):** the moby loop skips any moby with `moby+0xA0 == stamp`,
  where stamp = block+0x10 + 1 for the next query (func_00277258.c:583; asm `lw v1,0xA0(t8); beq v1,s1`;
  CollLine checks the same at func_002761F8.c:653-657). Write `moby+0xA0 = *(s32*)(block+0x10) + 1` for
  each moby to leave out, **immediately** before your CollSphere or CollLine call. There is nothing to
  restore: the next query uses a new stamp. Do not do this while the stamp is negative.

## 4. How the follow camera uses spheres (cost per tick)

From the decomp, with v2 L00 disassembly checks. B = st+0xB0, D = st+0x18C, deficit = st+0x220.
- **HideObstructions** (L00_004243C0-g8-dbg.c:57-58): 1 moby query (§5). Centre = midpoint of B and the
  current camera position. r = (D − deficit + 1.5)/2, about 3.1 at D 4.64. flags 1, skip = hero.
- **Arm sweep** `cam_follow_sweep_probe` (L00_004240D8-g8-dbg.c:40-86; v2 0x4255E8), skipped when
  s16 st+0x23E is set. 7 steps; step 0 is not tested. The radius starts at g_cam_collision_base_radius
  0.25 (v2 0x1B1470), grows by st+0x234·st+0x22C = 0.1 per step, and the step count is
  FloatToInt(st+0x230 − 1) = 7. So **6 CollSphere** calls, radius 0.35, 0.45 … 0.85, at distances
  0.35, 0.8, 1.35, 2.0, 2.75, 3.6 (× (D−deficit)/D) from B along the goal arm, mask = point mask,
  skip = hero. A sample inside a camera volume (§6c) is not tested. On a hit, cam_push_point_out_of_walls
  (L00_00423CA8-g8.c:44, 55) makes up to 2 more CollSphere calls and the goal is re-aimed.
- **Probe** `cam_follow_probe_collision` (L00_004239A8-g8.c:43-64; v2 0x424EB8): up to 3 tries at B+goal.
  Each try is a camera-volume test, or else CollSphere with **r = 0.25 + 0.1·1·(8−1) = 0.95**, mask
  st+0x238 (0x14 or 0x34), skip = *(v2 0x1B8E64), a moby pointer held by the camera manager (likely the
  hero, not traced). After a hit the probe point moves to +0x30. The contact is classed against the up
  axis: dot > 0.75 / < −0.75 → st+0x228 = ±1.
- **LOS**: 1 CollLine B → B+goal (L00_004243C0-g8-dbg.c:134).
- **move_eye eye push** (v2 0x4275F4/0x427648): up to 3 CollSphere, mask 0x36, skip 0, r 0.25, applied
  only for near-vertical contacts (|dot| > 0.5). In level 13 below z 120 it uses r 0.3 and always applies.
- update_collision: 2 floor/ceiling CollLines; update_orientation: a CollLine on reposition only.
- Total: typically **~10 CollSphere + 3-4 CollLine + 1 moby query per tick**, worst case ~25 sphere
  tests. Cost scales with the cells under the box: about (2r/4+1)³ 4-unit cells, each with all its
  vertices unpacked. The port runs this code in EE-exact soft float (`openrac_ps2_add`/`mul`/`sub` calls
  in the native body), so one CollSphere costs more than a host-float routine would. Small r (≤ 1) is
  1-8 cells. Measure in the mod (std::chrono) before adding many calls.

## 5. "Mobys within radius of a point": func_00277F48 (H)

Named `coll_process_hit` in names.tsv; the name is misleading. It is the same engine routine as UYA's
func_003D4D00 (`(pos, flags, skip, event, radius)`, rac3 src/frontbin/3D3CB0.c:14).
- Prototype (nonmatching/functional/func_00277F48.c:113):
  `int func_00277F48(void *pos, int flags, char *skip, void *rec, float r)`.
  Native (x86 disassembly of func_00277F58 on ntsc2, func_00277F48 on ntsc): edi pos, esi flags,
  edx skip, ecx rec, xmm0 r →
  `using CollHitFn = int (*)(gaddr pos, std::uint32_t flags, gaddr skip, gaddr event, float r);`
- It tests **mobys only** (no level faces), with the same moby loop as CollSphere: triangle meshes and
  primitives against the sphere. flags bits 0x4, 0x10, 0x20 and 0x80 apply to their triangles.
  pos must be 16-byte aligned, and pos ± r must be inside the world cube. There is no clamp at 10.
- Returns the **count** of mobys whose collision touches the sphere. Their pointers are written to the
  **hit list at block + 0x4080** (u32 each, 0-terminated; ~127 fit before the ring at +0x4280, and
  there is no bounds check). It also writes block+0x18 = the first moby, +0x1C = 63, +0x20 = (0,0,0,1)
  (it **overwrites the hit point**), and bumps the stamp.
  **Pass event = 0.** Non-zero posts hit-event records (damage) to mobys flagged 0x4000
  (func_00277F48.c:552-597).
- Per-level addresses: kCollHitV200/V101 in §2 (= CollLine + 0x2C38).
- Camera use: one call per tick in HideObstructions (§4). Its result decides which mobys the camera
  ignores (§3d).

## 6. Other sphere-ish functions (none is a better entry point)

- a. `CollideSweptSphere` func_002FE9D0 (src/game/002FC8D0.c:318-414; boot plus level copies, v2 L00
  0x37EDB8). A projectile step: CollSphere at the end point, otherwise a CollLine from sphere+0x00 to the
  end. On a hit it either bounces the velocity or snaps the position, returns 1 or 2, and writes into its
  `SweptSphere` struct. It needs a valid owner moby (it calls moby_get_facing_direction on
  sphere->moby+0x10). **Not a continuous sweep, and not useful for the camera.**
- b. `cam_clamp_segment_by_collision` func_L00_0042A908 (L00_0042A908-g8.c:18-52; v2 0x42BDF8). 16
  CollSphere samples along start→end, radius 0.075 → 0.5, mask 0x10. Used by
  moby_update_follow_position, another camera type, not the follow camera.
- c. **Camera collision volumes**: rac2 does have UYA's camera_collider (rac3-strafe-camera.md §2c says
  it does not). `FindIntersectingGridPrimitive` func_L00_002DB350 (src/levels/shared/L00_002D7940-grid.c:26-55;
  v2 L00 0x2DB718) is a 64×64 grid of 16-unit cells of level-authored shapes. `coll_is_point_in_grid`
  func_L00_004204A0 (v2 L00 0x4219A0) tests a point against it. `coll_find_nearby_shape_points`
  func_L00_00420388 (L00_00420388-g8-dbg.c:15-42; v2 L00 0x421888) pushes a point out of a shape: it
  writes block+0x30 (outside) and block+0x20 (on the surface). These are level-shared functions with
  per-level copies (catalogue rows as in §2). The probe pushes the camera out of these shapes; the
  sweep and LOS ignore samples or hits inside them. A mod boom ignores them today (M: minor).
- d. names.tsv:237-240 `coll_sphere` / `coll_capsule` / `coll_sphere_mobys` at func_L00_001F10E0 and
  nearby are not rac2 functions. They are imported names; the catalogue has no such rows.
- e. **SDK `collision_triangles(lo, hi, out, max)`** (since 1.12; mod.hpp has a std::vector wrapper,
  available in SDK 1.20). The 1.36 docs list it as "no" for rac2. But the 1.1.1-dev ntsc2 binary has
  `rac2_collision_triangles` (0xEC2 bytes, reads 0x1BAF00 and the moby table) installed in the rac2 play
  table at the slot api_collision_triangles calls (+0x230). M: **it is probably implemented now; check
  that it returns triangles in a level.** If it does, it gives level triangles (moby == -1) with their
  type byte: material = type & 0x1F, bit 0x80 = the 0x80 faces. That is everything §7b needs, with no
  guest call and exact mask filtering of your own.

## 7. Call recipes

Common setup: `at = (v2 ? kCollSphereV200 : kCollSphereV101)[level]`,
`fn = mod.function_at<CollSphereFn>(at)`, `block` as in §2, `hero = memory.read<gaddr>(0x18C0B0)`.

### 7a. Is a sphere at P overlapping, and where should it go?
```cpp
openrac::StackFrame f(mod, 0x10);
const gaddr q = f.address();
write xyz of P at q, 1.0f at q+12;
for (int i = 0; i < 3; ++i) {                       // the game resolves corners in up to 3 passes
    if (fn(q, mask, hero, r) == 0) break;           // clear
    const gaddr moby = memory.read<gaddr>(block + 0x18);     // 0 = level
    // closest = read3(block+0x20); pushed = read3(block+0x30); depth = r - |P - closest|
    copy 16 bytes block+0x30 -> q;                   // move to the pushed centre and test again
}
```
mask: the game's point mask `relocate_level(0, v2 ? 0x1B17D0 : 0x1B1850)` (0x94/0xB4), or 0x36 for the
eye. To leave mobys out, stamp them first (§3d) or skip a moby hit and test again.

### 7b. Sweep a sphere of radius r from A to B (UYA's method)
1. Split A→B into pieces of at most ~3 u. This keeps R small (cost) and under the 63-triangle cap. For
   each piece [a, b]: mid = (a+b)/2, R = r + |b−a|/2 + 0.05 (must be ≤ 10, and mid ± R inside the world cube).
2. To leave mobys out (optional), call the moby query `hit(mid, mask, hero, 0, R)`, then stamp the listed
   mobys: n = return value, pointers at block+0x4080; write moby+0xA0 = *(s32*)(block+0x10) + 1.
3. `n = fn(mid, mask | 0x50, hero, R)`: list mode plus two-sided. If n == 63, the list may be full:
   split the piece and redo it.
4. For each listed triangle (V0/V1/V2 at block+0x5280+64·i, +0x00/+0x10/+0x20), run the swept-sphere
   test below. Keep the smallest t. The first piece with a hit gives the contact:
   centre = a + (b−a)·t, fraction overall = (piece offset + t·|b−a|)/|B−A|.
5. To apply the mask's face filters yourself (with collision_triangles), skip faces with type & 0x80,
   and faces with material == (mask>>8)&0x1F when mask & 0x20. In list mode the game has already applied
   them.

Swept sphere vs triangle (two-sided, as with 0x10). This exact code matched a brute-force reference in
20000 random cases (0 mismatches, 2289 hits):
```cpp
// Smallest t in [0, tmax) with a t^2 + b t + c = 0; c <= 0 means touching at t = 0.
static bool first_root(float a, float b, float c, float tmax, float& t) {
    if (c <= 0.0f) { t = 0.0f; return true; }
    if (a <= 1e-12f) return false;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;
    const float r = (-b - std::sqrt(disc)) / (2.0f * a);
    if (r < 0.0f || r >= tmax) return false;
    t = r; return true;
}
// Sphere radius r moving A -> A+d (t in [0,1]) against v0 v1 v2. Lowers t_best (start it at 1) and sets
// n_out (unit, from the surface toward the sphere) on an earlier contact.
static bool sweep_triangle(V3 A, V3 d, float r, V3 v0, V3 v1, V3 v2, float& t_best, V3& n_out) {
    V3 n = cross(v1 - v0, v2 - v0);
    const float nl = std::sqrt(dot(n, n));
    if (!(nl > 1e-12f)) return false;
    n = n * (1.0f / nl);
    float d0 = dot(A - v0, n);
    if (d0 < 0.0f) { n = n * -1.0f; d0 = -d0; }          // face the start (two-sided)
    const float nd = dot(d, n);
    const V3 tn = cross(v1 - v0, v2 - v0);
    auto inside = [&](V3 q) {
        return dot(cross(v1 - v0, q - v0), tn) >= 0 && dot(cross(v2 - v1, q - v1), tn) >= 0
            && dot(cross(v0 - v2, q - v2), tn) >= 0;
    };
    bool hit = false;
    if (d0 >= r) {
        if (nd >= 0.0f) return false;                      // moving away: never within r of the plane
        const float t0 = (d0 - r) / -nd;
        if (t0 >= t_best) return false;
        if (inside(A + d * t0 - n * r)) { t_best = t0; n_out = n; return true; }
    } else if (inside(A - n * d0)) {                       // already overlapping the face
        t_best = 0.0f; n_out = n; return true;
    }
    const V3 vs[3] = {v0, v1, v2};
    const float dd = dot(d, d);
    for (int i = 0; i < 3; ++i) {
        float t;
        const V3 w = A - vs[i];                             // corner
        if (first_root(dd, 2.0f * dot(d, w), dot(w, w) - r * r, t_best, t)) {
            t_best = t; const V3 p = A + d * t - vs[i]; const float l = std::sqrt(dot(p, p));
            n_out = l > 1e-9f ? p * (1.0f / l) : n; hit = true;
        }
        const V3 e = vs[(i + 1) % 3] - vs[i];               // edge
        const float ee = dot(e, e), ed = dot(e, d), ew = dot(e, w);
        const float a = ee * dd - ed * ed, b = 2.0f * (ee * dot(d, w) - ed * ew);
        const float c = ee * (dot(w, w) - r * r) - ew * ew;
        if (first_root(a, b, c, t_best, t)) {
            const float f = (ew + ed * t) / ee;
            if (f >= 0.0f && f <= 1.0f) {
                t_best = t; const V3 p = A + d * t - (vs[i] + e * f); const float l = std::sqrt(dot(p, p));
                n_out = l > 1e-9f ? p * (1.0f / l) : n; hit = true;
            }
        }
    }
    return hit;
}
```
(V3 with +, −, ×scalar, dot and cross, as in mod.cpp.) Cost: 1-2 game calls per piece, plus cheap host
maths over at most 63 triangles.

### 7c. Cheap alternative: the game's own way
Sample CollSphere along the path the way the arm sweep does (§4: 6 spheres growing 0.35 → 0.85). Or use
it as a distance oracle ("sphere tracing"): at P, `fn(P, mask, skip, r + L)`. With no hit, advance by L;
otherwise d = |P − closest|, and you have contact when d ≤ r + ε, else advance by d − r. It is safe
(it never overshoots) but slow when grazing parallel to a wall, so cap it at ~8 steps.

### 7d. For the boom jitter (thin lines on edges, branches and grazing floors)
1. Replace the 5-ray cast with the 7b sweep. Suggested settings, following UYA: r 0.3-0.4 from the guarded
   hang point along the boom, line mask (`relocate_level(0, 0x1B17CC/0x1B184C)`), mobys stamped out.
2. Set L = t·len. The sphere centre at contact is already r clear of the face, so only a small epsilon is
   needed, not the 0.35 line margin. Contact normal = n_out.
3. Keep the instant-in / eased-out length logic. A sphere does not flicker on edges the way a line does,
   because an edge hit is continuous in t.
4. The hang-point guard (pivot → anchor) can use the same sweep with UYA's 0.4.

## 8. approach_eased analogues (H for the code)

- **`math_approach_target_with_braking`** func_002AAC80 (src/game/002AAC80-smoothing.c:8-38; boot plus
  level copies, v2 L00 0x31F4B0, v1 L00 0x31E238):
  `void (float *speed, float error, float accel, float brake, float limit)`. Native: speed is a guest
  pointer. This is UYA's approach_eased without the position update:
  - If speed has the same sign as error: when |error| < speed²/(2·brake), brake (step toward 0 by
    brake, or 1.1·brake when it would overshoot by more than |error|). Otherwise ApproachValue speed
    toward ±min(limit, sqrt(2·brake·|error|)) by accel.
  - Then clamp |speed| to |error| (no overshoot).
  - Otherwise (opposite sign, or error == 0) step speed by brake toward the target and clamp at error.
  - The caller adds speed to the value: src/game/002B52A0-g8.c:130 (vertical velocity, 0.0111/0.0111,
    max 1.0); src/game/00287BD8.c:61 (camera turn ease, 0.000582 rad/tick², max 0.0524 rad/tick).
  - Note: it never zeroes speed by itself, so with error exactly 0 and speed ≠ 0 it dithers by ±brake.
    UYA's version snaps instead.
  - **Re-implement it in C++** rather than calling the game (pure maths, no guest memory needed).
- `ApproachValue` func_002AA2E8 (src/game/002A7750.c:833): `float (float *v, float target, float step)`:
  move by at most step, return |remaining|.
- The follow camera's own easing is the spring `math_step_spring_damper_clamped` func_002702C8
  (src/game/0026EA00.c:580): `v += k·diff − c·v`, clamped to ±limit (when limit ≠ 0) and to |diff|,
  returns current + v. It eases D, h and the eye springs. The deficit recovers by cosine blend over a
  2000 timer (HideObstructions :114-125).
- Mobys within radius: §5.

## 9. Not determined / risks

- Not run in game: everything here comes from code. The CollSphere call path is the same as CollLine's,
  which works, but CollSphere itself has not been called from the mod yet. Check first: at the Dobbo
  crate spot, compare CollSphere's +0x20 with a CollLine hit on the same floor.
- What the 0x80 faces are, what flag 0x4's moby set is, and what 0x4000 mobys are (§3): read from code
  only. Material 0 = water is inferred from Barlow's ground-type code (M).
- `*(v2 0x1B8E64)` (the probe's skip moby) is not traced.
- Whether `mod.function("func_00277268")` equals `function_at(level copy)` at runtime (M), and whether
  collision_triangles really answers on rac2 (M, §6e).
- L30: the addresses come from the catalogue only (CollLine + 0x1060 / + 0x2C38); not checked on the disc.
- Perf: soft-float cost per call not measured.
- The functional C is a port rewrite rated "medium" by its authors, though it passed a gate check against
  retail. A few retail details are different on purpose (leftover w words, a leftover a3 in a cache
  entry). None of them affect the outputs listed here.
