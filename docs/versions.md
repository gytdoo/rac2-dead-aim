# Versions and support

What Dead Aim does on each OpenRAC build, game disc, platform and controller, and how it gets on
with other mods. Dead Aim works out what the running build offers when the game starts: there is
nothing to set up.

## OpenRAC builds

| OpenRAC | Mod interface | Loads | Settings menu | Mouse and gyro |
|---|---|:---:|---|---|
| 1.0 releases | 1.14 | No | | |
| 1.1.0 release | 1.20 | Yes | Text pages in the game's menu style | Dead Aim's own (the Mouse and Gyro tabs) |
| Development builds | 1.31 and later | Yes | The full page: tabs, controller diagram, crosshair preview | OpenRAC's own, where the build has its keyboard, mouse and gyro support |

- **Text pages** hold the same settings as the full page, as plain lines. Development builds from
  before interface 1.31 get them too.
- **Mouse and gyro** on builds with OpenRAC's own support: Dead Aim uses OpenRAC's bindings and
  settings (open them with **`** or **Start + Select**), and its Mouse and Gyro tabs are hidden.
  On builds without it, the Mouse and Gyro tabs set Dead Aim's own mouse buttons, mouse camera and
  gyro.

## Game discs

| Disc | OpenRAC name | Supported |
|---|---|:---:|
| USA v1.01 | `ntsc` | Yes |
| USA v2.00 (Greatest Hits) | `ntsc2` | Yes |
| PAL, Japan | | No |

Both USA discs get every feature. Most of the automated testing ran on v1.01; Auto Gadgets and the
wrench toss have been checked less on v2.00.

## Platforms

| Platform | Built | Dead Aim's gyro | Tested |
|---|:---:|:---:|:---:|
| Linux x86_64, Steam Deck | Yes | Yes | Yes |
| Linux arm64 | Yes | Yes | Not yet |
| Windows x86_64 | Yes | No | Not yet |
| macOS (Apple silicon and Intel) | Yes | No | Not yet |
| Android arm64 (OpenRAC's app) | Yes | No | Yes |

- **Dead Aim's gyro** reads controllers through Linux's raw HID devices, so it is only in the
  desktop Linux builds; the others have no Gyro tab. On Windows and macOS development builds,
  OpenRAC's own gyro still turns the game's cameras (Classic, the Glider, vehicles), and Steam
  Input's "gyro as mouse" turns the Modern camera. OpenRAC's Android app has no gyro yet, on any
  build.
- **On Android** OpenRAC gives screen touches as the mouse, so Dead Aim leaves the mouse to
  OpenRAC's touch buttons and camera: no Mouse tab, and a touch is never a click.
- **OpenRAC's settings** changed mid-game reach Dead Aim at once on Linux. Elsewhere it uses them
  as they were when the game started.

## Controllers and gyro

OpenRAC does not pass the gyro to mods, so Dead Aim reads the controller itself, with the report
layouts of the SDL that OpenRAC is built with and a port of OpenRAC's own gyro filter. It sends the
controller no commands; the one thing it asks for is a PlayStation pad's calibration, which Linux's
own driver reads too. Read access to the controller's device is needed; Steam's controller rules
(`steam-devices`) give it.

| Controller | Development builds | 1.1.0 release |
|---|:---:|:---:|
| DualSense, DualSense Edge | Yes | Yes |
| DualShock 4 over USB | Yes | Yes |
| DualShock 4 over Bluetooth | Yes | Yes |
| Switch Pro Controller | Yes | Yes |
| Joy-Cons (alone, paired or in the grip) | Yes | Yes |
| Steam Deck | Yes | Yes |
| Wireless HORIPAD for Steam | Yes | Yes |
| Third-party Switch pads (PDP Afterglow, HORI Wireless Switch Pad) | Yes | No |

- **Motion switched on:** Linux's own drivers turn on the motion of Sony's and Nintendo's
  controllers when they connect (the DualShock 4's full reports, the Switch pads' motion sensor), so
  those work on every build. Third-party Switch pads get theirs only from OpenRAC's own gyro, which
  1.1.0 doesn't have.
- **On development builds**, OpenRAC's own gyro also turns the game's own cameras (Classic, the
  Glider, vehicles) for any controller it supports. Dead Aim's reading covers the Modern camera.
  OpenRAC's Android app has no gyro yet.
- **Not read:** third-party PlayStation pads, 8BitDo pads in their own modes and the Steam
  Controller, as SDL has to configure them first. Steam Input's "gyro as mouse" works with any of
  them, through the mouse.

## Other mods

| Mod | With Dead Aim |
|---|---|
| Auto Gadgets (Joebannerlord) | Works. Dead Aim's own Auto Gadgets turns itself off, so his handles the gadgets. |
| Contextual Wrench and Fire (Joebannerlord) | Conflict: both change the shoulder buttons and Fire. |
| Steam Deck Pack (Moritz Wallinger) | Conflict: both turn the camera from Steam Input's gyro and trackpad. Dead Aim reads the Steam Deck's gyro itself. |
| RAC-Board (Mateusz Kłysz) | Conflict: the board rides on the shoulder buttons, which Dead Aim remaps, and both move the camera. |
| Shoulder Camera (OpenRAC, not listed yet) | Conflict: both move the camera and hold L2. |
| Any other Going Commando mod | Works. None of the others change the controls or the camera. |

A conflict shows on the mod's row in the launcher ("Conflicts with"), and the game won't load the
two together: turn one of them off.

Checked on 27 November 2026 against every Going Commando mod in OpenRAC's mod index: Accessibility
Pack, Ammo Max, Boldan Nanotech, Cutscene Manager, Difficulty, HD Achievements, Item Randomizer,
Megaturret Fix, Multiplayer, New Game++, Platinum Weapons, Seeker Fix, Showroom Star Explorer,
Stadia button icons, Wrench Swap and Xbox Icons.

## Multiplayer

Dead Aim is each player's own: other players in a party don't need it, and everyone keeps their own
layout and camera settings. Party play has not been tested yet.

## Reporting a problem

[Open an issue](https://github.com/gytdoo/rac2-dead-aim/issues) with your OpenRAC version, the disc
(v1.01 or v2.00), your system, the controller, and the settings you changed. The game log helps
too: `openrac-rac2-ntsc.log` (or `-ntsc2.log` for v2.00) in the game's data folder, which on Linux
is `~/OpenRAC/active/rac2/data/`.
