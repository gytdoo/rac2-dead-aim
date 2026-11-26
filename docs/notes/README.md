# Research notes

How Going Commando's camera, aiming, reticles, collision and hero states work, as read from the
[Going Commando decompilation](https://github.com/OpenRAC/rac2-decomp) (and, for comparison, the
[Up Your Arsenal](https://github.com/OpenRAC/rac3-decomp) and
[Deadlocked](https://github.com/OpenRAC/rac4-decomp) ones), written while the mod was made.
The source's comments point at them.

- Addresses are given for the USA v1.01 and v2.00 discs (v1 / v2). Confidence is marked H (read in
  the C and checked in the disassembly), M (read or inferred from the C) or L (a guess).
- Where a note ends with fixes or plans for the mod, they describe the code as it was when the note
  was written. The source is the authority on what the mod does now.

| Note | About |
|---|---|
| [rac2-spring-arm.md](rac2-spring-arm.md) | the follow camera: its state, orbit, pivot and springs |
| [rac2-follow-camera-tilt.md](rac2-follow-camera-tilt.md) | the follow camera's right-stick tilt and pitch limits |
| [rac2-sphere-collision.md](rac2-sphere-collision.md) | the game's sphere collision, used for the camera arm |
| [rac3-strafe-camera.md](rac3-strafe-camera.md) | Up Your Arsenal's lock-strafe camera, the model for Modern |
| [rac2-aim-camera.md](rac2-aim-camera.md) | the aim camera |
| [rac2-aim-movement.md](rac2-aim-movement.md) | first person and aiming on the move |
| [rac2-weapon-aim.md](rac2-weapon-aim.md) | how weapons read the hero's aim |
| [rac2-aim-pitch-limits.md](rac2-aim-pitch-limits.md) | aim pitch limits and the weapon basis |
| [rac2-lobbed-aim-reticle.md](rac2-lobbed-aim-reticle.md) | thrown and lobbed weapons |
| [rac2-weapon-reticles.md](rac2-weapon-reticles.md) | every HUD reticle and its gate |
| [rac2-special-situations.md](rac2-special-situations.md) | hero state groups, cameras and buttons per situation |
| [rac2-menu-ui.md](rac2-menu-ui.md) | a mod's menu page: the game's glyphs, the renderer and its limits |
| [rac2-camera-situations-2.md](rac2-camera-situations-2.md) | camera hand-offs, override cameras and the stuck-camera rescue |
| [rac2-robot-and-turret-controls.md](rac2-robot-and-turret-controls.md) | the Hypnomatic's robots, turrets and remote-controlled gadgets |
| [rac2-auto-gadgets.md](rac2-auto-gadgets.md) | Auto Gadgets: each gadget's targeting, equip and restore |
| [rac3-camera-review.md](rac3-camera-review.md) | the Modern camera against Up Your Arsenal's, with recommendations |
| [rac4-camera-review.md](rac4-camera-review.md) | the Modern camera against Deadlocked's, with recommendations |
