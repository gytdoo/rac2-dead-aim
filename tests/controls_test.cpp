// SPDX-License-Identifier: GPL-3.0-or-later
#include "controls.hpp"
#include "wrench_toss.hpp"

#include <array>
#include <cstdio>

using namespace strafe;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

using Report = std::array<std::uint8_t, kFullReport>;

// A full report with the sticks at rest and `high` / `low` buttons held (bits set = held), each
// held shoulder at a pressure of its own.
Report report(std::uint8_t high = 0, std::uint8_t low = 0) {
    Report r{};
    r[kButtonsLow] = static_cast<std::uint8_t>(~low);
    r[kButtonsHigh] = static_cast<std::uint8_t>(~high);
    r[2] = r[3] = r[4] = r[5] = 0x80;
    if (high & kL1) r[kL1Pressure] = 210;
    if (high & kR1) r[kR1Pressure] = 190;
    if (high & kL2) r[kL2Pressure] = 200;
    if (high & kR2) r[kR2Pressure] = 180;
    return r;
}

bool down(const Report& r, std::uint8_t bit) { return (r[kButtonsHigh] & bit) == 0; }

Report run(std::uint8_t high, const Config& config, Gate gate = Gate::Active) {
    Report r = report(high);
    rewrite(r.data(), kFullReport, config, gate);
    return r;
}

}  // namespace

// Crouch: Deadlocked. Held back on the run for as long as it is held; Cross or the game's throw take
// it; stopping gives it; a tap is given late; the toss spends it.
void run_crouch_tests() {
    using R = RunCrouch::R1;
    {
        RunCrouch c;
        for (int i = 0; i < 40; ++i) c.step(true, true, false, false, false);
        check(c.step(true, true, false, false, false) == R::Hide, "crouch: held on the run, never given");
        check(c.step(false, true, false, false, false) == R::Pass, "crouch: he stops: given (a crouch)");
        check(c.step(true, false, false, false, false) == R::Pass, "crouch: let go after it was seen: nothing owed");
    }
    {
        RunCrouch c;
        c.step(true, true, false, false, false);
        check(c.step(true, true, true, false, false) == R::Pass, "crouch: Cross on the run: R1 with it (long jump)");
        check(c.step(true, true, false, false, false) == R::Hide, "crouch: after the jump, held back again (a run on landing)");
        check(c.step(true, false, false, false, false) == R::Pass, "crouch: let go: nothing owed");
    }
    {
        RunCrouch c;
        c.step(true, true, false, false, false);
        check(c.step(true, true, false, false, true) == R::Hide, "crouch: the toss spends it, still held back");
        check(c.step(true, false, false, false, false) == R::Pass, "crouch: let go after a toss: nothing owed");
    }
    {
        RunCrouch c;
        c.step(true, true, false, false, false);
        check(c.step(true, true, false, true, false) == R::Pass, "crouch: Square with no toss: R1 to the game's throw");
    }
    {   // A double tap on the run (the Charge Boots): each tap reaches the game, a little late.
        RunCrouch c;
        c.step(true, true, false, false, false);
        c.step(true, true, false, false, false);
        check(c.step(true, false, false, false, false) == R::Press, "crouch: a tap is given");
        check(c.step(true, false, false, false, false) == R::Press, "crouch: for two reports");
        check(c.step(true, false, false, false, false) == R::Pass, "crouch: then let go");
        check(c.step(true, true, false, false, false) == R::Hide, "crouch: the second tap waits");
        check(c.step(true, false, false, false, false) == R::Press, "crouch: and is given too");
    }
    {   // A long hold let go on the run gives nothing.
        RunCrouch c;
        for (int i = 0; i <= RunCrouch::kTap; ++i) c.step(true, true, false, false, false);
        check(c.step(true, false, false, false, false) == R::Pass, "crouch: a long hold let go: nothing");
    }
}

// The toss: a press waits for the wrench, the launch comes kRelease ticks into the swing.
void toss_tests() {
    toss::Motion m;
    m.request();
    int launch_at = -1;
    for (int t = 0; t < 30; ++t) {
        if (m.step(false, true, 0.0f, false).launch && launch_at < 0) launch_at = t;
    }
    check(launch_at == toss::Motion::kRelease, "toss: the launch at the release tick");
    toss::Motion busy;
    busy.request();
    for (int t = 0; t < 10; ++t) check(!busy.step(false, false, 0.0f, false).launch, "toss: no launch while the wrench is out");
    bool launched = false;
    for (int t = 0; t < 12; ++t) launched = launched || busy.step(false, true, 0.0f, false).launch;
    check(launched, "toss: a press kept until the wrench is back");
    check(toss::arm({}).empty() && toss::arm({1, 0, 0, 0}).size() == 3, "toss: the arm posed only while needed");
    check(toss::catch_reach(10.0f) == 0.0f && toss::catch_reach(1.5f) == 1.0f, "toss: the reach from distance");
}

