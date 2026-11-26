# rac2 FOLLOW camera: right-stick tilt, the pitch rate limits, and how a mod drives it

Sources: rac2-decomp v1.01 C (src/levels/shared) plus a rabbitizer disassembly of the v2.00 level 0.
Unless marked, addresses are for **level 0 (L00)**. In v2.00 the level code is +0x1510 from v1.01 and the level sdata constants are -0x60.
rec = *(u32*)g_follow_camera_object (v1 0x1B8E10 / v2 0x1B8D90), st = *(rec+0x70). The follow mode is rec+0x86 == 0.

## Per-tick order (v2 hero_refresh_linked_object 0x4278C8 / v1 0x4263B8)
TranslateObjectAndView -> cam_follow_update_us2 0x4239D0 (picks the setup: distance/height/speeds)
-> cam_follow_update_collision 0x423150 (pivot st+0x60/+0xC0, look-at st+0xB0, ceiling/floor lines)
-> cam_follow_run_frame 0x427850 (v1 0x426340):
   cam_follow_update_reach 0x4269A0  (goal orbit st+0x160 lerped 0.85 toward st+0x00 - st+0xB0, length clamp)
   ship_read_steer_and_throttle 0x4264A8 (v1 0x424F98)   <- reads the sticks
   cam_vehicle_update_turn 0x426628 (v1 0x425118)        <- tilt st+0x1C8 and the RATE CAP
   FollowCameraHideObstructions 0x4258D0 -> cam_follow_probe_collision 0x424EB8 -> cam_follow_constrain_distance 0x424CD8
                                     (pushes the goal eye out of the floor and walls, rewrites st+0x160, st+0x210 = goal eye, st+0x218 = its z)
   cam_follow_move_eye 0x4271E0 (v1 0x425CD0)            <- smoothed orbit st+0x170, eye rec+0x30, eye push
   follow_camera_build_view 0x426A70 (v1 0x425560)       <- look angle st+0x34, the pitch, rows
-> weapon_reset_view_state 0x423768 (v1 0x422268, C in L00_0041ABB8-g8.c:174): END-OF-TICK RESET:
   st+0x1D0 = 0 (input mask), st+0x1DC = st+0x1E0 = st+0x1E4 = 0, st+0x1D4 = yaw rate (*0x1A8904),
   st+0x3C = 0.005, st+0x40 = 0.2 (look-angle spring), st+0x13C/140/144/148 = 0.015/0.2/0.0125/0.2 (PIVOT springs, not tilt), st+0x10 = 1.
   So values a mod writes in on_frame (after the tick) are used by the next tick, then reset.

## Stick read: ship_read_steer_and_throttle (L00_00424F98-g8.c)
Input block = st+0x1C0. Yaw = g_stick_axis_x (0x138280); if mask bit 1 of st+0x1D0 is set, or the stick reads 0, it uses st+0x1DC.
Tilt input = g_stick_axis_y (0x138284), sign set by the invert option (v2 0x1A7BAC). If mask bit 2 (st+0x1D0 & 2) is set,
**or the stick reads exactly 0**, the input is st+0x1E4 / 0.6981318 (cam_set_tuning_value writes st+0x1E4; it is zeroed each tick).

## cam_vehicle_update_turn (L00_00425118-g8.c; v2 0x426628..0x42699C)
v2 constant block SIG (L00 0x1B144C; v1 L00 0x1B14AC):
 +0x00 1.0, +0x04 1.0 (spring k,c) | +0x0C 0.02 factor_idle | +0x10 0.01 factor_straight |
 +0x14 0.0305433 **turn_limit** (rad/tick) | +0x18 0.6981318 divisor (40 deg) | +0x3C/+0x40 0.015/0.2 eye yaw spring |
 +0x44/+0x48 0.015/0.2 **eye elevation spring**.
1. input u: |u|<=0.3 -> 0, otherwise (u -/+ 0.3)*1.4285715  (so the goal G is in -1..1).
2. factor = 0.01 if u==0 or (|u|<|tilt| and same sign), otherwise 0.02.
3. tilt st+0x1C8 = tilt + clamp(G - tilt, +-factor) (spring k=c=1 => vel st+0x1CC = clamped diff).
4. amount = 0.698*tilt. angle = atan2(orbit.up, |orbit|) = atan(sin(elevation)) of the goal orbit st+0x160.
5. **re-derive**: if |angle| < |amount| and factor == 0.01 then tilt = angle/0.698. This is the value you saw (0.21, 0.45...).
6. step st+0x198 = clamp(amount - angle, +-turn_limit); a step that lowers the orbit is set to 0 when st+0x228 < 0. The goal orbit is rotated by the step about norm(up x orbit).
   If amount < 0 then st+0x22 = 1.
