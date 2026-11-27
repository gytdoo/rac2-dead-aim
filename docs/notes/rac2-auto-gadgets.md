# rac2 Auto Gadgets for Dead Aim: spec

Goal: the player presses their mapped **Fire** near a target a gadget can use. The mod equips that gadget for a
moment, uses it, and puts the previous item back. The player picks a tier: **Off / Swingshot / Swingshot + Dynamo +
Tractor Beam / All** (All adds the Thermanator and the Hypnomatic).

Sources (path roots used below):
- **J** = `dev/reference/openRAC-RC2-AutoGadget`: Joebannerlord's Auto Gadgets 0.1.5 (commit 2257709).
  GPL-3.0-or-later, USA v1.01 only, OpenRAC 1.1.0 only. It covers Dynamo, Tractor Beam, Thermanator and
  Hypnomatic. The Swingshot is left to manual equip (J/README.md:30).
- **G** = `dev/reference/auto-swingshot`: glawks' Auto Swingshot 1.0.0 for **rac1** (commit 1aaa2ea). MIT.
- **D** = `dev/reference/rac2-decomp/src`: v1.01 C. `D/us2` is the v2.00 C. Per-level addresses come from
  `config/levels/functions.tsv` (v1) and `config/us2/levels/functions.tsv` (v2, matched by fingerprint).
- **AC** = `dev/repos/rac2-dead-aim`: our mod (`src/mod.cpp`, `src/controls.hpp`).
- Nothing here was run in game. Joe's README says he checked his gadget behaviour in play on v1.01
  (J/README.md:67).

Confidence: **H** = read in code (and in v2 C or tables where it says v2). **M** = inferred from code.
**L** = guess. Addresses are written "v1 / v2". The hero record is 0x189E20 on both discs, and every hero offset
below is the same on both (the us2 C of the item input differs from v1 only in names and a wrench cooldown,
`D/us2/levels/shared/L00_002C96D0-sw.c` against `D/levels/shared/L00_002C9448-g8.c`) **H**.

---

## 0. The design in brief

1. **Trigger**: a *fresh* Fire press (the Circle bit after our remap goes from up to down) while Ratchet can
   equip. If exactly one gadget in the player's tier has an eligible target and is owned, a gadget is started.
   Otherwise the press goes to the game unchanged.
2. **Eligibility** comes in two parts:
   - **Before the gadget is in hand**, the game computes nothing for it. Every gadget's target search runs only
     inside that gadget's own update or input case. So the mod has to compute a preview. The best preview calls
     the game's own search function: the level's copy, found with `function_at`, using the v1/v2 tables in §2.6.
     This is what Joe does for four gadgets. For the Swingshot we would call the game's three finders. A pure
     read-only version is possible for some gadgets (§2), but it is less exact.
   - **Once the gadget is in hand**, read what the game computes each tick: the hero's swingshot fields, the
     Dynamo's vars+0x18, the Thermanator's vars+0x40/+0x54, the Hypnomatic's vars+0x00, the Tractor latch
     hero+0x5C0. These decide "ready", "still the same target" and "used".
