// SPDX-License-Identifier: GPL-3.0-or-later
// The controller side of Dead Aim, free of the game so it can be tested on its own.
//
// Each shoulder (L1, L2, R1, R2) is given an action, which
// the mod reports as the game's own button for it: Jump as Cross, Fire as Circle, Wrench as Square,
// Aim as L1, Crouch as R1, and strafing as L2.
//
// Going Commando strafes while L2 or R2 is held: the hero's update tests the pad's held word
// against 3 (L2 | R2) and keeps his strafe timer (hero +0x1454) up while it is (rac2-decomp
// src/levels/shared/L18_002C4000.c, UpdateHeroInputAndHistory). So the mod drives strafing by
// what it reports for L2, and the game's own code does the rest (the strafe flips included). It
// never reports R2, so never L2 and R2 together: the few things that want both shoulders at once
// (a wrench target lock, a wrench equip chord) stay out of the way.
//
// In play L1 aims and R1 crouches (from idle the decider, src/levels/shared/L00_002CB3A8-decider.c,
// takes L1 held to state 1, left when L1 comes up, and R1 to state 4; checked by playing).
#pragma once

#include <algorithm>
#include <array>
#include <utility>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace strafe {

// The pad report as the console's pad library gives it: two button bytes (a bit clear while its
// button is held), the right stick's x and y, the left stick's x and y (0x80 at rest), then the
// buttons' pressures (0 to 255) in the order Right, Left, Up, Down, Triangle, Circle, Cross,
// Square, L1, R1, L2, R2. A digital pad's report stops after the buttons.
constexpr int kButtonsLow = 0;   // Select, L3, R3, Start, Up, Right, Down, Left
constexpr int kButtonsHigh = 1;  // L2, R2, L1, R1, Triangle, Circle, Cross, Square
constexpr int kLeftStickX = 4;
constexpr int kLeftStickY = 5;
constexpr int kFullReport = 18;

// Bits of kButtonsLow.
constexpr std::uint8_t kStart = 0x08;
constexpr std::uint8_t kRight = 0x20;
constexpr std::uint8_t kLeft = 0x80;

// Bits of kButtonsHigh.
constexpr std::uint8_t kL2 = 0x01;
constexpr std::uint8_t kR2 = 0x02;
constexpr std::uint8_t kL1 = 0x04;
constexpr std::uint8_t kR1 = 0x08;
constexpr std::uint8_t kTriangle = 0x10;
constexpr std::uint8_t kCircle = 0x20;
constexpr std::uint8_t kCross = 0x40;
constexpr std::uint8_t kSquare = 0x80;
constexpr std::uint8_t kShoulders = kL1 | kL2 | kR1 | kR2;

// Pressure bytes.
constexpr int kCirclePressure = 11;
constexpr int kCrossPressure = 12;
constexpr int kSquarePressure = 13;
constexpr int kL1Pressure = 14;
constexpr int kR1Pressure = 15;
constexpr int kL2Pressure = 16;
constexpr int kR2Pressure = 17;

// What a shoulder does. The values are the manifest's choices, in this order. None is the mouse
// buttons' only (older OpenRAC): a shoulder always does something.
enum class Action : std::uint8_t { ToggleStrafe = 0, Jump, Fire, Crouch, Aim, Wrench, None };
constexpr int kActionCount = 7;

struct ActionInfo {
    const char* value;  // the manifest's value
    const char* name;   // the manifest's name (the menu capitalises each word)
};

// Each action is the game's own button (Jump Cross, Fire Circle, Crouch R1, Aim L1), named for what
// it does most of the time: what it does anywhere else (swimming, ...) is the game's, as for that
// button.
constexpr std::array<ActionInfo, kActionCount> kActions{{
    {"strafe", "Toggle strafe"},
    {"jump", "Jump"},
    {"fire", "Fire"},
    {"crouch", "Crouch"},
    {"aim", "Aim"},
    {"wrench", "Wrench"},
    {"none", "Nothing"},
}};

constexpr const char* action_value(Action a) { return kActions[static_cast<int>(a)].value; }
constexpr const char* action_name(Action a) { return kActions[static_cast<int>(a)].name; }

inline Action action_from_value(std::string_view value) {
    for (int i = 0; i < kActionCount; ++i) {
        if (value == kActions[i].value) {
            return static_cast<Action>(i);
        }
    }
    return Action::ToggleStrafe;
}

