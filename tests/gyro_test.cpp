// SPDX-License-Identifier: GPL-3.0-or-later
// The gyro on its own: OpenRAC's filter (gyro_aim.hpp, its own tests ported), and the reports of
// each controller as SDL reads them (gyro_hid.hpp, from synthetic reports laid out as SDL's
// drivers read them).
#include "gyro.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The ported tests' CHECK: the condition itself as what failed, and its line.
void check_at(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("FAIL (line %d): %s\n", line, what);
        ++failures;
    }
}
#define CHECK(cond) check_at((cond), #cond, __LINE__)

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadiansPerDegree = kPi / 180.0f;

void put16(std::uint8_t* r, int at, int v) {
    r[at] = static_cast<std::uint8_t>(v & 0xFF);
    r[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

void put32(std::uint8_t* r, int at, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) r[at + k] = static_cast<std::uint8_t>((v >> (8 * k)) & 0xFF);
}

}  // namespace

// ---- OpenRAC's filter (gyro_aim.hpp), OpenRAC's tests/gyro_test.cpp ported ----
//
// A turn of the controller turns the camera by as much times the sensitivity; the resting offset
// is learnt while it lies still, not while it turns slowly; the spaces pick the right twist; the
// button and the dead zone gate it. The same cases and numbers as OpenRAC's; only the button is
// this repository's Button rather than an SDL_GamepadButton number.

