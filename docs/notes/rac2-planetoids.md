# Planetoids: gravity, first person and shots

How Going Commando handles its sphere worlds (the Jamming Array's planetoids), and what Dead Aim
changes there. Offsets are from the hero block at 0x189E20 (the mod's `kHero`). Paths are in the
[rac2 decompilation](https://github.com/OpenRAC/rac2-decomp), under `src/` unless they start with
`game/`. Confidence: H read in the code, M inferred, L a guess.

## 1. The gravity

- A level header can carry a sphere entry: an int flag, then the centre's x, y, z
  (`levels/shared/L00_002FC7A0-00300618-g8.c:391-399`). Its w is forced to 1.0, so gravity points
  at the centre and he walks on the outside. No radius is stored. H.
- At hero init the centre is copied to hero+0xB0, which stays put while the level's own global
  moves between levels: read it from the hero (`L00_002A3B80.c:146-147`). H.
- The mode byte hero+0x22C3 is written each tick: 0 down, 1 a strip's axis, 2 about the centre.
  Mode 2 is set exactly when the sphere flag is on and the axis flag off (`L00_002D3400.c:79-89`). H.
- Hero+0x2C0 is his gravity, downward: (0, 0, -1) on level ground. His matrix's third row,
  hero+0x20, is his up, and in mode 2 it leans with the gravity (`hero_update_lean_reference`,
  `L00_002B91C0.c:144-159`). M.
- Measured on the Jamming Array (level 26, USA v1.01): the centre is (500, 200, 500), the radius
  about 30, and Ratchet starts about 31 degrees off its top, at (499.9, 215.4, 526.0).

## 2. The game's first person

- It is allowed there: neither `hero_is_action_allowed` nor the menu toggle checks the gravity. H.
- The view (`UpdateHeroAimAndTransitions`, `L00_002D3798-aim-g8.c:72-152`): when the gravity's z
  (hero+0x2C8) is above -0.99, the look vector hero+0x14E0 is taken into his frame (the matrix at
  +0x40), its yaw and pitch clamped to 85 degrees there, and put back (the matrix at +0x00). Within
  8 degrees of straight up it uses the world's frame instead. H.
- The camera (`hero_update_camera_block`, `L00_002D64B8.c:36-56`): the eye is his position plus
  1.5 along hero+0x20, it looks along +0x14E0, and its right is the look crossed with hero+0x20, so
  its roll follows his up. H.
- **The flaw:** the look vector is kept in the world. As he walks round, his up turns under it, so
  a level look ends up pointing at the ground and then off into space. Seen in game with the mod
  off: walking from the start, the view went to open sky within four seconds. H.

## 3. Shots

- `part_steer_effect_direction` (`game/002FEE70.c:42-100`) runs while the sphere flag is on. Each
  tick it turns a shot's velocity as the local up turned since the last tick, keeping its speed. So
  a shot keeps its angle to the surface: a level one follows the curve and one fired upward keeps
  climbing. H.
- It steers the Lancer and Heavy Lancer, the Gravity Bomb (which also falls toward the centre), the
  Seeker, Hoverbomb, Mini-Nuke, Visibomb, Lava Gun, mini-turrets and others. The Vaporizer's bolt is
  not steered and flies straight. H.
- The first shot's direction: in first person a ray from the camera, pitch included, clamped to 20
  degrees of +0x760. In third person the game fires along +0x760, his facing, with no pitch. No
  path drops the pitch for a straight shot. H.
- Measured with the mod (level 26): a Lancer bolt fired 38 degrees up left at about 32 degrees to
  the surface and held 32 degrees for its 20 units.

## 4. What Dead Aim does

- **First person is the game's own, kept level:** each tick `first_person_carry` turns +0x14E0 by
  the turn his up made since the last tick (the same turn the game gives its shots). It uses
  hero+0x20 under any custom gravity, so crossing the game's 8-degree switch near the top does not
  jolt it. The mod's own first-person turning (stick, mouse, gyro) and its hand-off into first
  person (`view_from_camera`) work about the same up the game uses.
- **Shots go at the crosshair's point** in third person too, pitch included, as the game's first
  person aims them. In flight the game's steering keeps them at that angle to the surface.
- **Lock-strafe in first person:** the game's L1 and L2 fire there, so the mod never holds them
  while the game's first person shows (the game strafes there itself).
- **Tried and dropped:** the Modern camera brought into his eyes as a stand-in first person. It
  showed a faded Ratchet and lagged his jumps, and the carried first person needs neither.
