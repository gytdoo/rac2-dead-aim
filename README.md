# Dead Aim

Ratchet & Clank: Going Commando, played the Deadlocked way. Lock-strafe, remappable shoulder
buttons, a free camera for the stick, mouse and gyro, aiming on the move, each weapon's own
crosshair, Deadlocked's camera speeds and aim assist, and gadgets that come out when you need them.
An OpenRAC mod for Linux.

![The Buttons tab](https://raw.githubusercontent.com/gytdoo/rac2-dead-aim/main/docs/images/buttons-tab.png)

## Install

1. Select **Going Commando** in the OpenRAC launcher.
2. Open **Mods → Add from a link** and paste this repository's address:

   ```text
   https://github.com/gytdoo/rac2-dead-aim
   ```

3. Install, enable the mod and start the game.

You can also download the ZIP from [Releases](https://github.com/gytdoo/rac2-dead-aim/releases)
and add it with **Mods → Add a mod**.

## Features

- **Lock-strafe.** Ratchet faces the camera while you move: with a weapon out (the default),
  always, or never.
- **Crouch Behavior** (Deadlocked's by default). With **Deadlocked**, holding Crouch on the run
  never stops Ratchet: Crouch + Jump long jumps straight from the run, and with the wrench in hand
  Crouch + Square tosses it as he runs, his arm winding up and catching it on its return. **Joe
  Wrench** tosses it in the air too. Standing, Crouch is the game's own (crouch, the high jump, the
  crouch throw); **Standard** leaves Crouch entirely to the game.
- **Remappable shoulder buttons.** L1, L2, R1 and R2 can each Jump, Fire, Crouch, Aim, Wrench, or
  toggle the strafe lock. The defaults are L1 Jump, L2 Aim, R1 Crouch and R2 Fire.
- **Modern camera.** The camera stays where you point it with the right stick, mouse or gyro, and
  looks exactly along your view at any distance, as Up Your Arsenal's and Deadlocked's do. It is
  kept out of walls the way Up Your Arsenal's is: a sphere swept along the camera arm, with crates
  and other objects passed over rather than pushed into. Choose **Classic** for the game's own
  camera, untouched.
- **Aim where you look.** In third person the gun fires at what is under the crosshair.
- **Aim on the move.** Holding Aim eases the camera into Ratchet's view, then first person, and you
  keep walking (**Aim Control: Free Movement**, the default). If you play in first person (the
  game's own option on), holding Aim switches to the third-person camera and its crosshair instead.
  Choose **Standard** for the game's own stand-still aim.
- **Camera speed.** Deadlocked's speeds, Slowest to Fast (Normal by default): linear, with a quick
  turn when the stick is held right over. **Vertical Sensitivity** sets the vertical speed against
  the horizontal: Deadlocked's (the default), Up Your Arsenal's, or your own. The Classic camera
  keeps the game's own speed (Options > Camera).
- **Aim assist** (Deadlocked's by default, or Up Your Arsenal's, or off). With a weapon in hand,
  near an enemy the right stick turns more slowly and the view follows the enemy while you move
  (Deadlocked's reaches 20 degrees and 45 units, Up Your Arsenal's 15 and 30). The mouse and gyro
  are never slowed.
- **The game's own crosshairs.** Each weapon's own reticle shows in third person and on the move,
  as do the wrench's, the Swingshot's and the Hypnomatic's. One **Crosshair** setting sizes them
  alike (30% by default); at **Off** third person shows none and first person the game's own.
  Weapons that lock on or lob keep the game's markers.
- **Tight spaces.** Where there is little room behind Ratchet, the camera comes in close and he
  fades, so the walls never fill the view and the camera never fights him.
- **Auto Gadgets** (All by default). Fire at a gadget's target puts the gadget in hand, uses
  it, and gives your weapon back, as the later games do:
  - **Swingshot**: at the node or target you look at, standing, jumping or hovering; Ratchet turns
    to it. It stays out while another is in reach, and goes away once you are back on your feet with
    none near, or when you fire with none in reach (your weapon comes back and fires).
  - **All**: also the Dynamo at its activators, the Tractor Beam at what it can carry (hold Fire
    to carry, let go to drop), the Thermanator at pools and ice it can freeze or thaw (hold Fire;
    it stays out three seconds after so you can read its meter, and Fire at a target again
    resumes), and the Hypnomatic at Receiver Bots (hold Fire until the beam takes the bot; your
    weapon is back when Ratchet is).

  Each gadget switches only where the game itself would find a target for it. With two in reach
  at once, or the gadget already in hand, Fire does what the game's Circle does.
- **Spiderbot and Visibomb.** While you steer one, your Fire button detonates it.
- **Turrets.** Your Fire button fires them, and the right stick, mouse and gyro aim them (the left
  stick still works as in the stock game).
- **The Hypnomatic's robot.** It takes your layout: Fire, Jump and Aim (its look), lock-strafe,
  and Crouch as its strafe toggle; Circle shoots and Square fires the Hypnomatic's beam, as
  Ratchet's fire and wrench sit. Its camera, and Clank's, also turn with the mouse and gyro.
- **Planetoids.** The camera and aim work on the Jamming Array's small planetoids. The camera turns
  round the planetoid's own up, and shots follow its curve as they always have.
- **The menu.** Pause > Options > **Dead Aim** opens a page in the game's style. Switch tabs
  with L1 and R1. The Buttons tab shows your layout on a controller diagram, and the Camera tab a
  crosshair preview. Button prompts show your device's buttons, including keyboard keys.

![The Camera tab](https://raw.githubusercontent.com/gytdoo/rac2-dead-aim/main/docs/images/camera-tab.png)

## Mouse and gyro

On OpenRAC builds that have their own "Keyboard, mouse and gyro" support, the mod uses OpenRAC's
bindings and settings: the mouse's sensitivity and invert, and the gyro's mode, sensitivity, space,
dead zone, smoothing, calibration and button. Change them in OpenRAC's menu (**`** on the keyboard,
**Start + Select** on a pad). OpenRAC turns the game's own cameras; where the Modern camera turns
itself, the mod applies the same settings.

OpenRAC gives mods the mouse but not the gyro, so the mod reads the controller itself, the way
OpenRAC does: each controller's reports as SDL 3.4.18 (the SDL OpenRAC is built with) reads them,
through a port of OpenRAC's own gyro filter. It reads the DualSense and DualSense Edge, the
DualShock 4, the Switch Pro Controller (and pads that work as one), the Joy-Cons, alone or as a
pair, and the Steam Deck. It only listens (nothing is sent to the controller), follows the
controller you are using, and needs read access to the controller's raw device, which Steam's
controller rules (`steam-devices`) give.

On builds without that support, the mod brings its own mouse buttons, mouse camera and gyro, set in
the page's Mouse and Gyro tabs; the gyro goes through the same filter, at OpenRAC's defaults. They
work the game's own cameras the way OpenRAC's support does: the follow camera turns at once and
tilts within the game's own range, eased by the game past its floor and walls; other cameras (the
Glider's, vehicles') take them as their right stick. On these builds a Switch pad, and a
DualShock 4 over Bluetooth, send no motion: OpenRAC's own gyro is what turns it on.

## Classic camera

With **Camera: Classic** the mod leaves every camera to the game: its own follow camera, aim,
first person and crosshairs, and the camera settings are hidden. Your button layout, lock-strafe
and Auto Gadgets still apply, and so does the mouse and gyro (above).

## Where your layout applies

Your button layout applies on foot, in moves started on foot (jumps, ledges, the hover,
grinding, the Swingshot, Charge Boots, the Levitator) and as the Hypnomatic's robot; in turrets
your Fire button fires. The game's own buttons come back everywhere else:

- swimming and diving (R1/R2 boost);
- bolt cranks, vendors and scenes;
- vehicles, the Glider, the ship and space combat;
- playing as Clank or Giant Clank.

The Modern camera steps aside wherever the game frames the shot itself: another camera (the
Swingshot, cranks, turrets, scripted shots), grinding (the game follows the rail), a boss or arena
that sets the camera (the Arctic Leviathans, the arena fights), the Glider, Clank and the robot.

## Compatibility

- **Other mods:** it cannot run with Contextual Wrench and Fire (both change what the shoulder
  buttons and Fire do) or Shoulder Camera (both move the camera and hold L2); the launcher says so.
  With Joebannerlord's Auto Gadgets installed, this mod's Auto Gadgets stays off and leaves the
  gadgets to it.
- **Multiplayer:** the mod is each player's own: players in a party need not have it, and each
  keeps their own layout and camera.
- **Platform:** Linux x86_64 only.
- **Game:** Ratchet & Clank: Going Commando, USA v1.01 and v2.00 (OpenRAC's `ntsc` and `ntsc2`).
- **OpenRAC:** the 1.1.0 release or later (mod interface 1.20); the 1.0 releases cannot load it.
  Tested on the 1.1.0 release and on the development build (1.1.1, interface 1.40).
  - On 1.1.0 the settings are plain text pages in the game's own menu style, and the mouse and
    gyro are the mod's own (the Mouse and Gyro tabs).
  - Builds with interface 1.31 or later (the development builds) get the menu page, and builds
    with OpenRAC's own keyboard, mouse and gyro use those settings instead.

## Testing

**By hand** (Linux, the release and development launchers): lock-strafe and the button layouts, the
Modern camera in ordinary play, crosshairs and their size, Free Movement aiming, the Swingshot, the
menu page and the text pages, the mouse through OpenRAC's own support, the mod's own gyro, the
Levitator and the Glider.

**In automated runs** (development build, USA v1.01 and v2.00): a pass through every level, the
Jamming Array's planetoids, first-person throws, water and respawning, the wrench, Swingshot and
Hypnomatic reticles, Joba's turret, the Hypnomatic's robot, aim assist, tight corners, Crouch
Behavior on v1.01 (the run kept while Crouch is held, the long jump and the wrench toss from a run,
the toss in the air with Joe Wrench, and Default identical to the game's own), and
Auto Gadgets on v1.01 (the Swingshot standing, jumping and hovering, the Tractor Beam, the
Hypnomatic).

**Not yet tested:** grinding, bolt cranks, swimming and diving, Snivelak's turret, the auto Dynamo
and Thermanator, Auto Gadgets on v2.00, the attack robot, Giant Clank, the Arctic Leviathans and the
arena bosses, the Charge Boots with Deadlocked's Crouch Behavior, the wrench toss on v2.00, the
mod's own mouse, the gyro of controllers other than the DualSense, multiplayer parties, Windows and
macOS (not built).

Reports are welcome in the issues.

## Known limitations

- The game's first person is built around straight up, so on a planetoid Aim keeps the
  third-person camera, eased in over the shoulder, instead of first person.
- The gyro is read from the controller's raw device, so it works on Linux only, and only for the
  controllers above. Third-party PlayStation pads, 8BitDo pads in their own modes and the newer
  Steam Controller are not read: SDL has to write to them before they send motion.
- The menu's text is drawn in OpenRAC's overlay font, not the game's. Mods have no way to draw in
  the game's font, and the menu entry's place and icon are fixed by OpenRAC.

## Build

Requires Python 3, CMake 3.20 or newer, Ninja and a C++17 compiler.
`tools/fetch_sdk.py` downloads and verifies the OpenRAC mod SDK pinned in [sdk.lock](sdk.lock).

```sh
python3 tools/fetch_sdk.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/rac2_dead_aim -o dist
```

To install a build for testing, link `build/package/rac2_dead_aim` into OpenRAC's plugin
folder. Pushing a tag `v<version>` makes the workflow publish a release the launcher can install.

Under the hood:
- The controller drawing is generated by `tools/make_art.py`, which needs Pillow.
- [docs/notes](docs/notes) holds the research behind the mod: the follow camera, aiming, the
  reticles, collision, the special situations and Auto Gadgets, read from the
  [Going Commando decompilation](https://github.com/OpenRAC/rac2-decomp), with Up Your Arsenal's
  and Deadlocked's cameras for comparison.

## Credits and license

- **gytdoo** made the mod, with Claude (Anthropic) writing much of the code.
- The [OpenRAC](https://openrac.dev/) project made the native port, the mod SDK and the
  [Going Commando](https://github.com/OpenRAC/rac2-decomp),
  [Up Your Arsenal](https://github.com/OpenRAC/rac3-decomp) and
  [Deadlocked](https://github.com/OpenRAC/rac4-decomp) decompilations this mod's research relies
  on.
- Joebannerlord's
  [Contextual Wrench and Fire](https://github.com/Joebannerlord/openRAC-RC2-contextual-wrench-throw)
  provided the on-foot check and the weapon list.
- Crouch Behavior tosses the wrench the way Joebannerlord's
  [Contextual Wrench and Fire](https://github.com/Joebannerlord/openRAC-RC2-contextual-wrench-throw)
  (GPL-3.0-or-later) throws it: the game's own launch, its timing and the arm's pose, which came
  from Christian's [wrench throw](https://github.com/chistianh/wrench-throw) for Ratchet & Clank.
- Auto Gadgets is adapted from Joebannerlord's
  [Auto Gadgets](https://github.com/Joebannerlord/openRAC-RC2-AutoGadget) (GPL-3.0-or-later): when
  an equip is safe, Circle held back until the gadget is in hand and then pressed for it, the
  Dynamo's and the Tractor Beam's target checks (Oozla's boxes too), and the rules for giving the
  weapon back. Its temporary equip and Circle replay came from glawks'
  [Auto Swingshot](https://github.com/glawks/auto-swingshot) for Ratchet & Clank (MIT,
  [notice](LICENSE-AUTO-SWINGSHOT)), as does the rule for putting the Swingshot away.
- The gyro follows OpenRAC's own: its filter (`port/platform/gyro.cpp`, GPL-3.0-or-later, the
  OpenRAC contributors) is ported in [src/gyro_aim.hpp](src/gyro_aim.hpp), and each controller's
  reports are read by the layouts of [SDL](https://github.com/libsdl-org/SDL)'s HIDAPI drivers.
- Insomniac Games' Up Your Arsenal and Deadlocked inspired the controls, the camera and the
  menu.

The mod is licensed under [GPL-3.0-or-later](LICENSE). The SDK's build files and workflow carry
the [OpenRAC Mod SDK MIT notice](LICENSE-OPENRAC-SDK), and the Swingshot's part of Auto Gadgets
glawks' [MIT notice](LICENSE-AUTO-SWINGSHOT).
