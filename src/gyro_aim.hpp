// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from OpenRAC's port/platform/gyro.{h,cpp} (GPL-3.0-or-later), Copyright (c) 2026 the OpenRAC contributors
//
// Gyro aiming as OpenRAC does it: a controller's gyroscope turned into a turn of the camera, in
// radians. The filter is OpenRAC's own, line for line, so the mod's gyro turns the camera as
// OpenRAC's built-in gyro would; only the names follow this repository (members end in an
// underscore, the gyro button is this file's Button rather than an SDL_GamepadButton number).
//
// Each sample goes through: the gyro's resting offset (learnt while the controller lies still),
// the space (which twist turns the camera), a dead zone, a soft smoothing of the smallest motion
// (hand shake), then the sensitivity. The camera turns by what comes out: at sensitivity 1, as far
// as the controller turned. SDL's axes (SDL_sensor.h): x right, y up, z toward the player; a turn
// counter-clockwise about an axis is positive.
//
// The settings come from OpenRAC's "Gyro" section (kbm_openrac-mod.json): read_gyro in OpenRAC's
// core.hpp turns them into GyroOptions, the dead zone and smoothing from degrees a second into
// radians a second.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace gyro {

// Which of the controller's turns move the camera left and right.
enum class GyroSpace {
    Player,  // the turn about the way up, however the controller is held (needs its accelerometer)
    Yaw,     // the turn about the controller's own up axis (held flat)
    Roll,    // leaning the controller left and right, like a wheel
    Local    // both of the above added together
};

// What the gyro button does.
enum class GyroButtonAction {
    Enable,  // the gyro aims only while it is held
    Pause,   // the gyro aims except while it is held (lift it to re-centre)
    Toggle   // each press turns the gyro on or off
};

// The buttons OpenRAC offers as the gyro button, by SDL's names for them (SDL_gamepad.h): what the
// "gyro_button" setting may hold. Which of a controller's buttons each is follows SDL's mapping
// for it (SDL_gamepad.c, SDL_CreateMappingForHIDAPIGamepad); gyro_hid.hpp reads them.
enum class Button : std::uint8_t {
    None = 0,
    LeftStick,      // "leftstick"
    RightStick,     // "rightstick"
    LeftShoulder,   // "leftshoulder"
    RightShoulder,  // "rightshoulder"
    Misc1,          // "misc1": the PS5 microphone, the Switch capture, the Steam Deck's quick access
    Paddle1,        // "paddle1": the upper right paddle
    Paddle2,        // "paddle2": the upper left paddle
    Paddle3,        // "paddle3": the lower right paddle
    Paddle4,        // "paddle4": the lower left paddle
    Touchpad,       // "touchpad": the PS4 and PS5 touchpad click
};

// A button's bit in a set of held buttons, none for Button::None.
constexpr std::uint16_t button_bit(Button b) {
    return b == Button::None ? 0 : static_cast<std::uint16_t>(1u << static_cast<unsigned>(b));
}

// The gyro button's setting: SDL's name for a gamepad button, Button::None for "none" or any other
// text (as OpenRAC's gyro_button_of gives -1).
inline Button gyro_button_of(std::string_view name) {
    struct Name {
        const char* name;
        Button button;
    };
    static constexpr Name kButtons[] = {
        {"leftstick", Button::LeftStick},
        {"rightstick", Button::RightStick},
        {"leftshoulder", Button::LeftShoulder},
        {"rightshoulder", Button::RightShoulder},
        {"misc1", Button::Misc1},
        {"paddle1", Button::Paddle1},
        {"paddle2", Button::Paddle2},
        {"paddle3", Button::Paddle3},
        {"paddle4", Button::Paddle4},
        {"touchpad", Button::Touchpad},
    };
    for (const Name& n : kButtons) {
        if (name == n.name) {
            return n.button;
        }
    }
    return Button::None;
}

struct GyroOptions {
    bool enabled = false;
    GyroSpace space = GyroSpace::Player;
    float sensitivity_x = 2.0f;  // camera radians per controller radian, left and right
    float sensitivity_y = 2.0f;  // and up and down
    bool invert_x = false;
    bool invert_y = false;
    float dead_zone = 0.0f;  // radians a second below which the gyro is ignored
    float smoothing = 0.0f;  // radians a second below which motion is smoothed (0: none)
    bool auto_calibrate = true;
    Button button = Button::None;
    GyroButtonAction button_action = GyroButtonAction::Enable;
};

// One parse of a setting's text, null, "" or unknown giving the default.
inline GyroSpace gyro_space_from(const char* text) {
    if (text == nullptr) {
        return GyroSpace::Player;
    }
    if (std::strcmp(text, "yaw") == 0) {
        return GyroSpace::Yaw;
    }
    if (std::strcmp(text, "roll") == 0) {
        return GyroSpace::Roll;
    }
    if (std::strcmp(text, "local") == 0) {
        return GyroSpace::Local;
    }
    return GyroSpace::Player;
}

