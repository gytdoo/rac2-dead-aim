// SPDX-License-Identifier: GPL-3.0-or-later
// The wrench tossed on the run (Crouch: Deadlocked), free of the game so it can be tested.
//
// Adapted from Joebannerlord's Contextual Wrench and Fire
// (https://github.com/Joebannerlord/openRAC-RC2-contextual-wrench-throw, GPL-3.0-or-later), whose
// throw timing and arm pose came from Christian's wrench-throw mod for Ratchet & Clank
// (https://github.com/chistianh/wrench-throw, GPL-3.0-or-later): a press asks for a throw, kept a
// moment while the wrench is busy; the arm winds up over a few ticks, the game's own launch runs at
// the release, the arm follows through, and reaches out and closes as the wrench comes back.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace toss {

// How far each part of the throw is along, 0 to 1: the arm raised (Crouch held on the run), the
// release, the reach for the returning wrench, and the hand closing on it.
struct ArmPose {
    float aim = 0.0f;
    float release = 0.0f;
    float reach = 0.0f;
    float close = 0.0f;

    bool any() const { return aim + release + reach + close > 0.0f; }
};

struct Step {
    ArmPose pose;
    bool launch = false;  // the game's launch is due this tick
};

// One tick at a time, while the same wrench is in play.
class Motion {
public:
    static constexpr int kBuffer = 90;   // ticks a press waits for the wrench to be back in hand
    static constexpr int kRelease = 8;   // the swing's tick the wrench leaves
    static constexpr int kFinal = 21;    // the swing's last tick

    // A press: a throw is wanted (kept kBuffer ticks while the wrench is out).
    void request() {
        if (pending_ == 0) pending_ = kBuffer;
    }

    // `aim` the arm raised, `ready` the wrench in hand, `returning` how near the returning wrench
    // is (0 to 1), `caught` it came back this tick.
    Step step(bool aim, bool ready, float returning, bool caught) {
        if (pending_ > 0) --pending_;
        if (swing_ >= 0 && ++swing_ == kFinal) swing_ = -1;
        if (swing_ >= 0 && swing_ <= kRelease && (!ready || pending_ == 0)) swing_ = -1;
        if (swing_ < 0 && pending_ > 0 && ready) swing_ = 0;
        const bool launch = swing_ == kRelease;
        if (launch) pending_ = 0;

        aim_ = std::clamp(aim_ + (aim && ready && swing_ < 0 ? 0.125f : -0.125f), 0.0f, 1.0f);
        reach_ += std::clamp(returning - reach_, -0.18f, 0.25f);
        close_ = caught ? 1.0f : std::max(0.0f, close_ - 0.125f);

        float wind = 0.0f, release = 0.0f;
        if (swing_ >= 0) {
            const float load = std::min(1.0f, static_cast<float>(swing_) / 4.0f);
            const float snap = std::clamp(static_cast<float>(swing_ - 4) / 3.0f, 0.0f, 1.0f);
            const float follow = std::max(0.0f, static_cast<float>(swing_ - kRelease) / 12.0f);
            wind = ease(load) * (1.0f - ease(snap));
            release = ease(snap) * (1.0f - ease(follow));
        }
        return {{std::max(ease(aim_), wind), release, ease(reach_), ease(close_)}, launch};
    }

    bool swinging() const { return swing_ >= 0; }
    void cancel() { *this = Motion{}; }

private:
    static float ease(float t) { return t * t * (3.0f - 2.0f * t); }

    int pending_ = 0;
    int swing_ = -1;
    float aim_ = 0.0f, reach_ = 0.0f, close_ = 0.0f;
};

// One joint turned by x, y, z (radians, about its own axes), as a unit quaternion (x, y, z, w).
struct JointTurn {
    std::uint32_t joint;
    float rotation[4];
};

inline JointTurn turn(std::uint32_t joint, float x, float y, float z) {
    const float cx = std::cos(x * 0.5f), sx = std::sin(x * 0.5f);
    const float cy = std::cos(y * 0.5f), sy = std::sin(y * 0.5f);
    const float cz = std::cos(z * 0.5f), sz = std::sin(z * 0.5f);
    return {joint,
            {sx * cy * cz + cx * sy * sz, cx * sy * cz - sx * cy * sz, cx * cy * sz + sx * sy * cz,
             cx * cy * cz - sx * sy * sz}};
}

// The right arm's pose: small turns over his own run. Ratchet's skeleton (the same on both USA
// discs): right shoulder 53, elbow 54, forearm 55, wrist 56. None when the pose is all zero.
inline std::vector<JointTurn> arm(const ArmPose& p) {
    if (!p.any()) return {};
    return {
        turn(53, 0.55f * p.aim - 0.25f * p.release - 0.30f * p.reach, 0.18f * p.aim,
             -0.45f * p.aim + 0.82f * p.release + 0.55f * p.reach + 0.20f * p.close),
        turn(54, 0.10f * p.close, 0.0f, -1.12f * p.aim + 0.19f * p.release - 0.30f * p.reach - 0.68f * p.close),
        turn(56, 0.65f * p.aim - 0.18f * p.release, 0.0f, -0.14f * p.reach + 0.35f * p.close),
    };
}

// How near the returning wrench is, 0 to 1: from 4.5 units off the hand to 1.5.
inline float catch_reach(float distance) { return std::clamp((4.5f - distance) / 3.0f, 0.0f, 1.0f); }

}  // namespace toss
