# rac2 aim (L1 look, hero state 29) camera: where yaw/pitch really live

Source: rac2-decomp (v1.01 C) and a rabbitizer disassembly of the v2.00 disc's level 0.
v2.00 addresses are 0x80 lower than v1.01 for the level camera block.

## Chain per tick
cam_update_state (L00 copy: v1 0x2D9B70 / v2 0x2D9F38)
  -> cam_update_active -> current record's update; for the aim camera (record +0x86 == 4)
     that is cam_aligned_update_from_input (v1 0x4266E8 / v2 0x427BF8), src/levels/shared/L00_004266E8-g8-hazard.c
  -> copy record rows/pos into the camera block (pos +0x140, matrix +0x370)
     (if hero_is_action_allowed(), the first-person mode, it uses hero +0x14D0/+0x14E0 instead)
  -> mat_get_euler_angles(matrix, block+0x150) => the "yaw/pitch globals" are OUTPUTS.

## Authoritative state (aim camera)
rec = *(u32*)g_follow_camera_object  (v1 0x1B8E10 / v2 0x1B8D90)
rec+0x00 forward (unit, world)  <- the integrator; rotated by stick deltas
rec+0x10 side = norm(fwd x up), rec+0x20 up' = side x fwd (rebuilt each tick); rec+0x30 eye pos (rebuilt)
vars = *(u32*)(rec+0x70): +0 a (eased yaw input) +4 va, +8 b (eased pitch input) +C vb,
  +10 ang (accumulated pitch, rad, used for clamp; >0 = up), +14 z (fwd push), +18 vz
spring: v += 0.03*(t-x) - 0.4*v; yaw step = 0.0261799*|a_old|*a (90 deg/s max)
pitch soft clamp from 1.117 rad, hard 1.466 rad.

## Mod write set (absolute)
f = (cos p cos y, cos p sin y, -sin p)   (game euler: p<0 looks up)
rec+0x00 = f ; vars+0x00..0x0C = 0 ; vars+0x10 = asin(f . up) (clamp 1.466)
optional: rows +0x10/+0x20, euler globals (v2 0x1B8D54/58) for zero-lag hero facing.
Handback: just stop.
