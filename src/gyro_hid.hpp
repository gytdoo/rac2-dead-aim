// SPDX-License-Identifier: GPL-3.0-or-later
// A controller's motion read from its raw HID reports (/dev/hidraw, opened read-only), the way
// SDL 3.4.18's HIDAPI drivers read them. OpenRAC links that SDL statically and its gyro is what
// these drivers make of the reports, so the same reports read the same way give the mod the
// samples OpenRAC's gyro gets. Free of the system so it can be tested.
//
// SDL is zlib-licensed and none of its code is here: the layouts, scales and axis mappings are
// rewritten from its drivers, each named where it is used (all under src/joystick/ in SDL's tree):
// hidapi/SDL_hidapi_ps5.c, hidapi/SDL_hidapi_ps4.c, hidapi/SDL_hidapi_switch.c,
// hidapi/SDL_hidapi_steamdeck.c with hidapi/steam/controller_structs.h, and
// hidapi/SDL_hidapi_steam_hori.c. The ids are from usb_ids.h and controller_list.h, and which
// button SDL names "touchpad", "paddle1" and so on is from SDL_gamepad.c
// (SDL_CreateMappingForHIDAPIGamepad).
//
// Each sample is what SDL sends (SDL_SendJoystickSensor): the gyro in radians a second and the
// accelerometer in m/s^2, both on SDL's axes (SDL_sensor.h: x right, y up, z toward the player,
// counter-clockwise positive), with the sensor timestamp SDL builds for that device. What a reader
// sees depends on SDL having set the controller up: while the player's OpenRAC gyro setting is on,
// OpenRAC turns the sensors on on player 1's pad, and SDL puts the pad in the mode that sends
// motion (the PlayStation pads' enhanced reports, the Switch pads' full reports with the IMU on).
// The Steam Deck and the HORIPAD for Steam always send it.
#pragma once

#include "gyro_aim.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace gyro {

// The drivers, each with its own report layout.
enum class Family : std::uint8_t {
    DualSense,   // SDL_hidapi_ps5.c, Sony's own pads (a third party's takes another layout)
    DualShock4,  // SDL_hidapi_ps4.c, Sony's own pads (a third party's scale is asked of it)
    SwitchPro,   // SDL_hidapi_switch.c, a Pro Controller or a pad that reads as one
    JoyCon,      // SDL_hidapi_switch.c, a Joy-Con, alone or in the charging grip
    SteamDeck,   // SDL_hidapi_steamdeck.c
    HoriSteam,   // SDL_hidapi_steam_hori.c, the Wireless HORIPAD for Steam
};

struct Device {
    std::uint16_t vendor;
    std::uint16_t product;
    Family family;
    const char* name;
};

// The controllers whose gyro SDL reads through these layouts, by the USB (or Bluetooth) ids their
// hidraw node gives (HID_ID in its uevent). Third-party PlayStation pads are not here: SDL asks
// each for its capabilities with a feature report, and reads a third party's DualSense reports by
// another layout. Many third-party Switch pads (8BitDo's in Switch mode among them) give 057E:2009
// and read as a Pro Controller.
inline constexpr Device kDevices[] = {
    {0x054C, 0x0CE6, Family::DualSense, "DualSense Wireless Controller"},
    {0x054C, 0x0DF2, Family::DualSense, "DualSense Edge Wireless Controller"},
    {0x054C, 0x0E5F, Family::DualSense, "Access Controller for PS5"},  // SDL reads it as a DualSense
    {0x054C, 0x05C4, Family::DualShock4, "PS4 Controller"},            // the first DualShock 4
    {0x054C, 0x09CC, Family::DualShock4, "PS4 Controller"},            // the second (PS4 Slim)
    {0x054C, 0x0BA0, Family::DualShock4, "PS4 Controller"},            // Sony's wireless adapter
    {0x057E, 0x2009, Family::SwitchPro, "Nintendo Switch Pro Controller"},
    {0x0E6F, 0x0186, Family::SwitchPro, "PDP Afterglow Wireless Switch Controller"},
    {0x0E6F, 0x018B, Family::SwitchPro, "PDP Afterglow Wave Wireless Controller for Switch"},
    {0x0F0D, 0x00F6, Family::SwitchPro, "HORI Wireless Switch Pad"},
    {0x057E, 0x2006, Family::JoyCon, "Nintendo Switch Joy-Con (L)"},
    {0x057E, 0x2007, Family::JoyCon, "Nintendo Switch Joy-Con (R)"},
    {0x057E, 0x200E, Family::JoyCon, "Nintendo Switch Joy-Con Charging Grip"},
    {0x28DE, 0x1205, Family::SteamDeck, "Steam Deck"},
    {0x0F0D, 0x01AB, Family::HoriSteam, "Wireless HORIPAD For Steam"},
    {0x0F0D, 0x0196, Family::HoriSteam, "Wireless HORIPAD For Steam"},  // over Bluetooth
};