int main() {
    run_crouch_tests();
    toss_tests();
    const Config stock = stock_config();
    const Config defaults{};

    {   // The stock layout gives the game what it would have had (R2's strafe as L2).
        check(!down(run(0, stock), kL2), "stock, nothing held: no strafe");
        Report r = run(kR2, stock);
        check(down(r, kL2) && !down(r, kR2), "stock, R2: strafe as L2");
        r = run(kL1, stock);
        check(down(r, kL1) && r[kL1Pressure] == 210 && !down(r, kL2) && !down(r, kR1), "stock, L1: aim (game L1)");
        r = run(kR1, stock);
        check(down(r, kR1) && r[kR1Pressure] == 190 && !down(r, kL1), "stock, R1: crouch (game R1)");
    }
    {   // The defaults: L1 jump, L2 aim, R1 crouch, R2 fire, lock strafe on.
        Report r = run(0, defaults);
        check(down(r, kL2) && r[kL2Pressure] == 0xFF, "defaults: strafing with nothing held");
        r = run(kL1, defaults);
        check(down(r, kCross) && r[kCrossPressure] == 210 && !down(r, kL1), "L1 jumps");
        r = run(kL2, defaults);
        check(down(r, kL1) && r[kL1Pressure] == 200 && !down(r, kR1) && down(r, kL2),
              "L2 aims (game L1), never crouches, still strafing");
        r = run(kR1, defaults);
        check(down(r, kR1) && r[kR1Pressure] == 190 && !down(r, kL1), "R1 crouches (game R1), never aims");
        r = run(kR2, defaults);
        check(down(r, kCircle) && r[kCirclePressure] == 180 && !down(r, kR2), "R2 fires, never R2");
        r = run(kR1 | kL1, defaults);
        check(down(r, kR1) && down(r, kCross), "crouch and jump together: the high jump");
        r = run(kCross, defaults);
        check(down(r, kCross), "physical Cross untouched");
    }
    {   // A turret: only Fire, on Circle; the rest untouched.
        Report r = report(kR2);
        fire_only(r.data(), kFullReport, defaults);
        check(down(r, kCircle) && r[kCirclePressure] == 180 && down(r, kR2), "turret: R2 Fire pulls Circle, R2 left");
        Report q = report();
        fire_only(q.data(), kFullReport, defaults);
        check(!down(q, kCircle), "turret: nothing held, nothing pressed");
    }
    {   // The Hypnomatic's attack robot: Fire is Square, Wrench Circle (its beam), Crouch the toggle,
        // Aim lets the strafe go.
        Config robot = defaults;
        robot.fire_button = kSquare;
        robot.wrench_button = kCircle;
        robot.crouch_toggles = true;
        robot.aim_releases_strafe = true;
        Report r = run(0, robot);
        check(down(r, kL2), "robot: strafing with nothing held");
        r = run(kR2, robot);
        check(down(r, kSquare) && r[kSquarePressure] != 0 && !down(r, kCircle), "robot: Fire is Square");
        r = run(kR1, robot);
        check(!down(r, kR1) && !down(r, kL2), "robot: Crouch toggles the strafe off, presses no R1");
        r = run(kL2, robot);
        check(down(r, kL1) && !down(r, kL2), "robot: Aim lets the strafe go");
        r = run(kL1, robot);
        check(down(r, kCross) && down(r, kL2), "robot: Jump still strafing");
        robot.swap_circle_square = true;
        Report f = report(0, 0);
        f[kButtonsHigh] = static_cast<std::uint8_t>(f[kButtonsHigh] & ~kCircle);
        f[kCirclePressure] = 0x90;
        rewrite(f.data(), kFullReport, robot, Gate::Active);
        check(down(f, kSquare) && f[kSquarePressure] == 0x90 && !down(f, kCircle), "robot: Circle shoots (Square)");
        Report q = report(0, 0);
        q[kButtonsHigh] = static_cast<std::uint8_t>(q[kButtonsHigh] & ~kSquare);
        rewrite(q.data(), kFullReport, robot, Gate::Active);
        check(down(q, kCircle) && !down(q, kSquare), "robot: Square beams (Circle)");
    }
    {   // Toggle strafe flips the lock while held.
        Config c = defaults;
        c.actions[kShoulderL2] = Action::ToggleStrafe;
        Report r = run(kL2, c);
        check(!down(r, kL2) && r[kL2Pressure] == 0, "lock, toggle held: runs free");
        c.lock_strafe = false;
        r = run(kL2, c);
        check(down(r, kL2), "no lock, toggle held: strafes");
    }
    {   // Off foot (the Swingshot): the mapping holds, nothing strafes.
        Report r = run(kR2, defaults, Gate::Busy);
        check(down(r, kCircle) && !down(r, kL2) && !down(r, kR2), "swinging: R2 still holds Circle");
        r = run(kL1, defaults, Gate::Busy);
        check(down(r, kCross) && !down(r, kL1), "swinging: L1 still jumps, does not crouch");
    }
    {   // Outside play: raw.
        Report r = run(kR2 | kL1, defaults, Gate::Off);
        check(down(r, kR2) && down(r, kL1) && !down(r, kCircle) && !down(r, kCross), "off: raw");
    }
    {   // The mod's own mouse buttons (older OpenRAC): left fires, right swings the wrench, only in play.
        Report r = report();
        rewrite(r.data(), kFullReport, defaults, Gate::Active, kMouseButtonInfo[kMouseLeft].bit);
        check(down(r, kCircle) && r[kCirclePressure] == 0xFF, "left click fires");
        r = report();
        rewrite(r.data(), kFullReport, defaults, Gate::Active, kMouseButtonInfo[kMouseRight].bit);
        check(down(r, kSquare), "right click swings the wrench");
        r = report();
        rewrite(r.data(), kFullReport, defaults, Gate::Off, kMouseButtonInfo[kMouseLeft].bit);
        check(!down(r, kCircle), "no click in menus");
        r = report();
        rewrite(r.data(), kFullReport, defaults, Gate::Active, 0);
        check(!down(r, kCircle) && !down(r, kSquare), "no mouse: nothing");
    }
    {   // A shoulder set to Wrench, or to Nothing, which only the mouse buttons offer (older OpenRAC).
        Config c = defaults;
        c.actions[kShoulderL1] = Action::Wrench;
        c.actions[kShoulderR1] = Action::None;
        Report r = run(kL1 | kR1, c);
        check(down(r, kSquare) && !down(r, kL1) && !down(r, kR1) && !down(r, kCross), "L1 wrench, R1 nothing");
    }
    {   // A digital report: buttons only.
        std::array<std::uint8_t, 2> d{0xFF, static_cast<std::uint8_t>(~kR2)};
        rewrite(d.data(), 2, defaults, Gate::Active);
        check((d[1] & kCircle) == 0 && (d[1] & kL2) == 0 && (d[1] & kR2) != 0, "digital report rewritten");
    }
    {   // Menus.
        Report r = report(0, kStart);
        check(opening_menu(r.data(), kFullReport), "Start opens a menu");
        r = report(kTriangle);
        check(opening_menu(r.data(), kFullReport), "Triangle opens the Quick Select");
    }
    {   // The page's intent.
        Report r = report(kCross);
        check(read_intent(r.data(), kFullReport) == Intent::Choose, "Cross chooses");
        r = report(0, kLeft);
        check(read_intent(r.data(), kFullReport) == Intent::Previous, "Left steps back");
        r = report();
        r[kLeftStickY] = 0xFF;
        r[kLeftStickX] = 0xB0;
        check(read_intent(r.data(), kFullReport) == Intent::Moving, "stick down, a bit right: moving");
        check(!step_choice(Intent::Moving, 1, 5, 2), "moving steps nothing");
        check(step_choice(Intent::Previous, 0, 5, 1) == 4, "back from the first wraps");
        check(step_choice(Intent::Unknown, 0, 5, 1) == 1, "unknown takes the port's");
    }
    {   // The manifest's values.
        bool ok = action_from_value("x") == Action::ToggleStrafe;
        for (int i = 0; i < kActionCount; ++i) {
            ok = ok && action_from_value(action_value(static_cast<Action>(i))) == static_cast<Action>(i);
        }
        check(ok, "values round-trip");
    }

    if (failures == 0) {
        std::printf("all controls tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