3. **Flow** (Joe's phases, J/src/mod.cpp:24):
   - idle → equipping. Request the gadget. Hold Circle up in the report.
   - equipping → replay. Wait until the gadget is ready (event driven, timeout 100 ticks). Then press Circle,
     once for a tap gadget or held for a hold gadget.
   - active. Mirror the player's Fire for the hold gadgets.
   - canceling → restoring. Request the previous item when the gadget is at rest.
   - capture is a Hypnomatic-only phase. Joe uses it while control has passed to the robot.
4. **In our code**:
   - Pad part: in `on_pad`, right after `strafe::rewrite` (AC/src/mod.cpp:414) and the turret line (:416). Read Fire
     from the rewritten Circle bit. The mod suppresses Circle, presses it or holds it there.
   - Frame part: its own `mod.on_frame` callback, registered before `frame` (AC/src/mod.cpp:3745). It handles phase
     changes and the reads of game-computed state.
5. Everything needed is in API 1.20 (§6). The `rac2.item.equip` hook is optional.

---

## 1. Shared game facts

### 1.1 Items and hand classes (H)

| gadget | item (hero +0x1248) | hand moby class (*(hero+0x1220)+0xAA) | use | source |
|---|---|---|---|---|
| Swingshot | 13 | 0xD0 | tap (press edge within 5 or 8 ticks) | SDK hooks-rac2.md (Endako "item 13"); D/../include/moby_classes.h:26 |
| Dynamo | 36 | 0x825 | tap (press edge) | J/src/native.hpp:71; moby_classes.h:53 |
| Tractor Beam | **46** | 0xBC | press to grab, then carry | J/src/native.hpp:72; SDK hooks-rac2.md (tractor_beam_seller "item 46"); moby_classes.h:23 |
| Thermanator | **39** | 0xFB0 | hold | J/src/native.hpp:73; SDK hooks-rac2.md (thermanator_seller "item 39"); moby_classes.h:171 |
| Hypnomatic | 55 | 0x950 | hold until capture | J/src/native.hpp:74; SDK hooks-rac2.md (hypnomatic_maker "item 55"); moby_classes.h:68 |
| wrench | 10 | 0x47 | Square | J/src/native.hpp:58 |

### 1.2 Hero and global fields (H unless marked)

Slot 0 of the hero's item slots is the hand. It starts at hero+0x1220, and each slot is 0x50 bytes
(D/levels/shared/L00_002AFBC0-equip.c:8-36, D/levels/shared/L00_002C9448-g8.c:5-80).

| field | meaning |
|---|---|
| +0x1220 | hand moby (Joe `held_moby`) |
| +0x1230 | the hand item's button mask: 0x20 Circle for gadgets and guns, 0x80 for the wrench |
| +0x1239 / +0x123A | melee active / wrench thrown (Joe's `free_hand` test) |
| +0x1240 | ticks the item has been live in hand. It counts up while +0x1244 == 2 (D/levels/shared/L00_002AFBC0.c:558-559). The Swingshot's held-Circle path needs >= 16; the Hypnomatic's beam needs >= 16 |
| +0x1244 | slot mode. **2 = live** (its update runs; `hero_get_slot_object` returns the moby only in mode 2, D/levels/shared/L00_002A6420.c:588-599). **3 = being put away / switching** (L00_002AFBC0.c:564-567) |
| +0x1248 | item in hand |
| +0x1450 | the L2+R2 lock target. The Hypnomatic search returns it first if it is live (D/levels/shared/L00_003B3158.c:272-277) |
| +0x5C0 | the Tractor Beam's latched object (`aim_target`, L00_002C9448-g8.c:35, :256-279) |
| +0x98 | heading. Used by the Swingshot and Tractor cones |
| +0x340, +0x34E, +0x1E0, +0x24EC | Hypnomatic beam gates: grounded-like, extended range, capture cool-down, raised arm (J/src/native.hpp:321-324; D/levels/shared/L00_003B84F0-g8.c:191) |
| +0x2294 / +0x229C | state / group. 1 = look, 29 (0x1D) = aim |
| +0x22B4 | form (0 Ratchet, 4 Hypnomatic robot) |
| +0x22B7 | "wrench forced". While it is set, the hand is switched to the wrench and the old item saved in +0x2300 (equip.c:68-70, :97-105). The item is restored when the flag drops (+0x231C, D/levels/00_aranos_tutorial/L00_002C42E0-state-g8.c:1199). It is set in swimming, ledges, double jump and others (state-g8.c:242-331, 685, 780, 1173-1183). Joe calls it `no_menu` |
| +0x22B8 | item animation running (Joe `item_in_use`) |
| +0x22BC | "blocked". The equip request is ignored while it is set (equip.c:90-94). The Dynamo search is skipped and the item input returns early. It is set in swing states 0x24/0x25/0x2B/0x8B (state-g8.c:467-499) |
| +0x22BD / +0x22BF | hero update stopped / item tucked away (Joe's guards) |
| **+0x22C8** | **hand equip request** (`g_slot_index_first`, D/../include/names/executable.h:505). +0x22CC/+0x22D0/+0x22D4 are the other slots (:506-508) |
| +0x22E4 | current item chosen for the hand (`current[0]`, Joe `selected_item`) |
| +0x2300 / +0x231C | previous hand item saved by a forced wrench / restore request |
| 0x1A7398 (both discs) | `g_menu_choice_table[0]`: the item Circle draws from the wrench, and the item a hand request sets (equip.c:107-120, :221-225). v1 executable.h:642. v2: `g_itemEquipSlotTable = 0x001a7398` CONFIRMED (D/../symbol_addrs/usa-v2/symbol_addrs.txt:1890). Joe `remembered_item` |
| v1 0x1A7AF8 / v2 0x1A7B00 | owned bytes, one per item (v1 `g_item_owned_flags`, executable.h:758; v2 per the brief. On v2, 0x1A7AF8 is ship data, symbol_addrs.txt:659) **H v1, M v2** |
| pad 0x138180 (both) | +0x1A0 held, +0x1A4 pressed (edge), +0x1A8 **released** (edge), +0x1CC mode (non-zero = pad masked) (D/game/002B7E28-pad.c:25-38). The decomp's `g_pad_pressed_flags` is really *held* and `g_pad_held_flags` is really *pressed* (rac2-robot-and-turret-controls.md) |

### 1.3 How an equip happens (H)

1. `Item_Equip(item)`. SDK hook `rac2.item.equip` = func_002AD860. The decomp calls it
   `moby_handle_request_slot_state`, D/game/002AD860.c:12-28.
   - The item's record has a state. In state 0 it writes **hero+0x22C8 = item**. First it starts the class
     load if the class is not cached. It refuses (returns 0) if the class is not cached and a sequence is playing
     (`g_sequence_active_flag`).
   - States 1/2/3 write the other slots' words instead.
   - Returns 1 when the request was made.
2. Each tick, `UpdateHeroSlotCallbacks` calls `ApplyHeroEquipmentRequest(0)` for a live slot (L00_002AFBC0.c:546-583).
   This function, D/levels/shared/L00_002AFBC0-equip.c:64-286, does the following:
   - Returns early while **+0x22BC** is set (:90-94).
   - **Square pressed within 3 polls → the wrench** (:97-106).
   - **Circle pressed with the wrench in hand → `g_menu_choice_table[0]`** (:107-120), unless +0x22B7 is set.
   - Takes the request: current = request, menu choice = request (:204-232). An un-changeable slot cancels
     the change for this tick: state byte +0x123C == 2, or `vec_fast_helper_a` false (:233-236).
   - Waits until the item's class is cached (`queued`, :237-252).
   - Then starts the swap: slot mode 3, the request cleared (:253-281).
3. The old item is put away (mode 3). The new one is installed and becomes **mode 2**. +0x1240 counts from there.
4. The swap's length in ticks is **not known** (L). Neither Joe nor glawks waits a fixed time. Both poll for ready
   with a **100-tick timeout** (J/src/mod.cpp:19, :220, :268; G/src/mod.cpp:33, :215-221).

Writing +0x22C8 directly (what glawks does in rac1, G/src/mod.cpp:271, and what the brief suggests) skips step 1.
That is probably fine for hand gadgets: step 2's `queued` path loads the class itself (equip.c:242-246) **M**.
Calling the level's copy of Item_Equip (§2.6 table, as Joe does, J/src/mod.cpp:336-345) is the exact game path.
**Recommendation**: write +0x22C8 directly only when it reads 0 and the item is owned. Count the request as taken
when the hand shows the item in mode 2. If testing shows an uncached gadget never arrives, switch to the native
call.

### 1.4 "Equipped and ready" (the test before pressing Circle)

Joe's tests, J/src/native.hpp:41-45, :459-477, and J/src/mod.cpp:276-277:
- `drawn` = the hand moby is a valid address and +0x1244 == 2.
- `has(g)` = drawn, +0x1248 == g.item, +0x1230 == 0x20, and the hand class == g.hand_class.
- The request word +0x22C8 == 0.
- The game's **held** word pad+0x1A0 shows Circle **up**. Joe suppressed Circle from the first tick, so the
  replayed press is a new edge.
- The gadget's own ready test: see each gadget in §2.

glawks (rac1): item == Swingshot and the global item state == 2 (G/src/mod.cpp:191-192). Same idea.

---

## 2. Each gadget

### 2.1 Swingshot (item 13): tier 1 (new; Joe has none)

**How the game decides (H).** Case 13 of `ProcessHeroEquippedItemInput` (D/levels/shared/L00_002C9448-g8.c:282-321)
runs **only while the Swingshot is in hand**. It first calls three finders that leave their results in hero
fields (:284-286):

| finder (v1 L00 name) | target class | result fields | cones / range | then |
|---|---|---|---|---|
| `hero_find_active_swingshot_node` (D/levels/shared/L00_002C3E18.c:29-81) | 0x2F6 Swingshot Node | **+0xAA8 s32 ready**, +0xAA4 node, +0xAC4 score, +0xAB8/+0xAC0 node settings | horizontal 30° (5° in states 1/29), no vertical limit; range vars+0x20 or 30; xy > 4; node flags +0x34 bit 1 clear and 0x1000 set; vars+0 == 0; not inside the node's exclusion cuboid vars+0x1C; LOS from hero+0.5z (CollLine mask 2). **While group 0xD it returns the cached +0xAA8** (:35) | state **0x23** swing (group 13) |
| `hero_find_swingshot_node_in_front` (L00_002C3BB0.c:28-70) | 0x323 Swingshot Node 2 | **+0xADE s16 ready**, +0xAD0 node, +0xAD8 score | 45° / 45° (70°/65° in state 0x2C; 5° in 1/29); range vars+0x20 or 16; the node no more than 3 u below him; LOS | state **0x2B** (group 14, pulled to it) |
| `hero_find_blue_swingshot_target` (D/levels/shared/L00_002C2DE8-select-g8.c:27-68) | 0x99D Blue Swingshot Target | **+0xB14 s32 ready**, +0xB10 target | 30°, range 30, xy > 4, LOS. **While group 0x21 it returns the cached value** (:33) | state **0x8A** (group 33) |

The scorer is `hero_score_target_by_heading` (D/levels/shared/L00_002AE388-range.c:18-43): distance plus angle times
distance, and a "blocked" flag past range or past a cone. Then case 13 does this:
- If both 0x2F6 and 0x323 targets are ready, the nearer one wins (:302-306).
- It needs group < 2, 2, 4, 5, 12 or 30, or state 0x28 at frame >= 6, or state 29 (:296-299). This is wider than
  on foot: jumps and hover count.
- It needs a press edge within 5 ticks (8 with hero+0x34E, 11 in state 0x28), or Circle **held** with +0x1240 >= 16
  (:300-301).
- During grind state 0x27 a ready target chains into state 0x28 (:289-295).

**Before equip, the mod has to compute the targets** (the game does nothing without the Swingshot in hand). Two
options:
- (a) **Recommended**: call the level's copies of the three finders (addresses in §2.6). They take no arguments and
  write only these hero fields. Those fields are read only by Swingshot code: case 13, the state entries 0x23, 0x2B
  and 0x8A (state-g8.c:447-500), and the Swingshot's aim markers (D/game/0030F7B0-dbg.c:59-64). The game rewrites
  them every tick the Swingshot is held, so calling them early is harmless **M**. Save and restore the collision
  result block around the call (Joe's `PreserveCollision`, J/src/native.hpp:142-155): each finder calls CollLine.
  Skip the call while the group is 13/14/33. Then read +0xAA8 / +0xADE / +0xB14.
- (b) No game calls: walk the moby list (AC already reads the table, AC/src/mod.cpp:2052, 2507-2515) with the tests
  above and our `game_line` for the LOS. Mask 2 needs passing as `mask_override`; check this against the camera
  mask. The cuboid exclusion (`IsPointInCuboid(pos, vars+0x1C)`) cannot be copied without the level's cuboid
  table (unknown). Its cone uses the same scorer maths.

**After equip, read the game's own fields (H).** For "ready to press", require `has(Swingshot)` and that the target
the preview found is the one the game now reports (+0xAA4 / +0xAD0 / +0xB10 with its ready flag). If it is gone,
cancel and restore before firing. This is glawks' rule: "lost facing/range before firing; cancelling shot",
G/src/mod.cpp:193-207.

**Use**: one press tick. The game accepts an edge within 5 polls, so one tick is enough, as glawks does
(G/src/mod.cpp:208-213). After the press, pass the player's Fire through untouched. Fresh presses and holds then
chain swings the game's own way (the held path re-fires once +0x1240 >= 16).

**Restore** (glawks' rest rule, G/src/mod.cpp:225-245, adapted to rac2):
- Do not restore while the group is 13, 14 or 33, or while +0x22BC is set. The request would be ignored then
  (equip.c:90-94).
- Restore once Ratchet is in group 0 or 1 (stand, walk, run; glawks' `rest_state`, G/src/detection.hpp:46) with Fire
  up for **15 ticks** in a row (G/src/mod.cpp:34, :231-243).
- **And** no swingshot target is in range. Read the game's +0xAA8 == 0, +0xADE == 0 and +0xB14 == 0. These are game
  state now, since the Swingshot is in hand. This keeps the Swingshot while the player stands at the next node.
- Suggested extra (not in Joe or glawks, design L): a fresh Fire press at rest with no target in range restores at
  once and is swallowed, so the gun comes back when the player wants to shoot.

**Levels (M, from the class dispatch in D/../docs/game/moby-dispatch.tsv)**: 0x2F6 in 3, 4, 7, 9, 13, 20, 30; 0x323 in 3,
4, 8, 9, 11, 13, 16, 17, 18, 20; 0x99D in 3, 4, 11, 16, 20. That gives 3, 4, 7, 8, 9, 11, 13, 16, 17, 18, 20 and 30,
which agrees with rac2-special-situations.md. The finders exist in every level program except L01. L18 has no blue
target finder (§2.6).

**Our camera**: the Swingshot cones use Ratchet's heading +0x98. With Lock strafe "With weapon" and a gun drawn,
he faces the camera, so the cone follows the Modern camera at the press. Once the Swingshot is in hand,
`is_weapon` is false, so our lock drops (AC/src/mod.cpp:383-384). Decide whether to keep the lock as it was for
the auto gadget's length (design).

### 2.2 Dynamo (item 36): tier 2

**How the game decides (H).**
- `dynamo_update` is the hand's update (D/game/0030F7B0-launch-g8.c:64-175).
- In hand state 1, with +0x22BC clear, it sets **vars+0x18 = FindNearestEligibleTarget()** each tick (:94-97).
- `FindNearestEligibleTarget` (D/game/0030F7B0-nearest.c:44-82) looks at moby classes **0xCB5 / 0x12EA** that are
  not in state 2 (2 = used). The target must pass the sphere/view test `func_00283DB8(pos r0.5, vars+8)`, which is
  "in view within distance" **M**. The line from the **camera eye** to the target must be clear, or hit the hero,
  a 0xCB5 moby, or a point within 1 u of a 0x12EA. Of those, it takes the nearest to the hero within
  `g_nearest_search_range` (D_001AF54C, value not read).
- It fires on a Circle **press edge** (the decomp's `g_pad_held_flags` is really pressed) while a target is set
  (:105-113). States 2→3→4→1 time the launch and cool-down (:116-171).
- Eligibility follows the **camera**, not Ratchet's facing, so it works with the Modern camera as it is.

**Before equip**: Joe calls the level's copy of `FindNearestEligibleTarget` and filters class 0xCB5/0x12EA and
state != 2 (J/src/native.hpp:173-180), inside PreserveCollision. `self` is not used, so the call needs no arguments.
v2 addresses are in §2.6 **H** (same fingerprint). There is no pure-read substitute: the activators carry no
in-range flag.

**After equip**: ready = hand state (+0x20) == 1 (J/src/native.hpp:461-462). Same target = the Dynamo's own
**vars+0x18** equals the preview target. This is game state; Joe re-runs the search instead (J/src/mod.cpp:272-275).
Used = hand state 2..4 (J/src/native.hpp:482). At rest = state <= 1 (:496).

**Use**: a single press tick. Joe buffers a tap made during the equip and replays it (J/tests/plugin_test.cpp:349-354).

**Restore**: when the hand returns to state <= 1 after firing (J/src/mod.cpp:204-206, :464).

**Levels (H per Joe, J/src/levels.hpp:68-87; J/README.md:36; the dispatch agrees)**: 1 Oozla, 3 Endako (0x12EA),
6 Notak, 9 Dobbo, 11 Joba, 16 Snivelak, 17 Smolg, 20 Yeedil.

### 2.3 Tractor Beam (item 46): tier 2

**How the game decides (H).** Case 46 of the item input (L00_002C9448-g8.c:256-279) runs only with the Tractor in
hand. On a Circle **press edge**:
- If **hero+0x5C0** is set, it clears it (drop).
- Otherwise it sets hero+0x5C0 = `SelectBestEligibleTarget(&hero, 21, 0, 20.0, heading +0x98, 30°, 40°)`, then
  drops it again if CollLine from target+0.5z to hero+0xE0 (mask 2, skipping the target) hits.
- `SelectBestEligibleTarget` (D/levels/shared/L00_002AE388-select.c:16-59) walks the game's **target list**
  (`g_target_l00_list`, D_L00_001FACE0 in L00, null-terminated). It keeps class rows +0x46 == 21, moby flag 0x1000,
  in cone and range. Under a gravity mode (+0x22C3) it scores by the aim axis instead.

The beam itself (D/levels/shared/L00_00396288.c:38-139):
- Hand state 0 starts a 16-tick unfold timer.
- In state 1 it becomes active (state 3) when hero+0x5C0 is set and the timer has run out.
- In state 3 a Circle **release edge** (pad+0x1A8) drops the beam's hold and resets it to state 1 (:114-117). So in
  stock play you hold Circle to carry and press again to drop **M**.

**Before equip**: Joe calls the level's `SelectBestEligibleTarget` with the same arguments and the same LOS (J/src/native.hpp:202-228).
- Oozla's boxes (class 0xACB/0xACC) are a special case. They refresh their eligibility flag only every 16 ticks
  (per Joe; I did not check this). Joe previews them with the native scorer `hero_score_target_by_heading` and the
  box's reach pv+0x88, then waits for the game to list the box before pressing (:182-195, :230-258;
  J/src/mod.cpp:276).
- A read-only alternative: walk the game's target list ourselves (kind 21, flag 0x1000, a 20 u / 30° / 40° cone
  from +0x98, and our `game_line`). It is close but not exact, and the target list's per-level address needs
  `relocate_level(0, 0x1FACE0)`, which I have not checked (L).

**After equip**: ready = hand state == 1 (J/src/native.hpp:463-464). Used = **hero+0x5C0** set (:483). At rest = hand
state <= 1 and hero+0x5C0 == 0 (:497). Joe cancels if the latch ever changes from the target (J/src/mod.cpp:207-209).

**Use**: press. Joe then **holds Circle in the report even after the physical release**, which makes the carry
sticky (J/src/mod.cpp:288-299).
- He drops on a fresh Circle press, Jump (Cross, or group 2/4), Square or R2 (:147-148, :162-164, :170-173).
- He drops by writing hero+0x5C0 = 0 himself, so the Jump or Square reaches the game on the same tick (:428-441).
- For our mod Fire is a shoulder, so holding it while steering with the stick is natural. **Recommendation**:
  mirror the player's Fire (hold to carry, release to drop: write the latch 0 as Joe does, then restore).
  Optionally offer Joe's sticky mode.
- Jumping while the gadget is equipping cancels it (:161-164). Jump and Square must still be passed to the game.

**Restore**: after the drop, once the hand is at rest, or 8 ticks after the cancel (J/src/mod.cpp:464-465).

**Levels (H per Joe, J/src/levels.hpp:68-87; J/README.md:37)**: 1 Oozla (with the box preview), 2 Maktar, 7 Siberius,
12 Todano, 20 Yeedil.

### 2.4 Thermanator (item 39): tier "All"

**How the game decides (H).** `thermanator_update` (D/game/0032EB28-beam.c:80-209) runs each tick in hand. It does:
- The muzzle = joint 0 (:91).
- The range = `game_get_override_value(4.5)`, plus 6.5 in aim state 29 (:92-95).
- `CalculateAimedLaunchVelocity(dir, muzzle, 0, 1e-5, range, 0.25, 0.0030555558, 0)` and
  `coll_trace_projectile_to_impact(...)` → **vars+0x54** = the hit moby (:96-99).
- If the hit's class row +0x46 == **18** (a freezable pool or ice grid), it sets **vars+0x40 valid = 1**. It
  copies the grid's +0x14 frozen/reverse flag and +0x16 disabled flag (:100-120).
- Hand state 2 fires while Circle is **held** with a valid, enabled hit (:132-147). State 3 keeps firing while held
  and returns to 2 on release (:149-171).
- The freeze meter (`gauge_popup_show`) shows only while the Thermanator is in hand (:203-207).
- Its arc direction comes from the hero aim basis **+0x730** (or the camera while L1 is held)
  (D/game/002FDF40-g8.c CalculateAimedLaunchVelocity, `sourceObject == 0` branch). Our Modern aim writes +0x730
  only for `is_weapon` items (AC/src/mod.cpp:2080-2090). Extending it to the Thermanator would make the arc follow
  the camera (design, M).

**Before equip**: Joe runs the same four functions from the hand attachment point hero+0xC80, since the gadget is
not out yet: `game_get_override_value`, `CalculateAimedLaunchVelocity`, `coll_trace_projectile_to_impact`, and
`moby_transform_joint_to_world` once equipped (J/src/native.hpp:271-309). The grid test is kind 0x12 with
pv+0x16 == 0 (:261-269). This needs a StackFrame (0x50) for the vectors.

**After equip (game state, H)**: ready = hand state == 2 and **vars+0x40 != 0** and **vars+0x54** is the preview's
grid (J/src/native.hpp:465-469). Used = hand state 3 (:484). At rest = state <= 2 (:498).

**Use**: hold Circle while the player holds Fire. Cancel on release, on a changed target, or when the grid's frozen
flag flips (J/src/mod.cpp:301-320). That flip stops a freeze from thawing on the same press
(J/tests/plugin_test.cpp:486-494).

**Restore**: after a **delay** the player sets (Joe: 2–5 s, default 3, J/openrac-mod.json settings,
J/src/mod.cpp:467). This lets the player read the meter. A fresh Fire at a ready target during the delay resumes
without another equip (J/src/mod.cpp:150-151, :322-334).

**Levels (H per Joe; J/README.md:38)**: 4 Barlow, 6 Notak, 8 Tabora, 18 Damosel, 20 Yeedil.

### 2.5 Hypnomatic (item 55): tier "All"

**How the game decides (H).** `hypnomatic_update` → `target_lock_on_update(obj, vars)`
(D/levels/shared/L00_003B9CB8.c:119-122; body D/levels/shared/L00_003B3158-lock-g8.c:97-235).
- Origin and cone:
  - In third person: origin = hero + 0.75 × moby up, direction = **the follow camera's forward**
    (`g_cam_follow_matrix`, flat in state 29), 15° cone, 35 u.
  - First-person aim: the camera position, 5°/6° (:117-128, :144-146).
- `SelectCurrentOrPreferredTarget` (D/levels/shared/L00_003B3158.c:258-305) returns hero+0x1450 first if it is live.
  Otherwise it returns a class **0xEE1** Receiver Bot from `SelectBestTargetInCone` with vars+0x3B1 & 0x80,
  vars+0x20 > 0, and xy distance < vars+0x3E4 (or 0).
- A CollLine fallback along the aim (:148-159).
- The candidate must stay the same for `g_target_lock_timer` ticks before **vars+0x00** (AimState.target) is set
  (:160-178).
- The beam needs: grounded-like hero+0x340, !+0x34E, !`g_target_lock_disabled`, !+0x22BC, Circle **held**, and
  **+0x1240 >= 16** (:205). vars+0x48 = held (:206-212).
- The beam grows while Circle is held (`target_tether_draw_beam`, D/levels/shared/L00_003B84F0-g8.c:176, 191-215).
  Within 0.5 u of the target it captures: hero+0x1E0 = 30, the moby's flag bit 1 (:207-215).
- Then the Receiver Bot's state 7 hands control over: `hero_attach_to_object(4, 0x8D, robot)`, form 4
  (D/levels/shared/L18_00428F08-g8.c:273-330). L18's `g_hero_frozen_flag` is cleared after the attach (:321).

**Before equip**: Joe calls `SelectCurrentOrPreferredTarget` with the game's angles (J/src/native.hpp:378-446).
- He clears hero+0x1450 for the call (`FreshHypnoAim`, :361-376).
- He nulls the collision of nine classes the game also passes through (`HypnoCollision`, :328-359).
- He runs the CollLine fallback and the reach check.
- The origin and forward come from per-level camera globals (J/src/levels.hpp:85-87: L18 0x1B97C0 / 0x1B99F0).
  - Those are the camera block's +0x140 (`g_camera_l00_position` D_L00_001B8DC0 = block 0x1B8C80 + 0x140) and
    +0x370 (`g_cam_follow_matrix` D_L00_001B8FF0) (names in D/../include/names/shared_levels.h:1688, 1696) **H**.
  - Our mod already finds the block per level: `relocate_level(0, v1 0x1B8C80 / v2 0x1B8C00)`
    (rac2-special-situations.md). So we need no per-level camera table **M**.

A read-only alternative: Receiver Bots are few, only in 18/19/20. Walk the moby list for 0xEE1 with the vars tests,
35 u and a 15° cone round the camera forward, plus our `game_line` LOS. This is close to the game's rule. It skips
the nine-class pass-through and `SelectBestTargetInCone`'s near-angle details (L).

**After equip (game state, H)**: ready = +0x1240 >= 16, **vars+0x00** is our receiver (still valid), and grounded
(J/src/native.hpp:470-474). Used = **vars+0x48** != 0 (:485-488). At rest = +0x22B8 == 0 (:499).

**Use**: hold Circle while the player holds Fire. Joe cancels if the receiver leaves the cone or loses sight before
capture (J/src/mod.cpp:310-314; J/tests/plugin_test.cpp:578-592).

**Capture and return**:
- Joe's `capture` phase (J/src/mod.cpp:362-387) starts when the form is 4, or when his per-level flag
  `level.capture` (L18 0x1AFBB0 = `g_hero_frozen_flag`, L19 0x1B0040, L20 0x1AFF00; v1 only) is set.
- In that phase he leaves every input to the game (300-tick timeout before he sees the form).
- When the form returns to 0 he cancels, then restores the previous item once Ratchet can equip. A 3-tick grace
  applies (:466). He tolerates the game's own wrench request on return (:398-399).
- **The v2 address of the capture flag is unknown.** Replace it with disc-independent state: the target Receiver
  Bot's moby state == 7 (it is being taken over), or hero+0x1E0 != 0 just after the beam reaches it **M**.
- Our robot handling (form 4 config, AC/src/mod.cpp:385-398) stays as it is. Auto gadgets are off in form 4.

**Levels (H per Joe; dispatch of 0xEE1)**: 18 Damosel, 19 Grelbin, 20 Yeedil.

### 2.6 Per-level function addresses (v1 / v2), for `mod.function_at`

These are the level copies. They are identical code on both discs (same fingerprint in the two tsv tables) **H**.
Joe's v1 entries in J/src/levels.hpp all match this table (checked: L01, L18).
- Item_Equip, the Dynamo search and the Thermanator functions are "boot" functions with a copy in every level.
- CollLine is already in AC (`kCollLineV101/V200`, AC/src/mod.cpp:955-966). Its result block is decoded from its
  own lui/addiu (:987-993). Save 0x18..0x7F of that block around any game call (Joe's PreserveCollision).
- The moby table is already in AC (`kMobyTableV101/V200` with `relocate`, AC/src/mod.cpp:2052).
- "-" means no copy in that level.

| function | L01 | L02 | L03 | L04 | L06 | L07 | L08 | L09 | L11 | L12 | L13 | L16 | L17 | L18 | L19 | L20 | L30 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Item_Equip (func_002AD860) | 3174B0 / 3186D0 | 3258D8 / 326BD0 | 322908 / 323C80 | 343580 / 3447A0 | 362478 / 3637F0 | 317788 / 3188A0 | 330370 / 3314E8 | 31DAA8 / 31EC48 | 333D68 / 334F80 | 3250E8 / 3261F0 | 32B450 / 32C740 | 31AB68 / 31BDE0 | 3234E0 / 3247D0 | 33C3E8 / 33D640 | 31F1B8 / 320208 | 3367B8 / 337A28 | 3277E8 / 3288A0 |
| hero_find_swingshot_node_in_front | - | 2C7030 / 2C7330 | 2C67D8 / 2C6B58 | 2E74D8 / 2E77F8 | 303330 / 3036B0 | 2BE040 / 2BE258 | 2D27A8 / 2D2AC8 | 2C2E50 / 2C30E8 | 2D87C8 / 2D8AD8 | 2C84A0 / 2C86A8 | 2CE7D0 / 2CEAC8 | 2C0498 / 2C0720 | 2C4D50 / 2C5048 | 2DFB50 / 2DFDC8 | 2C3DC8 / 2C3FD8 | 2D7000 / 2D7280 | 2CA9B0 / 2CAB30 |
| hero_find_active_swingshot_node | - | 2C7298 / 2C7598 | 2C6A40 / 2C6DC0 | 2E7740 / 2E7A60 | 303598 / 303918 | 2BE2A8 / 2BE4C0 | 2D2A10 / 2D2D30 | 2C30B8 / 2C3350 | 2D8A30 / 2D8D40 | 2C8708 / 2C8910 | 2CEA38 / 2CED30 | 2C0700 / 2C0988 | 2C4FB8 / 2C52B0 | 2DFDB8 / 2E0030 | 2C4030 / 2C4240 | 2D7268 / 2D74E8 | 2CAC18 / 2CAD98 |
| hero_find_blue_swingshot_target | - | 2C7540 / 2C7840 | 2C6CE8 / 2C7068 | 2E79E8 / 2E7D08 | 303840 / 303BC0 | 2BE550 / 2BE768 | 2D2CB8 / 2D2FD8 | 2C3360 / 2C35F8 | 2D8CD8 / 2D8FE8 | 2C89B0 / 2C8BB8 | 2CECE0 / 2CEFD8 | 2C09A8 / 2C0C30 | 2C5260 / 2C5558 | - | 2C42D8 / 2C44E8 | 2D7510 / 2D7790 | 2CAEC0 / 2CB040 |
| FindNearestEligibleTarget (Dynamo) | 3ABBF8 / 3ACFF8 | 3BC6D0 / 3BDB60 | 3B88E0 / 3B9E20 | 3D9420 / 3DA810 | 3F9638 / 3FAB80 | 3A8F48 / 3AA228 | 3C3F28 / 3C5378 | 3B4B38 / 3B5EA8 | 3C92E8 / 3CA7E8 | 3BA750 / 3BBA28 | 3BECE8 / 3C01A0 | 3AFE88 / 3B12C8 | 3B8950 / 3B9E10 | 3D31A0 / 3D45D0 | 3B3858 / 3B4A88 | 3D24C8 / 3D38E0 | 3B2200 / 3B3480 |
| SelectBestEligibleTarget (Tractor) | 2AF340 / 2AF4B8 | 2B1B88 / 2B1C80 | 2B4148 / 2B42C0 | 2D4D70 / 2D4EE8 | 2EDE88 / 2EE000 | 2AD6A8 / 2AD720 | 2BFF38 / 2C00B0 | 2B12D8 / 2B13D0 | 2C6C10 / 2C6D88 | 2B40C0 / 2B4138 | 2B9690 / 2B9788 | 2AED68 / 2AEDE0 | 2AFC10 / 2AFD08 | 2CBBB0 / 2CBC28 | 2B1690 / 2B1708 | 2C1618 / 2C1690 | 2B5688 / 2B5600 |
| hero_score_target_by_heading | 2AEE40 / 2AEFB8 | 2B1688 / 2B1780 | 2B3C48 / 2B3DC0 | 2D4870 / 2D49E8 | 2ED988 / 2EDB00 | 2AD1A8 / 2AD220 | 2BFA38 / 2BFBB0 | 2B0DD8 / 2B0ED0 | 2C6710 / 2C6888 | 2B3BC0 / 2B3C38 | 2B9190 / 2B9288 | 2AE868 / 2AE8E0 | 2AF710 / 2AF808 | 2CB6B0 / 2CB728 | 2B1190 / 2B1208 | 2C1118 / 2C1190 | 2B5188 / 2B5100 |
| moby_transform_joint_to_world | 307390 / 308580 | 315618 / 3168E0 | 312898 / 313BE0 | 332928 / 333B18 | 3514C8 / 352810 | 307940 / 308A28 | 31E680 / 31F7C8 | 30D8D0 / 30EA40 | 323958 / 324B40 | 315148 / 316220 | 31B858 / 31CB18 | 30AD48 / 30BF90 | 312008 / 3132C8 | 32C5E0 / 32D808 | 30F168 / 310188 | 326528 / 327768 | 317EF8 / 318F80 |
| game_get_override_value | 376178 / 377590 | 387668 / 388B50 | 381428 / 382990 | 3A5BD8 / 3A6FF0 | 3C3598 / 3C4B08 | 375F90 / 377298 | 3907F0 / 391C68 | 37D3E0 / 37E778 | 394720 / 395B30 | 385730 / 386A30 | 389818 / 38ACF8 | 377B80 / 378FE8 | 3834E0 / 3849C8 | 39E2E8 / 39F730 | 37E010 / 37F258 | 397FE8 / 399448 | 3805E8 / 381890 |
| CalculateAimedLaunchVelocity | 3731A8 / 374598 | 384698 / 385B58 | 37E458 / 37F998 | 3A2C08 / 3A3FF8 | 3C05C8 / 3C1B10 | 372FC0 / 3742A0 | 38D820 / 38EC70 | 37A410 / 37B780 | 391750 / 392B38 | 382760 / 383A38 | 386848 / 387D00 | 374BB0 / 375FF0 | 380510 / 3819D0 | 39B318 / 39C738 | 37B040 / 37C260 | 395018 / 396450 | 37D618 / 37E898 |
| coll_trace_projectile_to_impact | 373808 / 374BF8 | 384CF8 / 3861B8 | 37EAB8 / 37FFF8 | 3A3268 / 3A4658 | 3C0C28 / 3C2170 | 373620 / 374900 | 38DE80 / 38F2D0 | 37AA70 / 37BDE0 | 391DB0 / 393198 | 382DC0 / 384098 | 386EA8 / 388360 | 375210 / 376650 | 380B70 / 382030 | 39B978 / 39CD98 | 37B6A0 / 37C8C0 | 395678 / 396AB0 | 37DC78 / 37EEF8 |
| SelectCurrentOrPreferredTarget (Hypno) | 3B3380 / 3B4788 | 3C6348 / 3C78A0 | 3C9138 / 3CA740 | 3E6FE8 / 3E84A0 | 404AC8 / 406018 | 3AFC98 / 3B0F80 | 3D20D0 / 3D3528 | 3C1B38 / 3C2F70 | 3D1438 / 3D2940 | 3C5310 / 3C66B0 | 3C7A58 / 3C8F18 | 3B8098 / 3B94E0 | 3C8C78 / 3CA200 | 3DB440 / 3DC878 | 3BF4E0 / 3C0718 | 3DC4F0 / 3DD9D0 | 3BF500 / 3BFA88 |

The other levels (0, 5, 10, 14, 15, 22-26) are in the same tsv rows. Regenerate the table from the tsv files by
function name, then fingerprint, rather than copying it by hand.

Joe's per-level **data** addresses (J/src/levels.hpp:51-63: collision_data, moby_base/end, camera, camera_forward,
capture) are v1 only. Our mod replaces them as follows:
- collision_data: decode it from CollLine, as `game_line` does.
- moby_base/end: our moby table via `relocate`.
- camera and camera_forward: block+0x140 and block+0x370 via `relocate_level(0, block)`.
- capture: the receiver state (see 2.5).

Joe also checks `mod.loaded_level() == play_status().level` before trusting a level's address (J/src/mod.cpp:51).
Keep that check.

---

## 3. Equip → use → restore (Joe's state machine, adapted)

Phases (J/src/mod.cpp:24): `idle, equipping, replay, active, capture, canceling, restoring`.

1. **idle → equipping** (J/src/mod.cpp:152-159, `begin` :228-263). All of these must hold:
   - a fresh press;
   - `can_equip`;
   - +0x22C8 == 0;
   - something drawn;
   - +0x22E4 is 0 or equals the hand;
   - the hand item is owned;
   - +0x22B8 == 0.

   Then take each owned gadget in the tier. **If two gadgets have targets, or the gadget is already in hand, do
   nothing and pass the press to the game** (:240-242).

   Save the previous hand item, +0x22E4, and `g_menu_choice_table[0]` (0x1A7398). Request the gadget and suppress
   Circle on this tick (:156).
2. **equipping**: suppress Circle every tick (`pending` :265-286). Cancel on any of these:
   - a timeout (100);
   - `!can_equip`;
   - the gadget not owned;
   - for a hold gadget, Fire released before ready;
   - the target changed;
   - for the Tractor, a jump.

   When ready (§1.4 plus the gadget's own test), go to **replay** and press Circle on that tick (:276-282).
3. **replay → active** (frame, :196-203): if the gadget reports "used" (`fired`, J/src/native.hpp:479-491) after the
   press, go to active. Otherwise cancel. Don't also run Joe's per-tick re-search in on_pad: use the game's own
   fields from §2.
4. **active**:
   - Tap gadgets do nothing until at rest.
   - Hold gadgets mirror Fire; release cancels (`held_input` :301-320).
   - The Tractor is as in 2.3.
   - The Dynamo cancels at rest (:205-206). The Tractor cancels when the latch is no longer its target (:207-209).
5. **canceling → restore** (`restore` :443-477), only while `can_equip`. It waits for the hand to be at rest. It also
   waits for the Tractor 8-tick release timeout, the Hypnomatic 3-tick grace and the Thermanator meter delay. Then
   it requests the previous item and moves to **restoring**.
   - If the previous item is no longer owned, or the hand already shows another item, it closes without restoring
     (:459-462). The player chose something else.
   - If the hand spawn failed (no hand while current is still our gadget), it requests the previous item after 8
     ticks (:448-457).
6. **restoring → idle** (:213-223): done when the hand shows the previous item in mode 2 and +0x22C8 == 0.
   - **Wrench special case** (:214-218): requesting the wrench also writes `g_menu_choice_table[0] = 10`
     (equip.c:221-225). Without a fix, the next Circle from the wrench would draw nothing new. If the previous item
     was the wrench, write the saved `g_menu_choice_table[0]` back. glawks' rac1 equivalent restores "the last held
     weapon" for the wrench (G/src/mod.cpp:265-268).
   - **Empty hand**: Joe never starts without something drawn (`p.drawn()`, :152). glawks restores only a previous
     item > 0 that is owned (G/src/mod.cpp:234-238). Do the same: with an empty hand, or a hand not in mode 2, there
     is no auto gadget.
   - Timeout 100: clear our own request and stop (:220-223).
7. `clear_own_request` (:408-417) clears +0x22C8 only if it still holds the mod's item. `cancel` (:419-426) releases a
   held Tractor and clears our pending request.

Swallowing (Joe `swallowed_`, :106-111, :176-178, :261, :280): after the mod takes a press, Circle stays suppressed
until the physical Fire is released. A held Fire then cannot fire the restored weapon or repeat the Dynamo
(J/tests/plugin_test.cpp:362-370). `circle_down_` starts true (:499), so a Fire held at load is not "fresh".

---

## 4. Edge cases (Joe's, and what they become in our mod)

| case | Joe | our mod |
|---|---|---|
| **Quick Select / manual choice** | His `rac2.item.equip` hook closes the auto gadget if a real equip happens during it (J/src/mod.cpp:89-98). `validate` also closes when +0x22C8, +0x22E4 or the hand show an item that is neither ours nor the previous one (:396-405). Tested: "manual selection wins" (J/tests/plugin_test.cpp:404-408). | Opening Triangle or Start makes our gate Off (AC/src/controls.hpp:284-287) → call cancel. After the menu, `validate` (state comparison) is enough. I did not find whether the Quick Select calls Item_Equip; its HUD, D/game/0034CEA0-menu-g8.c, has no call (M). So rely on state; the hook is optional. |
| **Menus** | Start or Triangle in the report, or play phase MENU → cancel (:118-124). Not PLAYING in frame → cancel (:189-194). The restore runs after, once `can_equip`. | Same, from our gate (`opening_menu` / `gate_now`, AC/src/mod.cpp:352-372, 380). |
| **Square / wrench** | Square down → release the Tractor, clear our request, close without restoring: the game's Square switches to the wrench (:128-136; equip.c:97-106). | Our Wrench action outputs Square. Read the rewritten Square bit. |
| **Jumping** | Cross, or group 2/4, stops a Tractor start and drops a held Tractor (:147-148, :162-164). Dead Aim (0.1.2) keeps the hold through a jump: the game's beam lets go only on a Fire release (tractor_beam_update, L00_00396288.c case 3), and a hold the game drops is seen as "its hold lost". Other gadgets may start in groups 2/4 (`is_foot_group`, J/src/input.hpp:34-36). | Read the rewritten Cross (our Jump). The Swingshot may start mid-jump (its case accepts group 2/5). A double jump sets +0x22B7 (forced wrench), and Joe's guard refuses there. |
| **R2** | With Contextual Wrench (R2 → Circle), R2 with a gun drawn always fires the gun. It cancels a pending equip (:138-145). R2 drops the Tractor and restores the gun, and holding R2 fires it after (J/tests/plugin_test.cpp:453-466). | **Does not apply**: our mod never reports R2 to the game in Active/Busy (AC/src/controls.hpp:11-13). Our single Fire is both "gun" and "gadget". Recommended guard (design, L): don't start an auto gadget while Fire was pressed within the last ~20 ticks, i.e. the player is shooting. Option: a setting "Auto gadgets on: Fire / Circle only". Physical Circle is untouched by our remap, so "Circle only" gives Joe's split. |
| **Fire pressed repeatedly** | A fresh edge is needed. Swallow until release. A tap buffered during the equip is replayed once. A fresh press during the Thermanator delay resumes (:150-151). | Same. |
| **Death** | Health <= 0, or state 0x31 / 0x61, → not alive. `validate` clears our request and closes, no restore (:60, :389-395). Tested (J/tests/plugin_test.cpp:410-414). | Same (our `gate_now` health test as well). |
| **Level change / reload** | `same_program` compares play_status().loads and loaded_level with the start. Otherwise close. During loading or front end, clear our request first (:347-358, :389-395). | Same. |
| **Gadget not owned** | `owned()` at start, every equipping tick and before the restore (J/src/native.hpp:85-87; J/src/mod.cpp:153, 233, 268, 451, 459). | Same, with the v1/v2 owned table. |
| **Ammo** | Not checked: these gadgets have no ammo. | Same. |
| **Equip refused** | `can_equip` (:74-76): PLAYING, alive, form 0, no vehicle, pad mode 0, +0x22BD/+0x22B7/+0x22BC/+0x22BF all 0, free hand (+0x123A/+0x1239 0), foot group. If the native Item_Equip returns 0, or +0x22C8 != item after it, `begin` fails and Circle goes to the game (:336-345). | Same guards. With a direct write, check the 100-tick timeout instead. |
| **Hypnomatic possession** | The capture phase gives every input to the game (:115-117, :362-387). On return, the game's own wrench request is tolerated (:398-399). | Our robot mapping runs during form 4. Auto gadgets are off. |
| **Other mod conflicts** | Joe refuses to load unless he is first in plugins.json (:506-531). | We remap inside one callback, so load order matters less. **Detect Joe's mod** (`mod.mods()` id `rac2-auto-gadgets`, API 1.14+). If it is loaded, turn ours off and log it. His mod only loads on ntsc with OpenRAC 1.1.0 (:525-528). |

---

## 5. Where it goes in Dead Aim

Today, `on_pad` (AC/src/mod.cpp:374-457) does this:
1. It builds the gate (Off for menus, scenes, vehicles and non-Ratchet forms; Busy off foot; Active on foot).
2. It calls `strafe::rewrite` (:414). The shoulders the player mapped to Fire, and the mod's mouse Fire, become
   Circle (bit 0x20 of byte 1 and pressure byte 11, AC/src/controls.hpp:187-257). Physical face buttons pass through.
3. Then the turret Fire (:416), Aim: Move and the stick.

`frame` and `aim_frame` are on_frame callbacks (:3745-3746). `frame` returns early in many cases (no camera, not
playing).

Recommended additions:

```
// controls.hpp: pure helpers, testable like rewrite()
inline bool circle_held(const uint8_t* r, int n) { return n >= 2 && held(r[kButtonsHigh], kCircle); }
inline void set_circle(uint8_t* r, int n, bool down, uint8_t p = 0xFF);   // Joe's circle_report

// mod.cpp on_pad, right after line 416 (after rewrite and the turret line), before Aim: Move:
if (s.auto_tier != Off && form == 0 && gate != strafe::Gate::Off && !in_turret) {
    // Fire intent = the rewritten Circle (mapped Fire shoulders + mouse Fire + physical Circle).
    // Jump / Wrench intent = the rewritten Cross / Square.
    auto_gadgets_pad(report, size, circle_held(report, size), held(report[1], kCross), held(report[1], kSquare), gate);
    // May clear Circle (suppress / swallow), press it (replay) or hold it (hold gadgets).
} else {
    auto_gadgets_interrupt();   // menus, gate Off, robot: cancel; never touch the report
}

// load(): before mod.on_frame(frame), so the camera code's early returns can't skip it
mod.on_frame(auto_gadgets_frame);   // ++tick, replay→active, canceling→restore, restoring→close,
                                    // and reads of the game's own target fields (§2)
```

- **(a) Fire intent before the remap**: reading the rewritten Circle is equivalent and simpler. It is
  `done.fire || physical Circle`. If physical Circle should not count, use `done.fire` (Outcome, controls.hpp:173-180).
- **(b) Suppress the weapon shot while the gadget equips**: clear Circle and its pressure on the press tick and on
  every equipping tick. Then the gun (or the wrench's draw-last-weapon on Circle, equip.c:112-118) never sees it.
- **(c) Press Circle for the gadget**: on the ready tick, press Circle (pressure 0xFF) for one tick (tap), or for as
  long as Fire is held (hold).
- **Where the previews run**: Joe calls the game's search functions inside on_pad at the fresh press, with the
  collision block preserved (J/src/mod.cpp:236; J/src/native.hpp:142-155). This is proven in play on v1. Do the
  same: one call per fresh press, not per tick.
  - Put the post-equip reads and the phase changes in `auto_gadgets_frame`, after the game's tick, when the
    gadget's fields are fresh.
  - Don't compute in on_frame and act a tick later in on_pad: that would delay every ordinary gun shot by a tick.
- **Gate**: start only in Joe's foot groups (Active, plus group 2/4). Keep tracking in Busy: the swing is Busy, and
  Fire must pass through as Circle there.
- **Lock strafe**: while the auto gadget is in hand, consider computing `config.lock_strafe` from the *previous*
  item, so the strafe does not drop for a Dynamo shot (AC/src/mod.cpp:383-384) (design).
- **Setting** (manifest, section "Buttons"): `auto_gadgets` choice `off` / `swingshot` / `traversal` (Swingshot,
  Dynamo, Tractor Beam) / `all`. Add Joe's `thermanator_return_delay` (2–5 s, default 3) if the Thermanator is kept.
- **Log** each start, cancel and restore with the reason, as glawks does (`Auto Swingshot:` lines,
  G/src/mod.cpp:147-164, 197, 210, 237). The user's test loop reads logs.
- **Tests**: Joe's offline harness (J/tests/plugin_test.cpp) simulates RAM, the pad, the native functions and the
  equip completing, step by step. A similar test of the pure part (the phase machine fed by a fake "game") fits
  `AC/tests`.

---

## 6. Credit, licences, SDK

### 6.1 What we would adapt from Joebannerlord's Auto Gadgets (GPL-3.0-or-later, same licence as ours)

- The phase machine and its rules: J/src/mod.cpp:24-46, 74-76, 100-225, 228-334, 347-484.
  - fresh press and swallow;
  - suppress while equipping, then replay;
  - tap versus hold;
  - "ambiguous or already in hand → stock";
  - Square, menu and jump handling;
  - `validate` against manual choices;
  - restore waits and grace periods;
  - the wrench `g_menu_choice_table` fix;
  - failed-spawn restore;
  - `clear_own_request`.
- The Player snapshot and `can_equip` guards (J/src/mod.cpp:26-76). Our `foot_group` and `is_weapon` already credit
  his Contextual Wrench and Fire (AC/src/mod.cpp:9-10).
- The adapter table and the per-gadget `ready` / `fired` / `resting` tests (J/src/native.hpp:60-75, 459-502).
- The target previews (J/src/native.hpp):
  - the Dynamo search and class filter (:173-180);
  - the Tractor search with the game's arguments, its LOS and the Oozla box preview (:182-259);
  - the Thermanator arc preview from hero+0xC80 and the grid test (:261-309);
  - the Hypnomatic search with `FreshHypnoAim`, `HypnoCollision`'s nine pass-through classes, the CollLine
    fallback and the reach check (:311-446);
  - `PreserveCollision` (:142-155);
  - `active_function`'s symbol check (:157-168), if we keep it.
- The idea of per-level function tables (J/src/levels.hpp). We would regenerate ours for v1 and v2 from the decomp
  tables rather than copy his.
- The Thermanator return-delay setting (J/openrac-mod.json) and the controls text (J/README.md:20-30).
- The shape of the tests, if we port them (J/tests/plugin_test.cpp).

Notice: keep `SPDX-License-Identifier: GPL-3.0-or-later`. Add a header line like Joe's, e.g. "Auto Gadgets adapted
from Joebannerlord's Auto Gadgets 0.1.5 (GPL-3.0-or-later,
https://github.com/Joebannerlord/openRAC-RC2-AutoGadget), whose temporary equip and Circle replay flow came from
glawks' Auto Swingshot (MIT)". Add a README credits line. Joe's own README also credits Codex, ReRAC2 and the
OpenRAC SDK JSON reader. We would not use his `src/common/json.*` or the plugins.json check.

### 6.2 What comes from glawks' Auto Swingshot (MIT; rac1)

- The original flow (G/src/mod.cpp:166-275), which Joe adapted and which our Swingshot tier follows directly:
  - request the item by its request word (:271);
  - wait for item == Swingshot and the item state ready, timeout 100 (:190-223);
  - cancel and restore if the target was lost before firing (:193-207);
  - **pulse Circle for one frame** (:208-213);
  - a player's own selection overrides (:227-230);
  - **restore after 15 rest frames with Circle up** (:231-243, `rest_state` G/src/detection.hpp:46);
  - restore only an owned previous item (:234-238);
  - the wrench restores the remembered weapon (:265-268);
  - a fresh press only (:186, :247);
  - one log line per decision.
- His distance, height and facing heuristics (G/src/detection.hpp) are **not** needed: rac2's own finders decide.
- His `ScopedPadCircle` edits the game's pad words around rac1's `Hero_Update` hook (G/src/mod.cpp:40-73, 286-292).
  rac2 has no such hook, so we edit the report in on_pad as Joe does.

Notice: MIT needs its copyright and permission text kept with substantial portions. Ship
`LICENSE-AUTO-SWINGSHOT` with "Copyright (c) 2026 glawks" and the MIT text (the copy in J/LICENSE-AUTO-SWINGSHOT),
and link it from the README credits (https://github.com/glawks/auto-swingshot).

### 6.3 SDK / API (our manifest `api` 1.20; our sdk.lock is 1.36.0)

All of these are in API 1.20 or older. **No version bump.**
- `on_pad` and `on_frame`; `memory`.
- `function_at`, `StackFrame` (`stack_push`/`stack_pop`) and `relocate` / `relocate_level`: API 1.0.
- `play_status` and `loaded_level`.
- `mods()`: API 1.14 (SDK 1.20 include/openrac/mod.hpp:1799-1825).
- Optional: `mod.hook<openrac::rac2::Item_Equip>` (`rac2.item.equip`, `i32(i32)`). The SDK 1.20 reference lists it
  for ntsc, pal and ntsc2 (sdk 1.20 docs/reference/hooks-rac2.md:22). If used, add `"rac2.item.equip"` to the
  manifest's `hooks` (today `[]`). That matters because the launcher shows shared hook points, and Joe's mod also
  takes this one.
- We don't need `mod.function(name)`. Joe uses it to cross-check addresses, and it resolves names "in this disc's
  build", so on v2 the names may differ (unknown).

---

## 7. Unknowns (to settle in play or with more reading)

1. How many ticks the swap takes from request to mode 2, for each gadget, and the first time in a level (class not
   cached). Log it.
2. Whether a direct +0x22C8 write works as well as the native Item_Equip for an uncached gadget (§1.3) **M**.
3. The v2 address of Joe's Hypnomatic capture flag (L18 `g_hero_frozen_flag` 0x1AFBB0 v1) is unknown. The
   replacement (receiver moby state 7 / hero+0x1E0) needs checking in play.
4. Whether `relocate_level(0, …)` maps the target list (0x1FACE0) and the camera block for every level (it does for
   the camera slot in our tests) **M**.
5. The Tractor's stock feel: whether the object needs Circle held to stay carried, or whether hero+0x5C0 alone
   carries it **M**. This decides between hold-to-carry (recommended for a shoulder Fire) and Joe's sticky mode.
6. The Oozla box eligibility refresh cadence (16 ticks per Joe) **L** (not read).
7. Whether the Quick Select's equip goes through Item_Equip **L**. Detect by state anyway.
8. What `vec_fast_helper_a` (an equip refusal, equip.c:236) and `func_00283DB8` (the Dynamo's visibility test) test
   exactly **L**.
9. Whether the Hypnomatic's `g_cam_follow_matrix` follows our Modern boom camera. It is the camera block's matrix,
   which our boom writes through the current camera **M**. Test that a Receiver Bot locks where the Modern camera
   looks.
10. The Thermanator arc follows hero+0x730, which our aim-follow does not write for gadgets. Decide whether to
    extend it.
11. The equip guards come from Joe's v1 tests. None of this has been run on v2.00. The hero offsets and the code
    are identical by the decomp tables, but the owned table's v2 address (0x1A7B00) comes from the brief, not from
    a symbol I read.