inline const Device* find_device(std::uint16_t vendor, std::uint16_t product) {
    for (const Device& d : kDevices) {
        if (d.vendor == vendor && d.product == product) {
            return &d;
        }
    }
    return nullptr;
}

// A PlayStation pad's own calibration, per axis (the gyro's x, y, z, then the accelerometer's): a
// reading in SDL's units is (raw - bias) * scale. Without one the parser uses SDL's fallback
// scale, as SDL does when it cannot read the feature report or finds it bad.
struct ImuCalibration {
    float bias[6] = {0, 0, 0, 0, 0, 0};
    float scale[6] = {0, 0, 0, 0, 0, 0};
};

enum class JoyConSide : std::uint8_t { Left, Right };

// How one hidraw node's reports are read.
struct Link {
    Family family = Family::DualSense;
    // DualSense Edge: SDL maps its back paddles and Fn buttons (paddle1 to paddle4).
    bool edge = false;
    // Joy-Con: which one this node is, and whether SDL holds it in its combined pair (both
    // Joy-Cons connected; SDL pairs them by default). In a pair SDL reads the controller upright;
    // alone it reads it as a sideways mini gamepad, with its axes turned to match.
    JoyConSide side = JoyConSide::Right;
    bool paired = false;
    // HORIPAD for Steam: the Bluetooth model, whose fixed sample step SDL makes longer.
    bool wireless = false;
    // DualSense and DualShock 4: the pad's calibration, from its feature report (below).
    std::optional<ImuCalibration> calibration;
};

// The link for a node of `device`. `interface_number` is the node's USB interface (the charging
// grip has one per Joy-Con); `paired` says whether both a left and a right Joy-Con are there.
//
// The grip: SDL asks each Joy-Con which it is, with a write. Read-only, this takes SDL's fallback
// for a grip slot it cannot ask (CalculateControllerType in SDL_hidapi_switch.c): interface 1 the
// left, any other the right.
inline Link link_for(const Device& device, int interface_number = -1, bool paired = false) {
    Link link;
    link.family = device.family;
    link.edge = device.vendor == 0x054C && device.product == 0x0DF2;
    if (device.family == Family::JoyCon) {
        if (device.product == 0x2006) {
            link.side = JoyConSide::Left;
        } else if (device.product == 0x2007) {
            link.side = JoyConSide::Right;
        } else {
            link.side = interface_number == 1 ? JoyConSide::Left : JoyConSide::Right;
        }
        link.paired = paired;
    }
    link.wireless = device.vendor == 0x0F0D && device.product == 0x0196;
    return link;
}

// What SDL keeps from one report to the next for a device, as plain data: the sensor clock and
// the DualShock 4's count of good Bluetooth checksums. A new Stream for each node opened, as SDL
// makes a new context for each device.
struct Stream {
    // DualSense and DualShock 4: the report's sensor clock, unwrapped (SDL's sensor_ticks), and
    // its last raw value (SDL's last_tick).
    std::uint64_t ticks = 0;
    std::uint32_t last_tick = 0;
    // DualShock 4 over Bluetooth: SDL's valid_crc_packets, a wrapping count.
    std::uint16_t good_crcs = 0;
    // Switch, Joy-Con, Steam Deck, HORIPAD: the clock SDL builds by fixed steps.
    std::uint64_t ns = 0;
    // Switch and Joy-Con: the step (SDL starts at 5 ms), and the window it is measured over.
    std::uint64_t step_ns = 5'000'000;
    std::uint64_t window_ns = 0;
    std::uint32_t window_samples = 0;
    bool window_open = false;
};

// One motion sample.
struct Sample {
    Vec3f gyro;            // radians a second
    Vec3f accel;           // m/s^2
    std::uint64_t ns = 0;  // the sensor timestamp, as SDL builds it
    std::uint16_t buttons = 0;  // the gyro-button candidates held, button_bit()s

    bool held(Button b) const { return (buttons & button_bit(b)) != 0; }
};

constexpr int kMaxSamples = 3;  // the Switch's three IMU samples in each full report

// What one report gave. To feed GyroAim as OpenRAC does, take each sample in turn: button() with
// the gyro button held, gyro() with its rate and clock, then accel(): SDL sends each sample's gyro
// event before its accelerometer's, and OpenRAC handles them in that order (platform_input.cpp,
// Input::gyro_sample on SDL_EVENT_GAMEPAD_SENSOR_UPDATE).
struct Report {
    // The device's full state report, the one SDL reads motion from (it may still carry none: a
    // Switch pad's IMU off); false for any other report.
    bool ok = false;
    // The gyro-button candidates held, also given in each sample.
    std::uint16_t buttons = 0;
    // Whether the samples are SDL's SDL_SENSOR_GYRO, the gyro OpenRAC aims with. False for the
    // left Joy-Con of a pair, whose SDL sends as SDL_SENSOR_GYRO_L: a pair aims with the right's.
    bool primary = true;
    // The motion samples, oldest first.
    int count = 0;
    Sample samples[kMaxSamples];
};