inline GyroButtonAction gyro_button_action_from(const char* text) {
    if (text != nullptr && std::strcmp(text, "pause") == 0) {
        return GyroButtonAction::Pause;
    }
    if (text != nullptr && std::strcmp(text, "toggle") == 0) {
        return GyroButtonAction::Toggle;
    }
    return GyroButtonAction::Enable;
}

struct Vec3f {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

namespace detail {

inline Vec3f sub(Vec3f a, Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

inline Vec3f min3(Vec3f a, Vec3f b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }

inline Vec3f max3(Vec3f a, Vec3f b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

inline float largest(Vec3f a) { return std::max({std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}); }

inline bool finite(Vec3f a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

inline float length(Vec3f a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

// How much of the motion at `speed` goes straight through, the rest smoothed (GyroWiki's soft
// tiered smoothing): none at half the threshold and under, all at the threshold and over.
inline float direct_share(float speed, float threshold) {
    if (threshold <= 0.0f) {
        return 1.0f;
    }
    const float low = threshold * 0.5f;
    return std::clamp((speed - low) / (threshold - low), 0.0f, 1.0f);
}

}  // namespace detail

// The gyro's filter, with no device in it (tests/gyro_test.cpp).
class GyroAim {
public:
    // How still the controller must be, and for how long, to learn its offset: the spread of each
    // axis under kStillSpread for kStillSeconds, the mean under kMaxOffset (a gyro's drift is
    // small; a slow steady turn is not drift).
    static constexpr float kStillSpread = 0.02f;  // radians a second (about 1.1 degrees)
    static constexpr float kStillSeconds = 1.5f;
    static constexpr float kMaxOffset = 0.1f;            // radians a second
    static constexpr float kStillAccelSpread = 0.4f;     // m/s^2
    static constexpr float kSmoothingSeconds = 0.0625f;  // the smoothed part's time constant
    static constexpr float kGravitySeconds = 0.25f;      // the accelerometer's low pass
    // Player space: how much of the twist off the way up still counts (GyroWiki's 1.41).
    static constexpr float kRelax = 1.41f;
    // A gap between samples longer than this is not motion (a sleep, a lost link).
    static constexpr float kMaxStep = 0.05f;

    void set_options(const GyroOptions& options) { options_ = options; }

    const GyroOptions& options() const { return options_; }

    // A gyro sample (radians a second, SDL's axes) at `ns` (the sensor's own clock).
    void gyro(Vec3f rate, std::uint64_t ns) {
        // A broken sample (a driver's NaN) would stay in the offset and the smoothing for good.
        if (!detail::finite(rate)) {
            return;
        }
        float dt = 0.0f;
        if (has_last_ && ns > last_ns_) {
            dt = static_cast<float>(static_cast<double>(ns - last_ns_) * 1e-9);
        }
        last_ns_ = ns;
        has_last_ = true;
        if (dt <= 0.0f || dt > kMaxStep) {
            return;
        }

        if (options_.auto_calibrate) {
            learn_offset(rate, last_accel_, dt);
        }
        const Vec3f v = detail::sub(rate, offset_);

        // The turn left (about the way up) and up.
        float yaw = v.y;
        switch (options_.space) {
            case GyroSpace::Yaw:
                yaw = v.y;
                break;
            case GyroSpace::Roll:
                yaw = v.z;
                break;
            case GyroSpace::Local:
                yaw = v.y + v.z;
                break;
            case GyroSpace::Player: {
                const float g = detail::length(gravity_);
                if (!has_gravity_ || g < 1.0f) {
                    yaw = v.y;  // no accelerometer: as Yaw
                    break;
                }
                // The turn about the way up, in the controller's y-z plane, a little relaxed so
                // that a controller held tilted still turns fully (GyroWiki's player space).
                const float world = (v.y * gravity_.y + v.z * gravity_.z) / g;
                const float most = std::sqrt(v.y * v.y + v.z * v.z);
                yaw = std::copysign(std::min(std::fabs(world) * kRelax, most), world);
                break;
            }
        }
        float pitch = v.x;

        if (!aiming()) {
            smooth_yaw_ = smooth_pitch_ = 0.0f;
            return;
        }

        // The dead zone, by speed: what is over it is scaled down so the turn starts smoothly.
        float speed = std::sqrt(yaw * yaw + pitch * pitch);
        if (options_.dead_zone > 0.0f) {
            if (speed <= options_.dead_zone) {
                yaw = pitch = 0.0f;
            } else {
                const float k = (speed - options_.dead_zone) / speed;
                yaw *= k;
                pitch *= k;
            }
            speed = std::sqrt(yaw * yaw + pitch * pitch);
        }

        // The smallest motion through a short average (hand shake), the rest straight through.
        const float direct = detail::direct_share(speed, options_.smoothing);
        if (direct < 1.0f) {
            const float k = 1.0f - std::exp(-dt / kSmoothingSeconds);
            smooth_yaw_ += (yaw * (1.0f - direct) - smooth_yaw_) * k;
            smooth_pitch_ += (pitch * (1.0f - direct) - smooth_pitch_) * k;
            yaw = yaw * direct + smooth_yaw_;
            pitch = pitch * direct + smooth_pitch_;
        } else {
            smooth_yaw_ = smooth_pitch_ = 0.0f;
        }

        const float sx = std::max(options_.sensitivity_x, 0.0f) * (options_.invert_x ? -1.0f : 1.0f);
        const float sy = std::max(options_.sensitivity_y, 0.0f) * (options_.invert_y ? -1.0f : 1.0f);
        yaw_ += yaw * sx * dt;
        pitch_ += pitch * sy * dt;
    }

    // An accelerometer sample (m/s^2, SDL's axes): the way up, for the player space.
    void accel(Vec3f a) {
        if (!detail::finite(a)) {
            return;
        }
        last_accel_ = a;
        if (!has_gravity_) {
            gravity_ = a;
            has_gravity_ = true;
            return;
        }
        // A plain low pass: the way up changes slowly, a shake of the hand quickly.
        constexpr float kStep = 1.0f / 250.0f;  // about a sample of a pad's 250 a second
        const float k = 1.0f - std::exp(-kStep / kGravitySeconds);
        gravity_.x += (a.x - gravity_.x) * k;
        gravity_.y += (a.y - gravity_.y) * k;
        gravity_.z += (a.z - gravity_.z) * k;
    }

    // Whether the gyro aims now: on in the options and by the button. `held` is the button now.
    void button(bool held) {
        if (options_.button == Button::None) {
            button_on_ = true;
            button_was_ = held;
            return;
        }
        const bool was_on = button_on_;
        switch (options_.button_action) {
            case GyroButtonAction::Enable:
                button_on_ = held;
                break;
            case GyroButtonAction::Pause:
                button_on_ = !held;
                break;
            case GyroButtonAction::Toggle:
                if (held && !button_was_) {
                    toggled_ = !toggled_;
                }
                button_on_ = toggled_;
                break;
        }
        button_was_ = held;
        // Turned on or off: the smoothing starts afresh, no motion from before carries on.
        if (was_on != button_on_) {
            smooth_yaw_ = smooth_pitch_ = 0.0f;
        }
    }

    bool aiming() const { return options_.enabled && button_on_; }

    // The turn gathered since the last take: yaw > 0 left, pitch > 0 up, radians.
    void take(float& yaw, float& pitch) {
        yaw = yaw_;
        pitch = pitch_;
        yaw_ = pitch_ = 0.0f;
    }

    void peek(float& yaw, float& pitch) const {
        yaw = yaw_;
        pitch = pitch_;
    }

    // A new controller, or none: all learnt state starts over.
    void reset() {
        const GyroOptions options = options_;
        *this = GyroAim{};
        options_ = options;
    }

    bool calibrated() const { return has_offset_; }

    Vec3f offset() const { return offset_; }

    bool has_gravity() const { return has_gravity_; }

private:
    void learn_offset(Vec3f rate, Vec3f accel, float dt) {
        if (count_ == 0) {
            sum_ = {};
            min_ = max_ = rate;
            accel_min_ = accel_max_ = accel;
            still_ = 0.0f;
        }
        sum_.x += rate.x;
        sum_.y += rate.y;
        sum_.z += rate.z;
        min_ = detail::min3(min_, rate);
        max_ = detail::max3(max_, rate);
        accel_min_ = detail::min3(accel_min_, accel);
        accel_max_ = detail::max3(accel_max_, accel);
        ++count_;
        still_ += dt;

        const bool moved = detail::largest(detail::sub(max_, min_)) > kStillSpread
                           || (has_gravity_ && detail::largest(detail::sub(accel_max_, accel_min_)) > kStillAccelSpread);
        if (moved) {
            count_ = 0;
            return;
        }
        if (still_ < kStillSeconds) {
            return;
        }
        const float n = static_cast<float>(count_);
        const Vec3f mean{sum_.x / n, sum_.y / n, sum_.z / n};
        count_ = 0;
        if (detail::largest(mean) > kMaxOffset) {
            return;
        }
        // The first time, the mean; then halfway there, so one odd window does not throw it.
        if (!has_offset_) {
            offset_ = mean;
            has_offset_ = true;
        } else {
            offset_.x += (mean.x - offset_.x) * 0.5f;
            offset_.y += (mean.y - offset_.y) * 0.5f;
            offset_.z += (mean.z - offset_.z) * 0.5f;
        }
    }

    GyroOptions options_;
    bool button_on_ = true;
    bool button_was_ = false;
    bool toggled_ = false;

    std::uint64_t last_ns_ = 0;
    bool has_last_ = false;

    Vec3f offset_;
    bool has_offset_ = false;
    // The still window: its sums, spreads and length.
    Vec3f sum_, min_, max_, accel_min_, accel_max_;
    int count_ = 0;
    float still_ = 0.0f;

    Vec3f gravity_;
    bool has_gravity_ = false;
    Vec3f last_accel_;

    float smooth_yaw_ = 0.0f;
    float smooth_pitch_ = 0.0f;

    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
};

}  // namespace gyro
