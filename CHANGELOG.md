# Changelog

## Unreleased

- Planetoids: first person (holding Aim, or with the game's first-person option on) stays level with
  the planetoid's surface as you walk round. The game's own keeps its view fixed and tips toward the
  ground, then off into space. Holding Aim there now goes into first person, as everywhere else.
- Planetoids: shots go where the crosshair points, up and down included, instead of always along the
  ground. In flight they keep their angle to the surface, as the game's own first-person shots do.
- With the game's first-person option on, lock-strafe no longer fires the weapon (the game's strafe
  button fires in first person).

## 0.1.0

The first pre-release, for testing. Every setting is in **Pause → Options → Dead Aim** and in the
launcher.

### Controls

- Lock-strafe: Ratchet keeps his back to the camera while a weapon is out, always, or never.
- L1, L2, R1 and R2 can each be Jump, Fire, Crouch, Aim, Wrench or Toggle strafe. Defaults: L1 Jump,
  L2 Aim, R1 Crouch, R2 Fire.
- Crouch Behavior: **Deadlocked** (holding Crouch doesn't stop a run, so Crouch + Jump long jumps
  and Crouch + Square throws the wrench while running), **Joe Wrench** (the throw in the air too),
  or **Standard** (the game's crouch).
- Auto Gadgets: Fire at a gadget's target uses the Swingshot, Dynamo, Tractor Beam, Thermanator or
  Hypnomatic, then switches back to your weapon.
- Fire also shoots turrets and detonates a thrown Spiderbot or Visibomb, and the Hypnomatic's robot
  uses your layout.
- The game's own controls stay in water, at cranks and vendors, in vehicles, and as Clank or Giant
  Clank.

### Camera and aiming

- Modern camera: a free camera for the stick, mouse and gyro, kept out of walls with a swept sphere
  as in Up Your Arsenal, and working on the Jamming Array's planetoids. **Classic** leaves every
  camera to the game.
- Shots go where the crosshair points. Each weapon shows its own reticle, and Crosshair sets their
  size.
- Aim Control: **Free Movement** (hold Aim for first person and keep walking; with the game's
  first-person option on, Aim switches to third person instead) or **Standard**.
- Deadlocked's five stick speeds. Holding the stick fully sideways turns faster.
- Vertical sensitivity: Deadlocked's, Up Your Arsenal's, or your own.
- Aim assist with Deadlocked's or Up Your Arsenal's values. The mouse and gyro are not affected.
- In tight spaces the camera moves in close and Ratchet fades out.

### Mouse and gyro

- Uses OpenRAC's own mouse and gyro settings where the build has them. On the 1.1.0 release, the
  mod's own Mouse and Gyro tabs.
- On Linux, the gyro works with the DualSense, DualShock 4, Switch Pro Controller, Joy-Cons and
  Steam Deck, through a port of OpenRAC's own gyro filter.
- On Android, touches stay with OpenRAC's touch buttons and camera.

### Compatibility

- OpenRAC 1.1.0 or a development build, with Going Commando USA v1.01 or v2.00.
- Built for Linux, Windows, macOS and Android. Linux and Android have been tested; Windows and macOS
  not yet.
- Joebannerlord's Auto Gadgets takes over from Dead Aim's own. Contextual Wrench and Fire, Steam
  Deck Pack, RAC-Board and Shoulder Camera are declared conflicts.
