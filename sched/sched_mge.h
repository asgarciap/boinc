// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2008 University of California
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

extern void send_work_mge();

//exposed functions to be available in the MGE API
BEST_APP_VERSION* get_best_app_version(WORKUNIT* wu);
int estimate_workunit_duration(WU_RESULT* wr, BEST_APP_VERSION* bavp);
int add_result_to_reply(WORKUNIT* wu, BEST_APP_VERSION* bavp);
void save_mge_sched_data(HOST&, const char* data, int len);
const char* get_mge_sched_data(HOST&);
DEVICE_STATUS get_last_device_status(HOST&);
void mge_log(const char* format, ...);