// The four shoulders, in the menu's order.
enum Shoulder { kShoulderL1 = 0, kShoulderL2, kShoulderR1, kShoulderR2, kShoulderCount };

struct ShoulderInfo {
    const char* key;  // the manifest's setting
    const char* name;
    std::uint8_t bit;
    int pressure;
};

constexpr std::array<ShoulderInfo, kShoulderCount> kShoulderInfo{{
    {"l1", "L1", kL1, kL1Pressure},
    {"l2", "L2", kL2, kL2Pressure},
    {"r1", "R1", kR1, kR1Pressure},
    {"r2", "R2", kR2, kR2Pressure},
}};

// The mouse's buttons, in the menu's order (OPENRAC_MOUSE_LEFT, _RIGHT, _MIDDLE): the mod's own
// mouse (older OpenRAC), where OpenRAC has no built-in keyboard and mouse binding them itself.
enum MouseButton { kMouseLeft = 0, kMouseRight, kMouseMiddle, kMouseButtonCount };

struct MouseButtonInfo {
    const char* key;
    const char* name;
    std::uint32_t bit;  // of openrac_input.mouse_held
};

constexpr std::array<MouseButtonInfo, kMouseButtonCount> kMouseButtonInfo{{
    {"mouse_left", "Left click", 1u},
    {"mouse_right", "Right click", 4u},
    {"mouse_middle", "Middle click", 2u},
}};

struct Config {
    // Strafe on foot without holding anything; a Toggle strafe shoulder held then runs free.
    bool lock_strafe = true;
    std::array<Action, kShoulderCount> actions{Action::Jump, Action::Aim, Action::Crouch, Action::Fire};
    std::array<Action, kMouseButtonCount> mouse{Action::Fire, Action::Wrench, Action::None};  // (older OpenRAC)
    // The game's buttons the actions press, for a body other than Ratchet's (the Hypnomatic's
    // robot): Fire and Wrench may swap, Crouch may be the strafe toggle, and Aim may let the strafe
    // go (the robot looks only standing still and not strafing).
    std::uint8_t fire_button = kCircle;
    std::uint8_t wrench_button = kSquare;
    bool crouch_toggles = false;
    bool aim_releases_strafe = false;
    // The face buttons Circle and Square swapped (the robot: its shot on Circle and its beam on
    // Square, as Ratchet's fire and wrench sit).
    bool swap_circle_square = false;
};

// The pressure byte of a face button.
constexpr int pressure_of(std::uint8_t button) {
    return button == kCircle ? kCirclePressure : button == kCross ? kCrossPressure : kSquarePressure;
}

// The game's own layout: no lock strafe, L1 aim, R1 crouch, L2 and R2 strafe.
constexpr Config stock_config() {
    Config c;
    c.lock_strafe = false;
    c.actions = {Action::Aim, Action::ToggleStrafe, Action::Crouch, Action::ToggleStrafe};
    return c;
}

// Where the shoulders stand this report.
enum class Gate {
    // Not the hero's to play (menus, scenes, vehicles, the ship, Clank): the pad goes raw.
    Off,
    // The hero in play in a state off foot (the Swingshot's swing, ...): the shoulders do what
    // they are set to, but nothing strafes for them.
    Busy,
    // On foot: the shoulders do what they are set to, and the strafe is the mod's.
    Active,
};

// What the mod made of one report: the strafe and the aim for the caller, all of it for the tests.
struct Outcome {
    bool strafe = false;
    bool jump = false;
    bool fire = false;
    bool crouch = false;
    bool aim = false;
    bool wrench = false;
};

inline bool held(std::uint8_t byte, std::uint8_t bit) { return (byte & bit) == 0; }