namespace detail {

constexpr float kPi = 3.141592653589793238462643383279502884f;   // SDL_PI_F
constexpr double kPiD = 3.141592653589793238462643383279502884;  // SDL_PI_D
constexpr float kGravity = 9.80665f;                              // SDL_STANDARD_GRAVITY

inline std::int16_t le16(const std::uint8_t* p) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | (p[1] << 8)));
}

inline std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
           | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// CRC-32 (the reflected 0xEDB88320 polynomial), continuing from `crc` (0 to start).
inline std::uint32_t crc32(std::uint32_t crc, const std::uint8_t* data, int size) {
    crc = ~crc;
    for (int i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

// The check SDL makes of a PlayStation pad's Bluetooth report (VerifyCRC in SDL_hidapi_ps4.c and
// SDL_hidapi_ps5.c): the report's last four bytes hold, little-endian, the CRC-32 of the HIDP
// header byte 0xA1 and the rest of the report.
inline bool bluetooth_crc_ok(const std::uint8_t* report, int size) {
    if (size < 5) {
        return false;
    }
    const std::uint8_t header = 0xA1;
    const std::uint32_t crc = crc32(crc32(0, &header, 1), report, size - 4);
    return crc == le32(report + size - 4);
}

inline Vec3f apply(const ImuCalibration& c, int first, std::int16_t x, std::int16_t y, std::int16_t z) {
    return {(static_cast<float>(x) - c.bias[first]) * c.scale[first],
            (static_cast<float>(y) - c.bias[first + 1]) * c.scale[first + 1],
            (static_cast<float>(z) - c.bias[first + 2]) * c.scale[first + 2]};
}

// SDL_hidapi_ps5.c, HIDAPI_DriverPS5_ApplyCalibrationData without a calibration: the gyro 64
// counts to GYRO_RES_PER_DEGREE (1024) a degree a second, the accelerometer ACCEL_RES_PER_G (8192)
// a g.
inline float dualsense_gyro(std::int16_t v) { return ((static_cast<float>(v) * 64.f) / 1024.0f) * kPi / 180.0f; }
inline float dualsense_accel(std::int16_t v) { return (static_cast<float>(v) / 8192.0f) * kGravity; }

// SDL_hidapi_ps4.c, HIDAPI_DriverPS4_LoadCalibrationData without a calibration (bias 0, scale 1),
// for Sony's pads (gyro 1/16 of a degree a second a count, accelerometer 1/8192 of a g).
inline float dualshock4_gyro_scale() { return static_cast<float>(1.0 * (1.0 / 16.0) * kPiD / 180.0); }
inline float dualshock4_accel_scale() { return static_cast<float>(1.0 * (1.0 / 8192.0) * static_cast<double>(kGravity)); }

// The PlayStation pads' sensor block, at `p` in the state packet: the gyro's x, y and z then the
// accelerometer's, little-endian int16s, already on SDL's axes (SDL takes PlayStation's as its
// own). `dualsense` picks the fallback scale.
inline Sample playstation_sample(const Link& link, const std::uint8_t* p, bool dualsense) {
    const std::int16_t gx = le16(p), gy = le16(p + 2), gz = le16(p + 4);
    const std::int16_t ax = le16(p + 6), ay = le16(p + 8), az = le16(p + 10);
    Sample s;
    if (link.calibration) {
        s.gyro = apply(*link.calibration, 0, gx, gy, gz);
        s.accel = apply(*link.calibration, 3, ax, ay, az);
    } else if (dualsense) {
        s.gyro = {dualsense_gyro(gx), dualsense_gyro(gy), dualsense_gyro(gz)};
        s.accel = {dualsense_accel(ax), dualsense_accel(ay), dualsense_accel(az)};
    } else {
        const float g = dualshock4_gyro_scale(), a = dualshock4_accel_scale();
        s.gyro = {static_cast<float>(gx) * g, static_cast<float>(gy) * g, static_cast<float>(gz) * g};
        s.accel = {static_cast<float>(ax) * a, static_cast<float>(ay) * a, static_cast<float>(az) * a};
    }
    return s;
}

// SDL_hidapi_ps5.c. The state packet (PS5StatePacket_t) follows the report id over USB (report
// 0x01, 64 bytes) and one byte more over Bluetooth (report 0x31, 78 bytes, its CRC checked); a
// 0x01 report of 10 or 78 bytes is the simple report, with no sensors. In the packet: the buttons
// from byte 7, the gyro and accelerometer from 15, and a 32-bit sensor clock at 27 in units of
// a third of a microsecond (HIDAPI_DriverPS5_HandleStatePacketCommon).
inline Report parse_dualsense(const Link& link, Stream& stream, const std::uint8_t* report, int size) {
    Report out;
    const std::uint8_t* p = nullptr;
    if (report[0] == 0x01 && size != 10 && size != 78 && size >= 1 + 31) {
        p = report + 1;
    } else if (report[0] == 0x31 && size >= 2 + 31 + 4 && bluetooth_crc_ok(report, size)) {
        p = report + 2;
    } else {
        return out;
    }
    out.ok = true;
    // rgucButtonsAndHat[1]: L1 0x01, R1 0x02, L3 0x40, R3 0x80. [2]: the touchpad 0x02 (SDL's
    // button 11, "touchpad"), the microphone 0x04 (12, "misc1"), and on the Edge the left Fn 0x10
    // (13, "paddle4"), the right Fn 0x20 (14, "paddle3"), the left paddle 0x40 (15, "paddle2") and
    // the right paddle 0x80 (16, "paddle1").
    const std::uint8_t b1 = p[8], b2 = p[9];
    std::uint16_t held = 0;
    if (b1 & 0x01) held |= button_bit(Button::LeftShoulder);
    if (b1 & 0x02) held |= button_bit(Button::RightShoulder);
    if (b1 & 0x40) held |= button_bit(Button::LeftStick);
    if (b1 & 0x80) held |= button_bit(Button::RightStick);
    if (b2 & 0x02) held |= button_bit(Button::Touchpad);
    if (b2 & 0x04) held |= button_bit(Button::Misc1);
    if (link.edge) {
        if (b2 & 0x10) held |= button_bit(Button::Paddle4);
        if (b2 & 0x20) held |= button_bit(Button::Paddle3);
        if (b2 & 0x40) held |= button_bit(Button::Paddle2);
        if (b2 & 0x80) held |= button_bit(Button::Paddle1);
    }
    out.buttons = held;

    // The clock goes on by the difference from the last report, wrapping at 32 bits.
    const std::uint32_t tick = le32(p + 27);
    stream.ticks += static_cast<std::uint32_t>(tick - stream.last_tick);
    stream.last_tick = tick;

    Sample s = playstation_sample(link, p + 15, true);
    s.ns = (stream.ticks * 1000) / 3;
    s.buttons = held;
    out.samples[0] = s;
    out.count = 1;
    return out;
}

// SDL_hidapi_ps4.c. The state packet (PS4StatePacket_t) follows the report id over USB (report
// 0x01, 64 bytes; a 10-byte one is the simple report) and three bytes in over Bluetooth (reports
// 0x11 to 0x19, 78 bytes, the second byte's 0x80 set). In the packet: the buttons from byte 4, a
// 16-bit sensor clock at 9 in units of 16/3 of a microsecond, the gyro and accelerometer from 12
// (HIDAPI_DriverPS4_HandleStatePacket). Over USB, byte 31 of the report has 0x04 set while
// Sony's wireless adapter has no pad (HIDAPI_DriverPS4_IsPacketValid).
//
// SDL's Bluetooth check forgives: a report whose CRC fails still counts unless three or more
// good ones stand in the count (each good one adds one, each bad one takes one off).
inline Report parse_dualshock4(const Link& link, Stream& stream, const std::uint8_t* report, int size) {
    Report out;
    const std::uint8_t* p = nullptr;
    if (report[0] == 0x01) {
        if (size < 64 || (report[31] & 0x04) != 0) {
            return out;
        }
        p = report + 1;
    } else if (report[0] >= 0x11 && report[0] <= 0x19) {
        if (size < 78 || (report[1] & 0x80) == 0) {
            return out;
        }
        if (bluetooth_crc_ok(report, 78)) {
            ++stream.good_crcs;
        } else {
            if (stream.good_crcs > 0) {
                --stream.good_crcs;
            }
            if (stream.good_crcs >= 3) {
                return out;
            }
        }
        p = report + 3;
    } else {
        return out;
    }
    out.ok = true;
    // rgucButtonsHatAndCounter[1]: L1 0x01, R1 0x02, L3 0x40, R3 0x80. [2]: the touchpad 0x02
    // (SDL's button 11, "touchpad").
    const std::uint8_t b1 = p[5], b2 = p[6];
    std::uint16_t held = 0;
    if (b1 & 0x01) held |= button_bit(Button::LeftShoulder);
    if (b1 & 0x02) held |= button_bit(Button::RightShoulder);
    if (b1 & 0x40) held |= button_bit(Button::LeftStick);
    if (b1 & 0x80) held |= button_bit(Button::RightStick);
    if (b2 & 0x02) held |= button_bit(Button::Touchpad);
    out.buttons = held;

    // The clock goes on by the difference from the last report, wrapping at 16 bits.
    const std::uint16_t tick = static_cast<std::uint16_t>(le16(p + 9));
    stream.ticks += static_cast<std::uint16_t>(tick - static_cast<std::uint16_t>(stream.last_tick));
    stream.last_tick = tick;

    Sample s = playstation_sample(link, p + 12, false);
    s.ns = (stream.ticks * 1000 * 16) / 3;
    s.buttons = held;
    out.samples[0] = s;
    out.count = 1;
    return out;
}

// SDL_hidapi_switch.c without the SPI flash's calibration (reading it takes subcommands): the
// gyro SWITCH_GYRO_SCALE (14.2842) counts a degree a second, the accelerometer SWITCH_ACCEL_SCALE
// (4096) a g, as LoadIMUCalibration falls back to.
constexpr float kSwitchGyroScale = kPi / 180.0f / 14.2842f;
constexpr float kSwitchAccelScale = kGravity / 4096.f;

// One IMU reading (accelerometer or gyro, the report's x, y, z) onto SDL's axes, as SendSensorUpdate
// does: first to the PlayStation pads' (x = -y, y = z, z = -x), then the right Joy-Con's x and y
// flipped back, then a lone Joy-Con, held sideways, turned a quarter.
inline Vec3f switch_axes(const Link& link, float scale, std::int16_t x, std::int16_t y, std::int16_t z) {
    float d0 = -(scale * static_cast<float>(y));
    float d1 = scale * static_cast<float>(z);
    float d2 = -(scale * static_cast<float>(x));
    if (link.family == Family::JoyCon) {
        if (link.side == JoyConSide::Right) {
            d0 = -d0;
            d1 = -d1;
        }
        if (!link.paired) {
            const float t = d2;
            if (link.side == JoyConSide::Left) {
                d2 = -d0;
                d0 = t;
            } else {
                d2 = d0;
                d0 = -t;
            }
        }
    }
    return {d0, d1, d2};
}

// The gyro-button candidates of a Switch full report's three button bytes (b0 the right side's:
// SR 0x10, SL 0x20, R 0x40, ZR 0x80; b1 the middle's: the right stick 0x04, the left 0x08, the
// capture button 0x20; b2 the left side's: SR 0x10, SL 0x20, L 0x40, ZL 0x80), by the button SDL
// sends for each bit and the name its mapping gives that button.
inline std::uint16_t switch_buttons(const Link& link, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2) {
    std::uint16_t held = 0;
    const auto set = [&](bool on, Button b) {
        if (on) held |= button_bit(b);
    };
    if (link.family == Family::SwitchPro) {
        // HandleFullControllerState; the Pro Controller's mapping adds misc1:b11.
        set(b0 & 0x40, Button::RightShoulder);
        set(b1 & 0x04, Button::RightStick);
        set(b1 & 0x08, Button::LeftStick);
        set(b1 & 0x20, Button::Misc1);
        set(b2 & 0x40, Button::LeftShoulder);
    } else if (link.paired) {
        // HandleCombinedControllerStateL and R, read through the pair's mapping: the capture
        // button SDL's 11 ("misc1"), the right SR 12 ("paddle1"), the left SL 13 ("paddle2"), the
        // right SL 14 ("paddle3"), the left SR 15 ("paddle4").
        if (link.side == JoyConSide::Left) {
            set(b1 & 0x08, Button::LeftStick);
            set(b1 & 0x20, Button::Misc1);
            set(b2 & 0x10, Button::Paddle4);
            set(b2 & 0x20, Button::Paddle2);
            set(b2 & 0x40, Button::LeftShoulder);
        } else {
            set(b0 & 0x10, Button::Paddle1);
            set(b0 & 0x20, Button::Paddle3);
            set(b0 & 0x40, Button::RightShoulder);
            set(b1 & 0x04, Button::RightStick);
        }
    } else {
        // HandleMiniControllerStateL and R, read through a lone Joy-Con's mini gamepad mapping:
        // SL is the left shoulder and SR the right, the stick is the left stick, and the left's L
        // and ZL (SDL's 13 and 15) are "paddle2" and "paddle4", the right's R and ZR (12 and 14)
        // "paddle1" and "paddle3".
        if (link.side == JoyConSide::Left) {
            set(b1 & 0x08, Button::LeftStick);
            set(b2 & 0x10, Button::RightShoulder);
            set(b2 & 0x20, Button::LeftShoulder);
            set(b2 & 0x40, Button::Paddle2);
            set(b2 & 0x80, Button::Paddle4);
        } else {
            set(b0 & 0x10, Button::RightShoulder);
            set(b0 & 0x20, Button::LeftShoulder);
            set(b0 & 0x40, Button::Paddle1);
            set(b0 & 0x80, Button::Paddle3);
            set(b1 & 0x04, Button::LeftStick);
        }
    }
    return held;
}

// SDL_hidapi_switch.c, HandleFullControllerState. The full report (0x30, or 0x31 with the NFC/IR
// block after) is the same over USB and Bluetooth: the report id, a timer, the battery, the three
// button bytes (report bytes 3 to 5), the sticks, a vibration byte, then three IMU samples of 12
// bytes from byte 13 (SwitchControllerIMUState_t: the accelerometer's x, y, z, then the gyro's,
// little-endian int16s). A report whose first sample's accelerometer is all zero has the IMU off.
//
// The report carries no clock. SDL steps its own by an interval it starts at 5 ms and measures
// against the host's clock: every 1000 samples or more, the time since the last measure over the
// samples since, so the step follows however fast the pad sends its reports. It hands the samples
// out last first: the third sample of the report is the first in time.
inline Report parse_switch(const Link& link, Stream& stream, const std::uint8_t* report, int size, std::uint64_t now_ns) {
    Report out;
    if ((report[0] != 0x30 && report[0] != 0x31) || size < 13 + 3 * 12) {
        return out;
    }
    out.ok = true;
    out.primary = !(link.family == Family::JoyCon && link.paired && link.side == JoyConSide::Left);
    out.buttons = switch_buttons(link, report[3], report[4], report[5]);

    const std::uint8_t* imu = report + 13;
    if (le16(imu) == 0 && le16(imu + 2) == 0 && le16(imu + 4) == 0) {
        return out;
    }
    if (!stream.window_open) {
        stream.window_open = true;
        stream.window_ns = now_ns;
        stream.window_samples = 0;
    }
    stream.window_samples += 3;
    if (stream.window_samples >= 1000) {
        if (now_ns > stream.window_ns) {
            stream.step_ns = (now_ns - stream.window_ns) / stream.window_samples;
        }
        stream.window_samples = 0;
        stream.window_ns = now_ns;
    }
    for (int k = 0; k < 3; ++k) {
        const std::uint8_t* m = imu + 12 * (2 - k);
        stream.ns += stream.step_ns;
        Sample& s = out.samples[k];
        s.accel = switch_axes(link, kSwitchAccelScale, le16(m), le16(m + 2), le16(m + 4));
        s.gyro = switch_axes(link, kSwitchGyroScale, le16(m + 6), le16(m + 8), le16(m + 10));
        s.ns = stream.ns;
        s.buttons = out.buttons;
    }
    out.count = 3;
    return out;
}

// SDL_hidapi_steamdeck.c with steam/controller_structs.h. The Deck's report is 64 bytes, no report
// id: a header (ValveInReportHeader_t: version 0x0001, type 9 for the Deck's state, length 64),
// then SteamDeckStatePacket_t: a packet number, the buttons (64 bits from byte 8), the trackpads,
// then the accelerometer's x, y, z from byte 24 and the gyro's from byte 30, little-endian int16s.
// The gyro reads +-2000 degrees a second over the int16's range, the accelerometer +-2 g. No clock
// in it: SDL steps its own by 4 ms a report.
inline Report parse_steam_deck(Stream& stream, const std::uint8_t* report, int size) {
    Report out;
    if (size != 64 || le16(report) != 0x0001 || report[2] != 9 || report[3] != 64) {
        return out;
    }
    out.ok = true;
    // The buttons as HIDAPI_DriverSteamDeck_HandleState sends them, named by the Deck's mapping
    // (misc1:b11 the quick access button, paddle1:b12 R4, paddle2:b13 L4, paddle3:b14 R5,
    // paddle4:b15 L5, touchpad:b17 the left trackpad's click).
    const std::uint32_t lo = le32(report + 8), hi = le32(report + 12);
    std::uint16_t held = 0;
    if (lo & 0x00000008u) held |= button_bit(Button::LeftShoulder);
    if (lo & 0x00000004u) held |= button_bit(Button::RightShoulder);
    if (lo & 0x00400000u) held |= button_bit(Button::LeftStick);
    if (lo & 0x04000000u) held |= button_bit(Button::RightStick);
    if (hi & 0x00040000u) held |= button_bit(Button::Misc1);
    if (hi & 0x00000400u) held |= button_bit(Button::Paddle1);
    if (hi & 0x00000200u) held |= button_bit(Button::Paddle2);
    if (lo & 0x00010000u) held |= button_bit(Button::Paddle3);
    if (lo & 0x00008000u) held |= button_bit(Button::Paddle4);
    if (lo & 0x00020000u) held |= button_bit(Button::Touchpad);
    out.buttons = held;

    const std::int16_t ax = le16(report + 24), ay = le16(report + 26), az = le16(report + 28);
    const std::int16_t gx = le16(report + 30), gy = le16(report + 32), gz = le16(report + 34);
    constexpr float kGyro = 2000.0f * (kPi / 180.0f);
    stream.ns += 4000 * 1000;
    Sample& s = out.samples[0];
    s.gyro = {(gx / 32768.0f) * kGyro, (gz / 32768.0f) * kGyro, (-gy / 32768.0f) * kGyro};
    s.accel = {(ax / 32768.0f) * 2.0f * kGravity, (az / 32768.0f) * 2.0f * kGravity, (-ay / 32768.0f) * 2.0f * kGravity};
    s.ns = stream.ns;
    s.buttons = held;
    out.count = 1;
    return out;
}

// SDL_hidapi_steam_hori.c's RemapValClamped: `val` from [a, b] onto [c, d], held within it.
inline float remap_clamped(float val, float a, float b, float c, float d) {
    if (a == b) {
        return (val - b) >= 0.0f ? d : c;
    }
    float t = (val - a) / (b - a);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return c + (d - c) * t;
}

// SDL_hidapi_steam_hori.c, HIDAPI_DriverSteamHori_HandleStatePacket. The report starts 0x07 over
// USB and 0x00 over Bluetooth; the buttons are in bytes 5 to 7, the gyro's three int16s from byte
// 12 and the accelerometer's from 18. SDL maps the gyro's +-2048 degrees a second onto the int16's
// range (negated: y from byte 12, z from 14, x from 16) and reads the accelerometer at 8 g over
// it (z from 18, y negated from 20, x from 22). The report has a clock SDL does not trust: it
// steps its own, 4 ms a report over USB, 8.333 ms over Bluetooth.
inline Report parse_hori_steam(const Link& link, Stream& stream, const std::uint8_t* report, int size) {
    Report out;
    if (size < 24 || (report[0] != 0x07 && report[0] != 0x00)) {
        return out;
    }
    out.ok = true;
    // The buttons it sends, named by its mapping (misc1:b11 the QAM button, paddle1:b13 FL,
    // paddle2:b12 FR, paddle3:b15 M2, paddle4:b14 M1).
    std::uint16_t held = 0;
    if (report[5] & 0x40) held |= button_bit(Button::Misc1);
    if (report[6] & 0x02) held |= button_bit(Button::Paddle4);
    if (report[6] & 0x04) held |= button_bit(Button::LeftShoulder);
    if (report[6] & 0x08) held |= button_bit(Button::RightShoulder);
    if (report[7] & 0x02) held |= button_bit(Button::LeftStick);
    if (report[7] & 0x04) held |= button_bit(Button::RightStick);
    if (report[7] & 0x08) held |= button_bit(Button::Paddle3);
    if (report[7] & 0x40) held |= button_bit(Button::Paddle2);
    if (report[7] & 0x80) held |= button_bit(Button::Paddle1);
    out.buttons = held;

    stream.ns += link.wireless ? 8333 * 1000 : 4000 * 1000;
    const float gyro_scale = static_cast<float>(2048) * static_cast<float>(kPi / 180.f);
    const float accel_scale = kGravity * 8 / 32768.0f;
    const auto gyro_axis = [&](int at) {
        return remap_clamped(-1.0f * le16(report + at), -32768.0f, 32767.0f, -gyro_scale, gyro_scale);
    };
    Sample& s = out.samples[0];
    s.gyro = {gyro_axis(16), gyro_axis(12), gyro_axis(14)};
    s.accel = {le16(report + 22) * accel_scale, -1 * le16(report + 20) * accel_scale, le16(report + 18) * accel_scale};
    s.ns = stream.ns;
    s.buttons = held;
    out.count = 1;
    return out;
}

}  // namespace detail

// One report into its samples: `report` from its first byte as read from the hidraw node (the
// report id first where the device numbers its reports), `size` the bytes read. `now_ns` is when it
// was read (CLOCK_MONOTONIC), used only by the Switch pads, whose clock SDL steps by an interval it
// measures against the host's; 0 keeps SDL's first step (5 ms). `stream` carries SDL's state from
// one report to the next: the result is a function of the link, the report, `now_ns` and the
// stream before, and changes nothing else.
inline Report parse_report(const Link& link, Stream& stream, const std::uint8_t* report, int size, std::uint64_t now_ns = 0) {
    if (report == nullptr || size < 1) {
        return Report{};
    }
    switch (link.family) {
        case Family::DualSense:
            return detail::parse_dualsense(link, stream, report, size);
        case Family::DualShock4:
            return detail::parse_dualshock4(link, stream, report, size);
        case Family::SwitchPro:
        case Family::JoyCon:
            return detail::parse_switch(link, stream, report, size, now_ns);
        case Family::SteamDeck:
            return detail::parse_steam_deck(stream, report, size);
        case Family::HoriSteam:
            return detail::parse_hori_steam(link, stream, report, size);
    }
    return Report{};
}

// A DualSense's calibration from its feature report 0x05 (`data` as HIDIOCGFEATURE gives it, the
// report id first; SDL reads 64 bytes and wants 35), as HIDAPI_DriverPS5_LoadCalibrationData
// builds it in SDL_hidapi_ps5.c: the gyro's biases at bytes 1, 3, 5; its readings at plus and
// minus a known speed (pitch 7 and 9, yaw 11 and 13, roll 15 and 17) and that speed's two halves
// (19, 21); the accelerometer's readings at plus and minus 1 g (x 23 and 25, y 27 and 29, z 31 and
// 33). None where SDL would keep its fallback: a short report, or any bias over 1024 counts or
// sensitivity more than half off the nominal (64 for the gyro, 1 for the accelerometer).
inline std::optional<ImuCalibration> dualsense_calibration(const std::uint8_t* data, int size) {
    if (data == nullptr || size < 35) {
        return std::nullopt;
    }
    const auto at = [&](int k) { return detail::le16(data + k); };
    std::int16_t bias[6];
    float sensitivity[6];
    const float numerator = static_cast<float>(at(19) + at(21)) * 1024.0f;
    for (int i = 0; i < 3; ++i) {
        bias[i] = at(1 + 2 * i);
        sensitivity[i] = numerator / static_cast<float>(at(7 + 4 * i) - at(9 + 4 * i));
    }
    for (int i = 0; i < 3; ++i) {
        const std::int16_t plus = at(23 + 4 * i), minus = at(25 + 4 * i);
        const std::int16_t range = static_cast<std::int16_t>(plus - minus);
        bias[3 + i] = static_cast<std::int16_t>(plus - range / 2);
        sensitivity[3 + i] = 2.0f * 8192.0f / static_cast<float>(range);
    }
    ImuCalibration c;
    for (int i = 0; i < 6; ++i) {
        const float nominal = i < 3 ? 64.0f : 1.0f;
        if (std::abs(bias[i]) > 1024 || std::fabs(1.0f - sensitivity[i] / nominal) > 0.5f) {
            return std::nullopt;
        }
        c.bias[i] = bias[i];
        c.scale[i] = i < 3 ? sensitivity[i] / 1024.0f * detail::kPi / 180.0f : sensitivity[i] / 8192.0f * detail::kGravity;
    }
    return c;
}

// A DualShock 4's calibration from its feature report (`data` as HIDIOCGFEATURE gives it, the
// report id first; SDL wants 35 bytes): 0x02 over USB and through Sony's adapter, 0x05 over
// Bluetooth, as HIDAPI_DriverPS4_LoadOfficialCalibrationData builds it in SDL_hidapi_ps4.c. The
// gyro's biases at 1, 3, 5; its readings at plus and minus a known speed, over Bluetooth and
// through the adapter (`bluetooth_layout`) the three pluses at 7, 9, 11 then the three minuses at
// 13, 15, 17, over USB pitch 7 and 9, yaw 11 and 13, roll 15 and 17; the speed's halves at 19 and
// 21; the accelerometer at plus and minus 1 g from 23 as the DualSense's. None where SDL would
// keep its fallback: a short or all-zero report, a reading no wider than its bias, or any bias
// over 1024 counts or scale more than half off 1.
inline std::optional<ImuCalibration> dualshock4_calibration(const std::uint8_t* data, int size, bool bluetooth_layout) {
    if (data == nullptr || size < 35) {
        return std::nullopt;
    }
    bool any = false;
    for (int i = 0; i < size; ++i) {
        any = any || data[i] != 0;
    }
    if (!any) {
        return std::nullopt;
    }
    const auto at = [&](int k) { return detail::le16(data + k); };
    std::int16_t plus[3], minus[3];
    for (int i = 0; i < 3; ++i) {
        if (bluetooth_layout) {
            plus[i] = at(7 + 2 * i);
            minus[i] = at(13 + 2 * i);
        } else {
            plus[i] = at(7 + 4 * i);
            minus[i] = at(9 + 4 * i);
        }
    }
    std::int16_t bias[6];
    float scale[6];
    const float numerator = static_cast<float>(at(19) + at(21)) * 16 / 1;  // gyro_denominator / gyro_numerator
    for (int i = 0; i < 3; ++i) {
        bias[i] = at(1 + 2 * i);
        const float denominator = static_cast<float>(std::abs(plus[i] - bias[i]) + std::abs(minus[i] - bias[i]));
        if (denominator == 0.0f) {
            return std::nullopt;
        }
        scale[i] = numerator / denominator;
    }
    for (int i = 0; i < 3; ++i) {
        const std::int16_t p = at(23 + 4 * i), m = at(25 + 4 * i);
        const std::int16_t range = static_cast<std::int16_t>(p - m);
        bias[3 + i] = static_cast<std::int16_t>(p - range / 2);
        scale[3 + i] = (2.0f * 8192 / 1) / static_cast<float>(range);  // accel_denominator / accel_numerator
    }
    ImuCalibration c;
    for (int i = 0; i < 6; ++i) {
        if (std::abs(bias[i]) > 1024 || std::fabs(1.0f - scale[i]) > 0.5f) {
            return std::nullopt;
        }
        double s = scale[i];
        if (i < 3) {
            s *= (1.0 / 16.0) * detail::kPiD / 180.0;
        } else {
            s *= (1.0 / 8192.0) * static_cast<double>(detail::kGravity);
        }
        c.bias[i] = bias[i];
        c.scale[i] = static_cast<float>(s);
    }
    return c;
}

}  // namespace gyro