namespace aim {

using gyro::Button;
using gyro::GyroAim;
using gyro::GyroButtonAction;
using gyro::GyroOptions;
using gyro::GyroSpace;
using gyro::Vec3f;

constexpr std::uint64_t kStepNs = 4'000'000;  // 250 samples a second, a DualShock 4's rate
constexpr float kStep = 0.004f;

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

GyroOptions on() {
    GyroOptions o;
    o.enabled = true;
    o.sensitivity_x = 1.0f;
    o.sensitivity_y = 1.0f;
    o.auto_calibrate = false;
    return o;
}

// `n` samples of `rate` from `ns` on.
std::uint64_t feed(GyroAim& g, Vec3f rate, int n, std::uint64_t ns) {
    for (int i = 0; i < n; ++i) {
        ns += kStepNs;
        g.gyro(rate, ns);
    }
    return ns;
}

void test_turn_is_the_controllers() {
    GyroAim g;
    GyroOptions o = on();
    o.space = GyroSpace::Yaw;
    g.set_options(o);
    // A quarter turn left in a second: the camera turns a quarter turn left.
    const float rate = kPi / 2.0f;
    feed(g, {0.0f, rate, 0.0f}, 251, 0);  // the first sample only starts the clock
    float yaw = 0.0f;
    float pitch = 0.0f;
    g.take(yaw, pitch);
    CHECK(near(yaw, rate, 0.01f));
    CHECK(near(pitch, 0.0f));
    // Taken: nothing more until it moves.
    g.take(yaw, pitch);
    CHECK(yaw == 0.0f);

    // Sensitivity 2 and inverted up and down.
    o.sensitivity_y = 2.0f;
    o.invert_y = true;
    g.set_options(o);
    feed(g, {0.5f, 0.0f, 0.0f}, 250, 10'000'000'000ull);
    g.take(yaw, pitch);
    CHECK(near(pitch, -2.0f * 0.5f * 249.0f * kStep, 0.01f));
}

void test_gap_is_not_motion() {
    GyroAim g;
    g.set_options(on());
    std::uint64_t ns = feed(g, {0.0f, 1.0f, 0.0f}, 2, 0);
    float yaw = 0.0f;
    float pitch = 0.0f;
    g.take(yaw, pitch);
    // A second without samples (a lost link), then one: no turn for the gap.
    g.gyro({0.0f, 1.0f, 0.0f}, ns + 1'000'000'000ull);
    g.take(yaw, pitch);
    CHECK(yaw == 0.0f);
}

void test_calibrates_when_still() {
    GyroAim g;
    GyroOptions o = on();
    o.auto_calibrate = true;
    o.space = GyroSpace::Yaw;
    g.set_options(o);
    // Lying on the desk with a drift of 0.03 rad/s on y, and a little noise.
    std::uint64_t ns = 0;
    for (int i = 0; i < 500; ++i) {
        const float noise = (i % 2 == 0 ? 1.0f : -1.0f) * 0.004f;
        ns += kStepNs;
        g.gyro({noise, 0.03f + noise, -noise}, ns);
    }
    CHECK(g.calibrated());
    CHECK(near(g.offset().y, 0.03f, 0.002f));
    float yaw = 0.0f;
    float pitch = 0.0f;
    g.take(yaw, pitch);
    // Still drifting: now it barely turns.
    feed(g, {0.0f, 0.03f, 0.0f}, 250, ns);
    g.take(yaw, pitch);
    CHECK(std::fabs(yaw) < 0.002f);
}

void test_shake_or_turn_is_not_still() {
    GyroAim g;
    GyroOptions o = on();
    o.auto_calibrate = true;
    g.set_options(o);
    // A slow but steady turn (0.2 rad/s, over kMaxOffset) is not learnt as drift.
    feed(g, {0.0f, 0.2f, 0.0f}, 1000, 0);
    CHECK(!g.calibrated());
    // Nor is a hand's shake.
    GyroAim h;
    h.set_options(o);
    std::uint64_t ns = 0;
    for (int i = 0; i < 1000; ++i) {
        ns += kStepNs;
        h.gyro({0.0f, (i % 10 < 5) ? 0.05f : -0.05f, 0.0f}, ns);
    }
    CHECK(!h.calibrated());
}

void test_spaces() {
    float yaw = 0.0f;
    float pitch = 0.0f;
    // Roll: leaning the controller left (counter-clockwise about z) turns left.
    GyroAim roll;
    GyroOptions o = on();
    o.space = GyroSpace::Roll;
    roll.set_options(o);
    feed(roll, {0.0f, 0.0f, 1.0f}, 101, 0);
    roll.take(yaw, pitch);
    CHECK(near(yaw, 0.4f, 0.01f));

    // Player space, the controller stood on its end (up is its z): its z twist is the turn.
    GyroAim player;
    o.space = GyroSpace::Player;
    player.set_options(o);
    player.accel({0.0f, 0.0f, 9.8f});
    feed(player, {0.0f, 0.0f, 1.0f}, 101, 0);
    player.take(yaw, pitch);
    CHECK(near(yaw, 0.4f, 0.01f));
    // Held flat, the same twist is a roll, not a turn.
    GyroAim flat;
    flat.set_options(o);
    flat.accel({0.0f, 9.8f, 0.0f});
    feed(flat, {0.0f, 0.0f, 1.0f}, 101, 0);
    flat.take(yaw, pitch);
    CHECK(near(yaw, 0.0f));
    // Held at 45 degrees, the relaxed turn still gives (nearly) the whole of it.
    GyroAim tilted;
    tilted.set_options(o);
    tilted.accel({0.0f, 6.93f, 6.93f});
    feed(tilted, {0.0f, 0.7071f, 0.7071f}, 101, 0);
    tilted.take(yaw, pitch);
    CHECK(near(yaw, 0.4f, 0.01f));
    // Added for the port: held at 45 degrees and twisted about its own y only, the part about the
    // way up (0.71 of it) is relaxed by 1.41 to nearly all of it, not left at 0.71.
    GyroAim twist;
    twist.set_options(o);
    twist.accel({0.0f, 6.93f, 6.93f});
    feed(twist, {0.0f, 1.0f, 0.0f}, 101, 0);
    twist.take(yaw, pitch);
    CHECK(near(yaw, 0.4f * 0.7071f * 1.41f, 0.01f));
    // No accelerometer: as Yaw.
    GyroAim none;
    none.set_options(o);
    feed(none, {0.0f, 1.0f, 0.0f}, 101, 0);
    none.take(yaw, pitch);
    CHECK(near(yaw, 0.4f, 0.01f));
}

void test_button() {
    float yaw = 0.0f;
    float pitch = 0.0f;
    GyroOptions o = on();
    o.button = Button::Misc1;  // any button
    o.button_action = GyroButtonAction::Enable;
    GyroAim g;
    g.set_options(o);
    g.button(false);
    feed(g, {0.0f, 1.0f, 0.0f}, 101, 0);
    g.take(yaw, pitch);
    CHECK(yaw == 0.0f);
    g.button(true);
    feed(g, {0.0f, 1.0f, 0.0f}, 101, 1'000'000'000ull);
    g.take(yaw, pitch);
    CHECK(yaw > 0.3f);

    // Pause: off while held.
    o.button_action = GyroButtonAction::Pause;
    g.set_options(o);
    g.button(true);
    feed(g, {0.0f, 1.0f, 0.0f}, 101, 2'000'000'000ull);
    g.take(yaw, pitch);
    CHECK(yaw == 0.0f);

    // Toggle: each press flips it.
    o.button_action = GyroButtonAction::Toggle;
    GyroAim t;
    t.set_options(o);
    t.button(false);
    CHECK(!t.aiming());
    t.button(true);
    CHECK(t.aiming());
    t.button(true);
    CHECK(t.aiming());
    t.button(false);
    t.button(true);
    CHECK(!t.aiming());

    // Off in the options: nothing.
    GyroAim off;
    off.set_options(GyroOptions{});
    feed(off, {0.0f, 1.0f, 0.0f}, 101, 0);
    off.take(yaw, pitch);
    CHECK(yaw == 0.0f);
}

void test_dead_zone_and_smoothing() {
    float yaw = 0.0f;
    float pitch = 0.0f;
    GyroOptions o = on();
    o.space = GyroSpace::Yaw;
    o.dead_zone = 0.05f;
    GyroAim g;
    g.set_options(o);
    feed(g, {0.0f, 0.04f, 0.0f}, 101, 0);
    g.take(yaw, pitch);
    CHECK(yaw == 0.0f);
    feed(g, {0.0f, 1.05f, 0.0f}, 101, 1'000'000'000ull);
    g.take(yaw, pitch);
    CHECK(near(yaw, 0.4f, 0.01f));

    // Smoothing: a fast turn goes straight through, a slow one comes out the same in the end.
    o.dead_zone = 0.0f;
    o.smoothing = 0.2f;
    GyroAim s;
    s.set_options(o);
    feed(s, {0.0f, 1.0f, 0.0f}, 101, 0);
    s.take(yaw, pitch);
    CHECK(near(yaw, 0.4f, 0.01f));
    feed(s, {0.0f, 0.05f, 0.0f}, 501, 1'000'000'000ull);
    s.take(yaw, pitch);
    CHECK(near(yaw, 0.1f, 0.01f));
}

void test_parse() {
    CHECK(gyro::gyro_space_from("roll") == GyroSpace::Roll);
    CHECK(gyro::gyro_space_from("yaw") == GyroSpace::Yaw);
    CHECK(gyro::gyro_space_from("local") == GyroSpace::Local);
    CHECK(gyro::gyro_space_from("") == GyroSpace::Player);
    CHECK(gyro::gyro_button_action_from("pause") == GyroButtonAction::Pause);
    CHECK(gyro::gyro_button_action_from("x") == GyroButtonAction::Enable);
    // Added for the port: null, toggle, and the gyro button's names (OpenRAC's gyro_button_of).
    CHECK(gyro::gyro_space_from(nullptr) == GyroSpace::Player);
    CHECK(gyro::gyro_button_action_from("toggle") == GyroButtonAction::Toggle);
    CHECK(gyro::gyro_button_action_from(nullptr) == GyroButtonAction::Enable);
    CHECK(gyro::gyro_button_of("touchpad") == Button::Touchpad);
    CHECK(gyro::gyro_button_of("rightstick") == Button::RightStick);
    CHECK(gyro::gyro_button_of("leftstick") == Button::LeftStick);
    CHECK(gyro::gyro_button_of("rightshoulder") == Button::RightShoulder);
    CHECK(gyro::gyro_button_of("leftshoulder") == Button::LeftShoulder);
    CHECK(gyro::gyro_button_of("paddle1") == Button::Paddle1);
    CHECK(gyro::gyro_button_of("paddle4") == Button::Paddle4);
    CHECK(gyro::gyro_button_of("misc1") == Button::Misc1);
    CHECK(gyro::gyro_button_of("none") == Button::None);
    CHECK(gyro::gyro_button_of("") == Button::None);
    CHECK(gyro::button_bit(Button::None) == 0);
}

// What a driver or a player may throw at it.
void test_edges() {
    float yaw = 0.0f;
    float pitch = 0.0f;
    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();

    // A NaN or infinite sample is dropped: no turn, and the offset and smoothing stay clean.
    GyroOptions o = on();
    o.space = GyroSpace::Player;
    o.smoothing = 0.2f;
    o.auto_calibrate = true;
    GyroAim g;
    g.set_options(o);
    g.accel({nan, 9.8f, 0.0f});
    CHECK(!g.has_gravity());
    std::uint64_t ns = feed(g, {nan, 1.0f, 0.0f}, 50, 0);
    ns = feed(g, {0.0f, inf, 0.0f}, 50, ns);
    g.take(yaw, pitch);
    CHECK(yaw == 0.0f && pitch == 0.0f);
    feed(g, {0.0f, 1.0f, 0.0f}, 101, ns);
    g.take(yaw, pitch);
    CHECK(std::isfinite(yaw) && near(yaw, 0.4f, 0.02f));

    // The clock going back, or standing still, is no motion.
    GyroAim back;
    back.set_options(on());
    back.gyro({0.0f, 1.0f, 0.0f}, 1'000'000'000ull);
    back.gyro({0.0f, 1.0f, 0.0f}, 500'000'000ull);
    back.gyro({0.0f, 1.0f, 0.0f}, 500'000'000ull);
    back.take(yaw, pitch);
    CHECK(yaw == 0.0f);

    // A sensitivity below zero (a hand-edited settings file) turns nothing, never backwards.
    GyroOptions neg = on();
    neg.sensitivity_x = -3.0f;
    neg.sensitivity_y = -3.0f;
    GyroAim n;
    n.set_options(neg);
    feed(n, {1.0f, 1.0f, 0.0f}, 101, 0);
    n.take(yaw, pitch);
    CHECK(yaw == 0.0f && pitch == 0.0f);

    // Invert horizontal: left turns right.
    GyroOptions inv = on();
    inv.space = GyroSpace::Yaw;
    inv.invert_x = true;
    GyroAim i;
    i.set_options(inv);
    feed(i, {0.0f, 1.0f, 0.0f}, 101, 0);
    i.take(yaw, pitch);
    CHECK(near(yaw, -0.4f, 0.01f));

    // Free fall (a gravity near zero): player space falls back to yaw, no division by zero.
    GyroOptions ps = on();
    ps.space = GyroSpace::Player;
    GyroAim fall;
    fall.set_options(ps);
    fall.accel({0.0f, 0.0f, 0.0f});
    feed(fall, {0.0f, 1.0f, 0.0f}, 101, 0);
    fall.take(yaw, pitch);
    CHECK(near(yaw, 0.4f, 0.01f));

    // Upside down (gravity along -y): turning left still turns left in player space? The way up is
    // the controller's -y, so its +y twist is a turn right: what the player sees.
    GyroAim flipped;
    flipped.set_options(ps);
    flipped.accel({0.0f, -9.8f, 0.0f});
    feed(flipped, {0.0f, 1.0f, 0.0f}, 101, 0);
    flipped.take(yaw, pitch);
    CHECK(near(yaw, -0.4f, 0.01f));

    // A dead zone exactly at the speed: nothing.
    GyroOptions dz = on();
    dz.space = GyroSpace::Yaw;
    dz.dead_zone = 0.5f;
    GyroAim d;
    d.set_options(dz);
    feed(d, {0.0f, 0.5f, 0.0f}, 101, 0);
    d.take(yaw, pitch);
    CHECK(yaw == 0.0f);

    // reset() keeps the options and forgets what it learnt.
    GyroOptions cal = on();
    cal.auto_calibrate = true;
    GyroAim r;
    r.set_options(cal);
    feed(r, {0.0f, 0.03f, 0.0f}, 500, 0);
    CHECK(r.calibrated());
    r.reset();
    CHECK(!r.calibrated());
    CHECK(r.options().auto_calibrate && r.options().enabled);

    // A second still spell moves the offset halfway to its mean, not all the way.
    GyroAim twice;
    twice.set_options(cal);
    ns = feed(twice, {0.0f, 0.02f, 0.0f}, 400, 0);
    CHECK(near(twice.offset().y, 0.02f, 0.001f));
    ns = feed(twice, {0.3f, 0.3f, 0.0f}, 5, ns);  // picked up: a new window starts
    feed(twice, {0.0f, 0.06f, 0.0f}, 400, ns);
    CHECK(near(twice.offset().y, 0.04f, 0.002f));

    // Turned off in the options mid-turn: nothing more, and back on it carries no stale motion.
    GyroAim off;
    GyroOptions sw = on();
    sw.smoothing = 0.5f;
    off.set_options(sw);
    ns = feed(off, {0.0f, 0.1f, 0.0f}, 100, 0);
    off.take(yaw, pitch);
    sw.enabled = false;
    off.set_options(sw);
    ns = feed(off, {0.0f, 0.1f, 0.0f}, 100, ns);
    off.take(yaw, pitch);
    CHECK(yaw == 0.0f);
    sw.enabled = true;
    off.set_options(sw);
    feed(off, {0.0f, 0.0f, 0.0f}, 2, ns);
    off.take(yaw, pitch);
    CHECK(std::fabs(yaw) < 1e-5f);

    // Toggle: a held button does not flip again on each sample; the button unset means always on.
    GyroOptions tg = on();
    tg.button = Button::Touchpad;
    tg.button_action = GyroButtonAction::Toggle;
    GyroAim t;
    t.set_options(tg);
    for (int k = 0; k < 10; ++k) {
        t.button(true);
    }
    CHECK(t.aiming());
    tg.button = Button::None;
    t.set_options(tg);
    t.button(false);
    CHECK(t.aiming());
}

void run() {
    test_turn_is_the_controllers();
    test_gap_is_not_motion();
    test_calibrates_when_still();
    test_shake_or_turn_is_not_still();
    test_spaces();
    test_button();
    test_dead_zone_and_smoothing();
    test_parse();
    test_edges();
}

}  // namespace aim

// ---- The controllers' reports (gyro_hid.hpp) ----
//
// Each report is built from the layout SDL's driver reads (named with each test), with a known
// rate in it: what comes out is that rate on SDL's axes, in radians a second.

namespace hid {

using gyro::Button;
using gyro::Family;
using gyro::JoyConSide;
using gyro::Link;
using gyro::Report;
using gyro::Stream;
using gyro::button_bit;
using gyro::parse_report;

// Within `eps` degrees a second (a rate), or m/s^2.
bool near_deg(float rad, float deg, float eps = 0.1f) { return std::fabs(rad / kRadiansPerDegree - deg) <= eps; }
bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

Link link_of(Family family) {
    Link l;
    l.family = family;
    return l;
}

// The reports' CRC (SDL_hidapi_ps4.c and SDL_hidapi_ps5.c, VerifyCRC): CRC-32 of 0xA1 then the
// report less its last four bytes, written into those.
void seal(std::uint8_t* r, int size) {
    const std::uint8_t header = 0xA1;
    put32(r, size - 4, gyro::detail::crc32(gyro::detail::crc32(0, &header, 1), r, size - 4));
}

void test_crc() {
    // The CRC-32 check value: the bytes "123456789".
    const std::uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    check(gyro::detail::crc32(0, digits, 9) == 0xCBF43926u, "CRC-32 of 123456789");
}

void test_devices() {
    const struct {
        std::uint16_t vendor, product;
        Family family;
    } kWanted[] = {
        {0x054C, 0x0CE6, Family::DualSense},  {0x054C, 0x0DF2, Family::DualSense},
        {0x054C, 0x05C4, Family::DualShock4}, {0x054C, 0x09CC, Family::DualShock4},
        {0x054C, 0x0BA0, Family::DualShock4}, {0x057E, 0x2009, Family::SwitchPro},
        {0x057E, 0x2006, Family::JoyCon},     {0x057E, 0x2007, Family::JoyCon},
        {0x057E, 0x200E, Family::JoyCon},     {0x28DE, 0x1205, Family::SteamDeck},
    };
    for (const auto& w : kWanted) {
        const gyro::Device* d = gyro::find_device(w.vendor, w.product);
        check(d != nullptr && d->family == w.family && d->name != nullptr, "a wanted controller is in the table");
    }
    check(gyro::find_device(0x045E, 0x0B12) == nullptr, "an Xbox pad (no gyro) is not");
    check(gyro::find_device(0x28DE, 0x1102) == nullptr, "the 2015 Steam Controller (needs writes) is not");

    check(gyro::link_for(*gyro::find_device(0x054C, 0x0DF2)).edge, "the Edge's link maps its paddles");
    check(!gyro::link_for(*gyro::find_device(0x054C, 0x0CE6)).edge, "the DualSense's does not");
    check(gyro::link_for(*gyro::find_device(0x057E, 0x2006)).side == JoyConSide::Left, "Joy-Con (L)");
    check(gyro::link_for(*gyro::find_device(0x057E, 0x2007)).side == JoyConSide::Right, "Joy-Con (R)");
    const gyro::Device& grip = *gyro::find_device(0x057E, 0x200E);
    check(gyro::link_for(grip, 1, true).side == JoyConSide::Left && gyro::link_for(grip, 1, true).paired,
          "the grip's interface 1 is the left Joy-Con");
    check(gyro::link_for(grip, 0, true).side == JoyConSide::Right, "its other interface the right");
    check(gyro::link_for(*gyro::find_device(0x0F0D, 0x0196)).wireless, "the HORIPAD over Bluetooth");
}

// SDL_hidapi_ps5.c: USB report 0x01 (64 bytes), the state packet from byte 1: buttons from
// packet byte 7, gyro from 15, accelerometer from 21, the clock (a third of a microsecond) at 27.
std::array<std::uint8_t, 64> dualsense_usb(int gx, int gy, int gz, std::uint32_t tick) {
    std::array<std::uint8_t, 64> r{};
    r[0] = 0x01;
    put16(r.data(), 1 + 15, gx);
    put16(r.data(), 1 + 17, gy);
    put16(r.data(), 1 + 19, gz);
    put16(r.data(), 1 + 21, 0);
    put16(r.data(), 1 + 23, 8192);  // 1 g up
    put16(r.data(), 1 + 25, 0);
    put32(r.data(), 1 + 27, tick);
    return r;
}

void test_dualsense() {
    Link link = link_of(Family::DualSense);
    Stream stream;
    // 16 counts a degree a second without calibration (64 / 1024): pitch -45, yaw 90, roll 10.
    auto r = dualsense_usb(-720, 1440, 160, 30000);
    r[1 + 8] = 0x40;  // L3
    r[1 + 9] = 0x06;  // the touchpad and the microphone
    Report out = parse_report(link, stream, r.data(), 64);
    check(out.ok && out.count == 1, "DualSense USB: a sample");
    const gyro::Sample& s = out.samples[0];
    check(near_deg(s.gyro.x, -45.0f) && near_deg(s.gyro.y, 90.0f) && near_deg(s.gyro.z, 10.0f), "DualSense USB: the rates");
    check(near(s.accel.y, 9.80665f) && near(s.accel.x, 0.0f), "DualSense USB: 1 g up");
    check(s.ns == 10'000'000, "DualSense USB: the clock in thirds of a microsecond");
    check(s.held(Button::LeftStick) && s.held(Button::Touchpad) && s.held(Button::Misc1) && !s.held(Button::RightStick),
          "DualSense USB: L3, the touchpad and the microphone");
    // 4 ms later; then the clock wraps at 32 bits and goes on.
    r = dualsense_usb(0, 0, 0, 30000 + 12000);
    out = parse_report(link, stream, r.data(), 64);
    check(out.samples[0].ns == 14'000'000, "DualSense USB: the next report 4 ms on");
    r = dualsense_usb(0, 0, 0, 0xFFFFFFF0u);
    parse_report(link, stream, r.data(), 64);
    const std::uint64_t before = stream.ticks;
    r = dualsense_usb(0, 0, 0, 0x00000020u);
    out = parse_report(link, stream, r.data(), 64);
    check(stream.ticks == before + 0x30 && out.samples[0].ns == (before + 0x30) * 1000 / 3, "DualSense: the clock wraps");

    // The simple report (10 bytes, Bluetooth before SDL's set-up) has no motion.
    check(!parse_report(link, stream, r.data(), 10).ok, "DualSense: the simple report");

    // Bluetooth: report 0x31 (78 bytes), the packet one byte later, its CRC checked.
    std::array<std::uint8_t, 78> bt{};
    bt[0] = 0x31;
    put16(bt.data(), 2 + 15, 0);
    put16(bt.data(), 2 + 17, -1440);  // yaw -90
    put16(bt.data(), 2 + 19, 0);
    put32(bt.data(), 2 + 27, 3000);
    seal(bt.data(), 78);
    Stream bts;
    out = parse_report(link, bts, bt.data(), 78);
    check(out.ok && out.count == 1 && near_deg(out.samples[0].gyro.y, -90.0f), "DualSense Bluetooth: the rate");
    check(out.samples[0].ns == 1'000'000, "DualSense Bluetooth: the clock");
    bt[20] ^= 0x01;
    check(!parse_report(link, bts, bt.data(), 78).ok, "DualSense Bluetooth: a bad CRC is dropped");

    // The Edge: its paddles and Fn buttons are paddle1 to paddle4 (the plain DualSense maps none).
    r = dualsense_usb(0, 0, 0, 0);
    r[1 + 9] = 0x80 | 0x10;  // the right paddle, the left Fn
    Link edge = link_of(Family::DualSense);
    edge.edge = true;
    Stream es;
    out = parse_report(edge, es, r.data(), 64);
    check(out.samples[0].held(Button::Paddle1) && out.samples[0].held(Button::Paddle4) && !out.samples[0].held(Button::Paddle2),
          "DualSense Edge: the right paddle and the left Fn");
    Stream ps;
    check(parse_report(link, ps, r.data(), 64).buttons == 0, "DualSense: no paddles");
}

// SDL_hidapi_ps5.c, HIDAPI_DriverPS5_LoadCalibrationData: feature report 0x05.
void test_dualsense_calibration() {
    std::array<std::uint8_t, 41> f{};
    f[0] = 0x05;
    put16(f.data(), 1, 12);   // pitch bias
    put16(f.data(), 3, -6);   // yaw bias
    put16(f.data(), 5, 3);    // roll bias
    for (int axis = 0; axis < 3; ++axis) {
        put16(f.data(), 7 + 4 * axis, 8378);    // plus
        put16(f.data(), 9 + 4 * axis, -8378);   // minus
    }
    put16(f.data(), 19, 540);  // the speed's halves: 1080 * 1024 / 16756 = 66.0 counts a degree
    put16(f.data(), 21, 540);
    for (int axis = 0; axis < 3; ++axis) {
        put16(f.data(), 23 + 4 * axis, 8192 + 20);   // at +1 g
        put16(f.data(), 25 + 4 * axis, -8192 + 20);  // at -1 g
    }
    const auto c = gyro::dualsense_calibration(f.data(), 41);
    check(c.has_value(), "DualSense calibration: read");
    if (!c) return;
    Link link = link_of(Family::DualSense);
    link.calibration = c;
    // Each axis at its bias but the yaw, 1560 counts over it: 1560 * 66.0 / 1024 degrees a second.
    auto r = dualsense_usb(12, -6 + 1560, 3, 0);
    put16(r.data(), 1 + 23, 8192 + 20);
    Stream stream;
    const Report out = parse_report(link, stream, r.data(), 64);
    const float yaw = 1560.0f * (1080.0f * 1024.0f / 16756.0f) / 1024.0f;
    check(near_deg(out.samples[0].gyro.y, yaw, 0.01f) && near_deg(out.samples[0].gyro.x, 0.0f, 0.01f),
          "DualSense calibration: the bias off, the sensitivity on");
    check(near(out.samples[0].accel.y, 9.80665f), "DualSense calibration: 1 g");

    check(!gyro::dualsense_calibration(f.data(), 34), "DualSense calibration: short");
    put16(f.data(), 3, 2000);
    check(!gyro::dualsense_calibration(f.data(), 41), "DualSense calibration: a bias SDL refuses");
    put16(f.data(), 3, -6);
    put16(f.data(), 19, 1500);
    check(!gyro::dualsense_calibration(f.data(), 41), "DualSense calibration: a sensitivity SDL refuses");
}

// SDL_hidapi_ps4.c: USB report 0x01 (64 bytes), the packet from byte 1: buttons from packet byte
// 4, the clock (16/3 of a microsecond) at 9, gyro from 12, accelerometer from 18.
std::array<std::uint8_t, 64> dualshock4_usb(int gx, int gy, int gz, int tick) {
    std::array<std::uint8_t, 64> r{};
    r[0] = 0x01;
    put16(r.data(), 1 + 9, tick);
    put16(r.data(), 1 + 12, gx);
    put16(r.data(), 1 + 14, gy);
    put16(r.data(), 1 + 16, gz);
    put16(r.data(), 1 + 20, 8192);  // 1 g up
    return r;
}

void test_dualshock4() {
    Link link = link_of(Family::DualShock4);
    Stream stream;
    // 1/16 of a degree a second a count: pitch 30, yaw -90, roll 0.
    auto r = dualshock4_usb(480, -1440, 0, 750);
    r[1 + 5] = 0x02 | 0x80;  // R1, R3
    r[1 + 6] = 0x02;         // the touchpad
    Report out = parse_report(link, stream, r.data(), 64);
    check(out.ok && out.count == 1, "DS4 USB: a sample");
    const gyro::Sample& s = out.samples[0];
    check(near_deg(s.gyro.x, 30.0f) && near_deg(s.gyro.y, -90.0f) && near_deg(s.gyro.z, 0.0f), "DS4 USB: the rates");
    check(near(s.accel.y, 9.80665f), "DS4 USB: 1 g up");
    check(s.ns == 4'000'000, "DS4 USB: the clock in 16/3 of a microsecond");
    check(s.held(Button::RightShoulder) && s.held(Button::RightStick) && s.held(Button::Touchpad) && !s.held(Button::LeftShoulder),
          "DS4 USB: R1, R3, the touchpad");
    // The 16-bit clock wraps: 65000 then 200 is 736 ticks on.
    r = dualshock4_usb(0, 0, 0, 65000);
    parse_report(link, stream, r.data(), 64);
    const std::uint64_t before = stream.ticks;
    r = dualshock4_usb(0, 0, 0, 200);
    out = parse_report(link, stream, r.data(), 64);
    check(stream.ticks == before + 736 && out.samples[0].ns == (before + 736) * 16000 / 3, "DS4: the clock wraps");
    // Sony's adapter with no pad: byte 31's 0x04.
    r[31] = 0x04;
    check(!parse_report(link, stream, r.data(), 64).ok, "DS4 adapter: no pad");
    check(!parse_report(link, stream, r.data(), 10).ok, "DS4: the simple report");

    // Bluetooth: reports 0x11 to 0x19 (78 bytes), the packet three bytes in, 0x80 in byte 1.
    std::array<std::uint8_t, 78> bt{};
    bt[0] = 0x11;
    bt[1] = 0xC0;
    put16(bt.data(), 3 + 9, 1500);       // 8 ms
    put16(bt.data(), 3 + 12 + 4, 800);   // roll 50
    seal(bt.data(), 78);
    Stream bts;
    out = parse_report(link, bts, bt.data(), 78);
    check(out.ok && near_deg(out.samples[0].gyro.z, 50.0f) && out.samples[0].ns == 8'000'000, "DS4 Bluetooth: the rate");
    bt[1] = 0x40;
    check(!parse_report(link, bts, bt.data(), 78).ok, "DS4 Bluetooth: no HID data in it");
    bt[1] = 0xC0;

    // SDL forgives a bad CRC until good ones stand at three or more after it.
    Stream fresh;
    std::array<std::uint8_t, 78> bad = bt;
    bad[40] ^= 0xFF;
    check(parse_report(link, fresh, bad.data(), 78).ok, "DS4 Bluetooth: a bad CRC with no good ones yet");
    for (int i = 0; i < 4; ++i) parse_report(link, fresh, bt.data(), 78);
    check(!parse_report(link, fresh, bad.data(), 78).ok, "DS4 Bluetooth: a bad CRC after good ones");
}

// SDL_hidapi_ps4.c, HIDAPI_DriverPS4_LoadOfficialCalibrationData: the two layouts.
void test_dualshock4_calibration() {
    const auto make = [](bool bluetooth) {
        std::array<std::uint8_t, 37> f{};
        f[0] = bluetooth ? 0x05 : 0x02;
        put16(f.data(), 1, 0);
        put16(f.data(), 3, 8);  // yaw bias
        put16(f.data(), 5, 0);
        const int plus = 8800, minus = -8800;  // 1080 * 16 / 17600 = 0.98 of the nominal
        for (int axis = 0; axis < 3; ++axis) {
            const int bias = axis == 1 ? 8 : 0;
            if (bluetooth) {
                put16(f.data(), 7 + 2 * axis, plus + bias);
                put16(f.data(), 13 + 2 * axis, minus + bias);
            } else {
                put16(f.data(), 7 + 4 * axis, plus + bias);
                put16(f.data(), 9 + 4 * axis, minus + bias);
            }
        }
        put16(f.data(), 19, 540);
        put16(f.data(), 21, 540);
        for (int axis = 0; axis < 3; ++axis) {
            put16(f.data(), 23 + 4 * axis, 8192);
            put16(f.data(), 25 + 4 * axis, -8192);
        }
        return f;
    };
    const auto usb = make(false), bt = make(true);
    const auto cu = gyro::dualshock4_calibration(usb.data(), 37, false);
    const auto cb = gyro::dualshock4_calibration(bt.data(), 37, true);
    check(cu && cb, "DS4 calibration: both layouts read");
    if (!cu || !cb) return;
    check(cu->bias[1] == 8.0f && near(cu->scale[1], cb->scale[1], 1e-9f), "DS4 calibration: the layouts agree");
    Link link = link_of(Family::DualShock4);
    link.calibration = cu;
    Stream stream;
    auto r = dualshock4_usb(0, 8 + 1600, 0, 0);
    const Report out = parse_report(link, stream, r.data(), 64);
    check(near_deg(out.samples[0].gyro.y, 1600.0f * (1080.0f * 16.0f / 17600.0f) / 16.0f, 0.01f), "DS4 calibration: applied");
    check(!gyro::dualshock4_calibration(std::array<std::uint8_t, 37>{}.data(), 37, false), "DS4 calibration: all zero");
    check(!gyro::dualshock4_calibration(usb.data(), 30, false), "DS4 calibration: short");
}

// SDL_hidapi_switch.c, HandleFullControllerState: report 0x30, the buttons at bytes 3 to 5, three
// IMU samples of 12 bytes from byte 13 (accelerometer x, y, z, gyro x, y, z), newest first.
constexpr float kSwitchCounts = 14.2842f;  // a degree a second

int counts(float deg) { return static_cast<int>(std::lround(deg * kSwitchCounts)); }

std::array<std::uint8_t, 64> switch_full(const int (&gyro)[3][3], int accel_z = 4096) {
    std::array<std::uint8_t, 64> r{};
    r[0] = 0x30;
    for (int k = 0; k < 3; ++k) {
        const int at = 13 + 12 * k;
        put16(r.data(), at + 4, accel_z);
        put16(r.data(), at + 6, gyro[k][0]);
        put16(r.data(), at + 8, gyro[k][1]);
        put16(r.data(), at + 10, gyro[k][2]);
    }
    return r;
}

void test_switch_pro() {
    Link link = link_of(Family::SwitchPro);
    Stream stream;
    // The report's x, y, z go to SDL's -y... x = -y, y = z, z = -x: a turn left (SDL y) is the
    // report's z. Three samples, newest first: yaw 30, 20, 10 degrees a second.
    const int g[3][3] = {{0, 0, counts(30)}, {0, 0, counts(20)}, {counts(40), counts(-50), counts(10)}};
    auto r = switch_full(g);
    r[3] = 0x40;  // R
    r[4] = 0x20;  // the capture button
    Report out = parse_report(link, stream, r.data(), 64, 1'000'000'000);
    check(out.ok && out.count == 3 && out.primary, "Switch Pro: three samples");
    check(near_deg(out.samples[0].gyro.y, 10.0f) && near_deg(out.samples[1].gyro.y, 20.0f) && near_deg(out.samples[2].gyro.y, 30.0f),
          "Switch Pro: the oldest (the report's last) first");
    check(near_deg(out.samples[0].gyro.x, 50.0f) && near_deg(out.samples[0].gyro.z, -40.0f), "Switch Pro: x = -y, z = -x");
    check(near(out.samples[0].accel.y, 9.80665f), "Switch Pro: 1 g up (the report's z)");
    check(out.samples[0].ns == 5'000'000 && out.samples[1].ns == 10'000'000 && out.samples[2].ns == 15'000'000,
          "Switch Pro: SDL's first step, 5 ms");
    check(out.samples[0].held(Button::RightShoulder) && out.samples[0].held(Button::Misc1), "Switch Pro: R and capture");

    // The IMU off: the report is read, no samples.
    const int zero[3][3] = {};
    r = switch_full(zero, 0);
    out = parse_report(link, stream, r.data(), 64, 1'000'000'000);
    check(out.ok && out.count == 0, "Switch Pro: the IMU off");
    // Not a full report: a subcommand reply, the simple report.
    r[0] = 0x21;
    check(!parse_report(link, stream, r.data(), 64).ok, "Switch Pro: a subcommand reply");
    r[0] = 0x3F;
    check(!parse_report(link, stream, r.data(), 64).ok, "Switch Pro: the simple report");

    // A report every 8 ms: after 1000 samples SDL measures the step, the time since over the
    // samples since (the first report opening the window).
    Stream usb;
    r = switch_full(g);
    std::uint64_t now = 2'000'000'000;
    for (int i = 0; i < 334; ++i, now += 8'000'000) parse_report(link, usb, r.data(), 64, now);
    check(usb.step_ns == 333ull * 8'000'000 / 1002, "Switch Pro: the step measured");
    const std::uint64_t at = usb.ns;
    out = parse_report(link, usb, r.data(), 64, now);
    check(out.samples[0].ns == at + usb.step_ns && out.samples[2].ns == at + 3 * usb.step_ns, "Switch Pro: the measured step");
    // No host clock given: SDL's 5 ms stays.
    Stream none;
    for (int i = 0; i < 400; ++i) parse_report(link, none, r.data(), 64);
    check(none.step_ns == 5'000'000, "Switch Pro: no clock, 5 ms");
}

void test_joycons() {
    // The report's own axes (x, y, z) as 10, 20, 30 degrees a second, in every sample.
    const int g[3][3] = {{counts(10), counts(20), counts(30)}, {counts(10), counts(20), counts(30)},
                         {counts(10), counts(20), counts(30)}};
    const auto r = switch_full(g);
    const auto axes = [&](JoyConSide side, bool paired, float x, float y, float z, const char* what) {
        Link link = link_of(Family::JoyCon);
        link.side = side;
        link.paired = paired;
        Stream stream;
        const Report out = parse_report(link, stream, r.data(), 64);
        const gyro::Sample& s = out.samples[0];
        check(out.ok && out.count == 3 && near_deg(s.gyro.x, x) && near_deg(s.gyro.y, y) && near_deg(s.gyro.z, z), what);
        return out;
    };
    // In a pair (upright): the left as a Pro Controller, the right with x and y flipped.
    const Report left_pair = axes(JoyConSide::Left, true, -20, 30, -10, "Joy-Con (L) in a pair");
    const Report right_pair = axes(JoyConSide::Right, true, 20, -30, -10, "Joy-Con (R) in a pair");
    check(!left_pair.primary && right_pair.primary, "a pair aims with the right Joy-Con's gyro");
    // Alone (sideways): turned a quarter.
    axes(JoyConSide::Left, false, -10, 30, 20, "Joy-Con (L) alone");
    axes(JoyConSide::Right, false, 10, -30, 20, "Joy-Con (R) alone");

    // Buttons: in a pair the right SR is paddle1; alone the left SL is the left shoulder.
    auto b = r;
    b[3] = 0x10;  // the right's SR
    b[5] = 0x20;  // the left's SL
    Link link = link_of(Family::JoyCon);
    link.side = JoyConSide::Right;
    link.paired = true;
    Stream s1;
    check(parse_report(link, s1, b.data(), 64).buttons == button_bit(Button::Paddle1), "Joy-Con (R) in a pair: SR");
    link.side = JoyConSide::Left;
    link.paired = false;
    Stream s2;
    check(parse_report(link, s2, b.data(), 64).buttons == button_bit(Button::LeftShoulder), "Joy-Con (L) alone: SL");
    link.paired = true;
    Stream s3;
    check(parse_report(link, s3, b.data(), 64).buttons == button_bit(Button::Paddle2), "Joy-Con (L) in a pair: SL");
}

// SDL_hidapi_steamdeck.c and steam/controller_structs.h: 64 bytes, the header (version 1, type 9,
// length 64), the buttons from byte 8, the accelerometer from 24, the gyro from 30; +-2000
// degrees a second and +-2 g over the int16's range.
std::array<std::uint8_t, 64> deck(int gx, int gy, int gz) {
    std::array<std::uint8_t, 64> r{};
    put16(r.data(), 0, 1);
    r[2] = 9;
    r[3] = 64;
    put16(r.data(), 28, 16384);  // the accelerometer's z: 1 g, SDL's y
    put16(r.data(), 30, gx);
    put16(r.data(), 32, gy);
    put16(r.data(), 34, gz);
    return r;
}

int deck_counts(float deg) { return static_cast<int>(std::lround(deg / 2000.0f * 32768.0f)); }

void test_steam_deck() {
    Link link = link_of(Family::SteamDeck);
    Stream stream;
    // SDL's x is the report's x, y its z, z its -y: pitch 20, yaw 90, roll 45.
    auto r = deck(deck_counts(20), deck_counts(-45), deck_counts(90));
    put32(r.data(), 8, 0x00020000u | 0x00000008u);  // the left trackpad's click, L1
    put32(r.data(), 12, 0x00040000u | 0x00000200u); // the quick access button, L4
    Report out = parse_report(link, stream, r.data(), 64);
    check(out.ok && out.count == 1, "Steam Deck: a sample");
    const gyro::Sample& s = out.samples[0];
    check(near_deg(s.gyro.x, 20.0f) && near_deg(s.gyro.y, 90.0f) && near_deg(s.gyro.z, 45.0f), "Steam Deck: the rates");
    check(near(s.accel.y, 9.80665f), "Steam Deck: 1 g up (the report's z)");
    check(s.ns == 4'000'000, "Steam Deck: SDL's 4 ms a report");
    check(s.held(Button::Touchpad) && s.held(Button::LeftShoulder) && s.held(Button::Misc1) && s.held(Button::Paddle2)
              && !s.held(Button::Paddle1),
          "Steam Deck: the left trackpad, L1, the quick access button, L4");
    out = parse_report(link, stream, r.data(), 64);
    check(out.samples[0].ns == 8'000'000, "Steam Deck: the next report 4 ms on");
    r[2] = 1;  // another Valve report (a Steam Controller's state)
    check(!parse_report(link, stream, r.data(), 64).ok, "Steam Deck: another report type");
    r[2] = 9;
    check(!parse_report(link, stream, r.data(), 63).ok, "Steam Deck: a short report");
}

// SDL_hidapi_steam_hori.c: 0x07 (USB) or 0x00 (Bluetooth), the gyro's int16s from byte 12 mapped
// onto +-2048 degrees a second negated (y 12, z 14, x 16), the accelerometer at 8 g (z 18, -y 20,
// x 22); SDL's own clock, 4 ms a report over USB and 8.333 ms over Bluetooth.
void test_hori_steam() {
    Link link = link_of(Family::HoriSteam);
    Stream stream;
    std::array<std::uint8_t, 64> r{};
    r[0] = 0x07;
    put16(r.data(), 12, -1600);  // yaw about +100
    put16(r.data(), 16, 800);    // pitch about -50
    put16(r.data(), 18, 4096);   // 1 g on z
    r[7] = 0x80 | 0x04;          // FL (paddle1), R3
    Report out = parse_report(link, stream, r.data(), 64);
    check(out.ok && out.count == 1, "HORIPAD: a sample");
    const gyro::Sample& s = out.samples[0];
    check(near_deg(s.gyro.y, 100.0f, 0.2f) && near_deg(s.gyro.x, -50.0f, 0.2f) && near_deg(s.gyro.z, 0.0f, 0.1f),
          "HORIPAD: the rates");
    check(near(s.accel.z, 9.80665f), "HORIPAD: 1 g");
    check(s.ns == 4'000'000 && s.held(Button::Paddle1) && s.held(Button::RightStick), "HORIPAD: USB step, FL, R3");
    Link bt = link;
    bt.wireless = true;
    Stream bs;
    r[0] = 0x00;
    check(parse_report(bt, bs, r.data(), 64).samples[0].ns == 8'333'000, "HORIPAD Bluetooth: 8.333 ms");
    r[0] = 0x05;
    check(!parse_report(link, stream, r.data(), 64).ok, "HORIPAD: another report");
}

// The whole way: a DualSense turned left at 90 degrees a second for a second, through OpenRAC's
// filter at sensitivity 1, turns the camera a quarter turn left.
void test_through_the_filter() {
    Link link = link_of(Family::DualSense);
    Stream stream;
    gyro::GyroAim aim;
    gyro::GyroOptions o;
    o.enabled = true;
    o.space = gyro::GyroSpace::Player;
    o.sensitivity_x = o.sensitivity_y = 1.0f;
    o.auto_calibrate = false;
    aim.set_options(o);
    for (int i = 0; i <= 250; ++i) {
        const auto r = dualsense_usb(0, 1440, 0, static_cast<std::uint32_t>(12000 * i));
        const Report out = parse_report(link, stream, r.data(), 64);
        for (int k = 0; k < out.count; ++k) {
            aim.button(out.samples[k].held(o.button));
            aim.gyro(out.samples[k].gyro, out.samples[k].ns);
            aim.accel(out.samples[k].accel);
        }
    }
    float yaw = 0.0f, pitch = 0.0f;
    aim.take(yaw, pitch);
    check(near(yaw, kPi / 2.0f, 0.01f) && near(pitch, 0.0f), "DualSense through GyroAim: a quarter turn");
}

void run() {
    test_crc();
    test_devices();
    test_dualsense();
    test_dualsense_calibration();
    test_dualshock4();
    test_dualshock4_calibration();
    test_switch_pro();
    test_joycons();
    test_steam_deck();
    test_hori_steam();
    test_through_the_filter();
}

}  // namespace hid

int main() {
    aim::run();
    hid::run();
    if (failures == 0) std::printf("all gyro tests passed\n");
    return failures == 0 ? 0 : 1;
}
