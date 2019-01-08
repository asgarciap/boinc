// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2018 University of California
//
// BOINC is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// BOINC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with BOINC.  If not, see <http://www.gnu.org/licenses/>.

#ifndef BOINC_DEVICESTATUS_H
#define BOINC_DEVICESTATUS_H

#include "miofile.h"
#include "common_defs.h"

// used for Android and mobile grid extension (MGE)
//
struct DEVICE_STATUS {
    bool on_ac_power;
    bool on_usb_power;
    double battery_charge_pct;
    int battery_state;      // see common_defs
    double battery_temperature_celsius;
    bool wifi_online;
    bool user_active;
    char device_name[256];
        // if present, a user-selected name for the device.
        // This will be stored by the client as hostinfo.domain_name,
        // and reported to schedulers.
    int parse(XML_PARSER&);
    void write(MIOFILE&);
    DEVICE_STATUS();
};

#endif
