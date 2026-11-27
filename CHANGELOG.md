# Changelog

## 0.1.0

The first pre-release, for testing.

- Crouch Behavior: Deadlocked's (held on the run it never stops Ratchet, so Crouch + Jump long jumps
  from the run and Crouch + Square tosses the wrench as he runs), Joe Wrench (the toss in the air
  too), or the game's own.
- Lock-strafe (with a weapon, always or off) and remappable L1, L2, R1 and R2 (Jump, Fire, Crouch,
  Aim, Wrench, toggle strafe). Defaults: L1 Jump, L2 Aim, R1 Crouch, R2 Fire.
- The Modern camera: free, for the stick, the mouse and the gyro, kept out of walls with an Up Your
  Arsenal-style sphere sweep, and following the Jamming Array's planetoids.
- Aiming where the camera looks in third person, and on the move in first person (Aim Control:
  Free Movement), eased into from the camera.
- Deadlocked's camera speeds with its quick turn, vertical sensitivity (Deadlocked's, Up Your
  Arsenal's or your own), and aim assist (Deadlocked's or Up Your Arsenal's) for the right stick.
  Played in first person (the game's option on), holding Aim switches to the third-person camera.
- The game's own reticles for every weapon, the wrench, the Swingshot and the Hypnomatic, with one
  Crosshair setting for their size (or Off).
- The game's own controls kept in water, turrets, cranks, vendors, vehicles and as Clank, Giant
  Clank or the Hypnomatic's robot. The camera steps aside wherever the game uses another camera.
- In tight spaces the camera comes in close and Ratchet fades, instead of the camera pressing into
  him.
- Auto Gadgets (on by default): Fire at a target puts the Swingshot, the Dynamo, the Tractor
  Beam, the Thermanator or the Hypnomatic in hand and uses it, then gives your weapon back.
- Fire detonates a thrown Spiderbot or Visibomb.
- Turrets fire on your Fire button and aim with the right stick, the mouse and the gyro.
- The Hypnomatic's robot takes your Fire, Jump and Aim buttons and lock-strafe, with Crouch as its
  strafe toggle.
- OpenRAC's built-in mouse and gyro settings used where the build has them, the mod's own where it
  has not (working the game's cameras as OpenRAC's does, within the game's limits).
- The gyro read as OpenRAC reads it, through a port of OpenRAC's own gyro filter: the DualSense and
  DualSense Edge, DualShock 4, Switch Pro Controller, Joy-Cons and Steam Deck.
- Classic camera: the mod leaves every camera to the game, and hides the camera settings.
- A menu page in the game's style (Pause > Options > Dead Aim), with text pages on older
  OpenRAC builds.
- Other mods: Joebannerlord's Auto Gadgets takes over from Dead Aim's own; Contextual Wrench and
  Fire, Steam Deck Pack, RAC-Board and Shoulder Camera are declared conflicts.
- Built for Linux (x86_64 and arm64), Windows, macOS and Android. The gyro reader is Linux-only, and
  only Linux has been tested.
