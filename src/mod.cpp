// SPDX-License-Identifier: GPL-3.0-or-later
// Dead Aim for Going Commando:
//   - lock-strafe (off, with a weapon in hand, always), and each shoulder (L1, L2, R1, R2) set to
//     toggle the strafe, jump, fire, crouch, aim or swing the wrench;
//   - the Modern camera: a free camera for the stick, the mouse and the gyro, kept out of walls by a
//     sphere swept along its arm; the weapons aim where it looks, and Aim switches to first person
//     on the move;
//   - each weapon's own crosshair in third person at one size, and Up Your Arsenal's and
//     Deadlocked's camera speeds and aim assist for the right stick;
//   - Auto Gadgets: Fire at a gadget's target uses the gadget, then the weapon is back;
//   - on OpenRAC builds without their own keyboard, mouse and gyro, the mod's own (older OpenRAC).
// A page of the pause menu's Options (Dead Aim) sets them all, as the launcher does.
//
// The on-foot check and the weapon list follow Joebannerlord's Contextual Wrench and Fire
// (GPL-3.0-or-later); the follow camera's turn follows the Keyboard and mouse mod in openrac-mods
// (GPL), which turns Ratchet & Clank's follow camera the same way. Auto Gadgets is adapted from
// Joebannerlord's Auto Gadgets (GPL-3.0-or-later) and glawks' Auto Swingshot (MIT; see its part).
#include "controls.hpp"
#include "art_dualsense.hpp"
#include "gyro.hpp"
#include "wrench_toss.hpp"

#include "openrac/mod.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <vector>

// The controller gyro reader (GyroDevice) works through Linux's raw HID devices: desktop Linux only
// (Android keeps apps from them). Elsewhere a build has no gyro of its own: OpenRAC's turns the
// game's cameras, and the Modern camera takes the stick and the mouse.
#if defined(__linux__) && !defined(__ANDROID__)
#define DEAD_AIM_GYRO_READER 1
#else
#define DEAD_AIM_GYRO_READER 0
#endif

// The mod's own mouse (older OpenRAC builds) is for a desktop's mouse. On Android, OpenRAC gives
// screen touches as the mouse: capturing it would make every touch a click and take touches from
// OpenRAC's touch buttons and camera, so there the mod leaves the mouse alone.
#if defined(__ANDROID__)
#define DEAD_AIM_OWN_MOUSE 0
#else
#define DEAD_AIM_OWN_MOUSE 1
#endif

#if DEAD_AIM_GYRO_READER
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {

using openrac::gaddr;

// ---------------------------------------------------------------------------------------------
// The game
// ---------------------------------------------------------------------------------------------

// The two USA discs: v1.01 (OpenRAC's "ntsc") and v2.00 ("ntsc2"). Set at load.
bool g_v200 = false;

// A global at its address on each disc.
struct ByDisc {
    gaddr v101, v200;
};

gaddr on_disc(ByDisc a) { return g_v200 ? a.v200 : a.v101; }

// A global of the executable (relocate) or of level 0's program (relocate_level), for the code
// loaded now.
gaddr boot_global(const openrac::Mod& mod, ByDisc a) { return mod.relocate(on_disc(a)); }
gaddr level0_global(const openrac::Mod& mod, ByDisc a) { return mod.relocate_level(0, on_disc(a)); }

// A function of each level's program, by level (0 to 30; 0 where a level has none): the v1.01 list,
// then the v2.00 one.
constexpr int kLevels = 31;
struct PerLevel {
    gaddr v101[kLevels], v200[kLevels];
};

// The hero record (D_00189E20, the same on USA v1.01 and v2.00) and the fields read of it.
constexpr gaddr kHero = 0x00189E20;
constexpr gaddr kHeroPosition = kHero + 0x80;    // f32[3]
constexpr gaddr kHeroFacing = kHero + 0x98;      // f32: the way he faces, radians (rot.z)
constexpr gaddr kHeroAimPitch = kHero + 0x774;   // f32: aiming, how far he looks down, radians (< 0 up)
constexpr gaddr kHeroAimYaw = kHero + 0x778;     // f32: aiming, the way he looks, radians
// Aiming (the Aim button held, state 29): the right stick turns his facing and moves the aim
// pitch at a rate (seen by holding them in play); the follow camera stands still.
constexpr std::int32_t kAimState = 29;
// The follow camera's tilt (its state +0x1C8, -1..1; +0x1CC how fast it moves): what the right
// stick's y drives toward (seen by holding the stick).
constexpr gaddr kStateTilt = 0x1C8, kStateTiltSpeed = 0x1CC;
constexpr gaddr kHeroItem = kHero + 0x1248;      // s32: the item in hand
constexpr gaddr kHeroVehicle = kHero + 0x1830;   // u32: the vehicle he rides, 0 on foot
constexpr gaddr kHeroMoby = kHero + 0x2290;      // u32: his moby
constexpr gaddr kHeroState = kHero + 0x2294;     // s32: his state
constexpr std::int32_t kRemoteState = 0x1C;      // his state steering a thrown Spiderbot or Visibomb (Circle ends it)
constexpr gaddr kHeroGroup = kHero + 0x229C;     // s32: his state's group
constexpr gaddr kHeroNoUpdate = kHero + 0x22BD;  // u8: his update stopped
constexpr gaddr kHeroHealth = kHero + 0x24CC;    // s32
constexpr gaddr kHeroForm = kHero + 0x22B4;      // u8: his form: 0 Ratchet, 1 Clank, 2 Giant Clank, 4 the Hypnomatic's robot
// The resident pad 0 (g_pad_state, D_00138180 on both USA discs): its mode, non-zero while a
// scene or a menu masks the pad.
constexpr gaddr kPadMode = 0x00138180 + 0x1CC;
constexpr gaddr kPadHeld = 0x00138180 + 0x1A0;  // u32: the buttons the game saw held last poll
// The moby table's first and end pointers (+0, +8), the executable's; its records, 0x100 bytes each.
constexpr ByDisc kMobyTable{0x001B1B5C, 0x001B1ADC};
constexpr gaddr kMobyRecord = 0x100;
constexpr gaddr kMobyClass = 0xAA;  // s16 of a moby
constexpr gaddr kMobyVars = 0x68;   // u32 of a moby: its own vars
constexpr int kClankClass = 87;     // the hero's moby while the player is Clank
// The Hypnomatic's robot (form 4; docs/notes/rac2-robot-and-turret-controls.md): its body is the hero
// moby; the attack robot (Damosel, Grelbin) fires with Square, the Receiver Bot has no attack and
// its Circle is the Hypnomatic's beam.
constexpr std::uint8_t kRobotForm = 4;
constexpr std::int16_t kAttackRobotClass = 0xEC0;

// The groups of the hero's states (hero +0x229C; docs/notes/rac2-special-situations.md). On foot:
// standing, moving, jumping, crouching, landing; the player's buttons are the mod's.
bool foot_group(std::int32_t group) {
    return group == 0 || group == 1 || group == 2 || group == 4 || group == 0xC;
}

// A move of his own begun on foot (ledges 3, the hover 5, melee and throws 6, 7, 8, 10, the
// swingshot 13, 14, 33, grinding 15, 16, deadly liquid 25, 26, charge boots 28, the Levitator 30):
// the buttons still the player's mapping (a held Jump keeps hovering), no strafe.
bool move_group(std::int32_t group) {
    switch (group) {
        case 3: case 5: case 6: case 7: case 8: case 10: case 13: case 14: case 15: case 16:
        case 25: case 26: case 28: case 30: case 33: return true;
        default: return false;
    }
}

// The swingshot and grinding: the game steers there, and aims for him.
bool swing_or_grind(std::int32_t group) {
    return group == 13 || group == 14 || group == 33 || group == 15 || group == 16;
}

// Scripted walks (to a vendor, a door): on foot by group, but the game's.
bool scripted_state(std::int32_t state) {
    return state == 0x62 || state == 0x63 || state == 0x64;
}

// Levels whose controls are their own: space combat (Feltzin 5, Hrugis 10, Gorn 15, Wupash 25)
// and Giant Clank's fights in orbit (Dobbo 22, Damosel 23).
bool raw_level(int level) {
    return level == 5 || level == 10 || level == 15 || level == 25 || level == 22 || level == 23;
}

// The weapons by item number (Contextual Wrench and Fire's is_weapon): not the wrench (10) or a
// gadget.
constexpr bool is_weapon(int item) {
    return (item >= 22 && item <= 32) || (item >= 41 && item <= 45) || item == 12 || item == 14
        || item == 16 || item == 17 || item == 18 || item == 37 || item == 53 || item == 9;
}

// The follow camera (rac2-decomp src/levels/shared/L00_004224D0*.c). g_follow_camera_object, a
// level 0 program global, points at it: its rows (+0x00 forward, +0x10 right, +0x20 up), its eye
// (+0x30), its state (+0x70). USA v1.01 has the pointer at D_L00_001B8E10; USA v2.00 0x80 below
// (found by its shape, checked in play). The state (CameraVars; its reset writes these): +0x00 and
// +0x210 copies of the eye, +0xB0 the pivot behind the hero the orbit is measured from, +0x160 the
// orbit (eye - pivot) and +0x170 its smoothed copy.
constexpr ByDisc kFollowCamera{0x001B8E10, 0x001B8E10 - 0x80};
constexpr gaddr kCamEye = 0x30;
constexpr gaddr kCamState = 0x70;
constexpr gaddr kCamType = 0x86;  // s16: 0 the follow camera, 4 the aim camera
constexpr std::int16_t kAimCameraType = 4;
// The camera block's phase, an s16 0x100 above the slot: not 0 while another camera blends in or out.
constexpr gaddr kPhaseAboveSlot = 0x100;
constexpr gaddr kStatePivot = 0xB0;
constexpr gaddr kStateOrbits[] = {0x160, 0x170};
constexpr gaddr kStateEyes[] = {0x00, 0x210};

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;
constexpr int kRightStickX = 2, kRightStickY = 3;

// An angle brought into -pi..pi.
float wrap_pi(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

// ---------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------

// Each choice setting's values and names, as the manifest has them (the menu capitalises each word
// of a name), in the manifest's order.
enum class Lock { Off = 0, Weapon = 1, Always = 2 };
constexpr const char* kLockValues[] = {"off", "weapon", "always"};
constexpr const char* kLockNames[] = {"Off", "With weapon", "Always"};

constexpr const char* kStyleValues[] = {"modern", "classic"};
constexpr const char* kStyleNames[] = {"Modern", "Classic"};

// Camera Speed: the game's own (Options > Camera), or Deadlocked's speeds by name: its own range
// (its option 0..64, x lerp(0.25, 1.25, option / 64); "Normal" its default, 50).
// Vertical Sensitivity: the vertical speed as a share of the horizontal, each game's (its stick's top
// tilt over its top turn: Deadlocked 2 over 3 degrees a tick, Up Your Arsenal 2 over 1.5), or the
// player's own.
constexpr const char* kVerticalValues[] = {"deadlocked", "uya", "custom"};
constexpr const char* kVerticalNames[] = {"Deadlocked", "Up Your Arsenal", "Custom"};
constexpr float kVerticalShare[] = {0.0349f / 0.05236f, 0.0349f / 0.02618f};
constexpr int kVerticalCustomLowest = 3, kVerticalCustomHighest = 20;  // tenths: 30 to 200 percent

// Aim Control: Free Movement (Aim switches view and he keeps moving) or Standard (the game's aim).
constexpr const char* kAimValues[] = {"free", "standard"};
constexpr const char* kAimNames[] = {"Free Movement", "Standard"};
constexpr const char* kCameraSpeedValues[] = {"slowest", "slower", "slow", "normal", "fast"};
constexpr const char* kCameraSpeedNames[] = {"Slowest", "Slower", "Slow", "Normal", "Fast"};
constexpr float kCameraSpeedScale[] = {0.25f, 0.5f, 0.75f, 1.03f, 1.25f};

constexpr const char* kAssistValues[] = {"off", "deadlocked", "uya"};
constexpr const char* kAssistNames[] = {"Off", "Deadlocked", "Up Your Arsenal"};

// Auto Gadgets (see its part, further down).
enum class AutoTier { Off, Swingshot, All };
// Crouch Behavior: Standard, the game's own; Deadlocked's (held on the run it never stops him); Joe Wrench
// (Deadlocked's, and the wrench tossed in the air too, as Joebannerlord's Contextual Wrench throws it).
enum class CrouchMode { Standard = 0, Deadlocked = 1, JoeWrench = 2 };
constexpr const char* kCrouchValues[] = {"game", "deadlocked", "joe"};
constexpr const char* kCrouchNames[] = {"Standard", "Deadlocked", "Joe Wrench"};
constexpr const char* kAutoValues[] = {"off", "swingshot", "all"};
constexpr const char* kAutoNames[] = {"Off", "Swingshot", "All"};

constexpr const char* kPageName = "Dead Aim";

struct V3 { float x, y, z; };
V3 read_v3(const openrac::Memory& m, gaddr a) { return {m.read<float>(a), m.read<float>(a + 4), m.read<float>(a + 8)}; }
void write_v3(const openrac::Memory& m, gaddr a, V3 v) { m.write<float>(a, v.x); m.write<float>(a + 4, v.y); m.write<float>(a + 8, v.z); }

namespace vec {
inline V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
}  // namespace vec
inline float length(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline V3 unit(V3 a, V3 fallback) {
    const float l = length(a);
    return l > 1e-6f ? V3{a.x / l, a.y / l, a.z / l} : fallback;
}

// A frame about an up (the world's, or a planetoid's): forward, side, up. The turn that takes
// straight up to `u` applied to x and y, so on level ground it is the world's own.
struct Frame {
    V3 f, s, u;
    V3 to_local(V3 v) const { return {vec::dot(v, f), vec::dot(v, s), vec::dot(v, u)}; }
    V3 to_world(V3 l) const {
        return {f.x * l.x + s.x * l.y + u.x * l.z, f.y * l.x + s.y * l.y + u.y * l.z, f.z * l.x + s.z * l.y + u.z * l.z};
    }
};
Frame frame_about(V3 u) {
    if (u.z < -0.999f) return {{1, 0, 0}, {0, -1, 0}, u};  // upside down: half a turn about x
    const float k = 1.0f / (1.0f + u.z);
    return {{1.0f - u.x * u.x * k, -u.x * u.y * k, -u.x}, {-u.x * u.y * k, 1.0f - u.y * u.y * k, -u.y}, u};
}

enum class Gadget { None, Swingshot, Dynamo, Tractor, Thermanator, Hypnomatic };
enum class AutoPhase { Idle, Equipping, Replay, Active, Capture, Canceling, Restoring };

struct Auto {
    AutoTier tier = AutoTier::Off;
    AutoPhase phase = AutoPhase::Idle;
    Gadget gadget = Gadget::None;  // the gadget the mod put in hand
    gaddr target = 0;              // its target (the Dynamo's, the Tractor Beam's, ...)
    bool frozen = false;           // the Thermanator's target frozen (a change of it ends the freeze)
    bool listed = true;            // the game's own search has the target too (Oozla's boxes wait for it)
    bool used = false;             // the Thermanator fired (its meter shown a while before it goes)
    bool saw_robot = false;        // the Hypnomatic's capture: control went to the robot
    int previous = 0;              // the item in hand before
    std::int32_t menu_choice = 0;  // the menu's choice before (put back for the wrench)
    int ticks = 0;                 // in this phase
    int rest = 0;                  // on foot with Fire up (the Swingshot)
    bool fire_was = true;          // Fire held in the last report (held at load is no press)
    bool swallow = false;          // Fire kept from the game until it is let go
    bool fire_after = false;       // Fire held through the restore: the weapon fires once it is back
    std::uint32_t loads = 0;
    bool others_checked = false;   // the loaded mods looked through once
    bool deferred = false;         // Joebannerlord's Auto Gadgets is loaded: Circle is its
};
Auto g_auto;

struct State {
    openrac::Mod* mod = nullptr;
    strafe::Config config;
    Lock lock = Lock::Weapon;
    CrouchMode crouch = CrouchMode::Deadlocked;  // crouch (strafe::RunCrouch, the toss)
    strafe::RunCrouch run_crouch;

    bool has_page = false;
    strafe::Intent intent = strafe::Intent::Unknown;
    std::chrono::steady_clock::time_point intent_at{};

    // The camera.
    gaddr camera = 0;
    std::uint32_t camera_loads = 0xFFFFFFFF;
    bool modern = true;        // camera_style: the boom camera for every input (the gun aims where it looks)
    // The centre reticle's size, first and third person (not the target markers); 0 none of the
    // mod's (crosshair_shown).
    float crosshair_size = 0.3f;
    std::vector<gaddr> blades;  // the Multi-Star's blades seen so far
    int blink = 0;  // the Pulse Rifle / Vaporizer overlay's counter (0..20)
    bool stick_owned = false;  // the mod reads the right stick in third person (the game sees it at rest)
    float stick_x = 0, stick_y = 0;  // the right stick, -1..1 (x right, y down)
    float stick_yaw_rate = 0, stick_pitch_rate = 0;  // radians a tick
    int camera_speed = 3;           // Camera Speed: one of Deadlocked's (kCameraSpeedScale), Normal by default
    int vertical = 0;             // Vertical Sensitivity: 0 Deadlocked's, 1 Up Your Arsenal's, 2 Custom
    float vertical_custom = 0.7f; // Custom Sensitivity: the vertical speed as a share of the horizontal
    int quick_ticks = 0;            // ticks the stick has been held right over (Deadlocked's quick turn)
    int assist_style = 1;           // Aim Assist: 0 off, 1 Deadlocked's, 2 Up Your Arsenal's
    float assist = 0;               // its weight now (0..1): the stick slowed, the view drawn along
    gaddr assist_target = 0;        // the enemy it has
    V3 assist_rel{0, 0, 0};         // where that enemy was from him last tick
    std::uint32_t assist_loads = 0xFFFFFFFF;  // the level's moby arrays that enemy is of
    bool left_live = false;         // the left stick off centre (the assist's tracking runs while a stick moves)
    std::uint32_t frame_no = 0;     // frames of the mod's own (frame()), to tell consecutive ticks
    std::uint32_t feet_frame = 0;   // the frame align_view last saw his feet
    V3 hero_seen{0, 0, 0};          // where he was last frame (seen every frame, the boom's or not)
    std::uint32_t hero_seen_frame = 0;
    V3 hero_step{0, 0, 0};          // how far he went in the last tick
    int fp_ease = 0;                // Free Movement's ease in: ticks so far (0 none)
    float fp_ease_from = 0;         // the boom's length when it began
    bool boom_owned = false;
    float boom_lift = 0, boom_length = 0;
    int boom_clear = 0;
    bool aim_held = false;  // the player's Aim button this tick
    bool boom_fresh = false;
    float boom_yaw = 0, boom_turn = 0, boom_yaw_written = 0;  // in the up's frame (the world's on level ground)
    // The boom's heading and the one last written, as directions in the world: carried along the
    // ground as the up turns (a planetoid's), so the frame's own turning never turns the view.
    V3 boom_heading{1, 0, 0}, boom_heading_written{1, 0, 0};
    std::uint8_t page_buttons = 0;
    bool aim_input = false;  // the game's aim button held in the last report
    // Aim with movement (Modern, Aim Control: Free Movement): first person while the aim button is held.
    bool aim_move = true;
    bool fp_want = false;      // the aim button held, on foot (from the pad)
    bool fp_active = false;    // the mod turned first person on
    bool tp_active = false;    // the mod turned the player's own first person off (Aim held)
    int faded = 0x80;          // the alpha the boom last gave Ratchet (0x80 his own; -1 not known, new mobys)
    std::uint32_t faded_loads = 0xFFFFFFFF;  // the level's moby arrays it was given to
    V3 feet_last{0, 0, 0};     // the follow camera's feet point last tick (align_view)
    bool feet_valid = false;
    gaddr backpack = 0;        // Clank on his back (faded with him), found once a level
    std::uint32_t backpack_loads = 0xFFFFFFFF;
    bool in_turret = false;    // manning a turret (frame), for the pad's Fire
    float last_want = 1e9f;    // the room behind him the boom found last tick
    float aim_zoom = 1.0f;     // the boom's length scale, eased in while aiming on a planetoid
    bool fp_refused = false;   // the game did not take it: the game's aim this press
    int fp_wait = 0;
    std::int32_t fp_saved = 0;  // the player's own first-person setting, put back after
    int fp_grace = 0;          // ticks after leaving it: the boom keeps the view's direction
    float boom_speed = 0.0f;   // the ease out's speed, u a tick (0: not easing)
    float anchor_room = 1.0f;
    float want_ring[30] = {};  // the boom's want, the last kWantHold ticks
    int want_at = 0;
    int moby_side = 0;         // the camera in a moby: -1 put in front of it, 1 behind, 0 none
    gaddr moby_in = 0;
    float boom_inside = 0;     // this tick, the nearest level face along the boom, with no margin
    bool stick_live = false;   // the right stick off centre in the last report (the game's, when it has it)
};

State s;

// Crosshair not Off: the centre crosshairs are the mod's, in third person, and sized in first.
bool crosshair_shown() { return s.crosshair_size >= 0.05f; }

// The menu's page (see "The menu" below): which is shown, and the new page's tab, cursor and the
// presses it takes in its next frame.
int g_shown = -2;       // the page shown (-1 the hub or the new page, a tab's text page, -2 none)
bool g_fancy = false;   // the new page drawn (its overlay taken), else the text pages (older OpenRAC: text pages)
int g_tab = 0;          // the tab shown on it
int g_cursor = 0;       // the row under the game's cursor
bool g_asking = false;  // the game's question is up (drawn by the game: the page stands aside)
int g_tab_step = 0;     // L1 / R1 pressed on the page
bool g_square = false;  // Square pressed on the page

// OpenRAC's own mouse and gyro settings (its built-in keyboard-mouse mod): see kbm_refresh.
struct Kbm {
    float sens_x = 1.0f, sens_y = 1.0f;
    bool invert_y = false, capture = true;
    std::string gyro = "off";  // off, always, aiming
    float gyro_x = 2.0f, gyro_y = 2.0f;
    bool gyro_invert_x = false, gyro_invert_y = false;
    std::string gyro_space = "player";
    float gyro_dead_zone = 0.0f, gyro_smoothing = 3.0f;  // degrees a second, as its settings give them
    bool gyro_calibrate = true;
    std::string gyro_button = "none", gyro_button_action = "enable";
    std::string strafe_bind = "ShiftLeft", r2_bind = "KeyR,MouseForward";  // L2's and R2's keys
};
Kbm g_kbm;
constexpr float kRadiansPerPixel = 0.0015f;  // OpenRAC's mouse: a pixel's turn at sensitivity 1 (kbm::DirectLook)
bool kbm_gyro_on() { return g_kbm.gyro == "always" || g_kbm.gyro == "aiming"; }

// OpenRAC's gyro settings as its filter takes them (its keyboard-mouse core.hpp, read_gyro).
gyro::GyroOptions kbm_gyro_options() {
    gyro::GyroOptions o;
    o.enabled = kbm_gyro_on();
    o.space = gyro::gyro_space_from(g_kbm.gyro_space.c_str());
    o.sensitivity_x = g_kbm.gyro_x;
    o.sensitivity_y = g_kbm.gyro_y;
    o.invert_x = g_kbm.gyro_invert_x;
    o.invert_y = g_kbm.gyro_invert_y;
    o.dead_zone = g_kbm.gyro_dead_zone * 0.017453292f;
    o.smoothing = g_kbm.gyro_smoothing * 0.017453292f;
    o.auto_calibrate = g_kbm.gyro_calibrate;
    o.button = gyro::gyro_button_of(g_kbm.gyro_button);
    o.button_action = gyro::gyro_button_action_from(g_kbm.gyro_button_action.c_str());
    return o;
}

// Whether this OpenRAC has the built-in keyboard, mouse and gyro (its mod listed, with a mouse and a
// gyro setting): found once (kbm_refresh).
bool g_builtin_input = false;
bool g_builtin_known = false;

// Who turns the camera from the mouse and the gyro: OpenRAC's built-in where the build has one,
// else this mod's own mouse buttons, mouse camera and gyro (older OpenRAC), for builds before
// OpenRAC had them. Once those are dropped, always OpenRAC's.
bool openrac_input() {
    return g_builtin_input;
}

// What the mouse and gyro ask of the view this tick, in radians: a turn (> 0 left) and a pitch
// (> 0 up).
struct Look {
    float yaw = 0, pitch = 0;
};

// The mod's own mouse and gyro (older OpenRAC): their part, "Older OpenRAC: the mod's own mouse and
// gyro", further down.
void own_read_config(const openrac::Mod& mod);
void own_restore_defaults();
void own_reset();
void own_capture(const openrac::Mod& mod, bool playing);
gyro::GyroOptions own_gyro_options();
std::uint32_t own_mouse_buttons(const openrac::Mod& mod);
void own_pad_stick(std::uint8_t* report, int size);
Look own_look(const openrac::Input& input, bool gyro_read, Look gyro);
void own_game_camera(const openrac::Mod& mod, const openrac::Memory& memory, gaddr camera, bool here, bool fp,
                     bool aiming, bool boom_view, bool turret_aim, Look look);

// A setting told to the launcher (a change on the page, or the defaults). A value the port refuses
// is logged, once: the page then shows it for this run only.
bool g_told_refused = false;
template <class T>
void save_setting(const char* key, const T& value) {
    if (!s.mod->set_setting(key, value) && !g_told_refused) {
        s.mod->warn(std::string("OpenRAC refused a value of ") + key + ": the page keeps it for this run only");
        g_told_refused = true;
    }
}

// The index of `value` among a setting's values, or `fallback` for none of them.
template <std::size_t N>
int index_of(const char* const (&values)[N], const std::string& value, int fallback) {
    for (std::size_t i = 0; i < N; ++i) {
        if (value == values[i]) return static_cast<int>(i);
    }
    return fallback;
}

void set_modern(bool modern);

void read_config(const openrac::Mod& mod) {
    s.lock = static_cast<Lock>(index_of(kLockValues, mod.setting<std::string>("lock_mode"), static_cast<int>(Lock::Weapon)));
    for (int i = 0; i < strafe::kShoulderCount; ++i) {
        s.config.actions[i] = strafe::action_from_value(mod.setting<std::string>(strafe::kShoulderInfo[i].key));
    }
    s.crouch = static_cast<CrouchMode>(index_of(kCrouchValues, mod.setting<std::string>("crouch"), 1));
    g_auto.tier =
        static_cast<AutoTier>(index_of(kAutoValues, mod.setting<std::string>("auto_gadgets"), static_cast<int>(AutoTier::All)));
    const bool modern = index_of(kStyleValues, mod.setting<std::string>("camera_style"), 0) == 0;
    if (modern != s.modern) set_modern(modern);
    s.crosshair_size = std::clamp(static_cast<float>(mod.setting<double>("crosshair_size")), 0.0f, 1.5f);
    s.aim_move = index_of(kAimValues, mod.setting<std::string>("aim_control"), 0) == 0;
    s.camera_speed = index_of(kCameraSpeedValues, mod.setting<std::string>("camera_speed"), 3);
    s.vertical = index_of(kVerticalValues, mod.setting<std::string>("vertical_sensitivity"), 0);
    s.vertical_custom = std::clamp(static_cast<float>(mod.setting<double>("vertical_sensitivity_custom")), 0.3f, 2.0f);
    s.assist_style = index_of(kAssistValues, mod.setting<std::string>("aim_assist"), 1);
    own_read_config(mod);  // (older OpenRAC)
}

// ---------------------------------------------------------------------------------------------
// Game calls: the game's memory and functions the parts below share
// ---------------------------------------------------------------------------------------------

// The game's RAM: 32 MiB, the executable's from 0x00100000 (the kernel's below it).
constexpr gaddr kRamStart = 0x00100000, kRamEnd = 0x02000000;
bool in_ram(gaddr a) { return a >= kRamStart && a < kRamEnd; }

// A moby that is there: in RAM, on a record's alignment, and not being taken out (its state, +0x20,
// below 0x80).
bool moby_live(const openrac::Memory& memory, gaddr m) {
    return in_ram(m) && (m & 0xF) == 0 && memory.read<std::uint8_t>(m + 0x20) < 0x80;
}

// A moby's kind, from its class's row (+0x24; the row's +0x46): 5 an enemy, 18 a freezable pool's
// grid, 21 a Tractor Beam target.
int moby_kind(const openrac::Memory& memory, gaddr m) {
    const gaddr row = memory.read<gaddr>(m + 0x24);
    return in_ram(row) ? memory.read<std::int16_t>(row + 0x46) : -1;
}

// The moby table's records, from its first pointer to its end pointer (+0, +8), one a kMobyRecord;
// none while it is not set up: for (const gaddr m : moby_table(mod, memory)).
struct Mobys {
    struct At {
        gaddr m;
        gaddr operator*() const { return m; }
        At& operator++() {
            m += kMobyRecord;
            return *this;
        }
        bool operator!=(const At& other) const { return m != other.m; }
    };
    gaddr first = 0, last = 0;  // the first record, and the one past the last whole one
    At begin() const { return {first}; }
    At end() const { return {last}; }
};
constexpr gaddr kMobysMost = 8192;  // a table of more records than this is not set up

Mobys moby_table(const openrac::Mod& mod, const openrac::Memory& memory) {
    const gaddr table = boot_global(mod, kMobyTable);
    const gaddr first = memory.read<gaddr>(table), end = memory.read<gaddr>(table + 8);
    if (!in_ram(first) || end <= first || end >= kRamEnd || (end - first) / kMobyRecord > kMobysMost) return {};
    return {first, first + (end - first) / kMobyRecord * kMobyRecord};
}

// The game's own collision line (CollLine: from, to, the mask, a moby to pass over, a hit event),
// as the follow camera asks it (docs/notes/rac2-spring-arm.md): each level's program has its copy,
// at these addresses (0 none); it reads its two points as 16-byte vectors (so they go on the
// game's stack) and leaves the nearest hit in that program's collision block (+0x20 the point,
// +0x40 the face's normal), whose address its own first words build (lui / addiu at +0x14, +0x18).
// The SDK's collision_line answers nothing on Going Commando.
using CollLineFn = int (*)(gaddr, gaddr, std::uint32_t, gaddr, gaddr);
constexpr PerLevel kCollLine{
    {0x2DE4B0, 0x2D3C80, 0x2E19C8, 0x2DF038, 0x2FED10, 0x2F6C50, 0x31DB98, 0x2D4510, 0x2EA300, 0x2DA5C0,
     0x2F9150, 0x2F0318, 0x2E1BC8, 0x2E8FA0, 0x2E5DB0, 0x2F7250, 0x2D7790, 0x2DF570, 0x2F8D60, 0x2DBF58,
     0x2F24B8, 0, 0x2E4708, 0x2E8DE8, 0x2DD4D0, 0x2F15D0, 0x2DEB50, 0, 0, 0,
     0x2E5368},
    {0x2DE878, 0x2D3FE8, 0x2E1E10, 0x2DF4F8, 0x2FF080, 0x2F7018, 0x31E060, 0x2D4778, 0x2EA670, 0x2DA8A8,
     0x2F9518, 0x2F0678, 0x2E1E20, 0x2E93E0, 0x2E61F8, 0x2F7618, 0x2D7B50, 0x2DF9B0, 0x2F9110, 0x2DC1B8,
     0x2F2878, 0, 0x2E4AD0, 0x2E91B0, 0x2DD898, 0x2F1998, 0x2DEF98, 0, 0, 0,
     0x2E5630}};
// v2.00's collision blocks, for a CollLine whose first words do not give it.
constexpr gaddr kCollBlockV200[kLevels] = {
    0x1BF180, 0x1BF900, 0x1C0940, 0x1BFDC0, 0x1BFB00, 0x1BFA80, 0x1BFC80, 0x1BF5C0, 0x1BFF40, 0x1BFDC0,
    0x1BFA40, 0x1C0D80, 0x1C01C0, 0x1BFC80, 0x1BF8C0, 0x1BFB80, 0x1BFEC0, 0x1C0000, 0x1BFB80, 0x1BFE00,
    0x1C0380, 0, 0x1C0040, 0x1C0A00, 0x1BEA80, 0x1BF5C0, 0x1BEF40, 0, 0, 0,
    0};
constexpr ByDisc kCamLineMask{0x1B184C, 0x1B17CC};  // the camera's line mask (level 0's program)
// The mask the game's own camera sweeps its arm with (its "point" mask; the line mask above skips
// some faces, which let the boom through a wall: docs/notes/rac2-camera-situations-2.md).
constexpr ByDisc kCamPointMask{0x1B1850, 0x1B17D0};

// This level's copy of one of the game's functions, or 0 (none in this level, or its program not in).
gaddr level_function(const openrac::Mod& mod, const PerLevel& table) {
    const int level = mod.play_status().level;
    if (level < 0 || level >= kLevels || mod.loaded_level() != level) return 0;
    return (g_v200 ? table.v200 : table.v101)[level];
}

// The level program's CollLine, or 0.
gaddr coll_line_function(const openrac::Mod& mod) { return level_function(mod, kCollLine); }

// The level program's collision block, from CollLine's own first words (lui / addiu at +0x14, +0x18).
gaddr collision_block(const openrac::Mod& mod, const openrac::Memory& memory) {
    const gaddr at = coll_line_function(mod);
    if (at == 0) return 0;
    const std::uint32_t hi = memory.read<std::uint32_t>(at + 0x14), lo = memory.read<std::uint32_t>(at + 0x18);
    if ((hi >> 26) == 0x0F && (lo >> 26) == 0x09) {
        return ((hi & 0xFFFFu) << 16) + static_cast<gaddr>(static_cast<std::int32_t>(static_cast<std::int16_t>(lo & 0xFFFFu)));
    }
    return g_v200 ? kCollBlockV200[mod.play_status().level] : 0;
}

// The level program's collision result (+0x18..+0x7F), kept round a call of the game's that runs
// collision of its own, for the game's code that reads the last result after (Joe's
// PreserveCollision).
class KeepCollision {
public:
    KeepCollision(const openrac::Mod& mod, const openrac::Memory& memory) : memory_(memory), block_(collision_block(mod, memory)) {
        for (int i = 0; block_ != 0 && i < kWords; ++i) saved_[i] = memory_.read<std::uint32_t>(block_ + 0x18 + 4 * i);
    }
    ~KeepCollision() {
        for (int i = 0; block_ != 0 && i < kWords; ++i) memory_.write<std::uint32_t>(block_ + 0x18 + 4 * i, saved_[i]);
    }
    KeepCollision(const KeepCollision&) = delete;
    KeepCollision& operator=(const KeepCollision&) = delete;
    bool ok() const { return block_ != 0; }

private:
    static constexpr int kWords = 0x68 / 4;
    openrac::Memory memory_;
    gaddr block_;
    std::uint32_t saved_[kWords] = {};
};

struct LineHit {
    bool hit = false;
    gaddr moby = 0;
    float point[3] = {0, 0, 0}, normal[3] = {0, 0, 0};
};

// The game's collision line from `from` to `to`, by the camera's line mask (or `mask_override`),
// passing over the hero's moby: false when it could not be asked.
bool game_line(const openrac::Mod& mod, const openrac::Memory& memory, const float from[3], const float to[3], LineHit& out,
               std::uint32_t mask_override = 0) {
    out = LineHit{};
    const gaddr at = coll_line_function(mod);
    const auto line = at != 0 ? mod.function_at<CollLineFn>(at) : nullptr;
    if (line == nullptr) return false;
    const gaddr block = collision_block(mod, memory);
    if (block == 0) return false;
    const std::uint32_t mask = mask_override != 0 ? mask_override : memory.read<std::uint32_t>(level0_global(mod, kCamLineMask));
    openrac::StackFrame frame(mod, 0x20);
    const gaddr a = frame.address();
    for (int k = 0; k < 3; ++k) {
        memory.write<float>(a + 4 * k, from[k]);
        memory.write<float>(a + 0x10 + 4 * k, to[k]);
    }
    memory.write<float>(a + 12, 1.0f);
    memory.write<float>(a + 0x1C, 1.0f);
    out.hit = line(a, a + 0x10, mask, memory.read<gaddr>(kHeroMoby), 0) != 0;
    if (out.hit) {
        out.moby = memory.read<gaddr>(block + 0x18);
        for (int k = 0; k < 3; ++k) {
            out.point[k] = memory.read<float>(block + 0x20 + 4 * k);
            out.normal[k] = memory.read<float>(block + 0x40 + 4 * k);
        }
        // The face's normal comes unscaled: made a unit one (none if it is not finite).
        const float n = std::sqrt(out.normal[0] * out.normal[0] + out.normal[1] * out.normal[1] + out.normal[2] * out.normal[2]);
        for (int k = 0; k < 3; ++k) out.normal[k] = (n > 0 && std::isfinite(n)) ? out.normal[k] / n : 0.0f;
    }
    return true;
}

// The camera in use: g_follow_camera_object's slot in level 0's program (see kFollowCamera).
gaddr camera_slot(const openrac::Mod& mod) { return level0_global(mod, kFollowCamera); }

float row_dot(const openrac::Memory& memory, gaddr a, gaddr b) {
    float d = 0;
    for (int k = 0; k < 3; ++k) d += memory.read<float>(a + 4 * k) * memory.read<float>(b + 4 * k);
    return d;
}

// Whether `camera` looks like a camera near the hero: in RAM, its three rows unit length and at
// right angles, its eye within 30 units of him, a state pointer.
bool camera_shaped(const openrac::Memory& memory, gaddr camera) {
    if (!in_ram(camera) || (camera & 0xF) != 0) return false;
    const float f = row_dot(memory, camera, camera), r = row_dot(memory, camera + 0x10, camera + 0x10);
    const float u = row_dot(memory, camera + 0x20, camera + 0x20);
    const float fr = row_dot(memory, camera, camera + 0x10), fu = row_dot(memory, camera, camera + 0x20);
    if (!(std::fabs(f - 1) < 0.05f && std::fabs(r - 1) < 0.05f && std::fabs(u - 1) < 0.05f
          && std::fabs(fr) < 0.05f && std::fabs(fu) < 0.05f)) {
        return false;
    }
    float d2 = 0;
    for (int k = 0; k < 3; ++k) {
        const float e = memory.read<float>(camera + kCamEye + 4 * k) - memory.read<float>(kHeroPosition + 4 * k);
        d2 += e * e;
    }
    return d2 < 30.0f * 30.0f && in_ram(memory.read<gaddr>(camera + kCamState));
}

// The follow camera of this level, 0 while there is none to be seen (a load, a scene).
// The game's camera records (g_camera_slot_table, D_L00_001B91D0 on v1.01; 0x80 lower on v2.00 as
// the slot is; rac2-decomp src/levels/shared/L00_002D7940-setup-g8.c): 48 of 0xA0 bytes, each with
// its camera id at +0x86 (0 the follow camera).
constexpr ByDisc kCameraTable{0x001B91D0, 0x001B91D0 - 0x80};
constexpr int kCameraRecords = 48;
constexpr gaddr kCameraRecordSize = 0xA0;

gaddr find_camera(const openrac::Mod& mod) {
    const auto status = mod.play_status();
    if (status.loads == s.camera_loads && s.camera != 0) return s.camera;
    const auto memory = mod.memory();
    gaddr camera = memory.read<gaddr>(camera_slot(mod));
    // The slot is the camera in use (the game switches it: cam_switch_active). Another camera there
    // (a level that starts in first person, a scene): the follow camera from the game's records.
    if (camera_shaped(memory, camera) && memory.read<std::int16_t>(camera + kCamType) != 0) {
        camera = 0;
        const gaddr table = level0_global(mod, kCameraTable);
        for (int i = 0; i < kCameraRecords && camera == 0; ++i) {
            const gaddr rec = table + static_cast<gaddr>(i) * kCameraRecordSize;
            if (camera_shaped(memory, rec) && memory.read<std::int16_t>(rec + kCamType) == 0) camera = rec;
        }
    }
    if (!camera_shaped(memory, camera)) return 0;
    s.camera = camera;
    s.camera_loads = status.loads;
    return camera;
}

// ---------------------------------------------------------------------------------------------
// The buttons
// ---------------------------------------------------------------------------------------------

// Where the buttons stand: Off outside the hero's play (menus, scenes, the ship and Giant Clank's
// levels, a vehicle, Clank, the pad masked, the hero dead or stopped); Busy in play but in a state
// off foot (the Swingshot's swing, ...); Active on foot.
strafe::Gate gate_now(const openrac::Mod& mod) {
    const auto status = mod.play_status();
    if (status.phase != OPENRAC_PLAY_PLAYING || raw_level(status.level)) {
        return strafe::Gate::Off;
    }
    const auto memory = mod.memory();
    const auto moby = memory.read<gaddr>(kHeroMoby);
    const auto group = memory.read<std::int32_t>(kHeroGroup);
    const auto state = memory.read<std::int32_t>(kHeroState);
    const std::int16_t oclass = in_ram(moby) ? memory.read<std::int16_t>(moby + kMobyClass) : -1;
    // Not Ratchet (Clank, Giant Clank; the Hypnomatic's robot is let through, its buttons mapped in
    // on_pad), riding, dead, held by the game, or in a state of the game's own (swimming and diving,
    // turrets, cranks, vendors, scenes): the game's buttons, as it means them there.
    const std::uint8_t form = memory.read<std::uint8_t>(kHeroForm);
    const bool hard_off = moby == 0 || oclass == kClankClass || (form != 0 && form != kRobotForm)
        || memory.read<std::int32_t>(kHeroHealth) <= 0 || memory.read<gaddr>(kHeroVehicle) != 0
        || memory.read<std::uint8_t>(kHeroNoUpdate) != 0 || memory.read<std::int32_t>(kPadMode) != 0
        || scripted_state(state) || state == 0x61 || !(foot_group(group) || move_group(group));
    const strafe::Gate gate = hard_off ? strafe::Gate::Off : foot_group(group) ? strafe::Gate::Active : strafe::Gate::Busy;
    return gate;
}

// ---------------------------------------------------------------------------------------------
// Auto Gadgets
// ---------------------------------------------------------------------------------------------
//
// Fire at a gadget's target puts the gadget in hand, uses it, and gives the weapon back after.
// Adapted from Joebannerlord's Auto Gadgets (GPL-3.0-or-later,
// https://github.com/Joebannerlord/openRAC-RC2-AutoGadget): the guards for an equip; the gadget
// asked for with Circle held up until it is in hand, then pressed for it; the press kept from the
// game until Fire is let go; the wrench's menu choice put back. Its temporary equip and Circle
// replay came from glawks' Auto Swingshot for Ratchet & Clank (MIT,
// https://github.com/glawks/auto-swingshot; LICENSE-AUTO-SWINGSHOT), as does the Swingshot's rule
// for putting it away (on foot, Fire up a moment). Whether a target is there before the gadget is
// in hand is the game's own search, the level's copy called (docs/notes/rac2-auto-gadgets.md).


constexpr int kWrenchItem = 10;
constexpr std::int16_t kWrenchClass = 71;  // the wrench's moby in his hand
constexpr gaddr kHeroHand = kHero + 0x1220;            // u32: the moby in his hand
constexpr gaddr kHeroHandButton = kHero + 0x1230;      // u32: the button the item in hand answers (0x20 Circle)
constexpr gaddr kHeroMelee = kHero + 0x1239;           // u8: a melee swing; +1 the wrench thrown
constexpr gaddr kHeroHandMode = kHero + 0x1244;        // s32: 2 the item in hand live, 3 being put away
constexpr gaddr kHeroTractorLatch = kHero + 0x5C0;     // u32: what the Tractor Beam holds
constexpr gaddr kHeroGravityMode = kHero + 0x22C3;     // u8: walking on a gravity strip
constexpr gaddr kHeroWrenchForced = kHero + 0x22B7;    // u8: the wrench forced on him (ledges, hurt)
constexpr gaddr kHeroItemBusy = kHero + 0x22B8;        // u8: the item's animation running
constexpr gaddr kHeroBlocked = kHero + 0x22BC;         // u8: equip requests ignored (mid-swing)
constexpr gaddr kHeroTucked = kHero + 0x22BF;          // u8: the item tucked away
constexpr gaddr kHeroRequest = kHero + 0x22C8;         // s32: the item asked for (ApplyHeroEquipmentRequest)
constexpr gaddr kHeroChosen = kHero + 0x22E4;          // s32: the item chosen for the hand
// g_menu_choice_table[0] (s32, the same on both discs): the item Circle draws from the wrench.
constexpr gaddr kMenuChoice = 0x001A7398;
constexpr ByDisc kOwned{0x001A7AF8, 0x001A7B00};       // u8 an item: owned
constexpr int kAutoTimeout = 100;  // ticks for the game to answer an equip (Joe's and glawks')
constexpr int kSwingRest = 15;     // ticks on foot with Fire up before the Swingshot goes away (glawks')
constexpr int kMeterTicks = 180;   // the Thermanator stays out after a freeze, its meter read (Joe's 3 s)
constexpr int kCaptureTicks = 300; // the Hypnomatic's capture: the robot's control expected within
constexpr gaddr kHeroHandAge = kHero + 0x1240;         // s32: ticks the item in hand has been live
constexpr gaddr kHeroAttachment = kHero + 0xC80;       // f32[4]: his hand's attachment point
constexpr gaddr kHeroLockTarget = kHero + 0x1450;      // u32: the lock-on target (the Hypnomatic's search returns it first)
constexpr gaddr kHeroCaptureTimer = kHero + 0x1E0;     // s16: set as the Hypnomatic's beam takes a robot
// The camera block's eye (+0x140) and follow matrix (+0x370), level 0's program (the Hypnomatic aims by them).
constexpr ByDisc kCameraBlock{0x001B8C80, 0x001B8C00};

// The Swingshot's and the Hypnomatic's mobys, in his hand.
constexpr std::int16_t kSwingshotClass = 0xD0, kHypnomaticClass = 0x950;

struct GadgetInfo {
    const char* name;
    int item;
    std::int16_t hand;  // its moby's class, in his hand
    AutoTier tier;      // the setting that has it
};
constexpr GadgetInfo kGadgets[] = {
    {"", 0, 0, AutoTier::Off},
    {"Swingshot", 13, kSwingshotClass, AutoTier::Swingshot},
    {"Dynamo", 36, 0x825, AutoTier::All},
    {"Tractor Beam", 46, 0xBC, AutoTier::All},
    {"Thermanator", 39, 0xFB0, AutoTier::All},
    {"Hypnomatic", 55, kHypnomaticClass, AutoTier::All},
};
const GadgetInfo& gadget_info(Gadget g) { return kGadgets[static_cast<int>(g)]; }

// The Swingshot's targets, as the game's three finders leave them (> 0 one in reach): a swing node
// (class 0x2F6), a node that pulls him to it (0x323), a blue target (0x99D).
constexpr gaddr kSwingNodeReady = kHero + 0xAA8;   // s32
constexpr gaddr kPullNodeReady = kHero + 0xADE;    // s16
constexpr gaddr kBlueTargetReady = kHero + 0xB14;  // s32
// The game's searches, each level's copy (config/levels/functions.tsv; v2.00's matched by
// fingerprint): the Swingshot's three finders, the Dynamo's (FindNearestEligibleTarget), the
// Tractor Beam's (SelectBestEligibleTarget) and their scorer (hero_score_target_by_heading).
constexpr PerLevel kSwingFinder{
    {0x2C3E18, 0, 0x2C7298, 0x2C6A40, 0x2E7740, 0x2DC698, 0x303598, 0x2BE2A8,
     0x2D2A10, 0x2C30B8, 0x2DEB98, 0x2D8A30, 0x2C8708, 0x2CEA38, 0x2CB718, 0x2DCC98,
     0x2C0700, 0x2C4FB8, 0x2DFDB8, 0x2C4030, 0x2D7268, 0, 0x2C93D0, 0x2CDAB0,
     0x2C2F18, 0x2D7018, 0x2C4598, 0, 0, 0, 0x2CAC18},
    {0x2C4098, 0, 0x2C7598, 0x2C6DC0, 0x2E7A60, 0x2DC918, 0x303918, 0x2BE4C0,
     0x2D2D30, 0x2C3350, 0x2DEE18, 0x2D8D40, 0x2C8910, 0x2CED30, 0x2CBA18, 0x2DCF18,
     0x2C0988, 0x2C52B0, 0x2E0030, 0x2C4240, 0x2D74E8, 0, 0x2C9658, 0x2CDD38,
     0x2C3198, 0x2D7298, 0x2C4898, 0, 0, 0, 0x2CAD98}};
constexpr PerLevel kPullFinder{
    {0x2C3BB0, 0, 0x2C7030, 0x2C67D8, 0x2E74D8, 0x2DC430, 0x303330, 0x2BE040,
     0x2D27A8, 0x2C2E50, 0x2DE930, 0x2D87C8, 0x2C84A0, 0x2CE7D0, 0x2CB4B0, 0x2DCA30,
     0x2C0498, 0x2C4D50, 0x2DFB50, 0x2C3DC8, 0x2D7000, 0, 0x2C9168, 0x2CD848,
     0x2C2CB0, 0x2D6DB0, 0x2C4330, 0, 0, 0, 0x2CA9B0},
    {0x2C3E30, 0, 0x2C7330, 0x2C6B58, 0x2E77F8, 0x2DC6B0, 0x3036B0, 0x2BE258,
     0x2D2AC8, 0x2C30E8, 0x2DEBB0, 0x2D8AD8, 0x2C86A8, 0x2CEAC8, 0x2CB7B0, 0x2DCCB0,
     0x2C0720, 0x2C5048, 0x2DFDC8, 0x2C3FD8, 0x2D7280, 0, 0x2C93F0, 0x2CDAD0,
     0x2C2F30, 0x2D7030, 0x2C4630, 0, 0, 0, 0x2CAB30}};
constexpr PerLevel kBlueFinder{
    {0x2C40C0, 0, 0x2C7540, 0x2C6CE8, 0x2E79E8, 0x2DC940, 0x303840, 0x2BE550,
     0x2D2CB8, 0x2C3360, 0x2DEE40, 0x2D8CD8, 0x2C89B0, 0x2CECE0, 0x2CB9C0, 0x2DCF40,
     0x2C09A8, 0x2C5260, 0, 0x2C42D8, 0x2D7510, 0, 0x2C9678, 0x2CDD58,
     0x2C31C0, 0x2D72C0, 0x2C4840, 0, 0, 0, 0x2CAEC0},
    {0x2C4340, 0, 0x2C7840, 0x2C7068, 0x2E7D08, 0x2DCBC0, 0x303BC0, 0x2BE768,
     0x2D2FD8, 0x2C35F8, 0x2DF0C0, 0x2D8FE8, 0x2C8BB8, 0x2CEFD8, 0x2CBCC0, 0x2DD1C0,
     0x2C0C30, 0x2C5558, 0, 0x2C44E8, 0x2D7790, 0, 0x2C9900, 0x2CDFE0,
     0x2C3440, 0x2D7540, 0x2C4B40, 0, 0, 0, 0x2CB040}};
constexpr PerLevel kDynamoSearch{
    {0x3B1A38, 0x3ABBF8, 0x3BC6D0, 0x3B88E0, 0x3D9420, 0x3C6DC0, 0x3F9638, 0x3A8F48,
     0x3C3F28, 0x3B4B38, 0x3C94E0, 0x3C92E8, 0x3BA750, 0x3BECE8, 0x3BE5D8, 0x3CD920,
     0x3AFE88, 0x3B8950, 0x3D31A0, 0x3B3858, 0x3D24C8, 0, 0x3BF100, 0x3C6E00,
     0x3A68F8, 0x3BFA60, 0x3B0DA8, 0, 0, 0, 0x3B2200},
    {0x3B2E78, 0x3ACFF8, 0x3BDB60, 0x3B9E20, 0x3DA810, 0x3C82A0, 0x3FAB80, 0x3AA228,
     0x3C5378, 0x3B5EA8, 0x3CA9C0, 0x3CA7E8, 0x3BBA28, 0x3C01A0, 0x3BFAA0, 0x3CEE08,
     0x3B12C8, 0x3B9E10, 0x3D45D0, 0x3B4A88, 0x3D38E0, 0, 0x3C0540, 0x3C8240,
     0x3A7D30, 0x3C0F40, 0x3B2268, 0, 0, 0, 0x3B3480}};
constexpr PerLevel kTractorSearch{
    {0x2AE888, 0x2AF340, 0x2B1B88, 0x2B4148, 0x2D4D70, 0x2C7108, 0x2EDE88, 0x2AD6A8,
     0x2BFF38, 0x2B12D8, 0x2C9608, 0x2C6C10, 0x2B40C0, 0x2B9690, 0x2B6008, 0x2C7708,
     0x2AED68, 0x2AFC10, 0x2CBBB0, 0x2B1690, 0x2C1618, 0, 0x2B2F40, 0x2B7640,
     0x2AD988, 0x2C1A88, 0x2AEE88, 0, 0, 0, 0x2B5688},
    {0x2AE900, 0x2AF4B8, 0x2B1C80, 0x2B42C0, 0x2D4EE8, 0x2C7180, 0x2EE000, 0x2AD720,
     0x2C00B0, 0x2B13D0, 0x2C9680, 0x2C6D88, 0x2B4138, 0x2B9788, 0x2B6100, 0x2C7780,
     0x2AEDE0, 0x2AFD08, 0x2CBC28, 0x2B1708, 0x2C1690, 0, 0x2B2FB8, 0x2B76B8,
     0x2ADA00, 0x2C1B00, 0x2AEF80, 0, 0, 0, 0x2B5600}};
constexpr PerLevel kTargetScore{
    {0x2AE388, 0x2AEE40, 0x2B1688, 0x2B3C48, 0x2D4870, 0x2C6C08, 0x2ED988, 0x2AD1A8,
     0x2BFA38, 0x2B0DD8, 0x2C9108, 0x2C6710, 0x2B3BC0, 0x2B9190, 0x2B5B08, 0x2C7208,
     0x2AE868, 0x2AF710, 0x2CB6B0, 0x2B1190, 0x2C1118, 0, 0x2B2A40, 0x2B7140,
     0x2AD488, 0x2C1588, 0x2AE988, 0, 0, 0, 0x2B5188},
    {0x2AE400, 0x2AEFB8, 0x2B1780, 0x2B3DC0, 0x2D49E8, 0x2C6C80, 0x2EDB00, 0x2AD220,
     0x2BFBB0, 0x2B0ED0, 0x2C9180, 0x2C6888, 0x2B3C38, 0x2B9288, 0x2B5C00, 0x2C7280,
     0x2AE8E0, 0x2AF808, 0x2CB728, 0x2B1208, 0x2C1190, 0, 0x2B2AB8, 0x2B71B8,
     0x2AD500, 0x2C1600, 0x2AEA80, 0, 0, 0, 0x2B5100}};
// The Thermanator's arc (moby_transform_joint_to_world, game_get_override_value,
// CalculateAimedLaunchVelocity, coll_trace_projectile_to_impact) and the Hypnomatic's search
// (its receiver in the aim cone, or the lock-on target).
constexpr PerLevel kJoint{
    {0x311580, 0x307390, 0x315618, 0x312898, 0x332928, 0x329960, 0x3514C8, 0x307940,
     0x31E680, 0x30D8D0, 0x32C068, 0x323958, 0x315148, 0x31B858, 0x318ED0, 0x32A188,
     0x30AD48, 0x312008, 0x32C5E0, 0x30F168, 0x326528, 0, 0x316B30, 0x31B248,
     0x30F888, 0x323CB8, 0x311708, 0, 0, 0, 0x317EF8},
    {0x3127C8, 0x308580, 0x3168E0, 0x313BE0, 0x333B18, 0x32ABB8, 0x352810, 0x308A28,
     0x31F7C8, 0x30EA40, 0x32D2C0, 0x324B40, 0x316220, 0x31CB18, 0x31A198, 0x32B3E0,
     0x30BF90, 0x3132C8, 0x32D808, 0x310188, 0x327768, 0, 0x317D78, 0x31C490,
     0x310AD0, 0x324F10, 0x3129D0, 0, 0, 0, 0x318F80}};
constexpr PerLevel kOverrideValue{
    {0x37FEB8, 0x376178, 0x387668, 0x381428, 0x3A5BD8, 0x3950A8, 0x3C3598, 0x375F90,
     0x3907F0, 0x37D3E0, 0x396D88, 0x394720, 0x385730, 0x389818, 0x38A668, 0x39BA10,
     0x377B80, 0x3834E0, 0x39E2E8, 0x37E010, 0x397FE8, 0, 0x383638, 0x387D50,
     0x377140, 0x38EF50, 0x37F660, 0, 0, 0, 0x3805E8},
    {0x381320, 0x377590, 0x388B50, 0x382990, 0x3A6FF0, 0x396518, 0x3C4B08, 0x377298,
     0x391C68, 0x37E778, 0x3981F8, 0x395B30, 0x386A30, 0x38ACF8, 0x38BB58, 0x39CE88,
     0x378FE8, 0x3849C8, 0x39F730, 0x37F258, 0x399448, 0, 0x384AA0, 0x3891B8,
     0x3785A0, 0x3903C0, 0x380B48, 0, 0, 0, 0x381890}};
constexpr PerLevel kArc{
    {0x37CEE8, 0x3731A8, 0x384698, 0x37E458, 0x3A2C08, 0x3920D8, 0x3C05C8, 0x372FC0,
     0x38D820, 0x37A410, 0x393DB8, 0x391750, 0x382760, 0x386848, 0x387698, 0x398A40,
     0x374BB0, 0x380510, 0x39B318, 0x37B040, 0x395018, 0, 0x380668, 0x384D80,
     0x374170, 0x38BF80, 0x37C690, 0, 0, 0, 0x37D618},
    {0x37E328, 0x374598, 0x385B58, 0x37F998, 0x3A3FF8, 0x393520, 0x3C1B10, 0x3742A0,
     0x38EC70, 0x37B780, 0x395200, 0x392B38, 0x383A38, 0x387D00, 0x388B60, 0x399E90,
     0x375FF0, 0x3819D0, 0x39C738, 0x37C260, 0x396450, 0, 0x381AA8, 0x3861C0,
     0x3755A8, 0x38D3C8, 0x37DB50, 0, 0, 0, 0x37E898}};
constexpr PerLevel kArcHit{
    {0x37D548, 0x373808, 0x384CF8, 0x37EAB8, 0x3A3268, 0x392738, 0x3C0C28, 0x373620,
     0x38DE80, 0x37AA70, 0x394418, 0x391DB0, 0x382DC0, 0x386EA8, 0x387CF8, 0x3990A0,
     0x375210, 0x380B70, 0x39B978, 0x37B6A0, 0x395678, 0, 0x380CC8, 0x3853E0,
     0x3747D0, 0x38C5E0, 0x37CCF0, 0, 0, 0, 0x37DC78},
    {0x37E988, 0x374BF8, 0x3861B8, 0x37FFF8, 0x3A4658, 0x393B80, 0x3C2170, 0x374900,
     0x38F2D0, 0x37BDE0, 0x395860, 0x393198, 0x384098, 0x388360, 0x3891C0, 0x39A4F0,
     0x376650, 0x382030, 0x39CD98, 0x37C8C0, 0x396AB0, 0, 0x382108, 0x386820,
     0x375C08, 0x38DA28, 0x37E1B0, 0, 0, 0, 0x37EEF8}};
constexpr PerLevel kHypnoSearch{
    {0x3B8E58, 0x3B3380, 0x3C6348, 0x3C9138, 0x3E6FE8, 0x3CE120, 0x404AC8, 0x3AFC98,
     0x3D20D0, 0x3C1B38, 0x3D0840, 0x3D1438, 0x3C5310, 0x3C7A58, 0x3CA160, 0x3D4C80,
     0x3B8098, 0x3C8C78, 0x3DB440, 0x3BF4E0, 0x3DC4F0, 0, 0x3C5E50, 0x3CDB50,
     0x3AD648, 0x3C6DC0, 0x3B7AF8, 0, 0, 0, 0x3BF500},
    {0x3BA2A0, 0x3B4788, 0x3C78A0, 0x3CA740, 0x3E84A0, 0x3CF608, 0x406018, 0x3B0F80,
     0x3D3528, 0x3C2F70, 0x3D1D28, 0x3D2940, 0x3C66B0, 0x3C8F18, 0x3CB630, 0x3D6170,
     0x3B94E0, 0x3CA200, 0x3DC878, 0x3C0718, 0x3DD9D0, 0, 0x3C7298, 0x3CEF98,
     0x3AEA88, 0x3C82A8, 0x3B8FC0, 0, 0, 0, 0x3BFA88}};

bool item_owned(const openrac::Mod& mod, const openrac::Memory& memory, int item) {
    return item == kWrenchItem || (item > 0 && item < 56 && memory.read<std::uint8_t>(boot_global(mod, kOwned) + item) != 0);
}

// The Swingshot's node where the camera looks. Going Commando (and Up Your Arsenal, Deadlocked)
// pick a node off his body's heading, which the Modern camera's view need not be: standing, turning
// the camera at a node did nothing. So the nodes are found by the view (within 30 u, 35 degrees of
// it across, nearest first by the game's own score, distance x (1 + angle)), and
// each is checked by the game's three finders with his heading turned to it (then put back): only
// a node the game itself would swing to, in reach, in sight from his feet, and the one its choice
// between swing and pull nodes takes, is offered (docs/notes/rac3-camera-review.md, 6).
// Aiming or looking (states 1, 0x1D) the game measures from the camera itself: left to it.
constexpr std::int16_t kSwingNode = 0x2F6, kPullNode = 0x323, kBlueTarget = 0x99D;
constexpr gaddr kSwingNodePick = kHero + 0xAA4, kPullNodePick = kHero + 0xAD0, kBlueTargetPick = kHero + 0xB10;
constexpr gaddr kSwingNodeScore = kHero + 0xAC4, kPullNodeScore = kHero + 0xAD8;
constexpr gaddr kHeroTurnGoal = kHero + 0x1A0, kHeroTurnCarry = kHero + 0x1A4;  // f32: his turn's goal; its carried turn
constexpr float kSwingReach = 30.0f, kSwingCone = 35.0f * kDeg;

struct SwingPick {
    gaddr node = 0;
    float yaw = 0;  // his heading to it
};

// Whether the game's finders, with his heading `yaw`, take `node` (and it wins their choice).
bool swing_takes(const openrac::Mod& mod, const openrac::Memory& memory, const gaddr (&finders)[3], gaddr node, float yaw) {
    const float heading = memory.read<float>(kHeroFacing);
    memory.write<float>(kHeroFacing, yaw);
    for (const gaddr at : finders) {
        const auto find = at != 0 ? mod.function_at<int (*)()>(at) : nullptr;
        if (find != nullptr) find();
    }
    memory.write<float>(kHeroFacing, heading);
    const bool swing = memory.read<std::int32_t>(kSwingNodeReady) > 0, pull = memory.read<std::int16_t>(kPullNodeReady) > 0;
    const float swing_score = memory.read<float>(kSwingNodeScore), pull_score = memory.read<float>(kPullNodeScore);
    if (swing && memory.read<gaddr>(kSwingNodePick) == node) return !(pull && pull_score < swing_score);
    if (pull && memory.read<gaddr>(kPullNodePick) == node) return !(swing && swing_score < pull_score);
    return finders[2] != 0 && memory.read<std::int32_t>(kBlueTargetReady) > 0 && memory.read<gaddr>(kBlueTargetPick) == node;
}

SwingPick swing_pick(const openrac::Mod& mod, const openrac::Memory& memory) {
    const gaddr finders[3] = {level_function(mod, kSwingFinder), level_function(mod, kPullFinder),
                              level_function(mod, kBlueFinder)};
    const std::int32_t state = memory.read<std::int32_t>(kHeroState);
    const gaddr camera = find_camera(mod);
    if (finders[0] == 0 || camera == 0 || state == 1 || state == 0x1D) return {};
    const V3 view = read_v3(memory, camera), hero = read_v3(memory, kHeroPosition);
    if (view.x * view.x + view.y * view.y < 1e-4f) return {};
    const float view_yaw = std::atan2(view.y, view.x);
    std::vector<std::pair<float, gaddr>> near;
    for (const gaddr m : moby_table(mod, memory)) {
        const std::int16_t cls = memory.read<std::int16_t>(m + kMobyClass);
        if ((cls != kSwingNode && cls != kPullNode && cls != kBlueTarget) || memory.read<std::uint8_t>(m + 0x20) >= 0x80) continue;
        const V3 at = read_v3(memory, m + 0x10);
        const float dist = length(vec::sub(at, hero));
        if (dist > kSwingReach) continue;
        // Across only, as the game measures its own cone (the height is the finders' to judge).
        const float angle = std::fabs(wrap_pi(std::atan2(at.y - hero.y, at.x - hero.x) - view_yaw));
        if (angle > kSwingCone) continue;
        near.emplace_back(dist * (1.0f + angle), m);
    }
    std::sort(near.begin(), near.end());
    const KeepCollision keep(mod, memory);
    if (!keep.ok()) return {};
    for (std::size_t i = 0; i < near.size() && i < 4; ++i) {
        const gaddr m = near[i].second;
        const float yaw = std::atan2(memory.read<float>(m + 0x14) - hero.y, memory.read<float>(m + 0x10) - hero.x);
        if (swing_takes(mod, memory, finders, m, yaw)) return {m, yaw};
    }
    return {};
}

// His heading turned to the node for the press (the swing's start eases him round from there).
void face_node(const openrac::Memory& memory, float yaw) {
    memory.write<float>(kHeroFacing, yaw);
    memory.write<float>(kHeroTurnGoal, yaw);
    memory.write<float>(kHeroTurnCarry, 0.0f);
}

// The Dynamo's target now: the game's search (it looks from the camera), an activator not used.
gaddr dynamo_target(const openrac::Mod& mod, const openrac::Memory& memory) {
    const gaddr at = level_function(mod, kDynamoSearch);
    const auto search = at != 0 ? mod.function_at<gaddr (*)()>(at) : nullptr;
    if (search == nullptr) return 0;
    const KeepCollision keep(mod, memory);
    if (!keep.ok()) return 0;
    const gaddr t = search();
    if (!moby_live(memory, t)) return 0;
    const std::int16_t cls = memory.read<std::int16_t>(t + kMobyClass);
    return (cls == 0xCB5 || cls == 0x12EA) && memory.read<std::uint8_t>(t + 0x20) != 2 ? t : 0;
}

// The Tractor Beam's line of sight, as the game tests it: from the target, half a unit up, to him.
bool tractor_sees(const openrac::Mod& mod, const openrac::Memory& memory, gaddr t) {
    const gaddr at = coll_line_function(mod);
    const auto line = at != 0 ? mod.function_at<int (*)(gaddr, gaddr, std::uint32_t, gaddr, gaddr)>(at) : nullptr;
    if (line == nullptr) return false;
    openrac::StackFrame frame(mod, 0x10);
    for (int k = 0; k < 4; ++k) memory.write<float>(frame.address() + 4 * k, memory.read<float>(t + 0x10 + 4 * k));
    memory.write<float>(frame.address() + 8, memory.read<float>(t + 0x18) + 0.5f);
    return line(frame.address(), kHero + 0xE0, 2, t, 0) == 0;
}

// Oozla's boxes (0xACB, 0xACC): ready, and within their own reach of him.
bool box_class(std::int16_t cls) { return cls == 0xACB || cls == 0xACC; }
bool box_in_reach(const openrac::Memory& memory, gaddr t) {
    const gaddr pv = memory.read<gaddr>(t + kMobyVars);
    if (!in_ram(pv) || memory.read<std::uint8_t>(t + 0x20) != 1) return false;
    const float reach = memory.read<float>(pv + 0x88);
    float d = 0;
    for (int k = 0; k < 3; ++k) {
        const float e = memory.read<float>(t + 0x10 + 4 * k) - memory.read<float>(kHeroPosition + 4 * k);
        d += e * e;
    }
    return std::isfinite(reach) && std::sqrt(d) < reach;
}

struct TractorPick {
    gaddr target = 0;
    bool listed = true;  // the game's own search returns it (else a box it will list shortly)
};

// The Tractor Beam's target now: the game's search, with the game's arguments (its targets, 20 u,
// 30 degrees about his heading, 40 up and down) and its line of sight. Oozla's boxes update the flag
// that lists them only every 16 ticks: one in reach is found by the game's scorer, and pressed for
// once the game lists it (Joe's).
TractorPick tractor_target(const openrac::Mod& mod, const openrac::Memory& memory) {
    if (memory.read<gaddr>(kHeroTractorLatch) != 0) return {};
    const gaddr at = level_function(mod, kTractorSearch);
    const auto search = at != 0 ? mod.function_at<gaddr (*)(gaddr, int, int, float, float, float, float)>(at) : nullptr;
    if (search == nullptr) return {};
    const KeepCollision keep(mod, memory);
    if (!keep.ok()) return {};
    constexpr float kYawCone = 0.5235988f, kPitchCone = 0.6981317f;
    const float yaw = memory.read<float>(kHeroFacing);
    const bool oozla = mod.play_status().level == 1;
    const gaddr t = search(kHero, 21, 0, 20.0f, yaw, kYawCone, kPitchCone);
    if (moby_live(memory, t) && moby_kind(memory, t) == 21
        && (!oozla || !box_class(memory.read<std::int16_t>(t + kMobyClass)) || box_in_reach(memory, t))) {
        return tractor_sees(mod, memory, t) ? TractorPick{t, true} : TractorPick{};
    }
    if (!oozla || memory.read<std::uint8_t>(kHeroGravityMode) != 0) return {};
    const gaddr score_at = level_function(mod, kTargetScore);
    const auto score = score_at != 0 ? mod.function_at<float (*)(gaddr, float, float, gaddr, float, float)>(score_at) : nullptr;
    if (score == nullptr) return {};
    const Mobys mobys = moby_table(mod, memory);
    openrac::StackFrame frame(mod, 0x10);
    TractorPick best{};
    float best_score = 1e8f;
    for (const gaddr m : mobys) {
        if (!moby_live(memory, m) || !box_class(memory.read<std::int16_t>(m + kMobyClass)) || moby_kind(memory, m) != 21
            || memory.read<std::uint8_t>(m + 0x31) == 0 || (memory.read<std::uint16_t>(m + 0x34) & 2) != 0 || !box_in_reach(memory, m)) {
            continue;
        }
        const float value = score(m, yaw, 20.0f, frame.address(), kYawCone, kPitchCone);
        if (memory.read<std::int32_t>(frame.address()) == 0 && value < best_score && tractor_sees(mod, memory, m)) {
            best = {m, false};
            best_score = value;
        }
    }
    return best;
}

// A freezable pool's or ice's grid (kind 18) the Thermanator can work now, or 0.
gaddr freeze_grid(const openrac::Memory& memory, gaddr m, bool* frozen = nullptr) {
    if (!moby_live(memory, m) || moby_kind(memory, m) != 18) return 0;
    const gaddr pv = memory.read<gaddr>(m + kMobyVars);
    if (!in_ram(pv) || memory.read<std::int16_t>(pv + 0x16) != 0) return 0;
    if (frozen != nullptr) *frozen = memory.read<std::uint8_t>(pv + 0x14) != 0;
    return m;
}

// The Thermanator's target now: the game's own arc (its reach, its launch, its trace), from his hand
// (the gadget's muzzle once it is in hand), and the grid it lands on (Joe's preview).
gaddr thermanator_target(const openrac::Mod& mod, const openrac::Memory& memory, bool in_hand, bool* frozen) {
    const gaddr joint_at = level_function(mod, kJoint), reach_at = level_function(mod, kOverrideValue);
    const gaddr arc_at = level_function(mod, kArc), hit_at = level_function(mod, kArcHit);
    if (joint_at == 0 || reach_at == 0 || arc_at == 0 || hit_at == 0) return 0;
    const auto joint = mod.function_at<void (*)(gaddr, int, gaddr)>(joint_at);
    const auto reach = mod.function_at<float (*)(float)>(reach_at);
    const auto arc = mod.function_at<void (*)(gaddr, gaddr, gaddr, float, float, float, float, gaddr)>(arc_at);
    const auto trace = mod.function_at<int (*)(gaddr, gaddr, gaddr, gaddr, gaddr, gaddr, gaddr, float)>(hit_at);
    if (joint == nullptr || reach == nullptr || arc == nullptr || trace == nullptr) return 0;
    const KeepCollision keep(mod, memory);
    if (!keep.ok()) return 0;
    openrac::StackFrame frame(mod, 0x50);
    const gaddr muzzle = frame.address(), velocity = muzzle + 0x10, point = muzzle + 0x20, normal = muzzle + 0x30, hit = muzzle + 0x40;
    const gaddr hand = memory.read<gaddr>(kHeroHand);
    if (in_hand) {
        joint(hand, 0, muzzle);
    } else {
        for (int k = 0; k < 4; ++k) memory.write<float>(muzzle + 4 * k, memory.read<float>(kHeroAttachment + 4 * k));
    }
    const float range = reach(4.5f) + (memory.read<std::int32_t>(kHeroState) == 0x1D ? 6.5f : 0.0f);
    constexpr float kLeast = 1.0e-5f, kGravity = 0.0030555558f;
    arc(velocity, muzzle, 0, kLeast, range, 0.25f, kGravity, 0);
    memory.write<gaddr>(hit, 0);
    trace(muzzle, velocity, point, normal, hand, hit, 0, kGravity);
    return freeze_grid(memory, memory.read<gaddr>(hit), frozen);
}

// A Receiver Bot the Hypnomatic can take now: live, drawn, ready to be taken, or 0.
gaddr receiver_bot(const openrac::Memory& memory, gaddr m) {
    if (!moby_live(memory, m) || memory.read<std::int16_t>(m + kMobyClass) != 0xEE1 || memory.read<std::uint8_t>(m + 0x31) == 0) return 0;
    const gaddr pv = memory.read<gaddr>(m + kMobyVars);
    if (!in_ram(pv)) return 0;
    return (memory.read<std::uint8_t>(pv + 0x3B1) & 0x80) != 0 && memory.read<float>(pv + 0x20) > 0.0f ? m : 0;
}

// He stands so the Hypnomatic's beam can start (the game's own conditions for it).
bool hypno_footing(const openrac::Memory& memory) {
    return memory.read<std::int32_t>(kHero + 0x340) != 0 && memory.read<std::int16_t>(kHero + 0x34E) == 0
        && memory.read<std::int16_t>(kHeroCaptureTimer) == 0 && memory.read<std::int16_t>(kHero + 0x24EC) == 0;
}

// The Hypnomatic's target now: the game's search along the camera's view (35 u, 15 degrees; 5 in
// first person), with the classes its beam passes through set aside and the lock-on target not
// taken first, then the game's line along the view; within the bot's own reach (Joe's preview).
gaddr hypno_target(const openrac::Mod& mod, const openrac::Memory& memory) {
    const gaddr actor = memory.read<gaddr>(kHeroMoby);
    const gaddr search_at = level_function(mod, kHypnoSearch), line_at = coll_line_function(mod);
    if (search_at == 0 || line_at == 0 || !moby_live(memory, actor) || !hypno_footing(memory)) return 0;
    const auto search = mod.function_at<gaddr (*)(gaddr, gaddr, gaddr, float, float, float, float, float)>(search_at);
    const auto line = mod.function_at<CollLineFn>(line_at);
    if (search == nullptr || line == nullptr) return 0;
    const gaddr camera = level0_global(mod, kCameraBlock);
    const bool looking = memory.read<std::int32_t>(kHeroState) == 0x1D;
    const bool first_person = looking && (memory.read<std::uint16_t>(actor + 0x34) & 1) != 0;
    V3 origin;
    if (first_person) {
        origin = {memory.read<float>(camera + 0x140), memory.read<float>(camera + 0x144), memory.read<float>(camera + 0x148)};
    } else {
        origin = {memory.read<float>(kHeroPosition) + 0.75f * memory.read<float>(actor + 0xE0),
                  memory.read<float>(kHeroPosition + 4) + 0.75f * memory.read<float>(actor + 0xE4),
                  memory.read<float>(kHeroPosition + 8) + 0.75f * memory.read<float>(actor + 0xE8)};
    }
    V3 forward{memory.read<float>(camera + 0x370), memory.read<float>(camera + 0x374), looking ? 0.0f : memory.read<float>(camera + 0x378)};
    const float length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (!std::isfinite(length) || length < 0.001f) return 0;
    forward = {forward.x / length, forward.y / length, forward.z / length};
    openrac::StackFrame frame(mod, 0x30);
    const gaddr from = frame.address(), direction = from + 0x10, end = from + 0x20;
    const float o[3] = {origin.x, origin.y, origin.z}, f[3] = {forward.x, forward.y, forward.z};
    for (int k = 0; k < 3; ++k) {
        memory.write<float>(from + 4 * k, o[k]);
        memory.write<float>(direction + 4 * k, f[k]);
        memory.write<float>(end + 4 * k, o[k] + 35.0f * f[k]);
    }
    memory.write<float>(from + 12, 1.0f);
    memory.write<float>(direction + 12, 0.0f);
    memory.write<float>(end + 12, 1.0f);
    const KeepCollision keep(mod, memory);
    if (!keep.ok()) return 0;
    // The classes the beam passes through, their collision set aside for the search (as the game does).
    constexpr std::int16_t kPassed[] = {0x1146, 0x1185, 0x119B, 0x11A4, 0x1204, 0x11FC, 0x120C, 0x1135, 0x1148};
    std::vector<std::pair<gaddr, gaddr>> set_aside;
    for (const gaddr m : moby_table(mod, memory)) {
        if (set_aside.size() >= 64) break;
        const std::int16_t cls = memory.read<std::int16_t>(m + kMobyClass);
        if (std::find(std::begin(kPassed), std::end(kPassed), cls) == std::end(kPassed)) continue;
        const gaddr coll = memory.read<gaddr>(m + 0x98);
        if (coll == 0) continue;
        set_aside.emplace_back(m + 0x98, coll);
        memory.write<gaddr>(m + 0x98, 0);
    }
    const gaddr lock = memory.read<gaddr>(kHeroLockTarget);
    memory.write<gaddr>(kHeroLockTarget, 0);
    constexpr float kCone = 0.2617994f, kConeFirst = 0.0872665f, kWideFirst = 0.1047198f;
    constexpr float kNearPitch = 0.7853982f, kNearYaw = 0.6108652f;
    gaddr t = receiver_bot(memory, search(from, direction, 0, first_person ? kConeFirst : kCone, first_person ? kWideFirst : kCone,
                                          35.0f, kNearPitch, kNearYaw));
    memory.write<gaddr>(kHeroLockTarget, lock);
    if (t == 0 && line(from, end, 0, actor, 0) != 0) {
        t = receiver_bot(memory, memory.read<gaddr>(collision_block(mod, memory) + 0x18));
    }
    for (const auto& [at, coll] : set_aside) memory.write<gaddr>(at, coll);
    if (t == 0) return 0;
    const float dx = memory.read<float>(t + 0x10) - o[0], dy = memory.read<float>(t + 0x14) - o[1];
    const float d = std::sqrt(dx * dx + dy * dy);
    const float reach = memory.read<float>(memory.read<gaddr>(t + kMobyVars) + 0x3E4);
    return std::isfinite(d) && d <= 35.0f && (reach == 0.0f || d < reach) ? t : 0;
}

// The gadget's own vars (those of the moby in his hand), or 0.
gaddr hand_vars(const openrac::Memory& memory) {
    const gaddr hand = memory.read<gaddr>(kHeroHand);
    const gaddr pv = in_ram(hand) ? memory.read<gaddr>(hand + kMobyVars) : 0;
    return in_ram(pv) ? pv : 0;
}

// The target as the gadget in his hand has it (the Thermanator's grid, the Hypnomatic's bot).
gaddr hand_target(const openrac::Memory& memory, Gadget g, bool* frozen = nullptr) {
    const gaddr pv = hand_vars(memory);
    if (pv == 0) return 0;
    if (g == Gadget::Thermanator) return memory.read<std::int32_t>(pv + 0x40) != 0 ? freeze_grid(memory, memory.read<gaddr>(pv + 0x54), frozen) : 0;
    if (g == Gadget::Hypnomatic) return receiver_bot(memory, memory.read<gaddr>(pv));
    return 0;
}

// The item in his hand, live (in mode 2), or 0.
int hand_item(const openrac::Memory& memory) {
    const gaddr hand = memory.read<gaddr>(kHeroHand);
    if (!in_ram(hand) || memory.read<std::int32_t>(kHeroHandMode) != 2) return 0;
    return memory.read<std::int32_t>(kHeroItem);
}

// The gadget in his hand, ready for Circle.
bool gadget_in_hand(const openrac::Memory& memory, Gadget g) {
    const gaddr hand = memory.read<gaddr>(kHeroHand);
    return hand_item(memory) == gadget_info(g).item && memory.read<std::uint32_t>(kHeroHandButton) == strafe::kCircle
        && memory.read<std::int16_t>(hand + kMobyClass) == gadget_info(g).hand;
}

// The state of the moby in his hand (the Dynamo's and the Tractor Beam's: 1 ready), 0 for none.
std::uint8_t hand_state(const openrac::Memory& memory) {
    const gaddr hand = memory.read<gaddr>(kHeroHand);
    return in_ram(hand) ? memory.read<std::uint8_t>(hand + 0x20) : 0;
}

// He can take an item in hand now (Joe's can_equip): in play as Ratchet, on foot or hovering (the
// game swings from a hover too), alive, nothing holding his hand or the game's requests.
constexpr std::int32_t kHoverGroup = 5;
bool can_equip(const openrac::Mod& mod, const openrac::Memory& memory) {
    const std::int32_t state = memory.read<std::int32_t>(kHeroState);
    return mod.play_status().phase == OPENRAC_PLAY_PLAYING && memory.read<gaddr>(kHeroMoby) != 0
        && memory.read<std::int32_t>(kHeroHealth) > 0 && state != 0x31 && state != 0x61
        && memory.read<std::uint8_t>(kHeroForm) == 0 && memory.read<gaddr>(kHeroVehicle) == 0
        && memory.read<std::int32_t>(kPadMode) == 0 && memory.read<std::uint8_t>(kHeroNoUpdate) == 0
        && memory.read<std::uint8_t>(kHeroWrenchForced) == 0 && memory.read<std::uint8_t>(kHeroBlocked) == 0
        && memory.read<std::uint8_t>(kHeroTucked) == 0 && memory.read<std::uint8_t>(kHeroMelee) == 0
        && memory.read<std::uint8_t>(kHeroMelee + 1) == 0
        && (foot_group(memory.read<std::int32_t>(kHeroGroup)) || memory.read<std::int32_t>(kHeroGroup) == kHoverGroup);
}

// Each step of an Auto Gadget's use, for the log's debug level.
void auto_log(const std::string& what) {
    s.mod->debug("auto gadget: " + what);
}

void auto_idle() {
    g_auto.phase = AutoPhase::Idle;
    g_auto.ticks = 0;
    g_auto.gadget = Gadget::None;
    g_auto.target = 0;
    g_auto.fire_after = false;
    g_auto.used = false;
    g_auto.saw_robot = false;
}

// The request the mod made, taken back if the game has not taken it yet.
void auto_clear_request(const openrac::Memory& memory) {
    if (g_auto.gadget != Gadget::None && memory.read<std::int32_t>(kHeroRequest) == gadget_info(g_auto.gadget).item) {
        memory.write<std::int32_t>(kHeroRequest, 0);
    }
}

// The player took over (the wrench, the Quick Select): the gadget theirs now, but Circle from the
// wrench draws the weapon they had, not the gadget.
void auto_hand_over(const openrac::Memory& memory, const char* why) {
    auto_clear_request(memory);
    if (g_auto.gadget == Gadget::Tractor) memory.write<gaddr>(kHeroTractorLatch, 0);
    if (memory.read<std::int32_t>(kMenuChoice) == gadget_info(g_auto.gadget).item) {
        memory.write<std::int32_t>(kMenuChoice, g_auto.previous == kWrenchItem ? g_auto.menu_choice : g_auto.previous);
    }
    auto_log(why);
    auto_idle();
}

// Done with the gadget: dropped (the Tractor Beam's hold), and the item before asked back once he is
// free (auto_frame).
void auto_cancel(const openrac::Memory& memory, const char* why) {
    auto_clear_request(memory);
    if (g_auto.gadget == Gadget::Tractor && g_auto.phase == AutoPhase::Active) memory.write<gaddr>(kHeroTractorLatch, 0);
    g_auto.phase = AutoPhase::Canceling;
    g_auto.ticks = 0;
    auto_log(std::string(gadget_info(g_auto.gadget).name) + " done (" + why + ")");
}

// The item before back in hand.
void auto_restore(const openrac::Mod& mod, const openrac::Memory& memory, const char* why) {
    if (!item_owned(mod, memory, g_auto.previous)) {
        auto_log(std::string("the gadget stays (") + why + "; the item before is gone)");
        auto_idle();
        return;
    }
    memory.write<std::int32_t>(kHeroRequest, g_auto.previous);
    g_auto.phase = AutoPhase::Restoring;
    g_auto.ticks = 0;
    auto_log(std::string("item ") + std::to_string(g_auto.previous) + " back (" + why + ")");
}

void set_circle(std::uint8_t* report, int size, bool down) {
    if (size < 2) return;
    report[strafe::kButtonsHigh] = static_cast<std::uint8_t>(down ? (report[strafe::kButtonsHigh] & ~strafe::kCircle)
                                                                  : (report[strafe::kButtonsHigh] | strafe::kCircle));
    if (size > strafe::kCirclePressure) report[strafe::kCirclePressure] = down ? 0xFF : 0;
}

// The Hypnomatic's beam has the bot: its capture timer set, or the bot being taken over (state 7).
bool hypno_capturing(const openrac::Memory& memory, gaddr bot) {
    return memory.read<std::int16_t>(kHeroCaptureTimer) != 0 || (moby_live(memory, bot) && memory.read<std::uint8_t>(bot + 0x20) == 7);
}

// A fresh Fire: the one gadget in the setting with a target, put in hand (with Circle kept from
// the game until it is). Two with targets, or the one already in hand: the game's own Circle.
bool auto_begin(const openrac::Mod& mod, const openrac::Memory& memory, bool jumping) {
    Auto& a = g_auto;
    const int item = hand_item(memory);
    const std::int32_t chosen = memory.read<std::int32_t>(kHeroChosen);
    if (item == 0 || memory.read<std::int32_t>(kHeroRequest) != 0 || (chosen != 0 && chosen != item)
        || memory.read<std::uint8_t>(kHeroItemBusy) != 0 || !item_owned(mod, memory, item)) {
        return false;
    }
    Gadget found = Gadget::None;
    gaddr target = 0;
    bool listed = true;
    int count = 0;
    const auto wanted = [&](Gadget g) { return a.tier >= gadget_info(g).tier && item_owned(mod, memory, gadget_info(g).item); };
    const auto take = [&](Gadget g, gaddr t, bool l) {
        if (gadget_info(g).item == item) count = 99;  // in hand already: the game's
        ++count;
        found = g;
        target = t;
        listed = l;
    };
    if (wanted(Gadget::Swingshot)) {
        if (const SwingPick pick = swing_pick(mod, memory); pick.node != 0) take(Gadget::Swingshot, pick.node, true);
    }
    if (wanted(Gadget::Dynamo)) {
        if (const gaddr t = dynamo_target(mod, memory)) take(Gadget::Dynamo, t, true);
    }
    if (wanted(Gadget::Tractor) && !jumping) {
        const TractorPick pick = tractor_target(mod, memory);
        if (pick.target != 0) take(Gadget::Tractor, pick.target, pick.listed);
    }
    bool frozen = false;
    if (wanted(Gadget::Thermanator)) {
        bool f = false;
        if (const gaddr t = thermanator_target(mod, memory, item == gadget_info(Gadget::Thermanator).item, &f)) {
            take(Gadget::Thermanator, t, true);
            frozen = f;
        }
    }
    if (wanted(Gadget::Hypnomatic)) {
        if (const gaddr t = hypno_target(mod, memory)) take(Gadget::Hypnomatic, t, true);
    }
    if (count != 1) return false;
    a.frozen = frozen;
    a.gadget = found;
    a.target = target;
    a.listed = listed;
    a.previous = item;
    a.menu_choice = memory.read<std::int32_t>(kMenuChoice);
    a.loads = mod.play_status().loads;
    memory.write<std::int32_t>(kHeroRequest, gadget_info(found).item);
    a.phase = AutoPhase::Equipping;
    a.ticks = 0;
    a.swallow = true;
    auto_log(std::string(gadget_info(found).name) + " in hand for a target (item " + std::to_string(item) + " before)");
    return true;
}

// The pad's part (after the player's buttons are mapped: Fire is the game's Circle by then, Jump its
// Cross, Wrench its Square).
void auto_pad(std::uint8_t* report, int size, strafe::Gate gate) {
    Auto& a = g_auto;
    const openrac::Mod& mod = *s.mod;
    const auto memory = mod.memory();
    const bool fire = size >= 2 && strafe::held(report[strafe::kButtonsHigh], strafe::kCircle);
    const bool fresh = fire && !a.fire_was;
    a.fire_was = fire;
    if (!fire) a.swallow = false;
    if (!a.others_checked && mod.play_status().phase == OPENRAC_PLAY_PLAYING) {
        a.others_checked = true;
        for (const auto& other : mod.mods()) {
            if (other.id == "rac2-auto-gadgets") a.deferred = true;
        }
        if (a.deferred) mod.info("auto gadget: Joebannerlord's Auto Gadgets is loaded: ours is off, so the two never both switch");
    }
    if (a.tier == AutoTier::Off || a.deferred || gate == strafe::Gate::Off) {
        const bool capturing = a.gadget == Gadget::Hypnomatic && hypno_capturing(memory, a.target);
        if (!capturing && (a.phase == AutoPhase::Equipping || a.phase == AutoPhase::Replay || a.phase == AutoPhase::Active)) {
            auto_cancel(memory, "a menu or a scene");
        }
        return;
    }
    const std::int32_t group = memory.read<std::int32_t>(kHeroGroup);
    const bool jumping = (size >= 2 && strafe::held(report[strafe::kButtonsHigh], strafe::kCross)) || group == 2 || group == 4;
    // The wrench: the game puts it in hand for Square; the gadget is let go.
    if (a.phase != AutoPhase::Idle && a.phase != AutoPhase::Capture && size >= 2 && strafe::held(report[strafe::kButtonsHigh], strafe::kSquare)) {
        auto_hand_over(memory, "the wrench");
        a.swallow = false;
        return;
    }
    const Gadget g = a.gadget;
    switch (a.phase) {
        case AutoPhase::Idle:
            if (fresh && a.tier != AutoTier::Off && can_equip(mod, memory) && auto_begin(mod, memory, jumping)) {
                set_circle(report, size, false);
            }
            break;
        case AutoPhase::Equipping: {
            // Circle up until it is in hand and ready; then pressed once (a fresh press for the game).
            set_circle(report, size, false);
            const bool hold = g != Gadget::Swingshot && g != Gadget::Dynamo;
            if (!can_equip(mod, memory) || !item_owned(mod, memory, gadget_info(g).item) || (hold && !fire)
                || (g == Gadget::Tractor && jumping)) {
                auto_cancel(memory, "let go before it was ready");
                break;
            }
            if (g == Gadget::Dynamo && dynamo_target(mod, memory) != a.target) {
                auto_cancel(memory, "its target gone");
                break;
            }
            if (g == Gadget::Tractor) {
                const TractorPick pick = tractor_target(mod, memory);
                if (pick.target != a.target) {
                    auto_cancel(memory, "its target gone");
                    break;
                }
                a.listed = pick.listed;
            }
            const bool in_hand = gadget_in_hand(memory, g);
            if (g == Gadget::Thermanator) {
                bool f = false;
                if (thermanator_target(mod, memory, in_hand, &f) != a.target || f != a.frozen) {
                    auto_cancel(memory, "its target gone");
                    break;
                }
            }
            if (g == Gadget::Hypnomatic && hypno_target(mod, memory) != a.target) {
                auto_cancel(memory, "its target gone");
                break;
            }
            bool gadget_ready = true;
            if (g == Gadget::Dynamo || g == Gadget::Tractor) gadget_ready = hand_state(memory) == 1;
            if (g == Gadget::Thermanator) {
                bool f = false;
                gadget_ready = hand_state(memory) == 2 && hand_target(memory, g, &f) == a.target && f == a.frozen;
            }
            if (g == Gadget::Hypnomatic) {
                gadget_ready = memory.read<std::int32_t>(kHeroHandAge) >= 16 && hand_target(memory, g) == a.target && hypno_footing(memory);
            }
            const bool ready = in_hand && memory.read<std::int32_t>(kHeroRequest) == 0 && a.listed && gadget_ready
                && (memory.read<std::uint32_t>(kPadHeld) & strafe::kCircle) == 0;
            if (!ready) break;
            if (g == Gadget::Swingshot) {
                // The node again (the camera may have moved on to another), and him turned to it.
                const SwingPick pick = swing_pick(mod, memory);
                if (pick.node == 0) {
                    auto_cancel(memory, "its target gone");
                    break;
                }
                a.target = pick.node;
                face_node(memory, pick.yaw);
            }
            set_circle(report, size, true);
            // The Dynamo fires once; the others hold on while Fire does.
            a.swallow = g == Gadget::Dynamo && fire;
            auto_log(std::string(gadget_info(g).name) + " ready after " + std::to_string(a.ticks) + " ticks: pressed");
            a.phase = AutoPhase::Replay;
            a.ticks = 0;
            return;
        }
        case AutoPhase::Active:
            if (g == Gadget::Swingshot) {
                // A new press, not swinging: him turned to the node the camera looks at; with none,
                // the weapon back, and fired.
                if (fresh && group != 13 && group != 14 && group != 33 && memory.read<std::uint8_t>(kHeroBlocked) == 0) {
                    if (const SwingPick pick = swing_pick(mod, memory); pick.node != 0) {
                        face_node(memory, pick.yaw);
                    } else if (memory.read<std::int32_t>(kHeroState) != 1 && memory.read<std::int32_t>(kHeroState) != 0x1D) {
                        set_circle(report, size, false);
                        a.fire_after = true;
                        auto_restore(mod, memory, "Fire with no target");
                    }
                }
            } else if (g == Gadget::Tractor && (!fire || jumping)) {
                auto_cancel(memory, jumping ? "a jump" : "Fire let go");  // dropped
            } else if (g == Gadget::Thermanator || g == Gadget::Hypnomatic) {
                // Held while Fire is and on the same target (a frozen pool thawing is a new one).
                bool f = false;
                const gaddr now = hand_target(memory, g, &f);
                const bool capturing = g == Gadget::Hypnomatic && hypno_capturing(memory, a.target);
                if (!capturing && (!fire || now != a.target || f != a.frozen
                                   || (g == Gadget::Hypnomatic && hypno_target(mod, memory) != a.target))) {
                    auto_cancel(memory, !fire ? "Fire let go" : "its target gone");
                    if (fire) a.swallow = true;
                }
            }
            break;
        case AutoPhase::Canceling:
            // The Thermanator, still out after a freeze: Fire at a target resumes it.
            if (g == Gadget::Thermanator && a.used && fresh && gadget_in_hand(memory, g) && memory.read<std::int32_t>(kHeroRequest) == 0
                && (memory.read<std::uint32_t>(kPadHeld) & strafe::kCircle) == 0) {
                bool f = false;
                const gaddr t = thermanator_target(mod, memory, true, &f);
                bool hf = false;
                if (t != 0 && hand_state(memory) == 2 && hand_target(memory, g, &hf) == t && hf == f) {
                    a.target = t;
                    a.frozen = f;
                    a.swallow = false;
                    set_circle(report, size, true);
                    auto_log("Thermanator again: pressed");
                    a.phase = AutoPhase::Replay;
                    a.ticks = 0;
                    return;
                }
            }
            break;
        case AutoPhase::Restoring:
            // Fire held through the swap: kept from the game until the weapon is in hand, then given
            // to it as a new press (the weapon fires).
            if (a.fire_after) set_circle(report, size, false);
            break;
        case AutoPhase::Replay:
        case AutoPhase::Capture:
            break;
    }
    if (a.swallow) set_circle(report, size, false);
}

// The frame's part: the gadget's use seen, put away, the restore seen through, the player's own
// choices kept.
void auto_frame() {
    Auto& a = g_auto;
    if (a.phase == AutoPhase::Idle) return;
    const openrac::Mod& mod = *s.mod;
    const auto memory = mod.memory();
    ++a.ticks;
    const auto status = mod.play_status();
    const std::int32_t state = memory.read<std::int32_t>(kHeroState);
    const Gadget g = a.gadget;
    const std::uint8_t form = memory.read<std::uint8_t>(kHeroForm);
    if (status.loads != a.loads) {
        auto_clear_request(memory);
        auto_log("given up (a new level)");
        auto_idle();
        return;
    }
    // The Hypnomatic's capture: the robot's control (form 4) and the way back are the game's.
    if (g == Gadget::Hypnomatic && (a.phase == AutoPhase::Replay || a.phase == AutoPhase::Active || a.phase == AutoPhase::Capture)
        && (form == kRobotForm || hypno_capturing(memory, a.target))) {
        if (a.phase != AutoPhase::Capture) {
            a.phase = AutoPhase::Capture;
            a.ticks = 0;
            a.swallow = false;
            auto_log("Hypnomatic: the robot taken");
        }
        a.saw_robot = a.saw_robot || form == kRobotForm;
        if (!a.saw_robot && a.ticks > kCaptureTicks) {
            auto_log("Hypnomatic: no robot came");
            auto_idle();
        }
        return;
    }
    // (After a capture the health is the robot's until Ratchet is back.)
    if (!a.saw_robot && (memory.read<std::int32_t>(kHeroHealth) <= 0 || state == 0x31 || state == 0x61)) {
        auto_clear_request(memory);
        auto_log("given up (he died)");
        auto_idle();
        return;
    }
    const int gitem = gadget_info(g).item;
    const int item = hand_item(memory);
    const std::int32_t request = memory.read<std::int32_t>(kHeroRequest);
    if (a.phase == AutoPhase::Capture) {
        if (form != 0) return;
        auto_cancel(memory, "Ratchet back");
    }
    // The player chose an item of their own (the Quick Select, the wrench): theirs. The wrench the
    // game puts in his hand after a capture is the game's.
    const bool game_wrench = a.saw_robot && (item == kWrenchItem || request == kWrenchItem);
    if (!game_wrench && ((item != 0 && item != gitem && item != a.previous) || (request != 0 && request != gitem && request != a.previous))) {
        auto_hand_over(memory, "the player's own choice");
        return;
    }
    switch (a.phase) {
        case AutoPhase::Equipping:
            if (a.ticks > kAutoTimeout) auto_cancel(memory, "the game did not put it in hand");
            break;
        case AutoPhase::Replay: {
            // The press taken: the Swingshot fired (its states), the Dynamo fired (2..4), the Tractor
            // Beam holding its target.
            const gaddr pv = hand_vars(memory);
            const std::int32_t state_now = memory.read<std::int32_t>(kHeroState);
            const bool used = (g == Gadget::Swingshot && (state_now == 0x23 || state_now == 0x2B || state_now == 0x8A))
                || (g == Gadget::Dynamo && hand_state(memory) >= 2 && hand_state(memory) <= 4)
                || (g == Gadget::Tractor && memory.read<gaddr>(kHeroTractorLatch) != 0)
                || (g == Gadget::Thermanator && hand_state(memory) == 3)
                || (g == Gadget::Hypnomatic && pv != 0 && memory.read<std::int32_t>(pv + 0x48) != 0);
            if (used) {
                a.used = a.used || g == Gadget::Thermanator;
                a.phase = AutoPhase::Active;
                a.ticks = 0;
                a.rest = 0;
            } else if (g != Gadget::Swingshot || a.ticks > 4) {
                auto_cancel(memory, "the game did not take the press");
            }
            break;
        }
        case AutoPhase::Active:
            if (item != gitem) break;
            if (g == Gadget::Swingshot) {
                const std::int32_t group = memory.read<std::int32_t>(kHeroGroup);
                a.rest = (group == 0 || group == 1) && !a.fire_was && memory.read<std::uint8_t>(kHeroBlocked) == 0 ? a.rest + 1 : 0;
                if (a.rest >= kSwingRest && can_equip(mod, memory)) {
                    if (swing_pick(mod, memory).node == 0) auto_restore(mod, memory, "no target near");
                    else a.rest = 0;
                }
            } else if (g == Gadget::Dynamo && hand_state(memory) <= 1) {
                auto_cancel(memory, "fired");
            } else if (g == Gadget::Tractor && memory.read<gaddr>(kHeroTractorLatch) != a.target) {
                auto_cancel(memory, "its hold lost");
            }
            break;
        case AutoPhase::Canceling: {
            if (item == a.previous && request == 0) {
                auto_idle();  // never in hand: nothing to give back
                break;
            }
            if (!can_equip(mod, memory)) break;
            if (item != gitem && !(a.saw_robot && item == kWrenchItem)) {
                if (item != 0) {
                    auto_idle();  // something else in hand: left as it is
                } else if (a.ticks >= 8 && memory.read<std::int32_t>(kHeroChosen) == gitem) {
                    auto_restore(mod, memory, "the gadget never came");
                } else if (a.ticks > kAutoTimeout) {
                    auto_idle();
                }
                break;
            }
            bool at_rest = true;
            if (g == Gadget::Dynamo) at_rest = hand_state(memory) <= 1;
            if (g == Gadget::Tractor) at_rest = (hand_state(memory) <= 1 && memory.read<gaddr>(kHeroTractorLatch) == 0) || a.ticks >= 8;
            if (g == Gadget::Thermanator) at_rest = hand_state(memory) <= 2 && (!a.used || a.ticks >= kMeterTicks);
            if (g == Gadget::Hypnomatic) at_rest = memory.read<std::uint8_t>(kHeroItemBusy) == 0 && a.ticks >= 3;
            if (at_rest) auto_restore(mod, memory, "done");
            break;
        }
        case AutoPhase::Restoring:
            if (item == a.previous && request == 0) {
                if (a.previous == kWrenchItem) memory.write<std::int32_t>(kMenuChoice, a.menu_choice);
                auto_log("restored after " + std::to_string(a.ticks) + " ticks");
                auto_idle();
            } else if (a.ticks > kAutoTimeout) {
                if (request == a.previous) memory.write<std::int32_t>(kHeroRequest, 0);
                auto_log("the item before did not come back");
                auto_idle();
            } else if (request == 0 && item == gitem && can_equip(mod, memory)) {
                memory.write<std::int32_t>(kHeroRequest, a.previous);  // the game let it go: again
            }
            break;
        case AutoPhase::Idle:
        case AutoPhase::Capture:
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// Crouch: Deadlocked's, and the wrench tossed on the run (wrench_toss.hpp)
// ---------------------------------------------------------------------------------------------
//
// Deadlocked never crouches a running hero: Crouch held on the run is a modifier (strafe::RunCrouch
// holds the game's R1 back). With it, Square and the wrench in hand toss the wrench without
// stopping him: the game's own launch (rac2-decomp func_L00_002BA508,
// hero_setup_attached_object_transform, which sends the wrench in hand off along his facing and
// back to him), his right arm posed over his run through OpenRAC's joint offsets. Adapted from
// Joebannerlord's Contextual Wrench and Fire (GPL-3.0-or-later), which launches it the same way,
// from Christian's RC1 wrench-throw mod. Started on the run only, as Deadlocked throws.

// The launch, by level (rac2-decomp config/levels/functions.tsv and config/us2/levels/functions.tsv:
// the same code, fingerprint 4c09cfa931c9e530, on both discs).
constexpr PerLevel kWrenchLaunch{
    {0x2BA508, 0x2B9EF0, 0x2BD988, 0x2BF620, 0x2E01D8, 0x2D2D88, 0x2F9C88, 0x2B8348,
     0x2CB418, 0x2BC1C0, 0x2D5288, 0x2D1FF8, 0x2BFD38, 0x2C5490, 0x2C1E08, 0x2D3388,
     0x2B9D20, 0x2BBA10, 0x2D7380, 0x2BC780, 0x2CD658, 0, 0x2BEEE0, 0x2C35E0,
     0x2B9608, 0x2CD708, 0x2BAC88, 0, 0, 0, 0x2C1308},
    {0x2BA610, 0x2BA0F8, 0x2BDB10, 0x2BF828, 0x2E03E0, 0x2D2E90, 0x2F9E90, 0x2B8450,
     0x2CB620, 0x2BC348, 0x2D5390, 0x2D2200, 0x2BFE40, 0x2C5618, 0x2C1F90, 0x2D3490,
     0x2B9E28, 0x2BBB98, 0x2D7488, 0x2BC888, 0x2CD760, 0, 0x2BEFE8, 0x2C36E8,
     0x2B9710, 0x2CD810, 0x2BAE10, 0, 0, 0, 0x2C1310}};
constexpr gaddr kHeroCatch = kHero + 0x1200;      // vec: his hand's catch point (attachment 0's place)
constexpr gaddr kHeroWallWalk = kHero + 0x348;    // u8: walking a magnetic wall (Gravity Boots)
constexpr gaddr kMobyPosition = 0x10;             // vec of a moby
constexpr gaddr kMobyState = 0x20;                // u8 of a moby: the thrown wrench's 0 in hand, 11 coming back

struct Toss {
    toss::Motion motion;
    gaddr hand = 0;  // the wrench thrown, and the load and level it was in
    std::uint32_t loads = 0;
    int level = -1;
    bool aim = false;          // Crouch held on the run with the wrench: the arm raised
    bool sampled = false;      // a pad report since the last tick
    bool in_flight = false;    // the mod's toss away
    bool posed = false;        // the arm turned
    bool square_was = true;    // Square last report (a press is its going down)
    bool square_eaten = false; // the press the toss took, kept from the game until let go
};
Toss g_toss;

// Whether the wrench in hand can be tossed now, or (`start` false) a toss already going can go on:
// on foot as Ratchet, nothing stopping his hand, the wrench in it and ready; to start, on the run
// (or, Joe Wrench, in the air).
bool toss_ok(const openrac::Mod& mod, const openrac::Memory& memory, bool start) {
    if (mod.play_status().phase != OPENRAC_PLAY_PLAYING || level_function(mod, kWrenchLaunch) == 0) return false;
    const std::int32_t group = memory.read<std::int32_t>(kHeroGroup), state = memory.read<std::int32_t>(kHeroState);
    const bool air = group == 2 || group == 4;
    if (start ? !(group == 1 || (air && s.crouch == CrouchMode::JoeWrench)) : !(group == 0 || group == 1 || air || group == 12)) return false;
    if (state == 1 || state == 0x1D || state == 0x31 || state == 0x61) return false;  // aiming, mounted
    if (!in_ram(memory.read<gaddr>(kHeroMoby)) || memory.read<std::int32_t>(kHeroHealth) <= 0
        || memory.read<gaddr>(kHeroVehicle) != 0 || memory.read<std::uint8_t>(kHeroForm) != 0) {
        return false;
    }
    if (memory.read<std::uint8_t>(kHeroWrenchForced) != 0 || memory.read<std::uint8_t>(kHeroBlocked) != 0
        || memory.read<std::uint8_t>(kHeroNoUpdate) != 0 || memory.read<std::uint8_t>(kHeroTucked) != 0
        || memory.read<std::int32_t>(kPadMode) != 0 || memory.read<std::uint8_t>(kHeroWallWalk) != 0) {
        return false;
    }
    const gaddr hand = memory.read<gaddr>(kHeroHand);
    if (!moby_live(memory, hand) || memory.read<std::int32_t>(kHeroHandMode) != 2
        || memory.read<std::int32_t>(kHeroItem) != kWrenchItem || memory.read<std::uint32_t>(kHeroHandButton) != strafe::kSquare
        || memory.read<std::int16_t>(hand + kMobyClass) != kWrenchClass) {
        return false;
    }
    if (start && memory.read<std::uint8_t>(kHeroMelee) != 0) return false;
    const std::int32_t request = memory.read<std::int32_t>(kHeroRequest), chosen = memory.read<std::int32_t>(kHeroChosen);
    return (request == 0 || request == kWrenchItem) && (chosen == 0 || chosen == kWrenchItem);
}

void toss_pose(const openrac::Mod& mod, const toss::ArmPose& pose) {
    std::vector<openrac_joint_offset> joints;
    for (const toss::JointTurn& j : toss::arm(pose)) {
        joints.push_back({j.joint, {j.rotation[0], j.rotation[1], j.rotation[2], j.rotation[3]}, {0.0f, 0.0f, 0.0f}});
    }
    if ((!joints.empty() || g_toss.posed) && mod.hero_set_joints(joints)) g_toss.posed = !joints.empty();
}

void toss_cancel(const openrac::Mod& mod) {
    g_toss.motion.cancel();
    g_toss.in_flight = false;
    g_toss.hand = 0;
    if (g_toss.posed) toss_pose(mod, {});
}

// Each tick, after the pad: the swing stepped, the game's launch at its release, the arm posed.
void toss_tick(const openrac::Mod& mod) {
    const auto memory = mod.memory();
    if (s.crouch == CrouchMode::Standard || !toss_ok(mod, memory, false)) {
        g_toss.sampled = false;
        if (g_toss.hand != 0 || g_toss.posed) toss_cancel(mod);
        return;
    }
    if (!g_toss.sampled) return;
    g_toss.sampled = false;
    const gaddr hand = memory.read<gaddr>(kHeroHand);
    const auto status = mod.play_status();
    if (hand != g_toss.hand || status.loads != g_toss.loads || status.level != g_toss.level) {
        toss_cancel(mod);
        g_toss.hand = hand;
        g_toss.loads = status.loads;
        g_toss.level = status.level;
    }
    const std::uint8_t wrench = memory.read<std::uint8_t>(hand + kMobyState);
    const bool ready = wrench == 0 && memory.read<std::uint8_t>(kHeroMelee + 1) == 0;
    const bool caught = g_toss.in_flight && ready;
    if (caught) g_toss.in_flight = false;
    float reach = 0.0f;
    if (g_toss.in_flight && wrench == 11) {
        const V3 off = vec::sub(read_v3(memory, hand + kMobyPosition), read_v3(memory, kHeroCatch));
        reach = toss::catch_reach(std::sqrt(vec::dot(off, off)));
    }
    const toss::Step step = g_toss.motion.step(g_toss.aim, ready, reach, caught);
    if (step.launch) {
        const gaddr at = level_function(mod, kWrenchLaunch);
        const auto launch = at != 0 ? mod.function_at<void (*)()>(at) : nullptr;
        if (launch != nullptr) {
            launch();
            g_toss.in_flight = memory.read<std::uint8_t>(kHeroMelee + 1) != 0;
        }
    }
    toss_pose(mod, step.pose);
}

// The pad's part, in on_pad after the player's buttons are the game's: Crouch held back on the run
// (strafe::RunCrouch), Square with it tossing the wrench (taken from the game until let go).
void crouch_pad(const openrac::Mod& mod, const openrac::Memory& memory, std::uint8_t* report, int size, bool deadlocked) {
    const auto down = [&](std::uint8_t bit) { return size >= 2 && (report[strafe::kButtonsHigh] & bit) == 0; };
    const bool crouch = down(strafe::kR1), square = down(strafe::kSquare);
    const bool press = square && !g_toss.square_was;
    g_toss.square_was = square;
    if (!deadlocked) {
        s.run_crouch.reset();
        g_toss.aim = g_toss.square_eaten = false;
        return;
    }
    const std::int32_t group = memory.read<std::int32_t>(kHeroGroup);
    const bool moving = group == 1 || group == 2 || group == 4;  // running, or in the air
    const bool can_toss = crouch && toss_ok(mod, memory, true);
    g_toss.aim = can_toss;
    bool used = false, to_game = false;
    const bool tossing_here = group == 1 || (moving && s.crouch == CrouchMode::JoeWrench);
    if (press && crouch && tossing_here) {
        if (can_toss) {
            g_toss.motion.request();
            g_toss.square_eaten = used = true;
        } else if (group == 1) {
            to_game = true;  // no wrench to toss: the game's own throw (it brings the wrench out)
        }
    }
    if (!square) g_toss.square_eaten = false;
    if (g_toss.square_eaten && size >= 2) {
        report[strafe::kButtonsHigh] = static_cast<std::uint8_t>(report[strafe::kButtonsHigh] | strafe::kSquare);
        if (size >= strafe::kFullReport) report[strafe::kSquarePressure] = 0;
    }
    g_toss.sampled = true;
    using R1 = strafe::RunCrouch::R1;
    const R1 r1 = s.run_crouch.step(moving, crouch, down(strafe::kCross), to_game, used);
    if (r1 == R1::Hide && size >= 2) {
        report[strafe::kButtonsHigh] = static_cast<std::uint8_t>(report[strafe::kButtonsHigh] | strafe::kR1);
        if (size >= strafe::kFullReport) report[strafe::kR1Pressure] = 0;
    } else if (r1 == R1::Press && size >= 2) {
        report[strafe::kButtonsHigh] = static_cast<std::uint8_t>(report[strafe::kButtonsHigh] & ~strafe::kR1);
        if (size >= strafe::kFullReport) report[strafe::kR1Pressure] = 0xFF;
    }
}

void on_pad(int port, std::uint8_t* report, int size) {
    if (port != 0 || report == nullptr) {
        return;
    }
    const openrac::Mod& mod = *s.mod;
    const auto memory = mod.memory();
    s.intent = strafe::read_intent(report, size);
    s.intent_at = std::chrono::steady_clock::now();
    const bool menu = strafe::opening_menu(report, size);
    const strafe::Gate gate = menu ? strafe::Gate::Off : gate_now(mod);
    const std::int32_t item = memory.read<std::int32_t>(kHeroItem);
    strafe::Config config = s.config;
    config.lock_strafe = s.lock == Lock::Always
        || (s.lock == Lock::Weapon && gate != strafe::Gate::Off
            && is_weapon(g_auto.phase != AutoPhase::Idle && g_auto.previous != 0 ? g_auto.previous : item));
    // The Hypnomatic's robot: the player's layout on its buttons. Fire is its attack (Square) or,
    // for the Receiver Bot (none), its beam (Circle); Wrench is the beam; the face buttons swapped so
    // Circle shoots and Square beams, as Ratchet's fire and wrench sit. Crouch (it has none) toggles
    // the strafe, held for it as for Ratchet with a weapon; Aim lets the strafe go (it looks only
    // standing still).
    const std::uint8_t form = memory.read<std::uint8_t>(kHeroForm);
    if (form == kRobotForm) {
        const gaddr body = memory.read<gaddr>(kHeroMoby);
        const bool attacks = in_ram(body) && memory.read<std::int16_t>(body + kMobyClass) == kAttackRobotClass;
        config.lock_strafe = s.lock != Lock::Off && gate != strafe::Gate::Off;
        config.fire_button = attacks ? strafe::kSquare : strafe::kCircle;
        config.wrench_button = strafe::kCircle;
        config.swap_circle_square = true;
        config.crouch_toggles = true;
        config.aim_releases_strafe = true;
    }
    // The new page: L1 / R1 change the tab, Square asks to restore the defaults (taken in its frame).
    if (g_fancy && g_shown == -1 && size >= 2) {
        const std::uint8_t pressed = static_cast<std::uint8_t>(~report[strafe::kButtonsHigh]);
        const std::uint8_t fresh = static_cast<std::uint8_t>(pressed & ~s.page_buttons);
        if (fresh & strafe::kL1) g_tab_step = -1;
        if (fresh & strafe::kR1) g_tab_step = 1;
        if (fresh & strafe::kSquare) g_square = true;
        s.page_buttons = pressed;
    }
    const std::uint32_t mouse = own_mouse_buttons(mod);  // (older OpenRAC)
    const strafe::Outcome done = strafe::rewrite(report, size, config, gate, mouse);
    // In a turret, or steering a thrown Spiderbot or Visibomb (state 0x1C), the pad otherwise the
    // game's: the player's Fire is its Circle (the trigger; the remote's detonation). Only in play:
    // paused, or with the pad masked, the report is the menu's.
    const bool remote = memory.read<std::int32_t>(kHeroState) == kRemoteState;
    const bool in_play = mod.play_status().phase == OPENRAC_PLAY_PLAYING && memory.read<std::int32_t>(kPadMode) == 0;
    if ((s.in_turret || remote) && in_play && gate == strafe::Gate::Off && !menu) {
        strafe::fire_only(report, size, config, mouse);
    }
    if (form == 0) auto_pad(report, size, gate);
    // Crouch: Deadlocked's on the run, and the wrench toss (crouch_pad). In play only.
    crouch_pad(mod, memory, report, size, s.crouch != CrouchMode::Standard && form == 0 && in_play && !menu && gate == strafe::Gate::Active);
    s.aim_held = done.aim;
    // Aim with movement: the aim button asks for the other view (first person, or third person for a
    // player in first person); the game's L1 and L2 kept from it (in first person they fire), and
    // its own strafing used (it strafes in first person). Started on foot or mid-swing (the
    // Swingshot: the game takes first person there); once on, kept through his busy moments (a throw
    // of the Chopper or Multi-Star is a state of its own: dropping it there zoomed out to show the
    // throw).
    const std::int32_t group = memory.read<std::int32_t>(kHeroGroup);
    const bool swinging = group == 13 || group == 14 || group == 33;
    s.fp_want = s.modern && s.aim_move && done.aim && form == 0
        && (gate == strafe::Gate::Active
            || ((s.fp_active || s.tp_active || swinging) && gate == strafe::Gate::Busy));
    if (!done.aim) s.fp_refused = false;
    if ((s.fp_want && !s.fp_refused) || s.fp_active || s.tp_active) {
        if (size >= 2) report[strafe::kButtonsHigh] = static_cast<std::uint8_t>(report[strafe::kButtonsHigh] | strafe::kL1 | strafe::kL2);
        if (size >= strafe::kFullReport) report[strafe::kL2Pressure] = report[strafe::kL1Pressure] = 0;
    }
    // The game's aim button (L1 once mapped) held: aiming starts next tick (its reticle swapped in time).
    s.aim_input = size >= 2 && ((report[strafe::kButtonsHigh] & strafe::kL1) == 0 || (s.fp_want && !s.fp_refused));
    s.stick_live = size > kRightStickY && (std::abs(static_cast<int>(report[kRightStickX]) - 0x80) >= 24
                                           || std::abs(static_cast<int>(report[kRightStickY]) - 0x80) >= 24);
    s.left_live = size > strafe::kLeftStickY && (std::abs(static_cast<int>(report[strafe::kLeftStickX]) - 0x80) >= 24
                                                  || std::abs(static_cast<int>(report[strafe::kLeftStickY]) - 0x80) >= 24);
    if (size > kRightStickY) {
        s.stick_x = (static_cast<int>(report[kRightStickX]) - 0x80) / 127.0f;
        s.stick_y = (static_cast<int>(report[kRightStickY]) - 0x80) / 127.0f;
    }
    own_pad_stick(report, size);  // (older OpenRAC)
    // Modern, in third person: the right stick is the mod's (its camera), the game's at rest.
    if (s.stick_owned && size > kRightStickY) {
        report[kRightStickX] = 0x80;
        report[kRightStickY] = 0x80;
        s.stick_live = false;
    }
}

// ---------------------------------------------------------------------------------------------
// The camera
// ---------------------------------------------------------------------------------------------

// The follow camera on screen now: the camera in use, settled (no other camera blending in or out:
// the block's phase).
bool follow_live(const openrac::Mod& mod, const openrac::Memory& memory, gaddr camera) {
    const gaddr slot = camera_slot(mod);
    return camera != 0 && memory.read<gaddr>(slot) == camera && memory.read<std::int16_t>(camera + kCamType) == 0
        && memory.read<std::int16_t>(slot + kPhaseAboveSlot) == 0;
}

// The world's up as the follow camera has it (the block's up, 0x30 above the slot, sprung toward
// the hero's own on a planetoid or in an arena's gravity): straight up but there.
bool world_up(const openrac::Mod& mod, const openrac::Memory& memory) {
    const float z = memory.read<float>(camera_slot(mod) + 0x38);
    return !(z < 0.999f && z > -2.0f);  // unset (0 or junk) taken as straight up
}

// That up itself (straight up where it is unset).
V3 camera_up(const openrac::Mod& mod, const openrac::Memory& memory) {
    const V3 u = read_v3(memory, camera_slot(mod) + 0x30);
    const float l = length(u);
    return l > 0.5f && l < 1.5f ? V3{u.x / l, u.y / l, u.z / l} : V3{0, 0, 1};
}

// On a planetoid's curve (more than about 18 degrees off straight up): the game's first person
// is built about straight up, so it is not used there.
bool on_planetoid(const openrac::Mod& mod, const openrac::Memory& memory) {
    return camera_up(mod, memory).z < 0.95f;
}

// The game's camera override slots (docs/notes/rac2-camera-situations-2.md): bosses, arenas,
// zones, vehicles set the follow camera's distance, height and tilt through one of 32 slots of
// 0x40 bytes (the selected one's index at the first address, -1 none), chosen at the start of each
// camera tick. One that shapes the shot (a tilt given, a lock or steer flag, a low or far camera)
// is the game's own framing: the boom stands aside for it.
constexpr ByDisc kOverrideIndex{0x1B1884, 0x1B1804}, kOverrideTable{0x1BB9F0, 0x1BB970};
bool camera_overridden(const openrac::Mod& mod, const openrac::Memory& memory) {
    const std::int32_t index = memory.read<std::int32_t>(level0_global(mod, kOverrideIndex));
    if (index < 0 || index >= 32) return false;
    const gaddr slot = level0_global(mod, kOverrideTable) + static_cast<gaddr>(index) * 0x40;
    const float distance = memory.read<float>(slot), height = memory.read<float>(slot + 0x04), tilt = memory.read<float>(slot + 0x0C);
    bool flags = false;
    for (gaddr o : {0x18u, 0x19u, 0x1Au, 0x1Eu, 0x1Fu}) flags = flags || memory.read<std::uint8_t>(slot + o) != 0;
    return tilt >= 0.0f || flags || (height >= 0.0f && height < 1.0f) || distance > 6.0f;
}

// Plain play for the mod's camera and aim: Ratchet, on foot or in a move of his own, the follow
// camera on screen, no override of the game's framing the shot (on a planetoid too).
bool mod_camera_here(const openrac::Mod& mod, const openrac::Memory& memory, gaddr camera) {
    const std::int32_t group = memory.read<std::int32_t>(kHeroGroup);
    // Grinding (15, 16) the game's camera follows the rail itself: it is left to it, as its other
    // set pieces are, and the boom takes the view back on stepping off.
    return follow_live(mod, memory, camera) && memory.read<std::uint8_t>(kHeroForm) == 0
        && memory.read<gaddr>(kHeroVehicle) == 0 && (foot_group(group) || move_group(group)) && group != 15 && group != 16
        && !scripted_state(memory.read<std::int32_t>(kHeroState)) && !camera_overridden(mod, memory);
}

// Turns the point at `a` about the line through `p` along `u` (a unit vector) by an angle of
// cosine c and sine sn (Rodrigues).
void turn_about(const openrac::Memory& memory, gaddr a, V3 p, V3 u, float c, float sn) {
    const V3 v = vec::sub(read_v3(memory, a), p);
    const V3 t = vec::add(vec::add(vec::mul(v, c), vec::mul(vec::cross(u, v), sn)), vec::mul(u, vec::dot(u, v) * (1.0f - c)));
    write_v3(memory, a, vec::add(p, t));
}

// Turns the follow camera about up by `yaw` (radians), as the Keyboard and mouse mod does in
// Ratchet & Clank: the orbit and its smoothed copy, and the eyes carried round the pivot.
void turn_camera(const openrac::Memory& memory, gaddr camera, float yaw) {
    const gaddr state = memory.read<gaddr>(camera + kCamState);
    if (state == 0 || yaw == 0.0f) return;
    // About the camera's up (straight up, or a planetoid's).
    const V3 pivot = read_v3(memory, state + kStatePivot);
    const V3 u = camera_up(*s.mod, memory);
    const float c = std::cos(yaw), sn = std::sin(yaw);
    for (gaddr off : kStateOrbits) turn_about(memory, state + off, {0, 0, 0}, u, c, sn);
    for (gaddr off : kStateEyes) turn_about(memory, state + off, pivot, u, c, sn);
    turn_about(memory, camera + kCamEye, pivot, u, c, sn);
}



// ---------------------------------------------------------------------------------------------
// The gyro (a controller's, read from its raw reports beside the game; Linux)
// ---------------------------------------------------------------------------------------------
//
// OpenRAC gives mods no gyro, so the mod reads the controller itself, the way OpenRAC's own gyro
// does: the reports of every controller whose gyro SDL 3.4.18 (the SDL OpenRAC is built with) reads
// by a plain layout (gyro_hid.hpp: DualSense, DualShock 4, Switch Pro, Joy-Con, Steam Deck, the
// HORIPAD for Steam), through OpenRAC's own filter (gyro_aim.hpp, its port/platform/gyro.cpp). It
// serves OpenRAC's gyro settings on the boom (which throws OpenRAC's own turn away), and the mod's
// own gyro on older OpenRAC builds.
//
// The nodes are opened read-only: no command is sent to a controller. Linux's own drivers turn the
// motion of Sony's and Nintendo's pads on when they connect (hid-playstation reads a DualShock 4's
// calibration, which brings its full reports over Bluetooth; hid-nintendo turns a Switch pad's IMU
// on); the Steam Deck sends it anyway. Third-party Switch pads get theirs only from OpenRAC's SDL,
// while OpenRAC's gyro is on. The one request beyond the reports is a PlayStation pad's calibration
// (a feature report SDL and hid-playstation read too), once, at open.

class GyroDevice {
public:
    // Reads every controller's reports, the one in use through the filter (with `options`); the
    // turn gathered since the last call (yaw > 0 left, pitch > 0 up, radians, the sensitivity in
    // it). False while no controller sends motion.
    bool poll(openrac::Mod& mod, const gyro::GyroOptions& options, float& yaw, float& pitch) {
        yaw = pitch = 0.0f;
#if DEAD_AIM_GYRO_READER
        const auto now = std::chrono::steady_clock::now();
        if (now - last_scan_ >= std::chrono::seconds(2)) {
            last_scan_ = now;
            scan(mod);
        }
        aim_.set_options(options);
        bool motion = false;
        std::uint8_t buffer[128];
        for (std::size_t i = 0; i < nodes_.size();) {
            Node& n = nodes_[i];
            bool gone = false;
            for (int k = 0; k < 64; ++k) {
                const ssize_t got = ::read(n.fd, buffer, sizeof buffer);
                if (got < 0) {
                    gone = errno != EAGAIN && errno != EWOULDBLOCK;
                    break;
                }
                const gyro::Report r = gyro::parse_report(n.link, n.stream, buffer, static_cast<int>(got), now_ns());
                if (!r.ok) continue;
                // The controller in use (OpenRAC aims with player 1's): the one whose buttons last
                // changed, or the first to send motion. A pair of Joy-Cons aims with the right's.
                const bool pressed = r.buttons != n.buttons;
                n.buttons = r.buttons;
                if ((pressed && r.buttons != 0) || (active_ < 0 && r.count > 0)) use(aiming_node(i));
                if (static_cast<int>(i) != active_ || !r.primary) continue;
                const std::uint16_t partner = left_buttons();
                for (int j = 0; j < r.count; ++j) {
                    const gyro::Sample& m = r.samples[j];
                    aim_.button(((m.buttons | partner) & gyro::button_bit(options.button)) != 0);
                    aim_.gyro(m.gyro, m.ns);
                    aim_.accel(m.accel);
                    motion = true;
                }
            }
            if (gone) {
                mod.warn(std::string("gyro: the ") + n.device->name + " went (" + std::strerror(errno) + ")");
                drop(i);
                continue;
            }
            ++i;
        }
        aim_.take(yaw, pitch);
        if (motion && !told_motion_) {
            mod.info(std::string("gyro: motion from the ") + nodes_[static_cast<std::size_t>(active_)].device->name);
            told_motion_ = true;
        }
        if (aim_.calibrated() && !told_settled_) {
            mod.info("gyro: calibrated (it keeps learning its rest whenever the controller is held steady)");
            told_settled_ = true;
        }
        return motion;
#else
        (void)mod; (void)options;
        return false;
#endif
    }

    // For the menu (older OpenRAC): the controller in use, and its rest learnt.
    const char* name() const {
#if DEAD_AIM_GYRO_READER
        return active_ >= 0 ? nodes_[static_cast<std::size_t>(active_)].device->name : nullptr;
#else
        return nullptr;
#endif
    }
    bool calibrated() const { return aim_.calibrated(); }

    // Every node let go (no gyro wanted); the next poll looks for them again.
    void close() {
#if DEAD_AIM_GYRO_READER
        while (!nodes_.empty()) drop(nodes_.size() - 1);
        last_scan_ = {};
#endif
    }

private:
#if DEAD_AIM_GYRO_READER
    struct Node {
        std::string name;  // hidrawN
        int fd = -1;
        const gyro::Device* device = nullptr;
        int interface_number = -1;
        gyro::Link link;
        gyro::Stream stream;
        std::uint16_t buttons = 0;
    };

    static std::uint64_t now_ns() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    static std::string read_line(const std::string& path, const char* key) {
        std::string value;
        if (FILE* f = std::fopen(path.c_str(), "r")) {
            char line[256];
            while (std::fgets(line, sizeof line, f) != nullptr) {
                if (key == nullptr || std::strncmp(line, key, std::strlen(key)) == 0) {
                    value = line + (key != nullptr ? std::strlen(key) : 0);
                    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
                    break;
                }
            }
            std::fclose(f);
        }
        return value;
    }

    // Opens each hidraw node of a controller in the table not open yet (a Steam Deck has several:
    // its parser takes only the controller's reports), and pairs the Joy-Cons as SDL does: a left
    // and a right both there make a pair.
    void scan(openrac::Mod& mod) {
        DIR* dir = ::opendir("/sys/class/hidraw");
        if (dir == nullptr) return;
        while (const dirent* e = ::readdir(dir)) {
            const std::string name = e->d_name;
            if (name.rfind("hidraw", 0) != 0) continue;
            if (std::any_of(nodes_.begin(), nodes_.end(), [&](const Node& n) { return n.name == name; })) continue;
            const std::string sys = "/sys/class/hidraw/" + name + "/device";
            // HID_ID=bus:vendor:product, in hex: bus 0003 USB, 0005 Bluetooth.
            unsigned bus = 0, vendor = 0, product = 0;
            if (std::sscanf(read_line(sys + "/uevent", "HID_ID=").c_str(), "%x:%x:%x", &bus, &vendor, &product) != 3) continue;
            const gyro::Device* device = gyro::find_device(static_cast<std::uint16_t>(vendor), static_cast<std::uint16_t>(product));
            if (device == nullptr) continue;
            const std::string node = "/dev/" + name;
            const int fd = ::open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                if (!told_denied_) {
                    mod.warn(std::string("gyro: a ") + device->name + " is at " + node + " but it cannot be read ("
                             + std::strerror(errno) + "); Steam's controller rules (steam-devices) or a udev rule give access to it");
                    told_denied_ = true;
                }
                continue;
            }
            Node n;
            n.name = name;
            n.fd = fd;
            n.device = device;
            // The USB interface (the charging grip has one per Joy-Con): the parent of the HID device.
            const std::string iface = read_line(sys + "/../bInterfaceNumber", nullptr);
            n.interface_number = iface.empty() ? -1 : static_cast<int>(std::strtol(iface.c_str(), nullptr, 16));
            n.link = gyro::link_for(*device, n.interface_number);
            n.link.calibration = calibration(fd, *device, bus == 0x0005);
            mod.info(std::string("gyro: ") + device->name + " at " + node + (n.link.calibration ? " (calibrated by the pad)" : ""));
            nodes_.push_back(std::move(n));
        }
        ::closedir(dir);
        pair_joycons();
    }

    // A PlayStation pad's calibration (feature report 0x05 of a DualSense; a DualShock 4's 0x02 on
    // USB and through Sony's adapter, 0x05 over Bluetooth), as SDL reads it; none for the others
    // (SDL's fallback scale).
    static std::optional<gyro::ImuCalibration> calibration(int fd, const gyro::Device& device, bool bluetooth) {
        std::uint8_t buffer[64] = {};
        const auto feature = [&](std::uint8_t id) {
            buffer[0] = id;
            return ::ioctl(fd, HIDIOCGFEATURE(sizeof buffer), buffer);
        };
        if (device.family == gyro::Family::DualSense) {
            const int got = feature(0x05);
            return got > 0 ? gyro::dualsense_calibration(buffer, got) : std::nullopt;
        }
        if (device.family == gyro::Family::DualShock4) {
            const bool adapter = device.product == 0x0BA0;
            const int got = feature(bluetooth && !adapter ? 0x05 : 0x02);
            return got > 0 ? gyro::dualshock4_calibration(buffer, got, bluetooth || adapter) : std::nullopt;
        }
        return std::nullopt;
    }

    bool joycon(const Node& n, gyro::JoyConSide side) const {
        return n.device->family == gyro::Family::JoyCon && n.link.side == side;
    }

    void pair_joycons() {
        const auto any = [&](gyro::JoyConSide side) {
            return std::any_of(nodes_.begin(), nodes_.end(), [&](const Node& n) { return joycon(n, side); });
        };
        const bool paired = any(gyro::JoyConSide::Left) && any(gyro::JoyConSide::Right);
        for (Node& n : nodes_) {
            if (n.device->family == gyro::Family::JoyCon && n.link.paired != paired) {
                n.link.paired = paired;
                n.stream = {};
            }
        }
    }

    // The node that aims for node i: itself, or for the left Joy-Con of a pair, the right one.
    int aiming_node(std::size_t i) const {
        const Node& n = nodes_[i];
        if (n.link.paired && joycon(n, gyro::JoyConSide::Left)) {
            for (std::size_t j = 0; j < nodes_.size(); ++j) {
                if (joycon(nodes_[j], gyro::JoyConSide::Right)) return static_cast<int>(j);
            }
        }
        return static_cast<int>(i);
    }

    // A pair's left Joy-Con's buttons, for the gyro button on the right one's samples.
    std::uint16_t left_buttons() const {
        if (active_ < 0 || !nodes_[static_cast<std::size_t>(active_)].link.paired) return 0;
        for (const Node& n : nodes_) {
            if (joycon(n, gyro::JoyConSide::Left)) return n.buttons;
        }
        return 0;
    }

    // Another controller in use: the filter starts over (its rest, its smoothing), as OpenRAC's
    // does for a new controller.
    void use(int node) {
        if (node == active_) return;
        active_ = node;
        aim_.reset();
        told_motion_ = told_settled_ = false;
    }

    void drop(std::size_t i) {
        ::close(nodes_[i].fd);
        nodes_.erase(nodes_.begin() + static_cast<std::ptrdiff_t>(i));
        if (active_ == static_cast<int>(i)) {
            active_ = -1;
            aim_.reset();
            told_motion_ = told_settled_ = false;
        } else if (active_ > static_cast<int>(i)) {
            --active_;
        }
        pair_joycons();
    }

    std::vector<Node> nodes_;
    int active_ = -1;
    std::chrono::steady_clock::time_point last_scan_{};
    bool told_denied_ = false, told_motion_ = false, told_settled_ = false;
#endif
    gyro::GyroAim aim_;
};

GyroDevice g_gyro;

// The game's tilt override (docs/notes/rac2-follow-camera-tilt.md): with bit 2 of +0x1D0 set the
// tilt steps toward +0x1E4 / kFullTilt through the stick's dead zone instead of the stick's y. The
// boom sets it to its own lift, so the game's tilt never pulls against it.
constexpr gaddr kStateTiltMask = 0x1D0, kStateTiltOverride = 0x1E4;
constexpr float kFullTilt = 0.6981318f;  // the game's tilt at a full stick (40 degrees)
constexpr float kTiltDeadZone = 0.3f;

// The game's tilt asked for through its override: `t` of a full stick (-1..1, > 0 the eye high),
// past the stick's dead zone as the game takes the stick's.
void ask_tilt(const openrac::Memory& memory, gaddr state, float t) {
    memory.write<std::uint32_t>(state + kStateTiltMask, memory.read<std::uint32_t>(state + kStateTiltMask) | 2u);
    memory.write<float>(state + kStateTiltOverride,
                        t == 0.0f ? 0.0f : kFullTilt * std::copysign(kTiltDeadZone + (1.0f - kTiltDeadZone) * std::fabs(t), t));
}

// The boom camera (Modern), as Up Your Arsenal keeps its lock-strafe camera out of the
// world (docs/notes/rac3-strafe-camera.md) on Going Commando's follow camera (docs/notes/
// rac2-spring-arm.md): the camera is not moved through the level and pushed out after; each tick
// it is put afresh on a boom from the hero's camera pivot (+0xB0, which the game keeps clear), at
// the player's turn and lift, as long as the game's follow distance (+0x18C) unless a sphere swept
// along the boom (docs/notes/rac2-sphere-collision.md) meets the level first: then just short of it.
// It comes in at once behind a wall, quickly otherwise, and goes out once the way has been clear a
// moment, eased as Up Your Arsenal's. Written to the goal and smoothed orbits alike (the eye goes
// only by the springs from one to the other, so it stays), the eyes, with the springs stopped, the
// game's shortfall and sweep told to leave the length be, and its tilt made the lift's.
constexpr gaddr kStateAnchor = 0xC0, kStateDistance = 0x18C, kStateHeight = 0x190;
// The boom's hang point over the game's smoothed point: Up Your Arsenal's eye height (docs/notes/
// rac3-strafe-camera.md: eyeH 2.0). The game hangs its view from that point by its own height (the
// height spring, +0x28, eased toward +0x190), which a level may set otherwise: 4 on the Jamming
// Array's planetoids, where the game tips its view down to him. So the boom holds both at its own.
constexpr float kHangHeight = 2.0f;
constexpr gaddr kStateEyeSprings[3] = {0x180, 0x184, 0x188};
constexpr gaddr kStateShortfall = 0x220, kStateShortTimer = 0x224, kStateShortLock = 0x22A;
constexpr gaddr kStateNoSweep = 0x23E;
// What the game's view is built from (rac2-decomp src/levels/shared/L00_004224D0-follow-g8.c,
// follow_camera_build_view): it looks from the eye (+0xC0 smoothed + up x height + the orbit) toward
// his look point (+0xA0, unlagged) across, and by the orbit's rise plus the height less the look
// height (springs +0x28 and +0x24) up and down, turned by an extra yaw (+0x48) and a look angle
// (+0x34). Its own lag across, that half unit, and those two turns are what tipped the view off
// the boom, the more the shorter it was (39 degrees at its shortest).
constexpr gaddr kStateFeet = 0x60, kStateSmoothVel[2] = {0xD0, 0xE0};
constexpr gaddr kStateLookHeightNow = 0x24, kStateHeightNow = 0x28, kStateLookHeightVel = 0x2C, kStateHeightVel = 0x30;
constexpr gaddr kStateLookAngle = 0x34, kStateLookAngleVel = 0x38, kStateExtraYaw = 0x48;
constexpr float kBoomMargin = 0.35f, kBoomMin = 0.25f;
// Where there is little room behind him the camera comes in close, into his head at the closest,
// and he fades (his model, the item in his hand and Clank on his back, by the game's own alpha,
// +0x23 of a moby: 0x80 opaque) from kFadeFar in to kFadeAlpha at kFadeNear, as Deadlocked fades
// its player. (The stock camera stops at 1.5 and lets the walls in.)
constexpr float kFadeFar = 1.3f, kFadeNear = 0.5f;
constexpr int kFadeAlpha = 0x14;
// The game's own camera, though the boom places the view, still runs, and once it finds itself
// against the walls (its arm is never shorter than 1.5) it sets itself anew on the far side of him.
// With less than kStuckLength behind him the tick before, a jump of its direction is that rescue,
// not a respawn or a door, and is not taken: taking it spun the view round him.
constexpr float kStuckLength = 2.0f;
constexpr float kAnchorOutSeconds = 0.5f;
constexpr float kRoomInSeconds = 0.05f;  // the hang point lowered under a roof: eased, not snapped
constexpr float kMobyRoom = 0.45f;         // the camera kept this far off a moby's faces
constexpr int kWantHold = 30;  // ticks the nearest want is held
constexpr float kAutoJump = 0.5f;  // radians in a tick: a turn this big in one tick is the game's own (a respawn, a door)
constexpr float kHangRadius = 0.4f, kArmRadius = 0.35f;  // the sweeps' spheres (UYA: 0.4 and the arm)
constexpr float kChestHeight = 0.7f;  // UYA's ceiling test starts this far over his feet
constexpr float kEaseAccel = 0.00556f, kEaseTop = 0.333f;  // u a tick, a tick; u a tick (UYA NTSC)
constexpr float kBoomRadius = 0.3f;   // the lines round the middle one
constexpr float kBoomSlack = 0.15f;   // out only when the way is clear by more than this
constexpr float kBoomInSeconds = 0.08f;  // in, smoothly, when nothing is in front of it yet
constexpr int kBoomClearTicks = 6;   // and has been for this long
constexpr float kBoomHighest = 70.0f * kDeg, kBoomLowest = -60.0f * kDeg;  // the camera above / below
// Free Movement's ease into first person: this many ticks, the boom in along the view to the shortest,
// its hang point down to the first-person eye's height (the one view_from_camera gives the game).
constexpr int kAimEaseTicks = 10;
constexpr float kAimEaseEye = 1.5f;



// The world as the boom sees it: a moby the line meets is passed and the line goes on from just
// past it; with `mobys`, only a moby on Ratchet (Clank on his back, his gun) is, and further out a
// moby (a crate, the ship) stops it as the level does, so the camera is not put inside one for the
// game to push out again (that pushing shook it).
constexpr float kNearMoby = 2.0f;  // a moby this near Ratchet is on him
constexpr std::int16_t kClankOnBack = 1007;
bool level_line(const openrac::Mod& mod, const openrac::Memory& memory, const float from[3], const float to[3], LineHit& out,
                bool mobys = false) {
    float start[3] = {from[0], from[1], from[2]};
    const float d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length < 1e-4f) {
        out = LineHit{};
        return true;
    }
    for (int tries = 0; tries < 6; ++tries) {
        if (!game_line(mod, memory, start, to, out)) return false;
        if (!out.hit || !in_ram(out.moby)) return true;
        // A moby on Ratchet (Clank on his back, his gun) is passed: its place is his.
        const V3 hero = read_v3(memory, kHeroPosition);
        const V3 at = read_v3(memory, out.moby + 0x10);
        const float mx = at.x - hero.x, my = at.y - hero.y, mz = at.z - hero.z;
        const bool on_him = !mobys || mx * mx + my * my + mz * mz < kNearMoby * kNearMoby
            || memory.read<std::int16_t>(out.moby + kMobyClass) == kClankOnBack;
        if (!on_him) return true;
        for (int k = 0; k < 3; ++k) start[k] = out.point[k] + d[k] / length * 0.05f;
        const float left[3] = {to[0] - start[0], to[1] - start[1], to[2] - start[2]};
        if (left[0] * d[0] + left[1] * d[1] + left[2] * d[2] <= 0.0f) {
            out = LineHit{};
            return true;
        }
    }
    out = LineHit{};
    return true;
}


// ---- A sphere swept through the level (docs/notes/rac2-sphere-collision.md) ----
// Going Commando has no sweep, but CollSphere (each level's copy at CollLine + 0x1060) lists the
// triangles within a radius of a point (flags 0x40; two-sided with 0x10) at block + 0x5280, 64 bytes
// each (V0, V1, V2). The sweep is Up Your Arsenal's: the path cut in pieces of 3 u or less, each
// piece's triangles listed and swept exactly here. Unlike a line, a sphere meets an edge, a branch or
// a grazing floor continuously as it moves: no flicker. Mobys are left out as the game's camera
// leaves them out (it passes over the ship and crates): the mobys round the piece (the moby query,
// CollLine + 0x2C38, list at block + 0x4080) are stamped with the next query's stamp, which the
// query skips (moby+0xA0 == stamp).
using CollSphereFn = int (*)(gaddr pos, std::uint32_t flags, gaddr skip, float r);
using CollHitFn = int (*)(gaddr pos, std::uint32_t flags, gaddr skip, gaddr event, float r);
constexpr gaddr kSphereFromLine = 0x1060, kMobyQueryFromLine = 0x2C38;
constexpr gaddr kTriangleList = 0x5280, kMobyList = 0x4080, kCollStamp = 0x10;
constexpr int kTrianglesMost = 63;


// Smallest t in [0, tmax) with a t^2 + b t + c = 0; c <= 0 is touching at t = 0, which counts only
// when moving in (b < 0): a sphere that starts against a wall and moves away is not stopped by it
// (else a boom starting in a tight hallway never leaves its start).
bool first_root(float a, float b, float c, float tmax, float& t) {
    if (c <= 0.0f) {
        if (b >= 0.0f) return false;
        t = 0.0f;
        return true;
    }
    if (a <= 1e-12f) return false;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;
    const float r = (-b - std::sqrt(disc)) / (2.0f * a);
    if (r < 0.0f || r >= tmax) return false;
    t = r;
    return true;
}

// A sphere of radius r moving from A to A + d (t 0..1) against the triangle v0 v1 v2, two-sided:
// lowers t_best (start it at 1) on an earlier contact. (Checked against brute force: the notes.)
bool sweep_triangle(V3 A, V3 d, float r, V3 v0, V3 v1, V3 v2, float& t_best) {
    using namespace vec;
    V3 n = cross(sub(v1, v0), sub(v2, v0));
    const float nl = std::sqrt(dot(n, n));
    if (!(nl > 1e-12f)) return false;
    n = mul(n, 1.0f / nl);
    float d0 = dot(sub(A, v0), n);
    if (d0 < 0.0f) {
        n = mul(n, -1.0f);
        d0 = -d0;
    }
    const float nd = dot(d, n);
    const V3 tn = cross(sub(v1, v0), sub(v2, v0));
    const auto inside = [&](V3 q) {
        return dot(cross(sub(v1, v0), sub(q, v0)), tn) >= 0 && dot(cross(sub(v2, v1), sub(q, v1)), tn) >= 0
            && dot(cross(sub(v0, v2), sub(q, v2)), tn) >= 0;
    };
    bool hit = false;
    if (d0 >= r) {
        if (nd >= 0.0f) return false;
        const float t0 = (d0 - r) / -nd;
        if (t0 >= t_best) return false;
        if (inside(sub(add(A, mul(d, t0)), mul(n, r)))) {
            t_best = t0;
            return true;
        }
    } else if (inside(sub(A, mul(n, d0)))) {
        // Already against the face: a contact only when moving into it.
        if (nd >= 0.0f) return false;
        t_best = 0.0f;
        return true;
    }
    const V3 vs[3] = {v0, v1, v2};
    const float dd = dot(d, d);
    for (int i = 0; i < 3; ++i) {
        float t;
        const V3 w = sub(A, vs[i]);
        if (first_root(dd, 2.0f * dot(d, w), dot(w, w) - r * r, t_best, t)) {
            t_best = t;
            hit = true;
        }
        const V3 e = sub(vs[(i + 1) % 3], vs[i]);
        const float ee = dot(e, e), ed = dot(e, d), ew = dot(e, w);
        const float a = ee * dd - ed * ed, b = 2.0f * (ee * dot(d, w) - ed * ew);
        const float c = ee * (dot(w, w) - r * r) - ew * ew;
        if (ee > 1e-12f && first_root(a, b, c, t_best, t)) {
            const float f = (ew + ed * t) / ee;
            if (f >= 0.0f && f <= 1.0f) {
                t_best = t;
                hit = true;
            }
        }
    }
    return hit;
}

struct Coll {
    CollSphereFn sphere = nullptr;
    CollHitFn mobys = nullptr;
    gaddr block = 0;
};

// The level's CollSphere and moby query, and the results block (as game_line finds CollLine's).
bool coll_for_level(const openrac::Mod& mod, const openrac::Memory& memory, Coll& out) {
    const gaddr line = coll_line_function(mod);
    if (line == 0) return false;
    out.block = collision_block(mod, memory);
    if (out.block == 0 || memory.read<gaddr>(out.block) == 0 || memory.read<std::int32_t>(out.block + kCollStamp) < 0) return false;
    out.sphere = mod.function_at<CollSphereFn>(line + kSphereFromLine);
    out.mobys = mod.function_at<CollHitFn>(line + kMobyQueryFromLine);
    return out.sphere != nullptr && out.mobys != nullptr;
}

// What a piece of the sweep met: nothing, the level (`t_out` the first contact, 0..1 along it), or
// a place the game's collision cannot be asked about (its sphere past 0..1024 on an axis).
enum class Swept { Clear, Hit, Unknown };

// One piece [a, b] (with its triangles listed round its middle).
Swept sweep_piece(const Coll& c, const openrac::Memory& memory, gaddr q, V3 a, V3 b, float r, std::uint32_t mask, int depth,
                  float& t_out) {
    using namespace vec;
    const V3 d = sub(b, a);
    const float len = std::sqrt(dot(d, d));
    const V3 mid = add(a, mul(d, 0.5f));
    const float R = r + len * 0.5f + 0.05f;
    if (R > 10.0f) return Swept::Unknown;
    for (float v : {mid.x, mid.y, mid.z}) {
        if (v - R <= 0.0f || v + R >= 1024.0f) return Swept::Unknown;
    }
    memory.write<float>(q, mid.x);
    memory.write<float>(q + 4, mid.y);
    memory.write<float>(q + 8, mid.z);
    memory.write<float>(q + 12, 1.0f);
    const gaddr hero = memory.read<gaddr>(kHeroMoby);
    std::vector<gaddr> out;
    const int listed = c.mobys(q, 1, hero, 0, R);
    for (int i = 0; i < listed && i < 64; ++i) {
        const gaddr m = memory.read<gaddr>(c.block + kMobyList + 4 * static_cast<gaddr>(i));
        if (m == 0) break;
        out.push_back(m);
    }
    const std::int32_t next = memory.read<std::int32_t>(c.block + kCollStamp) + 1;
    for (gaddr m : out) memory.write<std::int32_t>(m + 0xA0, next);
    const int n = c.sphere(q, mask | 0x50, hero, R);
    if (n >= kTrianglesMost && depth < 2) {
        // The list may be full: the piece in halves.
        float t = 1.0f;
        const Swept first = sweep_piece(c, memory, q, a, mid, r, mask, depth + 1, t);
        if (first == Swept::Hit) t_out = t * 0.5f;
        if (first != Swept::Clear) return first;
        const Swept second = sweep_piece(c, memory, q, mid, b, r, mask, depth + 1, t);
        if (second == Swept::Hit) t_out = 0.5f + t * 0.5f;
        return second;
    }
    float t_best = 1.0f;
    bool hit = false;
    for (int i = 0; i < n && i < kTrianglesMost; ++i) {
        const gaddr e = c.block + kTriangleList + 64 * static_cast<gaddr>(i);
        const V3 v0 = read_v3(memory, e), v1 = read_v3(memory, e + 0x10), v2 = read_v3(memory, e + 0x20);
        if (sweep_triangle(a, d, r, v0, v1, v2, t_best)) hit = true;
    }
    if (hit) t_out = t_best;
    return hit ? Swept::Hit : Swept::Clear;
}

// A sphere of radius r from A to B through the level (mobys passed over): how far it goes, as a
// fraction of A..B (1 = all the way). False when it could not be asked (then nothing is known).
bool sphere_sweep(const openrac::Mod& mod, const openrac::Memory& memory, V3 A, V3 B, float r, std::uint32_t mask,
                  float& fraction) {
    using namespace vec;
    Coll c;
    if (!coll_for_level(mod, memory, c)) return false;
    const V3 d = sub(B, A);
    const float len = std::sqrt(dot(d, d));
    fraction = 1.0f;
    if (len < 1e-4f) return true;
    const int pieces = std::max(1, static_cast<int>(std::ceil(len / 3.0f)));
    openrac::StackFrame frame(mod, 0x10);
    const gaddr q = frame.address();
    for (int i = 0; i < pieces; ++i) {
        const V3 a = add(A, mul(d, static_cast<float>(i) / pieces)), b = add(A, mul(d, static_cast<float>(i + 1) / pieces));
        float t = 1.0f;
        const Swept piece = sweep_piece(c, memory, q, a, b, r, mask, 0, t);
        if (piece == Swept::Unknown) return false;
        if (piece == Swept::Hit) {
            fraction = (static_cast<float>(i) + t) / pieces;
            return true;
        }
    }
    return true;
}

// Ratchet faded to `alpha` (0x80 his own): his model, his items and what he wears, Clank on his back.
// While faded it is given every tick, so an item drawn meanwhile fades too; a new level's mobys (or
// the same level's made again) are given it once whatever it is.
void fade_hero(const openrac::Mod& mod, const openrac::Memory& memory, int alpha) {
    const auto loads = mod.play_status().loads;
    if (loads != s.faded_loads) {
        s.faded_loads = loads;
        s.faded = -1;
    }
    if (alpha == 0x80 && s.faded == 0x80) return;
    if (loads != s.backpack_loads) {
        s.backpack_loads = loads;
        s.backpack = 0;
        for (const gaddr m : moby_table(mod, memory)) {
            if (memory.read<std::uint8_t>(m + 0x20) < 0x80 && memory.read<std::int16_t>(m + kMobyClass) == kClankOnBack) {
                s.backpack = m;
                break;
            }
        }
    }
    const auto set = [&](gaddr m) {
        if (moby_live(memory, m)) memory.write<std::uint8_t>(m + 0x23, static_cast<std::uint8_t>(alpha));
    };
    set(memory.read<gaddr>(kHeroMoby));
    set(s.backpack);
    // His item slots (hero +0x1220, seven of 0x50 bytes: each an item's moby and its holster): the
    // gun in hand and what he wears.
    for (gaddr i = 0; i < 7; ++i) {
        set(memory.read<gaddr>(kHeroHand + i * 0x50));
        set(memory.read<gaddr>(kHeroHand + 4 + i * 0x50));
    }
    s.faded = alpha;
}

// The game's view made the boom's for the next tick: its smoothed hang point put across where he
// will be (his feet, +0x60, a tick on), so that it no longer trails him (its smoothing up and down,
// jumps' and steps', kept; lowered under a roof as the boom's is), and its look point's height,
// extra yaw and look angle taken out. It then looks from the eye along the boom at any length, and
// he walks along it (his run follows the view's turn), as in Up Your Arsenal and Deadlocked.
void align_view(const openrac::Memory& memory, gaddr state, V3 up, V3 anchor, float height) {
    const V3 feet = read_v3(memory, state + kStateFeet);
    // A tick's step: from the tick just before, or (after first person, a scene) his own last step.
    V3 step = s.feet_valid && s.feet_frame + 1 == s.frame_no ? vec::sub(feet, s.feet_last) : s.hero_step;
    s.feet_frame = s.frame_no;
    if (vec::dot(step, step) > 4.0f) step = {0, 0, 0};  // a teleport's, or a respawn's
    s.feet_last = feet;
    s.feet_valid = true;
    const auto across = [up](V3 w) { return vec::sub(w, vec::mul(up, vec::dot(w, up))); };
    // Across: his feet a tick on. Up and down: the boom's hang point (the game's own smoothing,
    // lowered under a roof), less the height the game adds on.
    const V3 hang = vec::sub(anchor, vec::mul(up, height));
    write_v3(memory, state + kStateAnchor, vec::add(across(vec::add(feet, step)), vec::mul(up, vec::dot(hang, up))));
    for (gaddr off : kStateSmoothVel) {
        const V3 w = read_v3(memory, state + off);
        write_v3(memory, state + off, vec::sub(w, across(w)));
    }
    memory.write<float>(state + kStateLookHeightNow, memory.read<float>(state + kStateHeightNow));
    memory.write<float>(state + kStateLookHeightVel, memory.read<float>(state + kStateHeightVel));
    memory.write<float>(state + kStateLookAngle, 0.0f);
    memory.write<float>(state + kStateLookAngleVel, 0.0f);
    memory.write<float>(state + kStateExtraYaw, 0.0f);
}

void boom_tick(const openrac::Mod& mod, const openrac::Memory& memory, gaddr camera, float pitch) {
    const gaddr state = memory.read<gaddr>(camera + kCamState);
    if (state == 0) return;
    const bool game_has_it = s.stick_live || !mod_camera_here(mod, memory, camera);
    if (game_has_it) {
        fade_hero(mod, memory, 0x80);
        s.boom_owned = false;
        s.last_want = 1e9f;
        s.boom_turn = 0.0f;
        return;
    }
    // Everything measured in the frame of the camera's up: straight up, or on a planetoid its own.
    const Frame fr = frame_about(camera_up(mod, memory));
    const auto heading_yaw = [&fr](V3 h) {
        const V3 l = fr.to_local(h);
        return std::atan2(l.y, l.x);
    };
    const V3 orbit = fr.to_local(read_v3(memory, state + kStateOrbits[1]));
    const float ground = std::sqrt(orbit.x * orbit.x + orbit.y * orbit.y);
    if (!s.boom_owned) {
        // Taken at once, from where the game's camera is (after a scene, a swing, Clank: no stock
        // camera until the player turns it, as Up Your Arsenal hands back).
        s.boom_lift = std::atan2(orbit.z, ground);
        s.boom_length = std::sqrt(ground * ground + orbit.z * orbit.z);
        s.boom_owned = true;
        s.boom_fresh = true;
        for (float& w : s.want_ring) w = 1e9f;
    }
    // Looking up (pitch > 0) takes the camera down.
    s.boom_lift = std::clamp(s.boom_lift - pitch, kBoomLowest, kBoomHighest);
    // The boom's direction is the player's (Up Your Arsenal's: collision never turns it). Taken from
    // the game's orbit when the boom takes over, then turned only by the player's turns; the game's
    // own camera re-aims its orbit away from walls each tick, and following that while holding the
    // player's direction made the two take turns: the view flicked from side to side every tick.
    // A jump of the game's is taken: the smoothed orbit read here follows the game's re-aiming only
    // through its springs (a few percent a tick), so a jump of kAutoJump in one tick is the camera
    // set anew (a respawn, a door, a reset; docs/notes/rac2-camera-situations-2.md).
    const float game_yaw = ground > 1e-4f ? std::atan2(orbit.y, orbit.x)
                                          : heading_yaw(vec::mul(read_v3(memory, camera), -1.0f));
    // The headings carried into this tick's frame.
    if (!s.boom_fresh) {
        s.boom_yaw = heading_yaw(s.boom_heading);
        s.boom_yaw_written = heading_yaw(s.boom_heading_written);
    }
    if (s.boom_fresh) {
        s.boom_yaw = game_yaw;
    } else {
        s.boom_yaw += s.boom_turn;
        const float jump = wrap_pi(game_yaw - (s.boom_yaw_written + s.boom_turn));
        if (std::fabs(jump) > kAutoJump && s.fp_grace == 0 && s.last_want >= kStuckLength) s.boom_yaw = game_yaw;
    }
    s.boom_fresh = false;
    s.boom_turn = 0.0f;
    s.boom_yaw_written = s.boom_yaw;
    s.boom_heading = s.boom_heading_written = fr.to_world({std::cos(s.boom_yaw), std::sin(s.boom_yaw), 0.0f});
    const float hx = std::cos(s.boom_yaw), hy = std::sin(s.boom_yaw);
    const V3 dir = fr.to_world({std::cos(s.boom_lift) * hx, std::cos(s.boom_lift) * hy, std::sin(s.boom_lift)});
    float distance = memory.read<float>(state + kStateDistance);
    if (!(distance > 0.5f && distance < 50.0f)) distance = std::fmax(s.boom_length, 1.0f);
    distance *= s.aim_zoom;
    const float height = kHangHeight;
    const V3 pivot = read_v3(memory, state + kStatePivot);
    // The eye hangs from the anchor (+0xC0, up by the height), put only as far up from his chest as
    // there is room under a ceiling or a branch, eased back out (all or nothing, it flicked between
    // the two under trees and in hallways: the shake).
    V3 anchor = vec::add(read_v3(memory, state + kStateAnchor), vec::mul(fr.u, height));
    // Up Your Arsenal's ceiling test for its eye (docs/notes/rac3-camera-review.md 3a): a 0.4 sphere
    // swept from his chest (0.7 over his feet) up to the hang point; anything over or beside his head
    // (a roof, a branch, the top of a wall just taller than him) brings the hang point down under it.
    // (The game's own clearance point looks only straight up, along a line.)
    const V3 chest = vec::add(read_v3(memory, state + kStateFeet), vec::mul(fr.u, kChestHeight));
    LineHit hit;
    const float pv[3] = {chest.x, chest.y, chest.z}, av[3] = {anchor.x, anchor.y, anchor.z};
    const V3 up{anchor.x - chest.x, anchor.y - chest.y, anchor.z - chest.z};
    const float up_len = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
    float room = 1.0f;
    const std::uint32_t sweep_mask = memory.read<std::uint32_t>(level0_global(mod, kCamPointMask));
    float frac = 1.0f;
    if (up_len > 1e-3f && sphere_sweep(mod, memory, chest, anchor, kHangRadius, sweep_mask, frac)) {
        room = std::clamp(frac - 0.02f / up_len, 0.0f, 1.0f);
    } else if (up_len > 1e-3f && level_line(mod, memory, pv, av, hit) && hit.hit) {
        const float to_hit = std::sqrt((hit.point[0] - pv[0]) * (hit.point[0] - pv[0]) + (hit.point[1] - pv[1]) * (hit.point[1] - pv[1])
                                       + (hit.point[2] - pv[2]) * (hit.point[2] - pv[2]));
        room = std::clamp((to_hit - 0.3f) / up_len, 0.0f, 1.0f);
    }
    if (room < s.anchor_room) {
        // Down: at once if the point is through the ceiling by much, else quickly.
        if (room < s.anchor_room - 0.9f) s.anchor_room = room;  // a teleport's
        else s.anchor_room += (room - s.anchor_room) * (1.0f - std::exp(-(1.0f / 60.0f) / kRoomInSeconds));
    } else {
        s.anchor_room += (room - s.anchor_room) * (1.0f - std::exp(-(1.0f / 60.0f) / kAnchorOutSeconds));
    }
    anchor = {chest.x + up.x * s.anchor_room, chest.y + up.y * s.anchor_room, chest.z + up.z * s.anchor_room};
    // Where the sphere cannot be asked: lines out along the boom, a margin past its length, its middle
    // and four round it (kBoomRadius off it: one thin line flickers between meeting an edge and
    // missing it as the hero moves). The nearest of them sets the length.
    const float reach = distance + kBoomMargin / 0.25f;
    const V3 side = unit(vec::cross(dir, fr.u), fr.f);
    const V3 rise = vec::cross(side, dir);
    const V3 offsets[5] = {{0, 0, 0},
                           {side.x * kBoomRadius, side.y * kBoomRadius, side.z * kBoomRadius},
                           {-side.x * kBoomRadius, -side.y * kBoomRadius, -side.z * kBoomRadius},
                           {rise.x * kBoomRadius, rise.y * kBoomRadius, rise.z * kBoomRadius},
                           {-rise.x * kBoomRadius, -rise.y * kBoomRadius, -rise.z * kBoomRadius}};
    float want = distance;
    s.boom_inside = 1e9f;
    bool swept = false;
    {
        // The arm: a sphere along the boom (Up Your Arsenal's 0.4 hang guard and its arm sweep),
        // and the middle line only to tell whether the camera is behind a face already.
        const float far = distance + 0.1f;
        float frac = 1.0f;
        const V3 end{anchor.x + dir.x * far, anchor.y + dir.y * far, anchor.z + dir.z * far};
        const bool ok = sphere_sweep(mod, memory, anchor, end, kArmRadius, sweep_mask, frac);
        if (ok) {
            swept = true;
            want = std::fmin(distance, frac * far - 0.05f);
            const float from[3] = {anchor.x, anchor.y, anchor.z}, to[3] = {end.x, end.y, end.z};
            if (level_line(mod, memory, from, to, hit) && hit.hit) {
                s.boom_inside = (hit.point[0] - anchor.x) * dir.x + (hit.point[1] - anchor.y) * dir.y + (hit.point[2] - anchor.z) * dir.z;
            }
        }
    }
    for (int i = 0; i < 5 && !swept; ++i) {
        const V3 o{anchor.x + offsets[i].x, anchor.y + offsets[i].y, anchor.z + offsets[i].z};
        const float from[3] = {o.x, o.y, o.z};
        const float to[3] = {o.x + dir.x * reach, o.y + dir.y * reach, o.z + dir.z * reach};
        if (level_line(mod, memory, from, to, hit) && hit.hit) {
            const float along = (hit.point[0] - o.x) * dir.x + (hit.point[1] - o.y) * dir.y + (hit.point[2] - o.z) * dir.z;
            const float facing = i == 0 ? std::fabs(dir.x * hit.normal[0] + dir.y * hit.normal[1] + dir.z * hit.normal[2]) : 1.0f;
            want = std::fmin(want, along - kBoomMargin / std::fmax(facing, 0.5f));
            if (i == 0) s.boom_inside = std::fmin(s.boom_inside, along);
        }
    }
    // Room behind the camera's spot, past the usual length, for putting it behind a moby.
    const float level_room = std::fmin(want, reach - kBoomMargin);
    s.last_want = want;
    want = std::clamp(want, kBoomMin, distance);
    // A moby (a crate, a tree, a pillar) the camera would sit in: the line along the boom meets
    // it before the camera's spot, and the line back from past the spot meets its far side after
    // it. The camera goes to the nearer side of it, in front or behind (if the level leaves room),
    // and keeps to that side while it is in the same one. A moby only passed through (the ship
    // overhead, Ratchet under it) is left alone, as the game's camera leaves it.
    {
        const float a0[3] = {anchor.x, anchor.y, anchor.z};
        const float end[3] = {anchor.x + dir.x * want, anchor.y + dir.y * want, anchor.z + dir.z * want};
        LineHit in;
        bool inside = false;
        float front = 0, back = 0;
        gaddr which = 0;
        if (level_line(mod, memory, a0, end, in, true) && in.hit && in.moby != 0) {
            const float e = (in.point[0] - a0[0]) * dir.x + (in.point[1] - a0[1]) * dir.y + (in.point[2] - a0[2]) * dir.z;
            const float far_len = std::fmin(want + 2.5f, level_room + 2.0f);
            const float far[3] = {anchor.x + dir.x * far_len, anchor.y + dir.y * far_len, anchor.z + dir.z * far_len};
            LineHit out_hit;
            float x = -1.0f;
            if (game_line(mod, memory, far, a0, out_hit) && out_hit.hit && out_hit.moby == in.moby) {
                x = (out_hit.point[0] - a0[0]) * dir.x + (out_hit.point[1] - a0[1]) * dir.y + (out_hit.point[2] - a0[2]) * dir.z;
            }
            if (e < want + kMobyRoom && (x < 0.0f || x > want - kMobyRoom)) {
                inside = true;
                which = in.moby;
                front = e - kMobyRoom;
                back = x < 0.0f ? 1e9f : x + kMobyRoom;
            }
        }
        if (inside) {
            const bool back_ok = back <= level_room && back <= distance + 2.0f;
            int side = (back_ok && back - want < want - front) ? 1 : -1;
            if (which == s.moby_in && s.moby_side == 1 && back_ok) side = 1;
            if (which == s.moby_in && s.moby_side == -1) side = -1;
            s.moby_in = which;
            s.moby_side = side;
            want = std::clamp(side > 0 ? back : front, kBoomMin, distance + 2.0f);
        } else {
            s.moby_side = 0;
            s.moby_in = 0;
        }
    }
    // The nearest the way has been over the last half second: the length goes out only to that
    // (a side line grazing a floor or a wall comes and goes as he moves; going out each time it
    // went and in each time it came back was the shake).
    s.want_ring[s.want_at] = want;
    s.want_at = (s.want_at + 1) % kWantHold;
    const float held = *std::min_element(s.want_ring, s.want_ring + kWantHold);
    want = std::fmin(want, held);
    // In at once; out only once the way has been clear a moment and by more than a little, then
    // eased (a length that goes in and out by a hair each tick is the shake).
    if (want < s.boom_length - 0.05f) {
        // In: at once only if the camera would be behind a level face (seen through it); else
        // quickly but smoothly, so a ledge or a pillar passing does not jerk it.
        if (s.boom_length > s.boom_inside) s.boom_length = want;
        else s.boom_length += (want - s.boom_length) * (1.0f - std::exp(-(1.0f / 60.0f) / kBoomInSeconds));
        s.boom_clear = 0;
        s.boom_speed = 0.0f;
    } else if (want > s.boom_length + (s.boom_speed > 0.0f ? 0.0f : kBoomSlack)) {
        // Out, once clear a moment; an ease under way goes on until it arrives.
        if (s.boom_speed > 0.0f || ++s.boom_clear >= kBoomClearTicks) {
            const float left = want - s.boom_length;
            // Up Your Arsenal's (fps_cam_place, approach_eased; NTSC): from rest, speeding up by
            // kEaseAccel a tick to kEaseTop, slowing by the same into the length.
            float v = std::fmin(s.boom_speed + kEaseAccel, kEaseTop);
            v = std::fmin(v, std::sqrt(2.0f * kEaseAccel * left));
            s.boom_speed = std::fmax(v, 1e-4f);
            s.boom_length = std::fmin(s.boom_length + s.boom_speed, want);
            if (s.boom_length >= want - 1e-3f) s.boom_speed = 0.0f;
        }
    } else {
        s.boom_clear = 0;
        s.boom_speed = 0.0f;
    }
    if (s.fp_ease > 0) {
        // Free Movement easing in: the boom in along the view, and its hang point down to his eyes.
        const float e = static_cast<float>(s.fp_ease) / kAimEaseTicks, k = e * e * (3.0f - 2.0f * e);
        s.boom_length = std::fmin(s.boom_length, s.fp_ease_from + (kBoomMin - s.fp_ease_from) * k);
        s.boom_speed = 0.0f;
        const V3 feet = read_v3(memory, state + kStateFeet);
        const float above = vec::dot(vec::sub(anchor, feet), fr.u) - kAimEaseEye;
        if (above > 0.0f) anchor = vec::sub(anchor, vec::mul(fr.u, above * k));
    }
    {
        const float t = std::clamp((s.boom_length - kFadeNear) / (kFadeFar - kFadeNear), 0.0f, 1.0f);
        fade_hero(mod, memory, kFadeAlpha + static_cast<int>(std::lround(t * (0x80 - kFadeAlpha))));
    }
    const V3 v{dir.x * s.boom_length, dir.y * s.boom_length, dir.z * s.boom_length};
    memory.write<float>(state + kStateHeight, kHangHeight);
    memory.write<float>(state + kStateHeightNow, kHangHeight);
    memory.write<float>(state + kStateHeightVel, 0.0f);
    align_view(memory, state, fr.u, anchor, height);
    write_v3(memory, state + kStateOrbits[0], v);
    write_v3(memory, state + kStateOrbits[1], v);
    write_v3(memory, state + kStateEyes[0], {pivot.x + v.x, pivot.y + v.y, pivot.z + v.z});
    write_v3(memory, state + kStateEyes[1], {pivot.x + v.x, pivot.y + v.y, pivot.z + v.z});
    write_v3(memory, camera + kCamEye, {anchor.x + v.x, anchor.y + v.y, anchor.z + v.z});
    for (gaddr off : kStateEyeSprings) memory.write<float>(state + off, 0.0f);
    memory.write<float>(state + kStateShortfall, distance - s.boom_length);
    memory.write<std::int16_t>(state + kStateShortTimer, 2000);
    memory.write<std::int16_t>(state + kStateShortLock, 0);
    memory.write<std::int16_t>(state + kStateNoSweep, 1);
    // The game's tilt, the lift's (> 0 the camera high), with no step.
    const float t = std::clamp(s.boom_lift / kFullTilt, -1.0f, 1.0f);
    ask_tilt(memory, state, t);
    memory.write<float>(state + kStateTilt, t);
    memory.write<float>(state + kStateTiltSpeed, 0.0f);
}

// First person, the mode the menu turns on once it is unlocked (rac2-decomp
// src/levels/shared/L00_002D3798-aim-g8.c, UpdateHeroAimAndTransitions; docs/notes/rac2-aim-camera.md):
// while hero_is_action_allowed() (the menu's toggle on, the hero playing, not in camera type 5),
// the view is the hero's aim (+0x14E0, a unit vector), which each tick the game turns by the
// stick's eased rates and builds again from its own yaw and pitch (clamped at 1.4835). So the mouse
// and gyro turn that vector; in Free Movement's first person the mod turns it by the stick too (the
// game's stick held at rest there), elsewhere the game's stick goes on turning it.
// The toggle is an s32 word, g_menu_toggle_option (docs/notes/rac2-aim-movement.md).
constexpr ByDisc kFirstPersonToggle{0x001A7908, 0x001A790C};
constexpr gaddr kHeroFirstPersonAim = kHero + 0x14E0;
constexpr std::int32_t kNoFirstPersonGroup = 0x14;
constexpr std::int16_t kNoFirstPersonCamera = 5;
constexpr float kFirstPersonPitchMax = 1.4835f;

// Aiming: hero state 29, or the aim camera in use (some weapons, the Chopper's and Multi-Star's
// family, aim through +0x1500 rather than state 29; the camera in use tells either way).
bool aiming_now(const openrac::Mod& mod, const openrac::Memory& memory) {
    if (memory.read<std::int32_t>(kHeroState) == kAimState) return true;
    const gaddr rec = memory.read<gaddr>(camera_slot(mod));
    return in_ram(rec) && memory.read<std::int16_t>(rec + kCamType) == kAimCameraType;
}

bool first_person_on(const openrac::Mod& mod, const openrac::Memory& memory) {
    if (memory.read<std::int32_t>(boot_global(mod, kFirstPersonToggle)) == 0) return false;
    if (memory.read<std::uint8_t>(kHeroForm) != 0) return false;
    if (memory.read<std::int32_t>(kHeroGroup) == kNoFirstPersonGroup) return false;
    if (memory.read<std::int32_t>(kHeroHealth) == 0) return false;
    // hero_is_action_allowed's own refusals (a vendor or scene, a turret, the Visibomb, 0x8C).
    const std::int32_t state = memory.read<std::int32_t>(kHeroState);
    if (state == 0x6F || state == 0x31 || state == 0x1C || state == 0x8C) return false;
    const gaddr rec = memory.read<gaddr>(camera_slot(mod));
    return !(in_ram(rec) && memory.read<std::int16_t>(rec + kCamType) == kNoFirstPersonCamera);
}

void first_person_tick(const openrac::Memory& memory, float yaw, float pitch) {
    if (yaw == 0.0f && pitch == 0.0f) return;
    const float x = memory.read<float>(kHeroFirstPersonAim), y = memory.read<float>(kHeroFirstPersonAim + 4);
    const float z = memory.read<float>(kHeroFirstPersonAim + 8);
    const float lift = std::clamp(std::atan2(z, std::sqrt(x * x + y * y)) + pitch, -kFirstPersonPitchMax, kFirstPersonPitchMax);
    const float heading = std::atan2(y, x) + yaw;
    memory.write<float>(kHeroFirstPersonAim, std::cos(lift) * std::cos(heading));
    memory.write<float>(kHeroFirstPersonAim + 4, std::cos(lift) * std::sin(heading));
    memory.write<float>(kHeroFirstPersonAim + 8, std::sin(lift));
}

// A turn the player gave the boom (the stick's, the mouse's, the gyro's): the follow camera turned
// with it, so that the game's orbit, which the boom reads back for the game's own jumps, follows;
// the boom's own direction turns by the player's turns only.
void apply_yaw(const openrac::Memory& memory, gaddr camera, float yaw) {
    turn_camera(memory, camera, yaw);
    s.boom_turn += yaw;
}

// Aiming where the camera looks, in third person (Modern; docs/notes/rac2-weapon-aim.md): the
// weapons read the hero's aim forward (+0x760) and their aid's axis (+0x778 its yaw, +0x774 its
// pitch, < 0 up as the aim camera leaves it) from the tick before, which in third person is his
// facing, flat. Going Commando's own first person aims at the crosshair (a ray from the camera,
// the shot from the muzzle to its hit); Up Your Arsenal's lock-strafe does too. So each tick a ray
// goes out from the camera through the middle of the picture, from as far out as the hero (what
// is between the camera and him does not count) on for kAimReach; the gun aims from its muzzle at
// what it meets (the level or a moby), or at its end, bent at most kAimBend off his facing. The aid
// then looks round the crosshair too. Lobbed weapons keep their throw (theirs is the flat forward).
constexpr gaddr kHeroAimForward = kHero + 0x760;
constexpr float kAimReach = 30.0f;  // Up Your Arsenal's crosshair ray (fps_aim_pos)
constexpr float kAimBend = 80.0f * kDeg;
constexpr float kAimPitchLimit = 85.0f * kDeg;  // the lobbed ones solve from points, not from this

// Thrown and lobbed weapons (docs/notes/rac2-lobbed-aim-reticle.md): each throws by
// CalculateAimedLaunchVelocity, every tick, at a target if it has one, else at the hero's place
// plus his look row (+0x730, the tick before's, which in third person is his facing) times its
// reach, on an arc of its speed and gravity. So that row is pointed so the game's arc goes through
// the crosshair's point: the arc from the gun through it, followed out to the reach from the hero.
// Known by the gun's model in his hand.
struct Lob {
    std::int16_t gun_class;
    float speed, gravity, nearest, reach;  // a tick, a tick^2, units
};
constexpr Lob kLobs[] = {
    {0x0A5A, 0.25f, 0.00333f, 1e-5f, 12.0f},       // Mini-Nuke
    {0x0CE3, 0.25f, 0.00333f, 1e-5f, 12.0f},       // Gravity Bomb
    {0x00C0, 1.0f / 6.0f, 0.0030556f, 1.5f, 10.0f},  // Bomb Glove
    {0x0232, 1.0f / 6.0f, 0.0030556f, 1.5f, 5.0f},   // Decoy Glove
    {0x0C5A, 0.25f, 0.0030556f, 1.5f, 4.5f},       // Miniturret Glove
    {0x0A65, 0.25f, 0.0030556f, 1.5f, 4.5f},       // Megaturret Glove
    {0x0907, 0.1333f, 0.0030556f, 1.6f, 4.5f},     // Spiderbot Glove
    {0x0E7A, 0.1333f, 0.0030556f, 1.6f, 4.5f},     // Tankbot Glove
    {0x0E14, 0.25f, 0.0040556f, 1.6f, 10.0f},      // Bouncer
    {0x0E9E, 0.25f, 0.0040556f, 1.6f, 10.0f},      // Heavy Bouncer
    {0x0C59, 0.25f, 0.0030556f, 1.5f, 11.75f},     // Meteor Gun (its reach is random, 8 to 15.5: the middle)
};
// Weapons that fire along the hero's weapon basis (+0x730 forward, +0x740 left, +0x750 up), which in
// third person the game fills with his body's flat facing each tick (update_hero_held_items_and_
// orientation): the Blitz Gun's and Cannon's rays, the Tesla Claw's beam, the Sheepinators'. The
// whole basis is turned to the aim (the Blitz turns its rays about the left row), read by the next
// tick's weapon update before the game rewrites it (docs/notes/rac2-aim-pitch-limits.md).
constexpr std::int16_t kBasisGuns[] = {0x05D1, 0x0AF1, 0x00B1, 0x0A5D, 0x0EAD};
constexpr gaddr kHeroLookRow = kHero + 0x730;
constexpr std::int16_t kMultiStarClass = 0x0AF6, kChopperClass = 0x0879;

const Lob* lob_of(std::int16_t gun_class) {
    for (const Lob& l : kLobs) if (l.gun_class == gun_class) return &l;
    return nullptr;
}

// The look row that makes the game's throw (from `hero` at `lob.reach`) go through `p` from `gun`.
V3 lob_row(const Lob& lob, V3 hero, V3 gun, V3 p) {
    const float hx = p.x - gun.x, hy = p.y - gun.y;
    const float h = std::sqrt(hx * hx + hy * hy);
    const auto towards = [&](V3 to) {
        V3 d{to.x - hero.x, to.y - hero.y, to.z - hero.z};
        const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        return l > 1e-4f ? V3{d.x / l, d.y / l, d.z / l} : V3{0, 0, 0};
    };
    const float dhero = std::sqrt((p.x - hero.x) * (p.x - hero.x) + (p.y - hero.y) * (p.y - hero.y) + (p.z - hero.z) * (p.z - hero.z));
    if (h < 1e-3f || dhero >= lob.reach) return towards(p);
    const float ux = hx / h, uy = hy / h;
    const float tp = std::fmax(h, lob.nearest) / lob.speed;
    const float vz = (p.z - gun.z) / tp + lob.gravity * tp * 0.5f;
    const auto at = [&](float t) {
        return V3{gun.x + ux * lob.speed * t, gun.y + uy * lob.speed * t, gun.z + vz * t - lob.gravity * t * t * 0.5f};
    };
    const auto off = [&](float t) {
        const V3 x = at(t);
        return std::sqrt((x.x - hero.x) * (x.x - hero.x) + (x.y - hero.y) * (x.y - hero.y) + (x.z - hero.z) * (x.z - hero.z));
    };
    float lo = tp, hi = tp * 1.5f + 1.0f;
    for (int i = 0; i < 40 && off(hi) < lob.reach; ++i) hi *= 1.5f;
    for (int i = 0; i < 32; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (off(mid) < lob.reach) lo = mid; else hi = mid;
    }
    return towards(at(0.5f * (lo + hi)));
}

// The game's own crosshair: each weapon puts a HUD marker with no place (drawn in the middle of
// the picture) in the game's list while it aims, in first person (hud_add_moby_marker); in third
// person the mod puts the same one there (the Lancer's: effect texture 38, scale 1, the game's
// green), at most once for the gun, while there is room (6 entries).
constexpr ByDisc kMarkers{0x1B7600, 0x1B7580};  // the executable's (boot_global, not level0_global)
constexpr int kMarkersMax = 6;
constexpr gaddr kMarkerSize = 0x30, kMarkerCount = 0x120;
// g_current_game_mode (s32, the same on both discs): 2 while a cinematic plays, when the game adds no
// markers (hud_add_moby_marker).
constexpr gaddr kGameMode = 0x1A8BB0;

// The HUD marker list, or 0 while the game adds no markers.
gaddr marker_list(const openrac::Mod& mod, const openrac::Memory& memory) {
    if (memory.read<std::int32_t>(mod.relocate(kGameMode)) == 2) return 0;
    return boot_global(mod, kMarkers);
}

// The camera's view: its forward row, made a unit one (false when it has none).
bool view_of(const openrac::Memory& memory, gaddr camera, V3& f) {
    const V3 v = read_v3(memory, camera);
    const float l = length(v);
    if (l < 1e-4f) return false;
    f = {v.x / l, v.y / l, v.z / l};
    return true;
}

// How far along the view `f` from `eye` the point `p` is (0 when it is behind): a ray through the
// crosshair starts as far out as Ratchet, so that what is between the camera and him does not count.
float depth_along(V3 eye, V3 f, V3 p) { return std::fmax(0.0f, vec::dot(vec::sub(p, eye), f)); }

// Each weapon's own first-person reticle, ported to third person (docs/notes/rac2-weapon-reticles.md):
// what its update queues on the HUD marker list while he aims (hud_add_moby_marker, no world point:
// drawn at the screen's centre, where the shot goes), with its colour rule read from the same
// fields the game reads (its cooldowns are kept in third person too). The Pulse Rifle and Vaporizer
// also blink a red overlay while the crosshair rests on a target: that test runs only in the game's
// aim mode, so the mod runs it along its own crosshair line. Weapons whose markers spawn on targets
// (Chopper / Multi-Star, Minirocket / Megarocket, RYNO II) are the game's: their lock markers show in
// third person already. Weapons with no reticle in first person get none.
enum class RedWhen { Cooldown, AmmoOut, BombOut };
struct ReticleRule {
    std::int16_t gun_class;
    std::int32_t type;
    float scale;
    gaddr cooldown;          // the f32 in the gun's vars (+0x68) that is > 0 while it recharges
    RedWhen red;
    bool ready_state_only;   // drawn only in gun state 1 (the Plasma Coil and Storm)
    bool blink;              // the Pulse Rifle / Vaporizer target overlay
    std::int32_t lock_at;    // its lock's target in its vars (its own marker shows on it): -1 none
    bool third = true;       // ported to third person (else first person's size only)
    bool first = true;       // sized in first person (else the game's own there)
};
constexpr ReticleRule kReticles[] = {
    {1490, 38, 1.0f, 0x0C, RedWhen::Cooldown, false, false, 0x00},   // Lancer
    {3077, 38, 1.0f, 0x0C, RedWhen::Cooldown, false, false, 0x00},   // Heavy Lancer
    {1489, 33, 1.3f, 0x00, RedWhen::Cooldown, false, false, -1},  // Blitz Gun
    {2801, 33, 1.3f, 0x00, RedWhen::Cooldown, false, false, -1},  // Blitz Cannon
    {3146, 38, 1.0f, 0x00, RedWhen::Cooldown, true, false, -1, true, false},  // Plasma Coil (targets in first person)
    {1482, 38, 1.0f, 0x00, RedWhen::Cooldown, true, false, -1},   // Plasma Storm
    {1486, 39, 2.0f, 0x00, RedWhen::Cooldown, false, false, -1},  // Seeker Gun
    {3310, 39, 2.0f, 0x00, RedWhen::Cooldown, false, false, -1},  // HK-22
    {1483, 39, 2.0f, 0x04, RedWhen::Cooldown, false, true, -1},   // Pulse Rifle
    {3076, 39, 2.0f, 0x04, RedWhen::Cooldown, false, true, -1},   // Vaporizer
    {2651, 31, 1.0f, 0x08, RedWhen::BombOut, false, false, -1},   // Hoverbomb Gun
    {3631, 31, 1.0f, 0x00, RedWhen::BombOut, false, false, -1},   // Tetrabomb Gun
    {177, 31, 1.0f, 0x00, RedWhen::AmmoOut, false, false, -1},    // Tesla Claw
    // Locking weapons: the centre reticle until they lock a target (their lock marker shows then).
    {2169, 38, 1.0f, 0x0C, RedWhen::Cooldown, false, false, 0x14},   // Chopper
    {2806, 38, 1.0f, 0x0C, RedWhen::Cooldown, false, false, 0x14},   // Multi-Star
    {1487, 39, 1.25f, 0x00, RedWhen::Cooldown, false, false, 0x10, true, false},  // Minirocket (targets in first person)
    {4100, 39, 1.25f, 0x1C, RedWhen::Cooldown, false, false, 0x00, true, false},  // Megarocket (the same)
};
// The game's colours (0xAABBGGRR), at the alpha it caps every marker to (g_moby_marker_max_alpha).
constexpr std::uint32_t kReticleGreen = 0x400FFF0Fu, kReticleRed = 0x400000FFu;
constexpr gaddr kAmmoTable = 0x00139688;  // s32 an item, the same on both discs (boot)

const ReticleRule* reticle_rule(std::int16_t gun_class) {
    for (const ReticleRule& r : kReticles) {
        if (r.gun_class == gun_class) return &r;
    }
    return nullptr;
}

// The Pulse Rifle / Vaporizer target test (UpdateScopedWeapon, vaporizer_update): along the line
// from the camera 128 u ahead, the first moby (mask 0x10, not Ratchet) is a target if it is on
// (+0x31) and a crate-like class (500..540) or alive (its health record > 0) and its class record
// says enemy (+0x46 == 5). The blink counts 0..20 while on one; the overlay shows from 11.
bool crosshair_on_target(const openrac::Mod& mod, const openrac::Memory& memory, gaddr camera) {
    const V3 eye = read_v3(memory, camera + kCamEye);
    V3 f;
    if (!view_of(memory, camera, f)) return false;
    // From Ratchet's depth along the view (first person's line starts at his head; from behind him
    // it would meet him and Clank on his back first), past anything on him.
    const V3 hero = read_v3(memory, kHeroPosition);
    float out = depth_along(eye, f, hero) + 0.5f;
    LineHit hit;
    gaddr m = 0;
    for (int tries = 0; tries < 3; ++tries) {
        const float from[3] = {eye.x + f.x * out, eye.y + f.y * out, eye.z + f.z * out};
        const float to[3] = {eye.x + f.x * (out + 128.0f), eye.y + f.y * (out + 128.0f), eye.z + f.z * (out + 128.0f)};
        if (!game_line(mod, memory, from, to, hit, 0x10) || !hit.hit || !in_ram(hit.moby)) return false;
        const V3 at = read_v3(memory, hit.moby + 0x10);
        const float dx = at.x - hero.x, dy = at.y - hero.y, dz = at.z - hero.z;
        if (dx * dx + dy * dy + dz * dz > kNearMoby * kNearMoby && memory.read<std::int16_t>(hit.moby + kMobyClass) != kClankOnBack) {
            m = hit.moby;
            break;
        }
        out += (hit.point[0] - from[0]) * f.x + (hit.point[1] - from[1]) * f.y + (hit.point[2] - from[2]) * f.z + 0.1f;
    }
    if (m == 0) return false;
    if (memory.read<std::uint8_t>(m + 0x31) == 0) return false;
    const std::int16_t cls = memory.read<std::int16_t>(m + kMobyClass);
    if (cls >= 500 && cls <= 540) return true;
    if ((memory.read<std::uint16_t>(m + 0x34) & 0x20) == 0) return false;
    const gaddr vars = memory.read<gaddr>(m + kMobyVars);
    if (!in_ram(vars)) return false;
    const gaddr info = memory.read<gaddr>(vars);
    if (!in_ram(info) || !(memory.read<float>(info) > 0.0f)) return false;
    return moby_kind(memory, m) == 5;
}

// The centre reticles' size (Crosshair Size). The game queues and draws its own first-person
// reticle inside its frame, out of every hook's reach, so in first person the mod queues the
// weapon's reticle itself, at the size, and fills the list's other slots with empty entries (no
// moby: never drawn): hud_add_moby_marker refuses a seventh, so the weapon's own full-size one is
// not added. Not for weapons that also put markers on targets in first person (the Plasma Coil,
// the Minirocket / Megarocket): those keep the game's own, full size.
constexpr std::int16_t kSizedTag = 0;

// The reticles' textures fill their 64 texels differently: the ring (38) small, the scope (39)
// large. Each texture gets the base that makes them look alike, the player's size on top of it
// (the game's own scales differ by weapon: 38 at 1.0, 39 at 1.25 or 2.0, 33 at 1.3).
float reticle_base(std::int32_t type, float game_scale) {
    switch (type) {
        case 38: return 1.67f;  // the ring: Lancer, Chopper, Multi-Star, Plasma Coil and Storm
        case 31: return 1.67f;  // Hoverbomb, Tetrabomb, Tesla Claw
        case 39: return 2.0f;   // the scope: Seeker, HK-22, Pulse Rifle, Vaporizer, Minirocket
        case 33: return 1.3f;   // the spread: Blitz Gun and Cannon
        case 36: return 1.67f;  // the Swingshot's
        case 37: return 1.67f;  // the wrench's
        default: return game_scale;
    }
}

void queue_marker(const openrac::Memory& memory, gaddr list, gaddr gun, std::int32_t type, float scale, std::uint32_t colour,
                  const V3* at = nullptr) {
    if (at == nullptr) scale = reticle_base(type, scale) * s.crosshair_size;  // a target's marker keeps its own size
    const std::int32_t count = memory.read<std::int32_t>(list + kMarkerCount);
    if (count < 0 || count >= kMarkersMax) return;
    const gaddr e = list + static_cast<gaddr>(count) * kMarkerSize;
    for (gaddr o = 0; o < kMarkerSize; o += 4) memory.write<std::uint32_t>(e + o, 0);
    memory.write<float>(e + 0x10, scale);
    memory.write<std::uint32_t>(e + 0x14, colour);
    memory.write<std::int32_t>(e + 0x18, type);
    memory.write<float>(e + 0x1C, 0.0f);
    memory.write<gaddr>(e + 0x20, gun);
    memory.write<std::int16_t>(e + 0x24, 0);
    memory.write<std::int16_t>(e + 0x26, kSizedTag);
    memory.write<float>(e + 0x28, 1.5707964f);
    memory.write<std::int32_t>(e + 0x2C, 2);
    if (at != nullptr) {
        write_v3(memory, e, *at);
        memory.write<std::int16_t>(e + 0x24, 1);
    }
    memory.write<std::int32_t>(list + kMarkerCount, count + 1);
}


// Whether the held gun has a reticle to port and nothing of the game's own says where the shot goes
// instead (the L2+R2 lock, the Lancer's lock, the Plasma Coil's target).
const ReticleRule* reticle_now(const openrac::Memory& memory, gaddr gun) {
    if (!in_ram(gun)) return nullptr;
    const ReticleRule* rule = reticle_rule(memory.read<std::int16_t>(gun + kMobyClass));
    if (rule == nullptr || !rule->third) return nullptr;
    if (memory.read<gaddr>(kHeroLockTarget) != 0) return nullptr;
    const gaddr vars = memory.read<gaddr>(gun + kMobyVars);
    if (rule->lock_at >= 0 && in_ram(vars) && memory.read<gaddr>(vars + static_cast<gaddr>(rule->lock_at)) != 0) return nullptr;
    if (rule->gun_class == 3146 && memory.read<gaddr>(kHero + 0xBF8) != 0) return nullptr;
    return rule;
}

void game_crosshair(const openrac::Mod& mod, const openrac::Memory& memory, gaddr gun, int item, gaddr camera,
                    const ReticleRule& rule) {
    const gaddr list = marker_list(mod, memory);
    if (list == 0) return;
    const std::int32_t count = memory.read<std::int32_t>(list + kMarkerCount);
    if (count < 0 || count >= kMarkersMax) return;
    for (int i = 0; i < count; ++i) {
        const gaddr e = list + static_cast<gaddr>(i) * kMarkerSize;
        if (memory.read<gaddr>(e + 0x20) == gun && memory.read<std::int16_t>(e + 0x24) == 0) return;  // the game's own
    }
    const std::uint8_t gun_state = memory.read<std::uint8_t>(gun + 0x20);
    if (rule.ready_state_only && gun_state != 1) return;
    if (rule.blink && rule.gun_class == 1483 && gun_state == 0) return;
    const gaddr vars = memory.read<gaddr>(gun + kMobyVars);
    if (!in_ram(vars)) return;
    const bool ammo = item >= 0 && item < 64 && memory.read<std::int32_t>(mod.relocate(kAmmoTable) + static_cast<gaddr>(item) * 4) != 0;
    const bool cooling = memory.read<float>(vars + rule.cooldown) > 0.0f;
    bool red = false;
    switch (rule.red) {
        case RedWhen::Cooldown: red = cooling && ammo; break;
        case RedWhen::AmmoOut: red = !ammo; break;
        case RedWhen::BombOut: red = cooling || !ammo || gun_state != 1; break;
    }
    if (rule.blink) {
        if (crosshair_on_target(mod, memory, camera)) {
            if (++s.blink > 20) s.blink = 0;
        } else {
            s.blink = 0;
        }
        if (!cooling && ammo && s.blink >= 11) queue_marker(memory, list, gun, 32, 1.5f, kReticleRed);
    }
    queue_marker(memory, list, gun, rule.type, rule.scale, red ? kReticleRed : kReticleGreen);
}

// The wrench's and the gadgets' own reticles (rac2-decomp: wrench_update, swingshot_update,
// hypnomatic_update; docs/notes/rac2-weapon-reticles.md): the game adds them only in its aim
// (hero state 1 or 29), which Free Movement's first person never enters. So there the mod adds them as
// the game would: the wrench's tan one, the Swingshot's red one and a blue one on the node it has
// picked, the Hypnomatic's green ring. (The Dynamo's socket marker shows in every state already; the
// other gadgets have none.)
void gadget_crosshair(const openrac::Mod& mod, const openrac::Memory& memory, gaddr held) {
    if (!first_person_on(mod, memory)) return;
    const std::int32_t state = memory.read<std::int32_t>(kHeroState);
    if (state == 1 || state == kAimState) return;  // the game's own
    const gaddr list = marker_list(mod, memory);
    if (list == 0) return;
    const std::int32_t count = memory.read<std::int32_t>(list + kMarkerCount);
    if (count < 0 || count >= kMarkersMax) return;
    switch (memory.read<std::int16_t>(held + kMobyClass)) {
        case kWrenchClass:
            if (memory.read<std::uint8_t>(held + 0x20) == 0) queue_marker(memory, list, held, 37, 1.0f, 0x40917267u);
            break;
        case kSwingshotClass: {
            queue_marker(memory, list, held, 36, 1.0f, 0x400000FFu);
            gaddr node = memory.read<std::int32_t>(kSwingNodeReady) != 0 ? memory.read<gaddr>(kSwingNodePick) : 0;
            if (node == 0) node = memory.read<gaddr>(kPullNodePick);
            if (in_ram(node)) {
                const V3 at = read_v3(memory, node + 0x10);
                queue_marker(memory, list, held, 36, 1.4f, 0x40FF0000u, &at);
            }
            break;
        }
        case kHypnomaticClass:
            queue_marker(memory, list, held, 38, 1.0f, kReticleGreen);
            break;
        default:
            break;
    }
}

// First person (aiming, or the first-person mode), Crosshair not 100 percent: the weapon's reticle
// queued at the size and the list filled so the game's own full-size one is refused.
void first_person_crosshair(const openrac::Mod& mod, const openrac::Memory& memory, gaddr camera) {
    if (!(aiming_now(mod, memory) || first_person_on(mod, memory) || (s.aim_input && is_weapon(memory.read<std::int32_t>(kHeroItem))))) return;
    const gaddr gun = memory.read<gaddr>(kHeroHand);
    if (!in_ram(gun)) return;
    const ReticleRule* rule = reticle_rule(memory.read<std::int16_t>(gun + kMobyClass));
    if (rule == nullptr) {
        gadget_crosshair(mod, memory, gun);
        return;
    }
    if (std::fabs(s.crosshair_size - 1.0f) < 1e-3f || !rule->first) return;
    const gaddr list = marker_list(mod, memory);
    if (list == 0 || memory.read<std::int32_t>(list + kMarkerCount) != 0) return;
    game_crosshair(mod, memory, gun, memory.read<std::int32_t>(kHeroItem), camera, *rule);
    for (std::int32_t n = memory.read<std::int32_t>(list + kMarkerCount); n >= 0 && n < kMarkersMax; ++n) {
        const gaddr e = list + static_cast<gaddr>(n) * kMarkerSize;
        for (gaddr o = 0; o < kMarkerSize; o += 4) memory.write<std::uint32_t>(e + o, 0);  // no moby: skipped
        memory.write<std::int32_t>(list + kMarkerCount, n + 1);
    }
}

// The Multi-Star's blade (class 2170; docs/notes/rac2-lobbed-aim-reticle.md, L00_0041CB80-g8.c)
// moves by its velocity (vars +0x00) each tick, no gravity. Thrown in third person it starts flat
// along his body, steering toward an aim point (vars +0x40, his facing * 64) only when not strafing
// (+0x36); strafing it goes straight. A target (+0x14) it homes on. A new blade with no target is
// turned at the crosshair's point, its speed kept, and its aim point put there.
constexpr std::int16_t kBladeClass = 2170;

void aim_blades(const openrac::Mod& mod, const openrac::Memory& memory, V3 point) {
    std::vector<gaddr> now;
    for (const gaddr m : moby_table(mod, memory)) {
        if (memory.read<std::uint8_t>(m + 0x20) >= 0x80) continue;
        if (memory.read<std::int16_t>(m + kMobyClass) != kBladeClass) continue;
        now.push_back(m);
        if (std::find(s.blades.begin(), s.blades.end(), m) != s.blades.end()) continue;
        const gaddr vars = memory.read<gaddr>(m + kMobyVars);
        if (!in_ram(vars) || memory.read<gaddr>(vars + 0x14) != 0) continue;
        const V3 v = read_v3(memory, vars);
        const float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        const V3 at = read_v3(memory, m + 0x10);
        const V3 to{point.x - at.x, point.y - at.y, point.z - at.z};
        const float far = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
        if (speed < 1e-4f || far < 1.0f) continue;
        write_v3(memory, vars, {to.x / far * speed, to.y / far * speed, to.z / far * speed});
        write_v3(memory, vars + 0x40, point);
    }
    s.blades = now;
}

// Modern, in play: the gun aims where the boom's camera looks (`camera`).
bool aim_follow_now(const openrac::Mod& mod, const openrac::Memory& memory, gaddr& camera) {
    camera = find_camera(mod);
    if (camera == 0 || !mod_camera_here(mod, memory, camera)) return false;
    if (swing_or_grind(memory.read<std::int32_t>(kHeroGroup))) return false;  // the game aims there
    if (aiming_now(mod, memory) || first_person_on(mod, memory)) return false;
    if (memory.read<gaddr>(kHeroMoby) == 0 || memory.read<gaddr>(kHeroVehicle) != 0 || memory.read<std::int32_t>(kHeroHealth) <= 0) return false;
    return is_weapon(memory.read<std::int32_t>(kHeroItem));
}

void aim_frame() {
    openrac::Mod& mod = *s.mod;
    const auto memory = mod.memory();
    // Classic: the game's own crosshairs and aim, untouched; and nothing out of play.
    const auto status = mod.play_status();
    if (!s.modern || status.phase != OPENRAC_PLAY_PLAYING || raw_level(status.level)) return;
    if (crosshair_shown()) {
        const gaddr cam = find_camera(mod);
        if (cam != 0) first_person_crosshair(mod, memory, cam);
    }
    gaddr camera = 0;
    if (!aim_follow_now(mod, memory, camera)) return;
    const V3 eye = read_v3(memory, camera + kCamEye);
    V3 f;
    if (!view_of(memory, camera, f)) return;
    const V3 hero = read_v3(memory, kHeroPosition);
    const float out = depth_along(eye, f, hero);
    const float from[3] = {eye.x + f.x * out, eye.y + f.y * out, eye.z + f.z * out};
    const float to[3] = {from[0] + f.x * kAimReach, from[1] + f.y * kAimReach, from[2] + f.z * kAimReach};
    LineHit hit;
    V3 at{to[0], to[1], to[2]};
    const bool hit_any = game_line(mod, memory, from, to, hit) && hit.hit;
    if (hit_any) at = {hit.point[0], hit.point[1], hit.point[2]};
    // On a planetoid the game bends shots round its curve: with no target under the crosshair, the
    // gun fires along the ground the way the camera faces, as the game's own does, and the shot
    // follows the curve.
    const bool level = world_up(mod, memory);
    const bool along_ground = !level && !(hit_any && hit.moby != 0);
    // The muzzle: the gun in his hand, or his chest.
    V3 muzzle{hero.x, hero.y, hero.z + 1.0f};
    const gaddr gun = memory.read<gaddr>(kHeroHand);
    if (in_ram(gun)) muzzle = read_v3(memory, gun + 0x10);
    V3 d{at.x - muzzle.x, at.y - muzzle.y, at.z - muzzle.z};
    const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dl < 0.5f) return;
    // Limited in the frame of the camera's up (a planetoid's too): no steeper than kAimPitchLimit, and
    // no further than kAimBend off his facing (his facing angle on level ground; on a planetoid his
    // look row, which the game fills with his facing each tick).
    const Frame fr = frame_about(camera_up(mod, memory));
    const V3 dl_local = fr.to_local(along_ground ? f : d);
    float yaw = std::atan2(dl_local.y, dl_local.x);
    const float lift = along_ground ? 0.0f
                                    : std::clamp(std::asin(std::clamp(dl_local.z / dl, -1.0f, 1.0f)), -kAimPitchLimit, kAimPitchLimit);
    const V3 look = fr.to_local(read_v3(memory, kHeroLookRow));
    const float facing = level ? memory.read<float>(kHeroFacing) : std::atan2(look.y, look.x);
    yaw = facing + std::clamp(wrap_pi(yaw - facing), -kAimBend, kAimBend);
    // Written as the game has them: a forward in the world, and its yaw and pitch (< 0 up) as world
    // angles (the aid's axis is spherical(yaw, pitch)).
    const V3 fwd = fr.to_world({std::cos(lift) * std::cos(yaw), std::cos(lift) * std::sin(yaw), std::sin(lift)});
    write_v3(memory, kHeroAimForward, fwd);
    memory.write<float>(kHeroAimYaw, std::atan2(fwd.y, fwd.x));
    memory.write<float>(kHeroAimPitch, -std::asin(std::clamp(fwd.z, -1.0f, 1.0f)));
    // A thrown or lobbed weapon: its throw through the same point.
    bool lobbing = false;
    if (in_ram(gun)) {
        const std::int16_t gun_class = memory.read<std::int16_t>(gun + kMobyClass);
        if (gun_class == kMultiStarClass || gun_class == kChopperClass) aim_blades(mod, memory, at);
        if (std::find(std::begin(kBasisGuns), std::end(kBasisGuns), gun_class) != std::end(kBasisGuns)) {
            const V3 left = unit(vec::cross(fr.u, fwd), fr.s);
            const V3 up = vec::cross(fwd, left);
            write_v3(memory, kHeroLookRow, fwd);
            memory.write<float>(kHeroLookRow + 12, 0.0f);
            write_v3(memory, kHeroLookRow + 0x10, left);
            memory.write<float>(kHeroLookRow + 0x1C, 0.0f);
            write_v3(memory, kHeroLookRow + 0x20, up);
            memory.write<float>(kHeroLookRow + 0x2C, 0.0f);
        }
        if (const Lob* lob = lob_of(gun_class)) {
            const gaddr hero_moby = memory.read<gaddr>(kHeroMoby);
            const V3 from = read_v3(memory, hero_moby + 0x10);
            write_v3(memory, kHeroLookRow, lob_row(*lob, from, muzzle, at));
            memory.write<float>(kHeroLookRow + 12, 0.0f);
            lobbing = true;
        }
    }
    // The crosshair, only where it is true: not for a thrown or lobbed weapon (it shows its own
    // landing marker), not while the game's aid has a target or a lock (its reticle shows where the
    // shot goes then).
    const std::int32_t item = memory.read<std::int32_t>(kHeroItem);
    const ReticleRule* rule = lobbing ? nullptr : reticle_now(memory, gun);
    if (crosshair_shown() && rule != nullptr) game_crosshair(mod, memory, gun, item, camera, *rule);
}

// The right stick, Modern: Deadlocked's (below), each turn's speed ramping up toward the stick's and
// dropping to it at once (so a turn stops when the stick does). Up and down go as Going Commando's
// stick goes with its camera option as set (a new game's: down looks up).
constexpr ByDisc kCameraInvert{0x001A7BA4, 0x001A7BAC};  // u8: the camera option's up and down

void stick_rate(float& rate, float target, float accel) {
    if (std::fabs(target) < std::fabs(rate) || rate * target < 0.0f) {
        rate = target;
    } else {
        const float d = target - rate;
        rate += std::copysign(std::fmin(std::fabs(d), accel), d);
    }
}

// Deadlocked's stick (docs/notes/rac4-camera-review.md, 1.2), its named Camera Speeds: linear past a
// 48/127 dead zone (full at 124/127), turning up to 3 degrees a tick (180 a second) and tilting 2
// (120) at its scale 1, ramped as Up Your Arsenal's (0.001939 and 0.001454 radians a tick^2); held
// right over (past 0.97) for two ticks, a quick turn on to twice that. Its scale (kCameraSpeedScale)
// multiplies them all.
constexpr float kDlDead = 48.0f / 127.0f, kDlSpan = 76.0f / 127.0f;
constexpr float kDlYaw = 0.05236f, kDlYawAccel = 0.001939f, kDlPitch = 0.0349f, kDlPitchAccel = 0.001454f;
constexpr float kDlQuick = 0.10472f, kDlQuickAccel = 0.00349f, kDlQuickFrom = 0.97f;

float deadlocked_shape(float u) {
    const float r = std::fabs(u);
    return r <= kDlDead ? 0.0f : std::copysign(std::fmin((r - kDlDead) / kDlSpan, 1.0f), u);
}

// Aim assist, Up Your Arsenal's and Deadlocked's (docs/notes/rac3-camera-review.md 1.10,
// rac4-camera-review.md 1.8): with a weapon in hand, the enemy nearest the crosshair within a cone
// (15 degrees and 30 u; Deadlocked 20 and 45), in sight, weighs w = (cos off - cos cone) /
// (1 - cos cone). The stick's top speeds are cut by 0.5 w (turn) and 0.6 w (tilt), and while either
// stick moves the view follows the enemy's motion past him (turn 0.666 w, at most 1.5 degrees a tick;
// tilt 0.85 w, faded in from 4 u to 12 u). The mod holds the tilt to 1.5 degrees a tick too, and
// a move of more than kTrackJump in a tick is not the enemy's own (another moby in its slot, a
// respawn): not followed.
constexpr float kAssistYaw = 0.5f, kAssistPitch = 0.6f;
constexpr float kTrackYaw = 0.666f, kTrackYawMost = 0.02618f, kTrackPitch = 0.85f, kTrackPitchMost = kTrackYawMost;
constexpr float kTrackJump = 2.0f;

struct AssistTarget {
    gaddr moby = 0;
    float w = 0;
    V3 at{0, 0, 0};
    float dist = 0;
};

bool enemy_alive(const openrac::Memory& memory, gaddr m) {
    if (memory.read<std::uint8_t>(m + 0x20) >= 0x80 || memory.read<std::uint8_t>(m + 0x31) == 0) return false;
    if ((memory.read<std::uint16_t>(m + 0x34) & 0x20) == 0 || moby_kind(memory, m) != 5) return false;
    const gaddr vars = memory.read<gaddr>(m + kMobyVars);
    if (!in_ram(vars)) return false;
    const gaddr info = memory.read<gaddr>(vars);
    return in_ram(info) && memory.read<float>(info) > 0.0f;
}

AssistTarget assist_target(const openrac::Mod& mod, const openrac::Memory& memory, gaddr camera) {
    const bool uya = s.assist_style == 2;
    const float cone = std::cos((uya ? 15.0f : 20.0f) * kDeg), range = uya ? 30.0f : 45.0f;
    const V3 eye = read_v3(memory, camera + kCamEye), view = unit(read_v3(memory, camera), {1, 0, 0});
    const V3 hero = read_v3(memory, kHeroPosition);
    AssistTarget best[3];
    for (const gaddr m : moby_table(mod, memory)) {
        if (!enemy_alive(memory, m)) continue;
        const V3 at = vec::add(read_v3(memory, m + 0x10), {0, 0, 0.8f});  // about its middle
        if (length(vec::sub(at, hero)) < kNearMoby) continue;
        const V3 d = vec::sub(at, eye);
        const float dist = length(d);
        if (dist > range || dist < 1.0f) continue;
        const float c = vec::dot(d, view) / dist;
        if (c <= cone) continue;
        AssistTarget t{m, (c - cone) / (1.0f - cone), at, dist};
        for (AssistTarget& b : best) {
            if (t.w > b.w) std::swap(b, t);  // the three nearest the crosshair, nearest first
        }
    }
    for (const AssistTarget& t : best) {
        if (t.moby == 0) break;
        const float from[3] = {eye.x, eye.y, eye.z}, to[3] = {t.at.x, t.at.y, t.at.z};
        LineHit hit;
        if (!game_line(mod, memory, from, to, hit) || !hit.hit || hit.moby == t.moby) return t;
        const float to_hit = std::sqrt((hit.point[0] - eye.x) * (hit.point[0] - eye.x) + (hit.point[1] - eye.y) * (hit.point[1] - eye.y)
                                       + (hit.point[2] - eye.z) * (hit.point[2] - eye.z));
        if (to_hit > t.dist - 0.5f) return t;
    }
    return {};
}

// The assist this tick: its weight eased in s.assist (the stick's friction), and the view's turn and
// tilt that follow the enemy (> 0 left, > 0 up), while either stick moves.
void assist_tick(const openrac::Mod& mod, const openrac::Memory& memory, gaddr camera, bool here, float& yaw, float& pitch) {
    yaw = pitch = 0.0f;
    if (const auto loads = mod.play_status().loads; loads != s.assist_loads) {
        s.assist_loads = loads;  // a new level's mobys: the enemy before is none of them
        s.assist_target = 0;
    }
    const AssistTarget t = here && s.assist_style != 0 && is_weapon(memory.read<std::int32_t>(kHeroItem))
        ? assist_target(mod, memory, camera) : AssistTarget{};
    s.assist += (t.w - s.assist) * 0.3f;
    const V3 rel = t.moby != 0 ? vec::sub(t.at, read_v3(memory, kHeroPosition)) : V3{0, 0, 0};
    const bool moving = s.left_live || std::fabs(s.stick_x) > 0.15f || std::fabs(s.stick_y) > 0.15f;
    const V3 delta = vec::sub(rel, s.assist_rel);
    if (t.moby != 0 && t.moby == s.assist_target && moving && vec::dot(delta, delta) < kTrackJump * kTrackJump) {
        const V3 right = read_v3(memory, camera + 0x10), up = read_v3(memory, camera + 0x20);
        yaw = std::clamp(-vec::dot(delta, right) / t.dist * kTrackYaw * t.w, -kTrackYawMost, kTrackYawMost);
        pitch = std::clamp(vec::dot(delta, up) / t.dist * kTrackPitch * t.w * std::clamp((t.dist - 4.0f) / 8.0f, 0.0f, 1.0f),
                           -kTrackPitchMost, kTrackPitchMost);
    }
    s.assist_target = t.moby;
    s.assist_rel = rel;
}

// This tick's turn and tilt from the right stick (radians; turn > 0 left, tilt > 0 up); false when
// it is at rest and has stopped.
bool modern_stick(const openrac::Mod& mod, const openrac::Memory& memory, float& yaw, float& pitch) {
    const float scale = kCameraSpeedScale[std::clamp(s.camera_speed, 0, static_cast<int>(std::size(kCameraSpeedScale)) - 1)];
    const float sx = deadlocked_shape(s.stick_x);
    const float sy = deadlocked_shape(s.stick_y);
    float top = kDlYaw * scale;
    const float share = s.vertical == 2 ? s.vertical_custom : kVerticalShare[s.vertical == 1 ? 1 : 0];
    float top_pitch = kDlYaw * scale * share;
    // The aim assist's friction.
    top *= 1.0f - kAssistYaw * s.assist;
    top_pitch *= 1.0f - kAssistPitch * s.assist;
    s.quick_ticks = std::fabs(s.stick_x) > kDlQuickFrom ? s.quick_ticks + 1 : 0;
    if (s.quick_ticks >= 2) {
        stick_rate(s.stick_yaw_rate, -std::copysign(kDlQuick * scale, sx), kDlQuickAccel * scale);
    } else {
        stick_rate(s.stick_yaw_rate, -top * sx, kDlYawAccel / kDlYaw * top * std::fabs(sx));
    }
    // The game's option byte set (as a new game has it) is its usual way, the stick down looking
    // up; clear, down looks down (tested: the game's own camera and this one agree).
    const bool down_up = memory.read<std::uint8_t>(boot_global(mod, kCameraInvert)) != 0;
    stick_rate(s.stick_pitch_rate, (down_up ? 1.0f : -1.0f) * top_pitch * sy,
               kDlPitchAccel / kDlPitch * top_pitch * std::fabs(sy));
    yaw = s.stick_yaw_rate;
    pitch = s.stick_pitch_rate;
    return yaw != 0.0f || pitch != 0.0f;
}

// ---- OpenRAC's own keyboard, mouse and gyro (its built-in "keyboard-mouse" mod) ----
// It is always on: with the cursor captured in play it turns the follow camera and the first-person
// and aim cameras itself from the mouse (and its gyro, if on), and binds the mouse's buttons to the
// pad's. So the mod leaves the mouse and that gyro to it everywhere it works, and fills in only
// where the mod's camera would throw its turn away: Modern's third-person boom (which owns its
// direction). There it turns as OpenRAC would, by OpenRAC's own settings (kbm::DirectLook: 0.0015
// rad a pixel), read as the built-in reads them this run (mod_setting), else from its `kbm_<key>`
// lines in the settings file.

// The built-in's index among the loaded mods (mod_entry), -1 none; found once.
int kbm_index(const openrac::Mod& mod) {
    static int found = -2;
    if (found != -2) return found;
    found = -1;
    const openrac_api& api = mod.api();
    if (api.api_minor < 14 || api.size < offsetof(openrac_api, mod_setting) + sizeof(void*) || !api.mod_count) return found;
    const int n = api.mod_count();
    for (int i = 0; i < n; ++i) {
        openrac_mod_entry e{};
        e.size = sizeof e;
        if (api.mod_entry(i, &e) == OPENRAC_HOOK_OK && e.id != nullptr && std::string(e.id) == "keyboard-mouse") found = i;
    }
    return found;
}

// A setting of the built-in as it reads it this run, or "" when there is none.
std::string kbm_live(const openrac::Mod& mod, const char* key) {
    const int i = kbm_index(mod);
    if (i < 0) return "";
    const openrac_api& api = mod.api();
    openrac_mod_entry e{};
    e.size = sizeof e;
    if (api.mod_entry(i, &e) != OPENRAC_HOOK_OK) return "";
    for (int k = 0; k < e.settings; ++k) {
        openrac_mod_setting m{};
        m.size = sizeof m;
        if (api.mod_setting(i, k, &m) == OPENRAC_HOOK_OK && m.key && std::string(m.key) == key) return m.value ? m.value : "";
    }
    return "";
}

// The port's settings file: its --settings, else the launcher's (the home's share folder), else the
// data folder's. The places are worked out once; until one of them has the file (OpenRAC writes it
// at its first change), each call looks again.
std::string kbm_settings_path(const openrac::Mod& mod) {
    static std::string found;
    static std::vector<std::string> tries;
    if (!found.empty()) return found;
    if (tries.empty()) {
        const std::string game = "rac2-" + std::string(mod.version());
        if (FILE* f = std::fopen("/proc/self/cmdline", "rb")) {
            std::string all;
            char buf[4096];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
            std::fclose(f);
            std::vector<std::string> args;
            for (std::size_t i = 0, j; i < all.size(); i = j + 1) {
                j = all.find('\0', i);
                if (j == std::string::npos) j = all.size();
                args.push_back(all.substr(i, j - i));
            }
            for (std::size_t i = 0; i + 1 < args.size(); ++i) {
                if (args[i] == "--settings") tries.push_back(args[i + 1]);
            }
        }
        if (const char* home = std::getenv("HOME")) tries.push_back(std::string(home) + "/.local/share/openrac/settings/" + game + ".ini");
        tries.push_back("settings/" + game + ".ini");
    }
    for (const std::string& t : tries) {
        if (FILE* f = std::fopen(t.c_str(), "r")) {
            std::fclose(f);
            found = t;
            break;
        }
    }
    return found;
}

// OpenRAC's mouse and gyro settings, at once and then every second: as the built-in reads them,
// else the file's. None while there is no built-in (older OpenRAC).
void kbm_refresh(const openrac::Mod& mod) {
    if (!g_builtin_known) {
        g_builtin_known = true;
        g_builtin_input = kbm_index(mod) >= 0 && !kbm_live(mod, "mouse_sensitivity").empty() && !kbm_live(mod, "gyro").empty();
        mod.info(std::string("mouse and gyro: ") + (g_builtin_input ? "OpenRAC has its built-in keyboard, mouse and gyro"
                                                                     : "this OpenRAC has no built-in mouse and gyro: the mod's own"));
    }
    if (!g_builtin_input) return;
    static int tick = 0;
    if (tick++ % 60 != 0) return;
    Kbm k;
    std::map<std::string, std::string> file;
    const std::string path = kbm_settings_path(mod);
    if (FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "r")) {
        char line[512];
        while (std::fgets(line, sizeof line, f)) {
            std::string l(line);
            if (l.rfind("kbm_", 0) != 0) continue;
            const auto eq = l.find('=');
            if (eq == std::string::npos) continue;
            const auto trim = [](std::string v) {
                const auto a = v.find_first_not_of(" \t\r\n\"");
                const auto b = v.find_last_not_of(" \t\r\n\"");
                return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
            };
            file[trim(l.substr(4, eq - 4))] = trim(l.substr(eq + 1));
        }
        std::fclose(f);
    }
    // The file first: OpenRAC writes a change into it at once, while mod_setting answers what the
    // built-in read at the start of the run. The live read only for what the file has not got.
    const auto get = [&](const char* key) {
        const auto it = file.find(key);
        return it != file.end() ? it->second : kbm_live(mod, key);
    };
    const auto num = [&](const char* key, float fallback) {
        const std::string v = get(key);
        char* end = nullptr;
        const float x = std::strtof(v.c_str(), &end);
        return !v.empty() && end != v.c_str() && std::isfinite(x) ? x : fallback;
    };
    const auto yes = [&](const char* key, bool fallback) {
        const std::string v = get(key);
        if (v.empty()) return fallback;
        return v == "true" || v == "1" || v == "on";
    };
    k.sens_x = num("mouse_sensitivity", 1.0f);
    k.sens_y = num("mouse_sensitivity_y", 1.0f);
    k.invert_y = yes("invert_y", false);
    k.capture = yes("capture_cursor", true);
    k.gyro = get("gyro").empty() ? "off" : get("gyro");
    k.gyro_x = num("gyro_sensitivity", 2.0f);
    k.gyro_y = num("gyro_sensitivity_y", 2.0f);
    k.gyro_invert_x = yes("gyro_invert_x", false);
    k.gyro_invert_y = yes("gyro_invert_y", false);
    k.gyro_space = get("gyro_space").empty() ? "player" : get("gyro_space");
    k.gyro_dead_zone = num("gyro_dead_zone", 0.0f);
    k.gyro_smoothing = num("gyro_smoothing", 3.0f);
    k.gyro_calibrate = yes("gyro_calibrate", true);
    if (!get("gyro_button").empty()) k.gyro_button = get("gyro_button");
    if (!get("gyro_button_action").empty()) k.gyro_button_action = get("gyro_button_action");
    if (!get("strafe").empty()) k.strafe_bind = get("strafe");
    if (!get("r2").empty()) k.r2_bind = get("r2");
    g_kbm = k;
}

// Aim with movement: Going Commando's own first-person mode while the aim button is held (it walks
// with the left stick, looks with the right, fires at the centre: docs/notes/rac2-aim-movement.md).
// In: the view put where the camera looked, the mode's word set (the player's own setting kept).
// Out: the setting put back, the gun back in his hand at once, the boom behind him along the view,
// close, easing out. If the game does not take it (a state that allows no first person), the aim
// button is the game's aim for that press.
constexpr gaddr kHeroFirstPersonRates = kHero + 0x14F0;  // f32 pitch rate, +4 yaw rate
constexpr gaddr kHeroFirstPersonEye = kHero + 0x151C;    // f32: the eye's height
constexpr gaddr kHeroGunAway = kHero + 0x14F8;
constexpr gaddr kHeroAimMode = kHero + 0x1500;  // s32: the aim some weapons take instead of state 29

constexpr float kAimZoom = 0.5f;  // Aim held on a planetoid: the boom this much of its length

// The first-person view set to look where the camera looks, no steeper than kSteepestView (true
// when it was).
constexpr float kSteepestView = 0.98f;  // the z of a unit view
bool view_from_camera(const openrac::Memory& memory, gaddr camera) {
    V3 f;
    if (!view_of(memory, camera, f)) return false;
    f.z = std::clamp(f.z, -kSteepestView, kSteepestView);
    write_v3(memory, kHeroFirstPersonAim, f);
    memory.write<float>(kHeroFirstPersonRates, 0.0f);
    memory.write<float>(kHeroFirstPersonRates + 4, 0.0f);
    memory.write<float>(kHeroFirstPersonEye, 1.5f);
    return true;
}

// Out of first person: the boom put behind him along the view, close, easing out (in the frame of
// the camera's up, a planetoid's too).
void boom_behind_view(const openrac::Mod& mod, const openrac::Memory& memory) {
    const V3 v = read_v3(memory, kHeroFirstPersonAim);
    memory.write<std::int32_t>(kHeroAimMode, 0);
    memory.write<float>(kHeroGunAway, 0.0f);
    const Frame fr = frame_about(camera_up(mod, memory));
    const V3 back = fr.to_local(vec::mul(v, -1.0f));
    const float h = std::sqrt(back.x * back.x + back.y * back.y);
    if (h <= 1e-3f) return;
    s.boom_yaw = std::atan2(back.y, back.x);
    s.boom_yaw_written = s.boom_yaw;
    s.boom_heading = s.boom_heading_written = fr.to_world({back.x / h, back.y / h, 0.0f});
    s.boom_turn = 0.0f;
    s.boom_lift = std::clamp(std::asin(std::clamp(back.z, -1.0f, 1.0f)), kBoomLowest, kBoomHighest);
    s.boom_length = 1.3f;
    s.boom_speed = 0.0f;
    s.boom_clear = kBoomClearTicks;
    s.boom_owned = true;
    s.boom_fresh = false;
    for (float& w : s.want_ring) w = 1e9f;
    s.fp_grace = 10;
    // The game's hang point did not follow him while he looked in first person: put over his feet
    // now, still, so the first view back is not from where he started aiming (the jerk out of it).
    if (const gaddr camera = find_camera(mod); camera != 0) {
        if (const gaddr state = memory.read<gaddr>(camera + kCamState); state != 0) {
            write_v3(memory, state + kStateAnchor, vec::add(read_v3(memory, state + kStateFeet), s.hero_step));
            for (gaddr off : kStateSmoothVel) write_v3(memory, state + off, {0, 0, 0});
        }
    }
    s.feet_valid = false;
}

// Aim Control: Free Movement. The aim button held switches to the other view: from third person to first person
// (where the player can keep moving), or, for a player who plays in first person (the game's own
// option on), to the Modern third-person camera and its crosshair. Let go, the view goes back,
// looking where it looked.
void aim_move_tick(openrac::Mod& mod) {
    const auto memory = mod.memory();
    const gaddr toggle = boot_global(mod, kFirstPersonToggle);
    // Any item in hand (a weapon, the wrench, a gadget), as the game's own aim (L1's look) takes any.
    // On a planetoid the aim stays in third person, the boom eased in over the shoulder.
    const bool planetoid = mod.play_status().phase == OPENRAC_PLAY_PLAYING && on_planetoid(mod, memory);
    const float zoom_to = planetoid && s.fp_want && !s.tp_active ? kAimZoom : 1.0f;
    s.aim_zoom += (zoom_to - s.aim_zoom) * (1.0f - std::exp(-(1.0f / 60.0f) / 0.12f));
    const bool held = s.fp_want && !s.fp_refused && memory.read<std::uint8_t>(kHeroForm) == 0
        && memory.read<gaddr>(kHeroVehicle) == 0 && mod.play_status().phase == OPENRAC_PLAY_PLAYING;
    const bool want = held && !planetoid;
    if (s.fp_grace > 0) --s.fp_grace;
    if (!want) s.fp_ease = 0;  // let go before it was in: the boom eases back out
    // The player's own first person, swapped for third person while held.
    if (s.tp_active) {
        if (held) return;
        const gaddr camera = find_camera(mod);
        if (camera != 0) view_from_camera(memory, camera);
        memory.write<std::int32_t>(toggle, s.fp_saved);
        s.tp_active = false;
        return;
    }
    if (held && !s.fp_active) {
        const gaddr camera = find_camera(mod);
        if (camera == 0) return;
        s.fp_saved = memory.read<std::int32_t>(toggle);
        if (s.fp_saved != 0) {
            // First person is the player's own setting: third person while held.
            if (!first_person_on(mod, memory)) return;  // the game shows no first person here
            boom_behind_view(mod, memory);
            memory.write<std::int32_t>(toggle, 0);
            s.tp_active = true;
            return;
        }
        if (!want) return;
        // Modern: first a short ease in, the boom along the view down into his eyes (Up Your Arsenal
        // blends into its look view over about 13 frames), then first person from there.
        if (s.modern && s.boom_owned && s.fp_ease < kAimEaseTicks) {
            if (s.fp_ease++ == 0) s.fp_ease_from = s.boom_length;
            return;
        }
        s.fp_ease = 0;
        if (!view_from_camera(memory, camera)) return;
        memory.write<std::int32_t>(toggle, 1);
        s.fp_active = true;
        s.fp_wait = 0;
        return;
    }
    if (!s.fp_active) return;
    if (want) ++s.fp_wait;
    if (want && s.fp_wait == 3 && !first_person_on(mod, memory)) {
        // Not taken: back, and the game's own aim for the rest of this press.
        memory.write<std::int32_t>(toggle, s.fp_saved);
        s.fp_active = false;
        s.fp_refused = true;
        return;
    }
    if (want) return;
    memory.write<std::int32_t>(toggle, s.fp_saved);
    s.fp_active = false;
    boom_behind_view(mod, memory);
}

// Manned turrets (state 0x31; docs/notes/rac2-robot-and-turret-controls.md). Both fire on Circle and
// aim from the left stick only; the game poses their camera and their shots from their angles each
// tick, so the right stick, the mouse and the gyro turn them by writing those angles.
// - Joba's (class 0xC20; manned: state 5, +0xBC 1): yaw +0xF8, pitch vars+0x44 (> 0 up), +-30 degrees.
// - Snivelak's (class 0x10EE; the hero's vehicle; aimed by the player in state 3, its intro in 2):
//   yaw +0xF8, pitch vars+0x90 (> 0 down), -30 (up) to +20 (down) degrees, rates vars+0x80, +0x84.
constexpr std::int16_t kJobaTurretClass = 0xC20, kSnivelakTurretClass = 0x10EE;
constexpr std::int32_t kMountedState = 0x31;
struct Turret {
    gaddr moby = 0;
    bool snivelak = false;
};

Turret turret_now(const openrac::Mod& mod, const openrac::Memory& memory) {
    if (memory.read<std::int32_t>(kHeroState) != kMountedState) return {};
    const gaddr ride = memory.read<gaddr>(kHeroVehicle);
    if (in_ram(ride) && memory.read<std::int16_t>(ride + kMobyClass) == kSnivelakTurretClass) {
        return memory.read<std::uint8_t>(ride + 0x20) == 3 ? Turret{ride, true} : Turret{};
    }
    const auto joba = [&](gaddr m) {
        return in_ram(m) && memory.read<std::int16_t>(m + kMobyClass) == kJobaTurretClass
            && memory.read<std::uint8_t>(m + 0x20) == 5 && memory.read<std::uint8_t>(m + 0xBC) == 1;
    };
    const gaddr under = memory.read<gaddr>(kHero + 0x33C);  // what he stands on
    if (joba(under)) return {under, false};
    for (const gaddr m : moby_table(mod, memory)) {
        if (joba(m)) return {m, false};
    }
    return {};
}

// Turns the turret by `yaw` (> 0 left) and `pitch` (> 0 up), radians, within its limits.
void turret_aim(const openrac::Memory& memory, const Turret& t, float yaw, float pitch) {
    const gaddr vars = memory.read<gaddr>(t.moby + kMobyVars);
    if (!in_ram(vars)) return;
    memory.write<float>(t.moby + 0xF8, memory.read<float>(t.moby + 0xF8) + yaw);
    if (t.snivelak) {
        const float down = std::clamp(memory.read<float>(vars + 0x90) - pitch, -30.0f * kDeg, 20.0f * kDeg);
        memory.write<float>(vars + 0x90, down);
        if (yaw != 0.0f) memory.write<float>(vars + 0x80, 0.0f);
        if (pitch != 0.0f) memory.write<float>(vars + 0x84, 0.0f);
    } else {
        memory.write<float>(vars + 0x44, std::clamp(memory.read<float>(vars + 0x44) + pitch, -30.0f * kDeg, 30.0f * kDeg));
    }
}

// What OpenRAC's mouse and gyro ask of the view the mod turns itself (`mod_view`), by OpenRAC's own
// settings: elsewhere OpenRAC turns the game's cameras itself. The mouse: right turns right, down
// looks down (Invert Y the other way). The gyro: the turn OpenRAC's filter gives (the sensitivity in
// it); set to "only while aiming", only while `aiming`.
Look builtin_look(const openrac::Input& input, bool mod_view, bool aiming, bool gyro_read, Look gyro) {
    Look look;
    if (!mod_view) return look;
    if (g_kbm.capture && input.raw().captured) {
        const float dx = input.mouse_dx(), dy = input.mouse_dy();
        look.yaw -= dx * g_kbm.sens_x * kRadiansPerPixel;
        look.pitch -= (g_kbm.invert_y ? -dy : dy) * g_kbm.sens_y * kRadiansPerPixel;
    }
    if (gyro_read && (g_kbm.gyro != "aiming" || aiming)) {
        look.yaw += gyro.yaw;
        look.pitch += gyro.pitch;
    }
    return look;
}

void frame() {
    openrac::Mod& mod = *s.mod;
    const auto memory = mod.memory();
    ++s.frame_no;
    {
        // His step each tick, whoever has the camera (the boom's hand-back after first person uses it).
        const V3 hero = read_v3(memory, kHeroPosition);
        s.hero_step = s.hero_seen_frame + 1 == s.frame_no ? vec::sub(hero, s.hero_seen) : V3{0, 0, 0};
        if (vec::dot(s.hero_step, s.hero_step) > 4.0f) s.hero_step = {0, 0, 0};
        s.hero_seen = hero;
        s.hero_seen_frame = s.frame_no;
    }
    aim_move_tick(mod);
    toss_tick(mod);  // Crouch: Deadlocked's wrench toss
    const auto status = mod.play_status();
    const auto input = mod.input();
    const bool playing = status.phase == OPENRAC_PLAY_PLAYING && !raw_level(status.level);
    kbm_refresh(mod);
    own_capture(mod, playing);  // (older OpenRAC)
    // The controller's gyro, by OpenRAC's gyro settings or the mod's own (older OpenRAC): read every
    // tick while a gyro is wanted, so its reports do not pile up while it is not used, and let go
    // while none is.
    const gyro::GyroOptions gyro_options = openrac_input() ? kbm_gyro_options() : own_gyro_options();  // (older OpenRAC)
    if (!gyro_options.enabled) g_gyro.close();
    Look gyro_turn;
    const bool gyro_read = gyro_options.enabled && g_gyro.poll(mod, gyro_options, gyro_turn.yaw, gyro_turn.pitch);
    // Out of play, or with no follow camera to be seen, the mod's hold on the pad's right stick and
    // the turret's Fire let go.
    const auto let_go = [] {
        s.stick_owned = false;
        s.in_turret = false;
        own_reset();  // (older OpenRAC)
    };
    if (!playing) {
        let_go();
        return;
    }
    const gaddr camera = find_camera(mod);
    if (camera == 0) {
        fade_hero(mod, memory, 0x80);
        let_go();
        return;
    }
    const bool fp = first_person_on(mod, memory), aiming = aiming_now(mod, memory);
    // Modern's third-person boom: where the mod turns the camera itself (else OpenRAC's mouse and
    // gyro turn it, and the game's stick).
    const bool here = mod_camera_here(mod, memory, camera);
    const Turret turret = turret_now(mod, memory);
    s.in_turret = turret.moby != 0;
    const bool boom_view = s.modern && here && !aiming && !fp;
    // Ratchet shown whole wherever the boom is not the view (it fades him only when it is in close):
    // first person, aiming, Classic, and every camera of the game's.
    if (!boom_view) fade_hero(mod, memory, 0x80);
    // Turrets aimed by the right stick, the mouse and the gyro: Modern's (Classic leaves every
    // camera, and every view, the game's).
    const bool turret_aim_on = s.in_turret && s.modern;
    // Where the mod turns the view itself with OpenRAC's mouse and gyro: the boom, and the first
    // person of Free Movement (OpenRAC's built-in drives that view through the right stick, which the
    // mod holds there for its own stick speed).
    const bool mod_view = boom_view || (s.fp_active && fp) || turret_aim_on;
    // Modern, in third person (the follow camera, not aiming or in first person): the right stick
    // is the mod's, as the mouse and gyro are.
    const bool third_person = here && !aiming && !fp;
    s.stick_owned = s.modern && (third_person || (s.fp_active && fp));
    float stick_yaw = 0, stick_pitch = 0;
    // The aim assist (Modern's third person and Free Movement's first person): its weight first, for the
    // stick's friction, and the view's follow of the enemy.
    float track_yaw = 0, track_pitch = 0;
    assist_tick(mod, memory, camera, s.stick_owned && !s.in_turret, track_yaw, track_pitch);
    // In a turret the right stick (which the game leaves alone there) turns it, as Modern's camera.
    if (s.stick_owned || turret_aim_on) modern_stick(mod, memory, stick_yaw, stick_pitch);
    if (!s.stick_owned && !turret_aim_on) s.stick_yaw_rate = s.stick_pitch_rate = 0;
    // What the mouse and gyro ask for this tick.
    // OpenRAC's "only while aiming": the game's aim (a strafe button held, or first person). Here the
    // player's Aim button held, or first person; the mod's lock-strafe, held with any weapon out, is
    // not aiming.
    const bool aiming_for_gyro = s.aim_held || fp || aiming || s.fp_active;
    const Look look = openrac_input() ? builtin_look(input, mod_view, aiming_for_gyro, gyro_read, gyro_turn)
                                      : own_look(input, gyro_read, gyro_turn);  // (older OpenRAC)
    const float yaw = stick_yaw + track_yaw + look.yaw, pitch = stick_pitch + track_pitch + look.pitch;
    const bool moved = yaw != 0.0f || pitch != 0.0f;
    // The game's own cameras (Classic's, Clank's, the robot's, the Glider's, a vehicle's, the aim
    // camera): only the mod's own mouse and gyro work them, as OpenRAC's built-in does on builds that
    // have one.
    if (!openrac_input()) own_game_camera(mod, memory, camera, here, fp, aiming, boom_view, turret_aim_on, look);  // (older OpenRAC)
    if (turret_aim_on) {
        if (moved) turret_aim(memory, turret, yaw, pitch);
        return;
    }
    if (fp) {
        if (moved) first_person_tick(memory, yaw, pitch);
        return;
    }
    if (boom_view) {
        // Modern: the turn to the boom's direction, then the boom put out at it.
        if (yaw != 0.0f) apply_yaw(memory, camera, yaw);
        boom_tick(mod, memory, camera, pitch);
    }
}

// ---------------------------------------------------------------------------------------------
// The page
// ---------------------------------------------------------------------------------------------

// What the pad said lately; Unknown when it said nothing for a moment (the port may keep the pad
// from mods while its page shows).
strafe::Intent recent_intent() {
    using namespace std::chrono;
    return steady_clock::now() - s.intent_at < milliseconds(100) ? s.intent : strafe::Intent::Unknown;
}

// ---------------------------------------------------------------------------------------------
// The menu: Options > Dead Aim opens one page of tabs that the mod draws over the game's own
// (the new page, OpenRAC 1.31 and newer), or on older OpenRACs a hub of text pages (its part,
// "Older OpenRAC: text pages"), both made from the same tabs and rows (docs/notes/rac2-menu-ui.md).
// Labels and choices keep to what the game's font draws (no % * = [ ] _ { } ~, no & or #) and to
// 46 bytes; the pad's buttons go in them as OpenRAC's button tokens (OPENRAC_UI_L1 ...), which the
// game's font draws as the pad's icons and the overlay as the player's device has the button.
// ---------------------------------------------------------------------------------------------

// A setting's choice as the menu shows it: the manifest's name, each word capitalised.
std::string title_case(std::string t) {
    bool start = true;
    for (char& ch : t) {
        if (start) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        start = ch == ' ';
    }
    return t;
}

template <std::size_t N>
std::vector<std::string> menu_names(const char* const (&names)[N]) {
    std::vector<std::string> v;
    for (const char* name : names) v.push_back(title_case(name));
    return v;
}

// The actions as the menu names them (the manifest's names, strafe::kActions).
std::vector<std::string> menu_actions() {
    std::vector<std::string> names;
    for (int i = 0; i < strafe::kActionCount; ++i) names.push_back(title_case(strafe::action_name(static_cast<strafe::Action>(i))));
    return names;
}

// A row of a tab: its label, the line the new page shows for it, its values, the one it shows and
// what choosing another does.
struct Row {
    std::string label;
    std::string help;
    std::function<std::vector<std::string>()> choices;
    std::function<int()> current;
    std::function<void(int)> set;
    bool slider = false;  // shown as a bar on the new page (its choices run low to high)
};

// A tab: its name, its rows (made again each time it shows, so they tell the settings as they are)
// and the drawing above them on the new page.
struct Pen;
struct Tab {
    const char* name;
    std::function<std::vector<Row>()> rows;
    void (*diagram)(const Pen& p, float x, float y, float w, float h, int cursor);
    bool own = false;  // the mod's own mouse and gyro's (older OpenRAC): shown without OpenRAC's only
};

std::vector<Tab> g_tabs;
openrac::UiPage g_main;
constexpr std::uint32_t kAskRestore = 1;

// The tabs shown, in order: the mod's own mouse and gyro's (older OpenRAC), the last, only where
// OpenRAC has none built in.
int tab_count() {
    int n = 0;
    for (const Tab& tab : g_tabs) n += !tab.own || !openrac_input() ? 1 : 0;
    return n;
}

Row action_row(const std::string& label, const std::string& help, strafe::Action* action, const char* key, bool can_be_nothing = false) {
    Row r;
    r.label = label;
    r.help = help;
    // A controller's button always does something: Nothing, the last action, is the mouse
    // buttons' only (older OpenRAC).
    r.choices = [can_be_nothing, action] {
        std::vector<std::string> v = menu_actions();
        if (!can_be_nothing && *action != strafe::Action::None) v.pop_back();
        return v;
    };
    r.current = [action] { return static_cast<int>(*action); };
    r.set = [action, key](int i) {
        *action = static_cast<strafe::Action>(i);
        save_setting(key, std::string(strafe::action_value(*action)));
    };
    return r;
}

// The camera style changed: the boom and the assist start afresh. (Ratchet is shown whole again by
// frame, wherever the boom is not the view.)
void set_modern(bool modern) {
    s.modern = modern;
    s.boom_owned = false;
    s.assist = 0.0f;
    own_reset();  // (older OpenRAC)
}

// Every setting back to the manifest's default, told to the launcher, then read back.
void restore_defaults() {
    const strafe::Config defaults;
    save_setting("lock_mode", std::string("weapon"));
    for (int i = 0; i < strafe::kShoulderCount; ++i) {
        save_setting(strafe::kShoulderInfo[i].key, std::string(strafe::action_value(defaults.actions[i])));
    }
    save_setting("crouch", std::string("deadlocked"));
    save_setting("auto_gadgets", std::string("all"));
    save_setting("camera_style", std::string("modern"));
    save_setting("aim_control", std::string("free"));
    save_setting("crosshair_size", 0.3);
    save_setting("camera_speed", std::string("normal"));
    save_setting("vertical_sensitivity", std::string("deadlocked"));
    save_setting("vertical_sensitivity_custom", 0.7);
    save_setting("aim_assist", std::string("deadlocked"));
    own_restore_defaults();  // (older OpenRAC)
    read_config(*s.mod);
}

std::vector<Row> buttons_rows() {
    std::vector<Row> rows;
    Row lock;
    lock.label = "Lock Strafe";
    lock.help = s.lock == Lock::Off      ? "Ratchet turns freely, facing where he runs."
                : s.lock == Lock::Weapon ? "Locks Ratchet's back to the camera while a weapon is out."
                                         : "Always locks Ratchet's back to the camera.";
    lock.choices = [] { return menu_names(kLockNames); };
    lock.current = [] { return static_cast<int>(s.lock); };
    lock.set = [](int i) {
        s.lock = static_cast<Lock>(i);
        save_setting("lock_mode", std::string(kLockValues[i]));
    };
    rows.push_back(lock);
    Row crouch;
    crouch.label = "Crouch Behavior";
    crouch.help = s.crouch == CrouchMode::Standard    ? "Crouch stops Ratchet on the run."
                  : s.crouch == CrouchMode::Deadlocked ? "Hold it on the run: " OPENRAC_UI_CROSS " long jumps, " OPENRAC_UI_SQUARE " tosses the wrench."
                                                       : "As Deadlocked, and " OPENRAC_UI_SQUARE " tosses the wrench in the air too.";
    crouch.choices = [] { return menu_names(kCrouchNames); };
    crouch.current = [] { return static_cast<int>(s.crouch); };
    crouch.set = [](int i) {
        s.crouch = static_cast<CrouchMode>(i);
        save_setting("crouch", std::string(kCrouchValues[i]));
    };
    rows.push_back(crouch);
    const char* icons[strafe::kShoulderCount] = {"L1", "L2", "R1", "R2"};
    const char* helps[strafe::kShoulderCount] = {"What " OPENRAC_UI_L1 " does as Ratchet.", "What " OPENRAC_UI_L2 " does as Ratchet.",
                                                 "What " OPENRAC_UI_R1 " does as Ratchet.", "What " OPENRAC_UI_R2 " does as Ratchet."};
    for (int i = 0; i < strafe::kShoulderCount; ++i) {
        rows.push_back(action_row(icons[i], helps[i], &s.config.actions[i], strafe::kShoulderInfo[i].key));
    }
    Row gadgets;
    gadgets.label = "Auto Gadgets";
    gadgets.help = g_auto.tier == AutoTier::Off       ? "Equip gadgets yourself."
                   : g_auto.tier == AutoTier::Swingshot ? "Swaps to the Swingshot automatically."
                                                        : "Swaps to all gadgets automatically.";
    gadgets.choices = [] { return menu_names(kAutoNames); };
    gadgets.current = [] { return static_cast<int>(g_auto.tier); };
    gadgets.set = [](int i) {
        g_auto.tier = static_cast<AutoTier>(i);
        save_setting("auto_gadgets", std::string(kAutoValues[i]));
    };
    rows.push_back(gadgets);
    return rows;
}

std::vector<Row> camera_rows() {
    std::vector<Row> rows;
    Row style;
    style.label = "Camera";
    style.help = s.modern ? "A free camera: weapons aim where you look." : "The game's own camera, untouched.";
    style.choices = [] { return menu_names(kStyleNames); };
    style.current = [] { return s.modern ? 0 : 1; };
    style.set = [](int i) {
        set_modern(i == 0);
        save_setting("camera_style", std::string(kStyleValues[i]));
    };
    rows.push_back(style);
    // Classic: the game's own camera, aim and crosshairs, untouched; nothing more to set.
    if (!s.modern) return rows;
    Row aim;
    aim.label = "Aim Control";
    aim.help = s.aim_move ? "Hold Aim to switch view and keep moving." : "Stand still to aim.";
    aim.choices = [] { return menu_names(kAimNames); };
    aim.current = [] { return s.aim_move ? 0 : 1; };
    aim.set = [](int i) {
        s.aim_move = i == 0;
        save_setting("aim_control", std::string(kAimValues[i]));
    };
    rows.push_back(aim);
    // The crosshair's size in tenths, Off (0) to 150 percent (15).
    Row crosshair;
    crosshair.label = "Crosshair";
    crosshair.slider = true;
    crosshair.help = crosshair_shown() ? "The centre crosshair's size, in first and third person."
                                       : "Off: the game's own first-person reticle only.";
    crosshair.choices = [] {
        std::vector<std::string> v{"Off"};
        for (int p = 10; p <= 150; p += 10) v.push_back(std::to_string(p) + " Percent");
        return v;
    };
    crosshair.current = [] { return std::clamp(static_cast<int>(std::lround(s.crosshair_size * 10.0f)), 0, 15); };
    crosshair.set = [](int i) {
        const double size = i / 10.0;
        s.crosshair_size = static_cast<float>(size);
        save_setting("crosshair_size", size);
    };
    rows.push_back(crosshair);
    Row speed;
    speed.label = "Camera Speed";
    speed.help = "Deadlocked's speeds. Full stick sideways turns faster.";
    speed.choices = [] { return menu_names(kCameraSpeedNames); };
    speed.current = [] { return s.camera_speed; };
    speed.set = [](int i) {
        s.camera_speed = i;
        save_setting("camera_speed", std::string(kCameraSpeedValues[i]));
    };
    rows.push_back(speed);
    Row vertical;
    vertical.label = "Vertical Sensitivity";
    vertical.help = s.vertical == 0   ? "Deadlocked's: vertical at two-thirds of horizontal."
                    : s.vertical == 1 ? "Up Your Arsenal's: vertical a third faster than horizontal."
                                      : "Your own, set on the row below.";
    vertical.choices = [] { return menu_names(kVerticalNames); };
    vertical.current = [] { return s.vertical; };
    vertical.set = [](int i) {
        s.vertical = i;
        save_setting("vertical_sensitivity", std::string(kVerticalValues[i]));
    };
    rows.push_back(vertical);
    if (s.vertical == 2) {
        Row custom;
        custom.label = "Custom Sensitivity";
        custom.slider = true;
        custom.help = "Higher: faster vertical. Horizontal stays the same.";
        custom.choices = [] {
            std::vector<std::string> v;
            for (int t = kVerticalCustomLowest; t <= kVerticalCustomHighest; ++t) v.push_back(std::to_string(t * 10) + " Percent");
            return v;
        };
        custom.current = [] {
            const int t = static_cast<int>(std::lround(s.vertical_custom * 10.0f));
            return std::clamp(t, kVerticalCustomLowest, kVerticalCustomHighest) - kVerticalCustomLowest;
        };
        custom.set = [](int i) {
            const double share = (i + kVerticalCustomLowest) / 10.0;
            s.vertical_custom = static_cast<float>(share);
            save_setting("vertical_sensitivity_custom", share);
        };
        rows.push_back(custom);
    }
    Row assist;
    assist.label = "Aim Assist";
    assist.help = s.assist_style == 0   ? "Off: the stick is never slowed or pulled."
                  : s.assist_style == 1 ? "Deadlocked's: near an enemy, the stick slows and follows."
                                        : "Up Your Arsenal's: a narrower, shorter pull than Deadlocked's.";
    assist.choices = [] { return menu_names(kAssistNames); };
    assist.current = [] { return s.assist_style; };
    assist.set = [](int i) {
        s.assist_style = i;
        save_setting("aim_assist", std::string(kAssistValues[i]));
    };
    rows.push_back(assist);
    return rows;
}

// The tab's rows as the game's page items: the new page's input only. The new page draws the rows
// itself, over the game's, so the game's are blank (a row of them peeked out between its panels).
void fill_items(openrac::UiContent& content, const std::vector<Row>& rows) {
    for (const Row& row : rows) {
        content.items.push_back(openrac::UiItem::choice_of(" ", std::vector<std::string>(row.choices().size(), " "),
                                                           static_cast<std::uint32_t>(row.current())));
    }
}

// The text pages' part (older OpenRAC: text pages), further down.
void text_show_hub();
void text_show_tabs();
void text_on_hub(const openrac::UiEvent& event);
void text_add_pages(openrac::Mod& mod);

void show_main() {
    if (!s.has_page) return;
    if (!g_fancy) {  // (older OpenRAC: text pages)
        text_show_hub();
        return;
    }
    openrac::UiContent content;
    content.title = kPageName;
    fill_items(content, g_tabs[g_tab].rows());
    g_main.set(content);
}

// Every page shows the settings as they are (after the launcher changed them, say).
void show_page() {
    show_main();
    if (!g_fancy) text_show_tabs();  // (older OpenRAC: text pages)
}

// A change to a row's value (the new page's and the text pages' alike): the step the pad asked for.
void change_row(const Row& row, const openrac::UiEvent& event) {
    const int count = static_cast<int>(row.choices().size());
    if (const auto next = strafe::step_choice(recent_intent(), row.current(), count, event.value())) row.set(*next);
}

void on_fancy(const openrac::UiEvent& event) {
    const int n = tab_count();
    if (event.is(OPENRAC_UI_SHOW)) {
        g_shown = -1;
        g_tab_step = 0;
        g_square = false;
        g_asking = false;
        show_main();
    } else if (event.is(OPENRAC_UI_HIDE)) {
        if (g_shown == -1) g_shown = -2;
    } else if (event.is(OPENRAC_UI_FRAME)) {
        g_cursor = event.cursor();
        if (g_tab_step != 0 && !g_asking) {
            g_tab = (g_tab + g_tab_step + n) % n;
            g_cursor = 0;
            show_main();
            g_main.sound(OPENRAC_UI_SOUND_PAGE);
        }
        g_tab_step = 0;
        if (g_square && !g_asking) g_asking = g_main.confirm("Restore the default controls?", kAskRestore);
        g_square = false;
    } else if (event.is(OPENRAC_UI_ANSWER) && event.tag() == kAskRestore) {
        g_asking = false;
        if (event.value() == 1) {
            restore_defaults();
            g_main.sound(OPENRAC_UI_SOUND_SELECT);
        }
        show_page();
    } else if (event.is(OPENRAC_UI_CHOOSE) || event.is(OPENRAC_UI_CHANGE)) {
        const std::vector<Row> rows = g_tabs[g_tab].rows();
        const int r = event.item();
        if (r < 0 || r >= static_cast<int>(rows.size())) return;
        if (event.is(OPENRAC_UI_CHANGE)) change_row(rows[r], event);
        show_main();
    }
}

void on_main(const openrac::UiPage&, const openrac::UiEvent& event) {
    if (!g_fancy) {  // (older OpenRAC: text pages)
        text_on_hub(event);
        return;
    }
    on_fancy(event);
}

// =============================================================================================
// Older OpenRAC: text pages
// =============================================================================================
//
// OpenRAC before 1.31 has no menu overlay (g_fancy false): Dead Aim is a hub of the tabs and
// Restore Defaults in the game's own page, and each tab a page of its own, its rows "Label: Value".
// Deleting this section, g_fancy and the `if (!g_fancy)` at each entry point (show_main,
// show_page, on_main, add_page) drops them.

std::vector<openrac::UiPage> g_text_pages;  // a page a tab

void text_show_tab(int t) {
    if (!s.has_page || t < 0 || t >= static_cast<int>(g_text_pages.size())) return;
    openrac::UiContent content;
    content.title = g_tabs[t].name;
    for (const Row& row : g_tabs[t].rows()) {
        content.items.push_back(openrac::UiItem::choice_of(row.label, row.choices(), static_cast<std::uint32_t>(row.current())));
    }
    g_text_pages[t].set(content);
}

void text_show_tabs() {
    for (int t = 0; t < static_cast<int>(g_tabs.size()); ++t) text_show_tab(t);
}

void text_show_hub() {
    openrac::UiContent content;
    content.title = kPageName;
    for (int t = 0; t < tab_count(); ++t) content.items.push_back(openrac::UiItem::button(g_tabs[t].name));
    content.items.push_back(openrac::UiItem::button("Restore Defaults"));
    g_main.set(content);
}

void text_on_hub(const openrac::UiEvent& event) {
    const int n = tab_count();
    if (event.is(OPENRAC_UI_SHOW)) {
        g_shown = -1;
        show_main();
    } else if (event.is(OPENRAC_UI_HIDE)) {
        if (g_shown == -1) g_shown = -2;
    } else if (event.is(OPENRAC_UI_CHOOSE)) {
        const int i = event.item();
        if (i >= 0 && i < n) {
            text_show_tab(i);
            if (g_text_pages[i].open()) g_main.sound(OPENRAC_UI_SOUND_PAGE);
        } else if (i == n) {
            g_main.confirm("Restore the default controls?", kAskRestore);
        }
    } else if (event.is(OPENRAC_UI_ANSWER) && event.tag() == kAskRestore) {
        if (event.value() == 1) {
            restore_defaults();
            g_main.sound(OPENRAC_UI_SOUND_SELECT);
        }
        show_page();
    }
}

void text_on_tab(int t, const openrac::UiEvent& event) {
    if (event.is(OPENRAC_UI_SHOW)) {
        g_shown = t;
        text_show_tab(t);
        return;
    }
    if (event.is(OPENRAC_UI_HIDE)) {
        if (g_shown == t) g_shown = -2;
        return;
    }
    if (event.is(OPENRAC_UI_BACK)) {
        show_main();
        g_main.open();
        return;
    }
    if (!event.is(OPENRAC_UI_CHANGE)) return;
    const std::vector<Row> rows = g_tabs[t].rows();
    const int r = event.item();
    if (r < 0 || r >= static_cast<int>(rows.size())) return;
    change_row(rows[r], event);
    // Shown again either way: the port may have shown a step the mod did not take.
    text_show_tab(t);
}

void text_add_pages(openrac::Mod& mod) {
    for (int t = 0; t < static_cast<int>(g_tabs.size()); ++t) {
        g_text_pages.push_back(
            mod.ui_page([t](const openrac::UiPage&, const openrac::UiEvent& event) { text_on_tab(t, event); }, OPENRAC_UI_OWN_BACK));
    }
}

// ---------------------------------------------------------------------------------------------
// The new page (OpenRAC 1.31 and newer): Options > Dead Aim opens one page with tabs (L1 / R1),
// drawn by the mod over the game's own page (on_overlay_phases, OPENRAC_OVERLAY_MENU), in Going
// Commando's menu look (teal glass, light edges, beveled plates), after Up Your Arsenal's and
// Deadlocked's controls pages: a diagram of what the tab sets on top, its settings below, what the
// one under the cursor does, the buttons. The game's page under it keeps the cursor and the values
// (its rows are the tab's); the mod draws over all of it. Older OpenRACs get the text pages
// ("Older OpenRAC: text pages", above), made from the same tabs and rows.
// ---------------------------------------------------------------------------------------------

// The picture is 512 x 448 units, stretched to the window; the overlay's text keeps its shape, so
// its width tells the stretch: a font cell is 6 by 8 pixels.
struct Pen {
    const openrac_overlay* o;
    float ax = 1.0f;  // horizontal units for one vertical unit of true length (round things stay round)
    bool dry = false;  // measuring only: nothing drawn

    explicit Pen(const openrac_overlay* overlay) : o(overlay) {
        if (o->text_width) {
            const float w = o->text_width(o->context, 10.0f, "MMMMMMMMMM");
            if (w > 1.0f) ax = std::clamp(w / 75.0f, 0.3f, 3.0f);
        }
    }
    void rect(float x, float y, float w, float h, std::uint32_t c) const {
        if (w > 0 && h > 0 && !dry) o->rect(o->context, x, y, w, h, c);
    }
    void edge(float x, float y, float w, float h, float t, std::uint32_t c) const {
        rect(x, y, w, t, c);
        rect(x, y + h - t, w, t, c);
        rect(x, y + t, t * ax, h - 2 * t, c);
        rect(x + w - t * ax, y + t, t * ax, h - 2 * t, c);
    }
    float width(float h, const std::string& t) const { return o->text_width ? o->text_width(o->context, h, t.c_str()) : t.size() * h * 0.64f; }
    void text(float x, float y, float h, std::uint32_t c, const std::string& t) const {
        if (!dry) o->text(o->context, x, y, h, c, t.c_str());
    }
    void text_centred(float cx, float y, float h, std::uint32_t c, const std::string& t) const { text(cx - width(h, t) / 2, y, h, c, t); }
    void text_right(float rx, float y, float h, std::uint32_t c, const std::string& t) const { text(rx - width(h, t), y, h, c, t); }
    // A filled ellipse: radii in true units (rx is scaled by ax), drawn in rows.
    void ellipse(float cx, float cy, float rx, float ry, std::uint32_t c) const {
        constexpr float step = 0.5f;
        for (float y = -ry; y < ry; y += step) {
            const float yc = y + step / 2;
            const float k = 1.0f - (yc * yc) / (ry * ry);
            if (k <= 0) continue;
            const float hw = rx * std::sqrt(k) * ax;
            rect(cx - hw, cy + y, 2 * hw, step, c);
        }
    }
    // A plate with its corners cut (Going Commando's buttons): `cut` true units. Rows only where
    // the corners are; the middle in one.
    void plate(float x, float y, float w, float h, float cut, std::uint32_t c) const {
        constexpr float step = 0.5f;
        const float band = std::fmin(cut, h / 2);
        for (float d = 0; d < band; d += step) {
            const float in = (cut - d) * ax;
            rect(x + in, y + d, w - 2 * in, step, c);
            rect(x + in, y + h - d - step, w - 2 * in, step, c);
        }
        rect(x, y + band, w, h - 2 * band, c);
    }
    // A line `t` thick (true units): one rectangle when it runs straight across or down, else small
    // squares along it.
    void line(float x0, float y0, float x1, float y1, float t, std::uint32_t c) const {
        if (std::fabs(y1 - y0) < 1e-3f) {
            rect(std::fmin(x0, x1) - t * ax / 2, y0 - t / 2, std::fabs(x1 - x0) + t * ax, t, c);
            return;
        }
        if (std::fabs(x1 - x0) < 1e-3f) {
            rect(x0 - t * ax / 2, std::fmin(y0, y1) - t / 2, t * ax, std::fabs(y1 - y0) + t, c);
            return;
        }
        const float dx = (x1 - x0) / ax, dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        const int n = std::max(1, static_cast<int>(len / (t * 0.5f)));
        for (int i = 0; i <= n; ++i) {
            const float f = static_cast<float>(i) / n;
            rect(x0 + (x1 - x0) * f - t * ax / 2, y0 + (y1 - y0) * f - t / 2, t * ax, t, c);
        }
    }
};

// Going Commando's menu colours (0xRRGGBBAA).
constexpr std::uint32_t kGlassDeep = 0x061A24FFu, kEdge = 0x8FE0FFE0u, kEdgeDim = 0x4FA8C8A0u, kWhite = 0xFFFFFFFFu,
                        kDim = 0x9CC4D2FFu, kOrange = 0xFFB040FFu;

std::string upper(std::string t) {
    for (char& ch : t) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return t;
}

// ---- Buttons as the player's device has them (button_label, 1.31) ----
// A PlayStation pad's buttons are drawn as the game draws them in its Help > Controls page and its
// prompts (a dark tab with its name for a shoulder, a dark disc with its coloured symbol for a face
// button); a lettered pad's as a disc with its letter; the keyboard's as a keycap with the key.
// OpenRAC's name for a bind ("ShiftLeft", "KeyR", "MouseForward") as a keycap shows it.
std::string key_name(std::string bind) {
    const auto comma = bind.find(',');
    if (comma != std::string::npos) bind = bind.substr(0, comma);
    static const struct { const char* from; const char* to; } names[] = {
        {"ShiftLeft", "SHIFT"}, {"ShiftRight", "SHIFT"}, {"ControlLeft", "CTRL"}, {"ControlRight", "CTRL"}, {"AltLeft", "ALT"},
        {"AltRight", "ALT"}, {"Space", "SPACE"}, {"Enter", "ENTER"}, {"Backspace", "BACK SPACE"}, {"Escape", "ESC"}, {"Tab", "TAB"},
        {"MouseLeft", "LMB"}, {"MouseRight", "RMB"}, {"MouseMiddle", "MMB"}, {"MouseForward", "MOUSE 5"}, {"MouseBack", "MOUSE 4"}};
    for (const auto& n : names) {
        if (bind == n.from) return n.to;
    }
    if (bind.rfind("Key", 0) == 0) bind = bind.substr(3);
    else if (bind.rfind("Digit", 0) == 0) bind = bind.substr(5);
    return upper(bind);
}

int device_label(std::uint32_t bit, std::string& label) {
    const openrac_api& api = s.mod->api();
    char buf[48] = {};
    int kind = OPENRAC_BUTTONS_PLAYSTATION;
    if (api.api_minor >= 31 && api.size >= offsetof(openrac_api, button_label) + sizeof(void*) && api.button_label) {
        kind = api.button_label(s.mod->handle(), bit, buf, sizeof buf);
    }
    label.clear();
    for (const char* c = buf; *c; ++c) {
        if (*c != '[' && *c != ']') label += *c;
    }
    // OpenRAC's prompts name only some buttons by their keys: L2 and R2 come back as the pad's while
    // the others are keys. Their keys then from OpenRAC's own binds (Strafe, R2); a build with no
    // built-in has none (older OpenRAC).
    if ((bit == OPENRAC_PAD_L2 || bit == OPENRAC_PAD_R2) && kind == OPENRAC_BUTTONS_PLAYSTATION && openrac_input()) {
        char probe[48] = {};
        if (api.api_minor >= 31 && api.button_label
            && api.button_label(s.mod->handle(), OPENRAC_PAD_CROSS, probe, sizeof probe) == OPENRAC_BUTTONS_KEYS) {
            label = key_name(bit == OPENRAC_PAD_L2 ? g_kbm.strafe_bind : g_kbm.r2_bind);
            return OPENRAC_BUTTONS_KEYS;
        }
    }
    return kind;
}

constexpr std::uint32_t kIconEdge = 0xC8D6E4FFu, kIconFill = 0x2A323CFFu;

// Draws button `bit` at (x, y), `h` tall; returns how wide it is.
float button_icon(const Pen& p, float x, float y, float h, std::uint32_t bit, bool lit = false) {
    std::string label;
    const int kind = device_label(bit, label);
    const std::uint32_t edge = lit ? kOrange : kIconEdge;
    if (kind == OPENRAC_BUTTONS_KEYS || kind == OPENRAC_BUTTONS_LETTERS) {
        if (label.empty()) label = "?";
        const float th = h * 0.62f, tw = p.width(th, label);
        if (kind == OPENRAC_BUTTONS_LETTERS && label.size() <= 2) {
            const float r = h / 2;
            p.ellipse(x + r * p.ax, y + r, r, r, edge);
            p.ellipse(x + r * p.ax, y + r, r - 0.9f, r - 0.9f, kIconFill);
            p.text_centred(x + r * p.ax, y + (h - th) / 2, th, kWhite, label);
            return 2 * r * p.ax;
        }
        // A keycap: light, its key in dark.
        const float w = tw + 6 * p.ax;
        p.plate(x, y, w, h, 2.0f, lit ? kOrange : 0x8A96A2FFu);
        p.plate(x + 0.8f * p.ax, y + 0.8f, w - 1.6f * p.ax, h - 2.2f, 1.6f, 0xDCE4ECFFu);
        p.text(x + 3 * p.ax, y + (h - th) / 2 - 0.4f, th, 0x1A2028FFu, label);
        return w;
    }
    const bool shoulder = bit == OPENRAC_PAD_L1 || bit == OPENRAC_PAD_L2 || bit == OPENRAC_PAD_R1 || bit == OPENRAC_PAD_R2;
    if (shoulder) {
        const char* name = bit == OPENRAC_PAD_L1 ? "L1" : bit == OPENRAC_PAD_L2 ? "L2" : bit == OPENRAC_PAD_R1 ? "R1" : "R2";
        const float w = h * 1.7f * p.ax, th = h * 0.62f;
        p.plate(x, y, w, h, 2.5f, edge);
        p.plate(x + 0.9f * p.ax, y + 0.9f, w - 1.8f * p.ax, h - 1.8f, 2.0f, kIconFill);
        p.text_centred(x + w / 2, y + (h - th) / 2, th, kWhite, name);
        return w;
    }
    const bool face = bit == OPENRAC_PAD_TRIANGLE || bit == OPENRAC_PAD_CIRCLE || bit == OPENRAC_PAD_CROSS || bit == OPENRAC_PAD_SQUARE;
    if (face) {
        const float r = h / 2, cx = x + r * p.ax, cy = y + r, k = r * 0.5f;
        p.ellipse(cx, cy, r, r, edge);
        p.ellipse(cx, cy, r - 0.9f, r - 0.9f, kIconFill);
        if (bit == OPENRAC_PAD_TRIANGLE) {
            const std::uint32_t c = 0x48D08CFFu;
            p.line(cx, cy - k, cx + k * 0.95f * p.ax, cy + k * 0.7f, 0.9f, c);
            p.line(cx, cy - k, cx - k * 0.95f * p.ax, cy + k * 0.7f, 0.9f, c);
            p.line(cx - k * 0.95f * p.ax, cy + k * 0.7f, cx + k * 0.95f * p.ax, cy + k * 0.7f, 0.9f, c);
        } else if (bit == OPENRAC_PAD_CIRCLE) {
            p.ellipse(cx, cy, k, k, 0xE84A5AFFu);
            p.ellipse(cx, cy, k - 0.9f, k - 0.9f, kIconFill);
        } else if (bit == OPENRAC_PAD_CROSS) {
            const std::uint32_t c = 0x7C9CF0FFu;
            p.line(cx - k * p.ax, cy - k, cx + k * p.ax, cy + k, 0.9f, c);
            p.line(cx - k * p.ax, cy + k, cx + k * p.ax, cy - k, 0.9f, c);
        } else {
            p.edge(cx - k * p.ax, cy - k, 2 * k * p.ax, 2 * k, 0.8f, 0xE07CC0FFu);
        }
        return 2 * r * p.ax;
    }
    // Start, Select and the rest: the device's name for it.
    if (label.empty() || static_cast<unsigned char>(label[0]) < 0x20) label = bit == OPENRAC_PAD_START ? "START" : "SELECT";
    const float th = h * 0.55f, w = p.width(th, label) + 6 * p.ax;
    p.plate(x, y, w, h, 3.0f, edge);
    p.plate(x + 0.9f * p.ax, y + 0.9f, w - 1.8f * p.ax, h - 1.8f, 2.5f, kIconFill);
    p.text(x + 3 * p.ax, y + (h - th) / 2, th, kWhite, label);
    return w;
}

// The game's label chips (Help > Controls): a dark blue plate, a light edge, white text, optionally
// a button before it. Placed with its `left` edge at x (or its right edge, when !left), its middle
// at y; returns the x of the edge facing the drawing (where its leader starts).
float chip(const Pen& p, bool left_side, float x, float y, std::uint32_t bit, const std::string& text, bool lit) {
    const float h = 13, th = 8.5f, pad = 4 * p.ax;
    Pen measure = p;
    measure.dry = true;
    const float iw = bit ? button_icon(measure, 0, 0, 9, bit) + 3 * p.ax : 0;
    const float w = pad + iw + p.width(th, text) + pad;
    const float x0 = left_side ? x - w : x;
    p.plate(x0, y - h / 2, w, h, 3.0f, lit ? kOrange : 0x9CD4FFFFu);
    p.plate(x0 + 0.8f * p.ax, y - h / 2 + 0.8f, w - 1.6f * p.ax, h - 1.6f, 2.5f, 0x163C78F0u);
    float cx = x0 + pad;
    if (bit) cx += button_icon(p, cx, y - 4.5f, 9, bit, lit) + 3 * p.ax;
    p.text(cx, y - th / 2, th, lit ? kOrange : kWhite, text);
    return left_side ? x : x0;
}

// A leader as the game draws them: across from the chip, then up or down to the part.
void elbow(const Pen& p, float x0, float y0, float px, float py) {
    const std::uint32_t c = 0xC8E8FFC8u;
    p.line(x0, y0, px, y0, 0.7f, c);
    p.line(px, y0, px, py, 0.7f, c);
}

// The game's blueprint inset (its Help > Controls panel, its Weapons page's picture box).
void blueprint(const Pen& p, float x, float y, float w, float h) {
    p.plate(x, y, w, h, 7, 0x8FD0FFE0u);
    p.plate(x + 1.2f * p.ax, y + 1.2f, w - 2.4f * p.ax, h - 2.4f, 6, 0x2A5CB0E0u);
    for (float gx = x + 10; gx < x + w - 4; gx += 12) p.rect(gx, y + 3, 0.5f, h - 6, 0x6FA8F030u);
    for (float gy = y + 10; gy < y + h - 4; gy += 12) p.rect(x + 3, gy, w - 6, 0.5f, 0x6FA8F030u);
}

// The DualSense art (tools/make_art.py: our own drawing, as runs of cells), its middle at
// (cx, cy), `k` true units a design unit; `lit` the parts lit (a bit a slot). Its parts' centres come
// back for the leader lines.
struct ArtAnchors {
    float x[art::kSlotCount] = {}, y[art::kSlotCount] = {};
};
ArtAnchors draw_dualsense(const Pen& p, float cx, float cy, float k, std::uint32_t lit) {
    const float cell = art::kCell * k;
    const float ox = cx - art::kPadW * cell * p.ax / 2, oy = cy - art::kPadH * cell / 2;
    std::uint32_t colour[art::kSlotCount] = {};
    // Flat dark slate, as the game's Help > Controls draws its controller.
    colour[art::kGlow] = 0x00000000u;
    colour[art::kEdge] = 0x1E242BFFu;
    colour[art::kBodyTop] = 0x48515CFFu;
    colour[art::kBodyMid] = 0x424A54FFu;
    colour[art::kBodyLow] = 0x3B424BFFu;
    colour[art::kRimLight] = 0x5E6874FFu;
    colour[art::kRimDark] = 0x2A3037FFu;
    colour[art::kTouchpad] = 0x4E5762FFu;
    colour[art::kTouchRim] = 0x2A3037FFu;
    colour[art::kLightbar] = 0x8CC4F0FFu;
    colour[art::kPartEdge] = 0x22282FFFu;
    colour[art::kPartLight] = 0x6A7480FFu;
    colour[art::kPartDark] = 0x22282EFFu;
    colour[art::kWell] = 0x2C333AFFu;
    colour[art::kCapRing] = 0x2A3037FFu;
    colour[art::kSymTriangle] = 0x48D08CFFu;
    colour[art::kSymCircle] = 0xE84A5AFFu;
    colour[art::kSymCross] = 0x7C9CF0FFu;
    colour[art::kSymSquare] = 0xE07CC0FFu;
    colour[art::kMark] = 0x9AA6B2FFu;
    for (int sl : {art::kL1, art::kL2, art::kR1, art::kR2, art::kDpad, art::kTriangle, art::kCircle, art::kCross, art::kSquare,
                   art::kLstick, art::kRstick, art::kPs, art::kSmall}) {
        colour[sl] = (lit >> sl) & 1 ? 0xD08A20FFu : 0x353C45FFu;
    }
    for (int y = 0; y < art::kPadH; ++y) {
        for (int i = art::kPadRowStart[y]; i < art::kPadRowStart[y + 1]; ++i) {
            const art::Run& r = art::kPadRuns[i];
            p.rect(ox + r.x * cell * p.ax, oy + y * cell, r.len * cell * p.ax + 0.05f, cell + 0.05f, colour[r.slot]);
        }
    }
    ArtAnchors a;
    const auto at = [&](int sl, float x, float y) {
        a.x[sl] = ox + x * cell * p.ax;
        a.y[sl] = oy + y * cell;
    };
    at(art::kL1, art::kL1X, art::kL1Y);
    at(art::kL2, art::kL2X, art::kL2Y);
    at(art::kR1, art::kR1X, art::kR1Y);
    at(art::kR2, art::kR2X, art::kR2Y);
    at(art::kDpad, art::kDpadX, art::kDpadY);
    at(art::kTriangle, art::kTriangleX, art::kTriangleY);
    at(art::kCircle, art::kCircleX, art::kCircleY);
    at(art::kCross, art::kCrossX, art::kCrossY);
    at(art::kSquare, art::kSquareX, art::kSquareY);
    at(art::kLstick, art::kLstickX, art::kLstickY);
    at(art::kRstick, art::kRstickX, art::kRstickY);
    return a;
}

// The diagrams, one a tab, in the box (x, y, w, h).
void diagram_buttons(const Pen& p, float x, float y, float w, float h, int cursor) {
    blueprint(p, x, y, w, h);
    const float cx = x + w / 2, cy = y + h / 2 + 6;
    const int slot_of_row[4] = {art::kL1, art::kL2, art::kR1, art::kR2};
    const std::uint32_t bit_of_row[4] = {OPENRAC_PAD_L1, OPENRAC_PAD_L2, OPENRAC_PAD_R1, OPENRAC_PAD_R2};
    // Row 0 is Lock Strafe, 1 Crouch (the Crouch shoulders, Cross and Square lit), then the shoulders.
    const int shoulder = cursor - 2;
    std::uint32_t lit = shoulder >= 0 && shoulder < 4 ? 1u << slot_of_row[shoulder] : 0u;
    if (cursor == 1) {
        for (int i = 0; i < 4; ++i) {
            if (s.config.actions[i] == strafe::Action::Crouch) lit |= 1u << slot_of_row[i];
        }
        lit |= 1u << art::kSquare;
        if (s.crouch != CrouchMode::Standard) lit |= 1u << art::kCross;
    }
    const ArtAnchors a = draw_dualsense(p, cx, cy, 0.88f, lit);
    // The shoulders: chips with their button and what it does now, lit with the row.
    const float ly[4] = {y + 30, y + 13, y + 30, y + 13};
    for (int i = 0; i < 4; ++i) {
        const bool left = i < 2, on = (lit >> slot_of_row[i]) & 1;
        const float edge = chip(p, left, left ? x + 132 : x + w - 132, ly[i], bit_of_row[i], upper(strafe::action_name(s.config.actions[i])), on);
        elbow(p, edge, ly[i], a.x[slot_of_row[i]], a.y[slot_of_row[i]]);
    }
    // What the other buttons do (the game's own, not changed here).
    const struct { int slot; float ly; const char* text; bool left; } others[] = {
        {art::kTriangle, y + 52, "QUICK SELECT", false}, {art::kCircle, y + 69, "FIRE", false},
        {art::kCross, y + 86, "JUMP", false},         {art::kSquare, y + 103, "WRENCH", false},
        {art::kRstick, y + 126, "LOOK AROUND", false},  {art::kLstick, y + 126, "WALK, RUN", true}};
    for (const auto& o : others) {
        const float edge = chip(p, o.left, o.left ? x + 100 : x + w - 100, o.ly, 0, o.text, (lit >> o.slot) & 1);
        elbow(p, edge, o.ly, a.x[o.slot], a.y[o.slot]);
    }
    const char* lock = s.lock == Lock::Off ? "STRAFE: HOLD" : s.lock == Lock::Always ? "STRAFE: ALWAYS" : "STRAFE: WITH WEAPON";
    chip(p, false, x + 8, y + 58, 0, lock, cursor == 0);
    chip(p, false, x + 8, y + 75, 0, upper(std::string("Crouch: ") + kCrouchNames[static_cast<int>(s.crouch)]), cursor == 1);
}

void diagram_camera(const Pen& p, float x, float y, float w, float h, int /*cursor*/) {
    blueprint(p, x, y, w, h);
    if (!s.modern) {
        // Classic: the game's own camera, and only words for it (no crosshair of the mod's).
        const float tx = x + 24;
        p.text(tx, y + 14, 12, kWhite, "CLASSIC CAMERA");
        const char* lines[] = {"GOING COMMANDO'S OWN CAMERA, AIM AND CROSSHAIRS,", "AS THE GAME MAKES THEM.",
                               "YOUR BUTTONS, LOCK STRAFE AND AUTO GADGETS STILL APPLY.",
                               "CHOOSE MODERN FOR THE FREE CAMERA AND ITS SETTINGS."};
        for (int i = 0; i < 4; ++i) p.text(tx, y + 36 + i * 14, 10, kDim, lines[i]);
        return;
    }
    // The crosshair at its size, as the game draws it (40 units by its scale, stretched as the
    // picture is), in a frame like a little screen.
    const float bx = x + 24, bw = 150, by = y + 8, bh = h - 26;
    p.rect(bx, by, bw, bh, kGlassDeep);
    p.edge(bx, by, bw, bh, 1.0f, kEdgeDim);
    const float ccx = bx + bw / 2, ccy = by + bh / 2, k = 40.0f / 64.0f * reticle_base(38, 1.0f) * s.crosshair_size;
    const struct { float r0, r1; std::uint32_t c; } bands[] = {{18.5f, 21.5f, 0x0FFF0FC0u}, {24.75f, 26.25f, 0x0FFF0F70u}};
    for (const auto& band : bands) {
        if (!crosshair_shown()) break;  // Off: none to show
        for (float yy = -band.r1; yy < band.r1; yy += 0.5f) {
            const float yc = yy + 0.25f, o2 = band.r1 * band.r1 - yc * yc;
            if (o2 <= 0) continue;
            const float xo = std::sqrt(o2), i2 = band.r0 * band.r0 - yc * yc;
            if (i2 <= 0) {
                p.rect(ccx - xo * k, ccy + yy * k, 2 * xo * k, 0.5f * k, band.c);
            } else {
                const float xi = std::sqrt(i2);
                p.rect(ccx - xo * k, ccy + yy * k, (xo - xi) * k, 0.5f * k, band.c);
                p.rect(ccx + xi * k, ccy + yy * k, (xo - xi) * k, 0.5f * k, band.c);
            }
        }
    }
    for (int side = -1; side <= 1 && crosshair_shown(); side += 2) {
        for (float off : {-3.0f, 1.0f}) {
            const float x0 = side < 0 ? -32.0f : 26.25f;
            p.rect(ccx + x0 * k, ccy + off * k, 5.75f * k, 2.0f * k, 0x0FFF0FC0u);
            p.rect(ccx + off * k, ccy + x0 * k, 2.0f * k, 5.75f * k, 0x0FFF0FC0u);
        }
    }
    if (!crosshair_shown()) p.text_centred(ccx, ccy - 5, 10, kDim, "OFF");
    p.text_centred(ccx, by + bh + 4, 10, kDim, "CROSSHAIR");
    // The camera style.
    const float tx = bx + bw + 24;
    p.text(tx, y + 14, 12, kWhite, "MODERN CAMERA");
    const char* lines[] = {"A FREE CAMERA FOR THE STICK,", "THE MOUSE AND THE GYRO.", "IT STAYS WHERE YOU POINT IT,",
                           "AND WEAPONS AIM WHERE YOU LOOK."};
    for (int i = 0; i < 4; ++i) p.text(tx, y + 36 + i * 14, 10, kDim, lines[i]);
}

// The game's panels (its Weapons, Gadgets and Quick Select pages): see-through teal glass, a thin
// light edge and a dimmer band inside it, corners cut.
constexpr std::uint32_t kPanelEdge = 0xA8E8FFF0u, kPanelBand = 0x3C8CB0A8u, kPanelFill = 0x0C3446B4u, kSelect = 0xFFFFFFFFu;
void panel(const Pen& p, float x, float y, float w, float h, float cut) {
    p.plate(x, y, w, h, cut, kPanelEdge);
    p.plate(x + 1.1f * p.ax, y + 1.1f, w - 2.2f * p.ax, h - 2.2f, cut - 0.5f, kPanelBand);
    p.plate(x + 2.6f * p.ax, y + 2.6f, w - 5.2f * p.ax, h - 5.2f, cut - 1.2f, kPanelFill);
}

// The game's bottom bars (its Ammo bar): a panel whose left end comes to a point.
void pointed_bar(const Pen& p, float x, float y, float w, float h) {
    constexpr float step = 0.5f;
    const float mid = y + h / 2;
    for (int layer = 0; layer < 3; ++layer) {
        const float in = layer == 0 ? 0.0f : layer == 1 ? 1.1f : 2.6f;
        const std::uint32_t c = layer == 0 ? kPanelEdge : layer == 1 ? kPanelBand : kPanelFill;
        for (float yy = y + in; yy < y + h - in; yy += step) {
            const float d = std::fabs(yy + step / 2 - mid);
            const float left = x + (d + in) * p.ax + in * p.ax;
            p.rect(left, yy, x + w - in * p.ax - left, step, c);
        }
    }
}

// =============================================================================================
// Older OpenRAC: the mod's own mouse and gyro
// =============================================================================================
//
// For OpenRAC builds without the built-in keyboard-mouse mod (openrac_input() false): the mod's own
// mouse buttons, mouse camera and gyro, with their settings (the manifest's sections
// marked "(older OpenRAC)") and the Mouse and Gyro tabs. Deleting this section, those settings and
// the touch points marked "(older OpenRAC)" elsewhere drops them. The controller reader (GyroDevice)
// stays: OpenRAC's own gyro settings turn the boom through it.

struct OwnInput {
    // The settings.
    bool mouse_camera = true;
    float mouse_speed = 1.0f;
    bool invert_y = false;
    bool gyro_camera = false;
    float gyro_speed = 1.0f;
    gyro::GyroSpace gyro_space = gyro::GyroSpace::Player;  // which of the controller's turns turn the camera
    bool gyro_invert_x = false, gyro_invert_y = false;
    // The cursor held in play.
    bool captured = false;
    // On the game's cameras, as OpenRAC's built-in works them: the follow camera's tilt held at an
    // elevation, other cameras' right stick.
    bool tilt_held = false;    // the follow camera's tilt held where the mouse and gyro put it
    float tilt_elevation = 0;  // the eye's elevation over him held (radians, > 0 high, looking down)
    bool emulating = false;    // another camera: the mouse and gyro are its right stick
    float emu_x = 0, emu_y = 0, emu_last = 0;  // that stick (-1..1, y down) and last tick's turn
};

OwnInput g_own;

// OpenRAC's gyro spaces (gyro_aim.hpp), in its order.
constexpr const char* kGyroTurnValues[] = {"player", "yaw", "roll", "local"};
constexpr const char* kGyroTurnNames[] = {"Player", "Yaw", "Roll", "Local"};

void own_read_config(const openrac::Mod& mod) {
    for (int i = 0; i < strafe::kMouseButtonCount; ++i) {
        s.config.mouse[i] = strafe::action_from_value(mod.setting<std::string>(strafe::kMouseButtonInfo[i].key));
    }
    g_own.mouse_camera = mod.setting<bool>("mouse_camera");
    g_own.mouse_speed = static_cast<float>(mod.setting<double>("mouse_speed"));
    g_own.invert_y = mod.setting<bool>("invert_y");
    g_own.gyro_camera = mod.setting<bool>("gyro_camera");
    g_own.gyro_speed = static_cast<float>(mod.setting<double>("gyro_speed"));
    g_own.gyro_space = gyro::gyro_space_from(kGyroTurnValues[index_of(kGyroTurnValues, mod.setting<std::string>("gyro_turn"), 0)]);
    g_own.gyro_invert_x = mod.setting<bool>("gyro_invert_x");
    g_own.gyro_invert_y = mod.setting<bool>("gyro_invert_y");
}

void own_restore_defaults() {
    const strafe::Config defaults;
    for (int i = 0; i < strafe::kMouseButtonCount; ++i) {
        save_setting(strafe::kMouseButtonInfo[i].key, std::string(strafe::action_value(defaults.mouse[i])));
    }
    save_setting("mouse_camera", true);
    save_setting("mouse_speed", 1.0);
    save_setting("invert_y", false);
    save_setting("gyro_camera", false);
    save_setting("gyro_speed", 2.0);
    save_setting("gyro_turn", std::string(kGyroTurnValues[0]));
    save_setting("gyro_invert_x", false);
    save_setting("gyro_invert_y", false);
}

// The game's cameras let go: the follow camera's held tilt, the other cameras' stick (a new camera
// style; out of play).
void own_reset() {
    g_own.tilt_held = false;
    g_own.emulating = false;
}

// The cursor held in play, for the mouse camera or a mouse button that does something (OpenRAC's
// built-in holds it itself).
void own_capture(const openrac::Mod& mod, bool playing) {
    const auto input = mod.input();
    bool buttons = false;
    for (const strafe::Action action : s.config.mouse) buttons = buttons || action != strafe::Action::None;
    const bool want = DEAD_AIM_OWN_MOUSE && !openrac_input() && (g_own.mouse_camera || buttons) && playing && input.focused() &&
                      !input.raw().overlay;
    if (want != g_own.captured) {
        mod.capture_mouse(want);
        g_own.captured = want;
    }
}

// The mod's own gyro settings as OpenRAC's filter takes them, the rest at OpenRAC's defaults (no
// dead zone, smoothing under 3 degrees a second, its rest learnt, no gyro button).
gyro::GyroOptions own_gyro_options() {
    gyro::GyroOptions o;
    o.enabled = g_own.gyro_camera;
    o.space = g_own.gyro_space;
    o.sensitivity_x = o.sensitivity_y = g_own.gyro_speed;
    o.invert_x = g_own.gyro_invert_x;
    o.invert_y = g_own.gyro_invert_y;
    o.smoothing = 3.0f * kDeg;
    return o;
}

// The mod's own mouse buttons, while the mouse is the game's (captured), where OpenRAC does not
// bind them itself.
std::uint32_t own_mouse_buttons(const openrac::Mod& mod) {
    const auto input = mod.input();
    return DEAD_AIM_OWN_MOUSE && !openrac_input() && input.raw().captured ? input.raw().mouse_held : 0;
}

// The mod's own mouse and gyro as another camera's right stick (own_game_camera), unless the
// player's own stick is pushed: past the pad's dead zone, so a little motion turns a little.
constexpr float kPadDead = 58.0f / 127.0f;  // the pad's own dead zone on the right stick (measured)

void own_pad_stick(std::uint8_t* report, int size) {
    if (s.stick_owned || s.stick_live || size <= kRightStickY || !g_own.emulating) return;
    const auto byte = [](float v) {
        const float r = v == 0.0f ? 0.0f : std::copysign(kPadDead + (1.0f - kPadDead) * std::fabs(v), v);
        return static_cast<std::uint8_t>(std::clamp(0x80 + static_cast<int>(std::lround(r * 127.0f)), 0, 255));
    };
    report[kRightStickX] = byte(g_own.emu_x);
    report[kRightStickY] = byte(g_own.emu_y);
}

// What the mod's own mouse and gyro ask for this tick, by its own settings. The mouse: about 9
// degrees for 100 pixels at 1x; right turns right, down looks down (Invert Y the other way).
Look own_look(const openrac::Input& input, bool gyro_read, Look gyro) {
    Look look;
    if (DEAD_AIM_OWN_MOUSE && g_own.mouse_camera && input.raw().captured) {
        const float dx = input.mouse_dx(), dy = input.mouse_dy();
        look.yaw -= dx * g_own.mouse_speed * 9.0f * kDeg / 100.0f;
        look.pitch -= (g_own.invert_y ? -dy : dy) * g_own.mouse_speed * 9.0f * kDeg / 100.0f;
    }
    if (gyro_read) {
        look.yaw += gyro.yaw;
        look.pitch += gyro.pitch;
    }
    return look;
}

// Aiming, as the game makes it (rac2-decomp src/levels/shared/L00_004266E8-g8-hazard.c,
// cam_aligned_update_from_input; docs/notes/rac2-aim-camera.md): g_follow_camera_object points at
// the camera in use, the aim camera while aiming (its type, +0x86, 4). Its forward (+0x00) is all
// that holds the aim: the stick turns it each tick, the rest (its side and up rows, the eye, the
// game's yaw and pitch globals, the hero's facing and look) is made from it. Its state's +0x10 is
// the pitch turned so far (> 0 up), which the game clamps at 1.466. So the mouse and gyro turn the
// forward and add to that pitch, and the stick goes on turning it too: nothing to hand back. Not
// while the aim camera blends in (the camera block's phase not 0).
constexpr gaddr kAimPitchSoFar = 0x10;
constexpr float kAimHard = 1.466f;

void aim_camera_tick(const openrac::Mod& mod, const openrac::Memory& memory, float yaw, float pitch) {
    const gaddr slot = camera_slot(mod);
    const gaddr rec = memory.read<gaddr>(slot);
    if (!in_ram(rec)) return;
    const std::int16_t type = memory.read<std::int16_t>(rec + kCamType);
    const std::int16_t phase = memory.read<std::int16_t>(slot + kPhaseAboveSlot);
    if (type != kAimCameraType || phase != 0 || (yaw == 0.0f && pitch == 0.0f)) return;
    const float fx = memory.read<float>(rec), fy = memory.read<float>(rec + 4), fz = memory.read<float>(rec + 8);
    const float lift = std::atan2(fz, std::sqrt(fx * fx + fy * fy));  // > 0 up
    const float lift2 = std::clamp(lift + pitch, -kAimHard, kAimHard);
    const float heading = std::atan2(fy, fx) + yaw;
    memory.write<float>(rec, std::cos(lift2) * std::cos(heading));
    memory.write<float>(rec + 4, std::cos(lift2) * std::sin(heading));
    memory.write<float>(rec + 8, std::sin(lift2));
    const gaddr vars = memory.read<gaddr>(rec + kCamState);
    if (in_ram(vars)) {
        memory.write<float>(vars + kAimPitchSoFar,
                            std::clamp(memory.read<float>(vars + kAimPitchSoFar) + (lift2 - lift), -kAimHard, kAimHard));
    }
}

// The mod's own mouse and gyro on the game's own cameras, as OpenRAC's built-in works them (its
// keyboard_mouse.cpp: follow_mouse, MouseLook; docs/notes/rac2-follow-camera-tilt.md), so a Classic
// camera keeps every limit the game gives it:
// - the follow camera (Classic's, Clank's, the robot's) turns about up at once; its tilt is held at
//   an elevation within the game's own range (its 40 degree setting: the eye up to about 57 degrees
//   over him), which the game is asked for through its own tilt override (as its stick would ask
//   it), and eases to at its own pace, past its floor and walls;
// - any other camera (the Glider's, a vehicle's) has them as its right stick: a tick's turn sets
//   the stick's x, the tilt a held y.
// The player's own right stick, pushed, has it instead.
constexpr float kPixelsFullTurn = 24.0f, kPixelsFullTilt = 160.0f;  // the stick: a full push
constexpr float kTiltLead = 10.0f * kDeg;                            // the held tilt kept this near the eye's

// The held tilt asked of the game for its next tick, through its tilt override (> 0 the eye high).
void follow_hold(const openrac::Memory& memory, gaddr state) {
    if (!g_own.tilt_held) return;
    ask_tilt(memory, state, std::clamp(std::atan(std::sin(g_own.tilt_elevation)) / kFullTilt, -1.0f, 1.0f));
}

float orbit_elevation(V3 v, V3 up) {
    const float l = length(v);
    return l > 1e-4f ? std::asin(std::clamp(vec::dot(v, up) / l, -1.0f, 1.0f)) : 0.0f;
}

void follow_turn(const openrac::Mod& mod, const openrac::Memory& memory, gaddr camera, float yaw, float pitch) {
    if (yaw != 0.0f) turn_camera(memory, camera, yaw);
    const gaddr state = memory.read<gaddr>(camera + kCamState);
    if (state == 0) return;
    if (pitch == 0.0f) {
        follow_hold(memory, state);
        return;
    }
    const V3 up = camera_up(mod, memory);
    const float limit = std::asin(std::fmin(1.0f, std::tan(kFullTilt)));
    const float now = orbit_elevation(read_v3(memory, state + kStateOrbits[0]), up);
    if (!g_own.tilt_held) g_own.tilt_elevation = now;
    // Kept near where the eye is (the game eases it at its own pace, and stops it at the floor): no
    // dead travel to wind back where the game would not follow.
    g_own.tilt_elevation = std::clamp(g_own.tilt_elevation, now - kTiltLead, now + kTiltLead);
    g_own.tilt_held = true;
    // Looking up takes the eye down. Only asked of the game, which moves the eye there itself, past
    // its floor (tilted by the mod at once, the eye went under the floor for a tick: seen).
    g_own.tilt_elevation = std::clamp(g_own.tilt_elevation - pitch, -limit, limit);
    follow_hold(memory, state);
}

// Another camera: a tick's turn and tilt (radians; yaw > 0 left, pitch > 0 up) as its right stick.
void emulate_stick(const openrac::Mod& mod, const openrac::Memory& memory, float yaw, float pitch) {
    const float dx = -yaw / kRadiansPerPixel, dy = -pitch / kRadiansPerPixel;  // pixels: right, down
    g_own.emu_x = std::clamp(0.5f * (dx + g_own.emu_last) / kPixelsFullTurn, -1.0f, 1.0f);
    g_own.emu_last = dx;
    // Down looks down: the stick that way, whichever way the game's option reads it.
    const bool inverts = memory.read<std::uint8_t>(boot_global(mod, kCameraInvert)) != 0;
    g_own.emu_y = std::clamp(g_own.emu_y + (inverts ? -dy : dy) / kPixelsFullTilt, -1.0f, 1.0f);
    g_own.emulating = true;
}

// The game's own cameras this tick (frame's dispatch, the same cases): the aim camera turned where
// the mouse and gyro turn it; the follow camera of Classic, Clank and the robot turned and its tilt
// held; any other camera (not a turret's) given them as its right stick.
void own_game_camera(const openrac::Mod& mod, const openrac::Memory& memory, gaddr camera, bool here, bool fp,
                     bool aiming, bool boom_view, bool turret_aim, Look look) {
    // Clank's and the robot's walking camera is the game's follow camera.
    const std::uint8_t form = memory.read<std::uint8_t>(kHeroForm);
    const bool other_body = (form == 1 || form == kRobotForm) && follow_live(mod, memory, camera)
        && !camera_overridden(mod, memory) && memory.read<gaddr>(kHeroVehicle) == 0;
    const bool own_follow = (here || other_body) && !s.in_turret && !fp && !aiming && !boom_view;
    const bool was_emulating = g_own.emulating;
    g_own.emulating = false;
    if (!own_follow) g_own.tilt_held = false;
    const bool moved = look.yaw != 0.0f || look.pitch != 0.0f;
    if (turret_aim || fp || boom_view) return;
    if (aiming) {
        if (moved) aim_camera_tick(mod, memory, look.yaw, look.pitch);
        return;
    }
    if (own_follow) {
        if (s.stick_live) g_own.tilt_held = false;  // the player's own stick has the tilt
        if (moved || g_own.tilt_held) follow_turn(mod, memory, camera, look.yaw, look.pitch);
        return;
    }
    if (s.in_turret) return;
    if (!was_emulating) g_own.emu_x = g_own.emu_y = g_own.emu_last = 0.0f;
    if (moved || was_emulating) emulate_stick(mod, memory, look.yaw, look.pitch);
}

// The Mouse and Gyro tabs. Sensitivities step through these (the setting is any value in range).
constexpr float kSpeeds[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f};
constexpr int kSpeedCount = static_cast<int>(std::size(kSpeeds));

int speed_index(float speed) {
    int best = 0;
    for (int i = 1; i < kSpeedCount; ++i) {
        if (std::fabs(kSpeeds[i] - speed) < std::fabs(kSpeeds[best] - speed)) best = i;
    }
    return best;
}

std::vector<std::string> speed_names() {
    std::vector<std::string> names;
    for (float v : kSpeeds) {
        char text[16];
        std::snprintf(text, sizeof text, "%gx", v);
        names.emplace_back(text);
    }
    return names;
}

Row speed_row(const std::string& label, const std::string& help, float* value, const char* key) {
    Row r;
    r.label = label;
    r.help = help;
    r.choices = speed_names;
    r.slider = true;
    r.current = [value] { return speed_index(*value); };
    r.set = [value, key](int i) {
        *value = kSpeeds[i];
        save_setting(key, static_cast<double>(*value));
    };
    return r;
}

std::vector<std::string> off_on() { return {"Off", "On"}; }

Row switch_row(const std::string& label, const std::string& help, bool* value, const char* key) {
    Row r;
    r.label = label;
    r.help = help;
    r.choices = off_on;
    r.current = [value] { return *value ? 1 : 0; };
    r.set = [value, key](int i) {
        *value = i != 0;
        save_setting(key, *value);
    };
    return r;
}

std::vector<Row> mouse_rows() {
    std::vector<Row> rows;
    rows.push_back(switch_row("Mouse Camera", "Turn the camera with the mouse.", &g_own.mouse_camera, "mouse_camera"));
    rows.push_back(speed_row("Sensitivity", "How far the camera turns.", &g_own.mouse_speed, "mouse_speed"));
    rows.push_back(switch_row("Invert Y", "Reverse the vertical axis.", &g_own.invert_y, "invert_y"));
    const char* names[strafe::kMouseButtonCount] = {"Left Click", "Right Click", "Middle Click"};
    for (int i = 0; i < strafe::kMouseButtonCount; ++i) {
        rows.push_back(action_row(names[i], std::string("What ") + names[i] + " does as Ratchet.", &s.config.mouse[i],
                                  strafe::kMouseButtonInfo[i].key, true));
    }
    return rows;
}

std::vector<Row> gyro_rows() {
    std::vector<Row> rows;
    rows.push_back(switch_row("Gyro Camera", "Aim by moving the controller.", &g_own.gyro_camera, "gyro_camera"));
    rows.push_back(speed_row("Sensitivity", "How far the camera turns.", &g_own.gyro_speed, "gyro_speed"));
    Row turn;
    turn.label = "Turn With";
    turn.help = g_own.gyro_space == gyro::GyroSpace::Player ? "Turn the controller, however you hold it."
                : g_own.gyro_space == gyro::GyroSpace::Yaw  ? "Twist the controller, held flat."
                : g_own.gyro_space == gyro::GyroSpace::Roll ? "Lean the controller, like a wheel."
                                                             : "Twist or lean the controller.";
    turn.choices = [] { return menu_names(kGyroTurnNames); };
    turn.current = [] { return static_cast<int>(g_own.gyro_space); };
    turn.set = [](int i) {
        g_own.gyro_space = gyro::gyro_space_from(kGyroTurnValues[i]);
        save_setting("gyro_turn", std::string(kGyroTurnValues[i]));
    };
    rows.push_back(turn);
    rows.push_back(switch_row("Invert X", "Reverse the horizontal axis.", &g_own.gyro_invert_x, "gyro_invert_x"));
    rows.push_back(switch_row("Invert Y", "Reverse the vertical axis.", &g_own.gyro_invert_y, "gyro_invert_y"));
    return rows;
}

// The mod's own mouse: its buttons' actions; the row under the cursor lit (rows 3, 4, 5).
void diagram_mouse(const Pen& p, float x, float y, float w, float h, int cursor) {
    blueprint(p, x, y, w, h);
    const float cx = x + w / 2, cy = y + h / 2 + 6, rx = 28, ry = 44;
    p.ellipse(cx, cy, rx + 1.4f, ry + 1.4f, 0x1E242BFFu);
    p.ellipse(cx, cy, rx, ry, 0x424A54FFu);
    const int lit = cursor - 3;
    const float top = cy - ry * 0.98f, split = cy - ry * 0.25f;
    for (int side = 0; side < 2; ++side) {
        for (float yy = top; yy < split; yy += 0.5f) {
            const float k = 1.0f - ((yy - cy) * (yy - cy)) / (ry * ry);
            if (k <= 0) continue;
            const float hw = rx * std::sqrt(k) * p.ax;
            const float x0 = side == 0 ? cx - hw + 1.5f * p.ax : cx + 1.5f * p.ax;
            p.rect(x0, yy, hw - 3 * p.ax, 0.5f, lit == side ? 0xD08A20FFu : 0x4E5762FFu);
        }
    }
    p.rect(cx - 0.6f * p.ax, top, 1.2f * p.ax, split - top, 0x1E242BFFu);
    p.line(cx - rx * 0.92f * p.ax, split, cx + rx * 0.92f * p.ax, split, 1.0f, 0x1E242BFFu);
    p.plate(cx - 4 * p.ax, cy - ry * 0.72f, 8 * p.ax, 16, 2.0f, 0x1E242BFFu);
    p.plate(cx - 2.8f * p.ax, cy - ry * 0.72f + 1.5f, 5.6f * p.ax, 13, 1.5f, lit == 2 ? 0xD08A20FFu : 0x2C333AFFu);
    const char* names[3] = {"LEFT CLICK", "RIGHT CLICK", "MIDDLE CLICK"};
    const float ly[3] = {cy - ry * 0.55f, cy - ry * 0.55f, y + 16};
    const float bx[3] = {cx - rx * 0.5f * p.ax, cx + rx * 0.5f * p.ax, cx};
    const float by[3] = {cy - ry * 0.6f, cy - ry * 0.6f, cy - ry * 0.72f};
    for (int i = 0; i < 3; ++i) {
        const bool left = i == 0;
        const float edge = chip(p, left, left ? x + 150 : x + w - 150, ly[i], 0,
                                std::string(names[i]) + ": " + upper(strafe::action_name(s.config.mouse[i])), lit == i);
        elbow(p, edge, ly[i], bx[i], by[i]);
    }
    p.text_centred(cx, y + h - 12, 9, 0xDCEBFFFFu, g_own.mouse_camera ? "THE MOUSE TURNS THE CAMERA" : "MOUSE CAMERA OFF");
}

// The mod's own gyro: the motions that turn and tip the view, and the controller's state.
void diagram_gyro(const Pen& p, float x, float y, float w, float h, int cursor) {
    blueprint(p, x, y, w, h);
    const float cx = x + w / 2, cy = y + h / 2 + 12;
    draw_dualsense(p, cx, cy, 0.7f, 0);
    const auto arc = [&](float ox, float oy, float rx, float ry, float a0, float a1, std::uint32_t c) {
        for (float t = a0; t <= a1; t += 0.04f) p.ellipse(ox + std::cos(t) * rx * p.ax, oy + std::sin(t) * ry, 1.1f, 1.1f, c);
        p.ellipse(ox + std::cos(a1) * rx * p.ax, oy + std::sin(a1) * ry, 2.4f, 2.4f, c);
    };
    const std::uint32_t turn_c = cursor == 2 ? kOrange : kWhite;
    // Roll leans it like a wheel, Local adds that to the twist; Player and Yaw twist it.
    const gyro::GyroSpace space = g_own.gyro_space;
    const bool roll = space == gyro::GyroSpace::Roll || space == gyro::GyroSpace::Local;
    const bool twist = space != gyro::GyroSpace::Roll;
    if (roll) arc(cx, cy - 4, 82, 56, -2.6f, -0.55f, turn_c);
    if (twist) arc(cx, cy - 60, 84, 9, 0.35f, 2.8f, turn_c);
    p.text_centred(cx, y + 2, 10, turn_c, !twist ? "TILT SIDEWAYS TO TURN" : roll ? "TWIST OR TILT TO TURN" : "TWIST TO TURN");
    const std::uint32_t tip_c = cursor == 4 ? kOrange : kDim;
    arc(cx + 108 * p.ax, cy - 2, 9, 30, -1.4f, 1.3f, tip_c);
    p.text(cx + 122 * p.ax, cy - 6, 10, tip_c, "TIP TO LOOK");
    p.text(cx + 122 * p.ax, cy + 6, 10, tip_c, "UP AND DOWN");
    const char* const pad = g_gyro.name();
    const std::string status = !g_own.gyro_camera    ? "GYRO CAMERA OFF"
                               : pad == nullptr       ? "NO CONTROLLER WITH A GYRO FOUND"
                               : g_gyro.calibrated() ? upper(std::string(pad) + " ready")
                                                     : "HOLD STILL A MOMENT TO CALIBRATE";
    p.text_centred(cx, y + h - 12, 10, g_own.gyro_camera && pad == nullptr ? kOrange : kDim, status);
}

std::vector<Tab> own_tabs() {
    std::vector<Tab> tabs;
    if (DEAD_AIM_OWN_MOUSE) tabs.push_back({"Mouse", mouse_rows, diagram_mouse, true});   // not on Android
    if (DEAD_AIM_GYRO_READER) tabs.push_back({"Gyro", gyro_rows, diagram_gyro, true});  // no reader, no tab
    return tabs;
}

// ---------------------------------------------------------------------------------------------
// The page's tabs, drawn and put in the menu
// ---------------------------------------------------------------------------------------------

void build_tabs() {
    g_tabs = {{"Buttons", buttons_rows, diagram_buttons}, {"Camera", camera_rows, diagram_camera}};
    for (const Tab& tab : own_tabs()) g_tabs.push_back(tab);  // (older OpenRAC)
}

void menu_overlay(void*, const openrac_overlay* o) {
    if (!g_fancy || g_asking || g_shown != -1 || o == nullptr || o->rect == nullptr || o->text == nullptr) return;
    if (o->size < offsetof(openrac_overlay, phase) + sizeof(uint32_t) || o->phase != OPENRAC_OVERLAY_MENU) return;
    if (g_tab >= tab_count()) g_tab = 0;
    if (g_tab < 0 || g_tab >= tab_count()) return;
    const Pen p(o);
    // The panels, as the game's pages lay theirs out: the title's, the tabs', the drawing's, the
    // settings', a bar for what the row does and one for the buttons.
    panel(p, 12, 10, 186, 32, 7);
    panel(p, 204, 10, 296, 32, 7);
    panel(p, 12, 46, 488, 188, 10);
    panel(p, 12, 238, 488, 146, 10);
    pointed_bar(p, 40, 388, 460, 20);
    panel(p, 12, 411, 488, 26, 8);
    // The title on an inset bar (as "Weapons" sits on its page).
    p.plate(18, 15.5f, 174, 21, 4, 0x7FC8E8C0u);
    p.plate(18 + 1.1f * p.ax, 16.6f, 174 - 2.2f * p.ax, 18.8f, 3.5f, 0x08283AE0u);
    p.text_centred(105, 21, 11, kWhite, upper(kPageName));
    // The tabs: L1 at the left end, R1 at the right, as the player's device names them.
    Pen measure = p;
    measure.dry = true;
    const float lw = button_icon(measure, 0, 0, 12, OPENRAC_PAD_L1), rw = button_icon(measure, 0, 0, 12, OPENRAC_PAD_R1);
    button_icon(p, 212, 20, 12, OPENRAC_PAD_L1);
    button_icon(p, 492 - rw, 20, 12, OPENRAC_PAD_R1);
    const float span_x0 = 212 + lw + 6, span_x1 = 492 - rw - 6, gap = 5;
    const int tabs = tab_count();
    const float tab_w = std::fmin(90.0f, (span_x1 - span_x0 - gap * (tabs - 1)) / tabs);
    float tx = (span_x0 + span_x1) / 2 - (tab_w * tabs + gap * (tabs - 1)) / 2;
    for (int t = 0; t < tabs; ++t) {
        const bool on = t == g_tab;
        p.plate(tx, 17, tab_w, 18, 4, on ? kSelect : kPanelBand);
        p.plate(tx + 1.1f * p.ax, 18.1f, tab_w - 2.2f * p.ax, 15.8f, 3.5f, on ? 0x1E5C7CF0u : 0x08283AD0u);
        p.text_centred(tx + tab_w / 2, 21, 10, on ? kWhite : kDim, upper(g_tabs[t].name));
        tx += tab_w + gap;
    }
    const std::vector<Row> rows = g_tabs[g_tab].rows();
    const int cursor = std::clamp(g_cursor, 0, std::max(0, static_cast<int>(rows.size()) - 1));
    // The settings: a plate a row, the cursor's lit; a value on the right, a bar for a slider.
    const int shown = 7;
    const int first = std::clamp(cursor - shown + 1, 0, std::max(0, static_cast<int>(rows.size()) - shown));
    for (int i = first; i < static_cast<int>(rows.size()) && i < first + shown; ++i) {
        const Row& row = rows[i];
        const float ry = 245 + (i - first) * 19.2f;
        const bool on = i == cursor;
        // The cursor's row outlined in white, as the game outlines what its cursor is on.
        p.plate(22, ry - 1.5f, 468, 17.5f, 4, on ? kSelect : 0x2E6E8C90u);
        p.plate(22 + 1.1f * p.ax, ry - 0.4f, 468 - 2.2f * p.ax, 15.3f, 3.5f, on ? 0x1E5C7CF0u : 0x08283AC8u);
        // The shoulder rows' labels as the device names its buttons (a PlayStation icon, a key).
        const std::uint32_t row_bit = row.label == "L1" ? OPENRAC_PAD_L1 : row.label == "L2" ? OPENRAC_PAD_L2
                                      : row.label == "R1" ? OPENRAC_PAD_R1 : row.label == "R2" ? OPENRAC_PAD_R2 : 0;
        if (row_bit) button_icon(p, 42, ry + 1.5f, 12, row_bit, on);
        else p.text(44, ry + 3, 10, on ? kWhite : kDim, upper(row.label));
        if (row.choices) {
            const std::vector<std::string> values = row.choices();
            const int v = std::clamp(row.current(), 0, std::max(0, static_cast<int>(values.size()) - 1));
            if (row.slider && values.size() > 1) {
                const float bx = 300, bw = 150, fill = bw * v / static_cast<float>(values.size() - 1);
                p.rect(bx, ry + 5, bw, 6, kGlassDeep);
                p.rect(bx, ry + 5, fill, 6, on ? kOrange : 0x40B8E0FFu);
                p.edge(bx, ry + 4.5f, bw, 7, 0.6f, kEdgeDim);
                p.text_right(bx - 8, ry + 3, 10, on ? kWhite : kDim, upper(values[v]));
            } else if (!values.empty()) {
                const std::string value = upper(values[v]);
                p.text_right(on ? 462 : 470, ry + 3, 10, on ? kOrange : kWhite, value);
                if (on) {
                    p.text(470, ry + 3, 10, kOrange, ">");
                    p.text_right(462 - p.width(10, value) - 4, ry + 3, 10, kOrange, "<");
                }
            }
        }
    }
    if (static_cast<int>(rows.size()) > shown) {
        // More rows than room: a bar showing where the list is.
        const float track = shown * 19.2f, bar = track * shown / rows.size();
        p.rect(493, 244, 1.5f * p.ax, track, kGlassDeep);
        p.rect(493, 244 + track * first / rows.size(), 1.5f * p.ax, bar, kEdge);
    }
    // What the row does, and the buttons.
    if (cursor < static_cast<int>(rows.size())) p.text(62, 393, 10, kWhite, upper(rows[cursor].help));
    // The buttons, as the game's prompts show them: the button, what it does.
    float fx = 24;
    const auto item = [&](std::initializer_list<std::uint32_t> bits, const char* what) {
        for (std::uint32_t b : bits) fx += button_icon(p, fx, 418, 12, b) + 2;
        p.text(fx + 2, 420, 10, kWhite, what);
        fx += p.width(10, what) + 16;
    };
    item({OPENRAC_PAD_CROSS}, "CHANGE");
    item({OPENRAC_PAD_L1, OPENRAC_PAD_R1}, "TABS");
    item({OPENRAC_PAD_SQUARE}, "DEFAULTS");
    item({OPENRAC_PAD_TRIANGLE}, "BACK");
    // The diagram.
    const float dx = 19, dy = 52, dw = 474, dh = 176;
    if (g_tabs[g_tab].diagram != nullptr) g_tabs[g_tab].diagram(p, dx, dy, dw, dh, cursor);
}

void add_page(openrac::Mod& mod) {
    if (!mod.has_pause_items()) {
        mod.warn("this OpenRAC has no pause menu items: set the controls in the launcher");
        return;
    }
    try {
        build_tabs();
        g_main = mod.ui_page([](const openrac::UiPage& page, const openrac::UiEvent& event) { on_main(page, event); });
        if (!g_fancy) text_add_pages(mod);  // (older OpenRAC: text pages)
        mod.ui_pause_item(kPageName, g_main);
        s.has_page = true;
        show_page();
    } catch (const openrac::Error& e) {
        mod.warn(std::string("no pause menu page (") + e.what() + "): set the controls in the launcher");
    }
}

void load(openrac::Mod& mod) {
    if (mod.game() != "rac2") {
        throw openrac::Error("Dead Aim is for Going Commando");
    }
    if (mod.version() != "ntsc" && mod.version() != "ntsc2") {
        throw openrac::Error("Dead Aim knows the USA discs (v1.01 and v2.00) so far, not "
                             + std::string(mod.version()));
    }
    s.mod = &mod;
    g_v200 = mod.version() == "ntsc2";
    read_config(mod);
    mod.on_pad(on_pad);
    if (mod.api().api_minor >= 31 && mod.api().size >= offsetof(openrac_api, on_overlay_phases) + sizeof(void*)
        && mod.api().on_overlay_phases != nullptr) {
        const int r = mod.api().on_overlay_phases(mod.handle(), menu_overlay, nullptr, OPENRAC_OVERLAY_MENU);
        g_fancy = r == OPENRAC_HOOK_OK;
        if (r != OPENRAC_HOOK_OK) mod.warn("the menu page's overlay was refused (" + std::to_string(r) + "): the text pages");  // (older OpenRAC: text pages)
    }
    mod.on_frame(auto_frame);
    mod.on_frame(frame);
    mod.on_frame(aim_frame);  // after the camera is placed

    mod.on_settings([] {
        read_config(*s.mod);
        if (g_shown != -2) show_page();
    });
    add_page(mod);
    std::string line = std::string("loaded: lock strafe ") + kLockNames[static_cast<int>(s.lock)];
    for (int i = 0; i < strafe::kShoulderCount; ++i) {
        line += std::string(", ") + strafe::kShoulderInfo[i].name + " " + strafe::action_name(s.config.actions[i]);
    }
    line += std::string(", crouch ") + kCrouchNames[static_cast<int>(s.crouch)];
    line += std::string(", camera ") + (s.modern ? "Modern" : "Classic");  // whose mouse and gyro: logged once known
    mod.info(line);
}

}  // namespace

OPENRAC_MOD_CPP("rac2-dead-aim", "0.1.0", load)
