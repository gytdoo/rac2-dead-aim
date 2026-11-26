// SPDX-License-Identifier: GPL-3.0-or-later
// The gyro, read by the mod itself from the controller's raw reports, beside the game's own
// reading of it. Free of the system so it can be tested.
//
// gyro_hid.hpp reads every controller OpenRAC's own gyro reads by a plain layout, as SDL reads it
// (parse_report and its device table), and gyro_aim.hpp turns the samples into a camera turn as
// OpenRAC does (GyroAim, ported from OpenRAC). Both come in with this header.
#pragma once

#include "gyro_aim.hpp"
#include "gyro_hid.hpp"