// Rewrites the shoulders of one report for `gate`, with the mouse's buttons held (`mouse_held`,
// openrac_input.mouse_held's bits) as more inputs: the mod's own mouse (older OpenRAC). With
// OpenRAC's built-in keyboard and mouse the caller passes none (OpenRAC binds them to the pad's
// buttons itself).
inline Outcome rewrite(std::uint8_t* report, int size, const Config& config, Gate gate, std::uint32_t mouse_held = 0) {
    Outcome out;
    if (report == nullptr || size < 2 || gate == Gate::Off) {
        return out;
    }
    if (config.swap_circle_square) {
        const std::uint8_t b = report[kButtonsHigh];
        const bool circle = (b & kCircle) == 0, square = (b & kSquare) == 0;
        report[kButtonsHigh] = static_cast<std::uint8_t>((b | kCircle | kSquare) & ~((circle ? kSquare : 0) | (square ? kCircle : 0)));
        if (size >= kFullReport) std::swap(report[kCirclePressure], report[kSquarePressure]);
    }
    const std::uint8_t high = report[kButtonsHigh];
    const bool full = size >= kFullReport;

    // Each held shoulder's action, with the strongest pressure behind each output button.
    bool toggle = false;
    std::uint8_t jump = 0, fire = 0, crouch = 0, aim = 0, wrench = 0;
    const auto take = [&](Action action, std::uint8_t pressure) {
        switch (action) {
            case Action::ToggleStrafe: toggle = true; break;
            case Action::Jump: out.jump = true; jump = std::max(jump, pressure); break;
            case Action::Fire: out.fire = true; fire = std::max(fire, pressure); break;
            case Action::Crouch:
                if (config.crouch_toggles) {
                    toggle = true;
                } else {
                    out.crouch = true;
                    crouch = std::max(crouch, pressure);
                }
                break;
            case Action::Aim: out.aim = true; aim = std::max(aim, pressure); break;
            case Action::Wrench: out.wrench = true; wrench = std::max(wrench, pressure); break;
            case Action::None: break;
        }
    };
    for (int i = 0; i < kShoulderCount; ++i) {
        const ShoulderInfo& s = kShoulderInfo[i];
        if (held(high, s.bit)) {
            take(config.actions[i], full ? std::max<std::uint8_t>(report[s.pressure], 1) : 0xFF);
        }
    }
    for (int i = 0; i < kMouseButtonCount; ++i) {  // (older OpenRAC)
        if ((mouse_held & kMouseButtonInfo[i].bit) != 0) {
            take(config.mouse[i], 0xFF);
        }
    }
    out.strafe = gate == Gate::Active && config.lock_strafe != toggle && !(config.aim_releases_strafe && out.aim);

    // The shoulders are the mod's now: all up, then the ones its actions press.
    std::uint8_t buttons = static_cast<std::uint8_t>(high | kShoulders);
    if (full) {
        for (const ShoulderInfo& s : kShoulderInfo) {
            report[s.pressure] = 0;
        }
    }
    const auto press = [&](bool on, std::uint8_t bit, int pressure_at, std::uint8_t pressure) {
        if (!on) return;
        buttons = static_cast<std::uint8_t>(buttons & ~bit);
        if (full) {
            report[pressure_at] = std::max(report[pressure_at], pressure);
        }
    };
    press(out.strafe, kL2, kL2Pressure, 0xFF);
    press(out.jump, kCross, kCrossPressure, jump);
    press(out.fire, config.fire_button, pressure_of(config.fire_button), fire);
    press(out.crouch, kR1, kR1Pressure, crouch);
    press(out.aim, kL1, kL1Pressure, aim);
    press(out.wrench, config.wrench_button, pressure_of(config.wrench_button), wrench);
    report[kButtonsHigh] = buttons;
    return out;
}

// Only the Fire action, on Circle, the rest of the report left as it is (a turret). The mouse's
// buttons held (`mouse_held`) as in rewrite (older OpenRAC).
inline void fire_only(std::uint8_t* report, int size, const Config& config, std::uint32_t mouse_held = 0) {
    if (report == nullptr || size < 2) return;
    const bool full = size >= kFullReport;
    std::uint8_t pressure = 0;
    bool fire = false;
    for (int i = 0; i < kShoulderCount; ++i) {
        const ShoulderInfo& s = kShoulderInfo[i];
        if (config.actions[i] == Action::Fire && held(report[kButtonsHigh], s.bit)) {
            fire = true;
            pressure = std::max<std::uint8_t>(pressure, full ? std::max<std::uint8_t>(report[s.pressure], 1) : 0xFF);
        }
    }
    for (int i = 0; i < kMouseButtonCount; ++i) {  // (older OpenRAC)
        if (config.mouse[i] == Action::Fire && (mouse_held & kMouseButtonInfo[i].bit) != 0) {
            fire = true;
            pressure = 0xFF;
        }
    }
    if (!fire) return;
    report[kButtonsHigh] = static_cast<std::uint8_t>(report[kButtonsHigh] & ~kCircle);
    if (full) report[kCirclePressure] = std::max(report[kCirclePressure], pressure);
}