=> the cap is turn_limit = 0.0305 rad/tick (1.75 deg/tick, ~105 deg/s at 60 Hz), in atan(sin el) units, = 0.044 tilt/tick.
At tilt +-1 the target is atan(sin el) = 0.698, so el = +-57 deg.

## Later lags and limits
- move_eye (L00_004224D0-g8.c:48): the smoothed orbit's elevation follows the goal's with
  math_step_spring_damper_stored(st+0x184, ..., k=SIG+0x44 0.015, c=SIG+0x48 0.2). That is second order, ~0.5 s to settle (a big lag of its own).
  Yaw: st+0x180 with SIG+0x3C/+0x40. Length: st+0x188, ease speeds.
  eye rec+0x30 = st+0xC0 + up*st+0x190 + st+0x170, then a coll_test_point r=0.25 push (pivots shifted by the same delta).
- Floor and walls: probe/constrain push the GOAL eye (st+0xB0 + st+0x160) out of geometry and set st+0x160 = pushed - st+0xB0.
  That limits a low eye, so looking up runs out.
- build_view (L00_004224D0-follow-g8.c:90-113): E = signed angle from the eye to the look-at (vertical = orbit.z + d);
  look-angle target t = 0 unless x = angle/0.698 < -0.1, then x = -x, x = 1-x if x>0.5, t = 2x*0.2618 (max 15 deg at x=0.5).
  h st+0x34 = stored spring(st+0x38, k=st+0x3C 0.005, c=st+0x40 0.2) (gate g_controller_active 0x18A160).
  pitch = clamp(E - h, +-1.2217 rad = 70 deg).

## Mod write set (recommended)
On level load, scan the level's sdata (0x1A0000-0x1C0000) for the 9 floats
[1,1,0.075,0.02,0.01,0.030543264,0.6981318,0.2,1.5]. Each v2 level has exactly one copy. L00 0x1B144C, L01 0x1B1B5C,
L02 0x1B2BCC ... (the eye springs are always at SIG+0x3C). Save the originals, then patch:
  SIG+0x14 turn_limit = 1.5     (needs >= 1.484 to arrive in one tick and < 2.35 so FastAddRotations does not wrap)
  SIG+0x44 = 1.0, SIG+0x48 = 1.0 (eye elevation spring: k=c=1 gives v = diff, which lands exactly)
  (optional SIG+0x3C/+0x40 = 1.0 for the yaw lag)
Each on_frame (rec+0x86 == 0), with T = commanded tilt clamped to -1..1:
  u32 st+0x1D0 |= 2                       (mask the right stick Y for the next tick)
  f32 st+0x1E4 = 0.6981318*sgn(T)*(0.3+0.7|T|)   (0 when T == 0) -> goal G == T after the deadzone remap
  f32 st+0x1C8 = T ; f32 st+0x1CC = 0
  look angle, pick one:  st+0x3C = st+0x40 = 1.0 (the game's 0..15 deg look-up, no lag)
                 or own it:  st+0x3C = 0, st+0x38 = 0, st+0x34 = extra (rad, >0 up) to look past the floor limit (up to the 70 deg clamp).
The next tick then sets the goal orbit to the commanded elevation, the game's probe/constrain clamps it to the floor and walls,
the eye takes that elevation in the same tick (with its own push), and build_view applies the clamp. Latency is 1 tick.
Restore SIG on mod disable. Pitch is monotonic in T, so drive T relatively from mouse/gyro (T += k*dy).

## Without the constant patch
Writing the state directly (rotate st+0x160 and st+0x170 to the target elevation, zero st+0x184, st+0x00 = st+0xB0 + st+0x160,
tilt = angle/0.698) bypasses the caps, but collision is only applied to the goal in the next tick. Overwriting st+0x170 after the
tick puts the eye back into a floor the game had just pushed it out of. You would have to copy the *game's* post-tick goal elevation
into st+0x170 (2 ticks of latency) and still fight the 0.0305 cap with the tilt. That works, but the patch is cleaner.
