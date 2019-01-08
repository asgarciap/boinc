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

#include "device_status.h"

DEVICE_STATUS::DEVICE_STATUS()
{
    on_ac_power = false;
    on_usb_power = false;
    battery_charge_pct = 0;
    battery_state =  BATTERY_STATE_UNKNOWN;
    battery_temperature_celsius = 0;
    wifi_online = false;
    user_active = false;
    strcpy(device_name, "");
}

int DEVICE_STATUS::parse(XML_PARSER& xp) {
    while (!xp.get_tag()) {
        if (xp.match_tag("/device_status")) {
            return 0;
        }
        if (xp.parse_bool("on_ac_power", on_ac_power)) continue;
        if (xp.parse_bool("on_usb_power", on_usb_power)) continue;
        if (xp.parse_double("battery_charge_pct", battery_charge_pct)) continue;
        if (xp.parse_int("battery_state", battery_state)) continue;
        if (xp.parse_double("battery_temperature_celsius", battery_temperature_celsius)) continue;
        if (xp.parse_bool("wifi_online", wifi_online)) continue;
        if (xp.parse_bool("user_active", user_active)) continue;
        if (xp.parse_str("device_name", device_name, sizeof(device_name))) continue;
    }
    return ERR_XML_PARSE;
}

void DEVICE_STATUS::write(MIOFILE& out) {
    out.printf(
        "    <device_status>\n"
        "        <on_ac_power>%d</oc_ac_power>\n"
        "        <on_usb_power>%d</on_usb_power>\n"
        "        <battery_charge_pct>%f</battery_charge_pct>\n"
        "        <battery_state>%d</battery_state>\n"
        "        <battery_temperature_celsius>%f</battery_temperature_celsius>\n"
        "        <wifi_online>%d</wifi_online>\n"
        "        <user_active>%d</user_active>\n"
        "        <device_name>%s</device_name>\n"
        "    </device_status>\n",
        on_ac_power ? 1 : 0,
        on_usb_power ? 1 : 0,
        battery_charge_pct,
        battery_state,
        battery_temperature_celsius,
        wifi_online ? 1 : 0,
        user_active ? 1 : 0,
        device_name
    );
}