// About to open a menu: Start (pause) or Triangle (the Quick Select). Such a report is left raw.
inline bool opening_menu(const std::uint8_t* report, int size) {
    return report != nullptr && size >= 2
        && (held(report[kButtonsLow], kStart) || held(report[kButtonsHigh], kTriangle));
}

// What the player means on a row of the mod's menu page. The port steps a choice on Cross, Left
// and Right alike, always forward, and the left stick turns into Left and Right past a third of
// the way; so a stick pushed down a little off straight would step the row it lands on. (The
// same trouble the Cheats mod in openrac-mods works around.)
enum class Intent { Unknown, Choose, Next, Previous, Moving };

inline Intent read_intent(const std::uint8_t* report, int size) {
    if (report == nullptr || size < 2) return Intent::Unknown;
    if (held(report[kButtonsHigh], kCross)) return Intent::Choose;
    if (held(report[kButtonsLow], kRight)) return Intent::Next;
    if (held(report[kButtonsLow], kLeft)) return Intent::Previous;
    if (size >= 6) {
        const int x = static_cast<int>(report[kLeftStickX]) - 0x80;
        const int y = static_cast<int>(report[kLeftStickY]) - 0x80;
        const int dead = 0x80 / 3;
        if (std::abs(y) >= std::abs(x) && std::abs(y) > dead) return Intent::Moving;
        if (x > dead) return Intent::Next;
        if (x < -dead) return Intent::Previous;
    }
    return Intent::Unknown;
}

// A choice's new index for a change the port reports (`said`), the choice now at `current` of
// `count`: the way the player steps it, the port's when the pad does not say, none for the cursor
// moving.
inline std::optional<int> step_choice(Intent intent, int current, int count, int said) {
    if (count <= 0) return std::nullopt;
    switch (intent) {
        case Intent::Moving: return std::nullopt;
        case Intent::Next: return (current + 1) % count;
        case Intent::Previous: return (current + count - 1) % count;
        default: return std::clamp(said, 0, count - 1);
    }
}

// Crouch: Deadlocked (dev notes rac4-wrench-throw.md). Going Commando crouches a running hero the
// moment R1 comes, and the run is lost; Deadlocked never crouches him on the run, so Crouch held on
// the run is a modifier: with Jump it long jumps from the run, with Square it throws the wrench
// (here the mod's toss: wrench_toss.hpp), and nothing stops him. So while he moves (or is in the
// air), the game's R1 (Crouch) is held back for as long as Crouch is held:
// - Cross: R1 goes to the game with it, on the same tick (the long jump reads R1 held on the X tick).
// - `to_game` (Square with no toss to make): the same, for the game's own throw.
// - Stopped: R1 goes to the game from then on (a crouch, the high jump, the game's throw).
// - A tap the game never saw is given after its release, for two reports, so a double tap still
//   starts the Charge Boots (their 30-tick window: rac2-decomp L00_002C9448-g8.c).
class RunCrouch {
public:
    static constexpr int kTap = 10;        // ticks held back that still make a tap
    static constexpr int kTapReports = 2;  // reports a held-back tap is given afterwards

    enum class R1 { Pass, Hide, Press };  // the game's R1 this tick: as it is, held back, or pressed

    // One tick: `moving` he moves on foot or is in the air (the setting on), `crouch` the game's R1
    // as the player's buttons make it, `cross` the game's Cross, `to_game` R1 wanted by the game this
    // tick, `used` Crouch spent this tick on the mod's toss.
    R1 step(bool moving, bool crouch, bool cross, bool to_game, bool used) {
        if (!crouch) {
            if (held_ > 0 && held_ <= kTap && !seen_ && !spent_) owed_ = kTapReports;
            held_ = 0;
            seen_ = spent_ = false;
            if (owed_ > 0) {
                --owed_;
                return R1::Press;
            }
            return R1::Pass;
        }
        owed_ = 0;
        if (used) spent_ = true;
        if (!moving || cross || to_game) {
            seen_ = true;
            if (cross || to_game) spent_ = true;
            return R1::Pass;
        }
        ++held_;
        return R1::Hide;
    }

    void reset() { *this = RunCrouch{}; }

private:
    int held_ = 0;
    int owed_ = 0;
    bool seen_ = false, spent_ = false;
};

}  // namespace strafe
