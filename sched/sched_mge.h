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

#ifndef BOINC_SCHED_MGE_H
#define BOINC_SCHED_MGE_H

extern void send_work_mge();

//exposed functions to be available in the MGE API
extern BEST_APP_VERSION* get_best_app_version(WORKUNIT* wu);
extern int estimate_workunit_duration(WU_RESULT* wr, BEST_APP_VERSION* bavp);
extern double avg_turnaround_time();
extern int add_result_to_reply(WORKUNIT* wu, BEST_APP_VERSION* bavp);
extern void save_mge_sched_data(long hostid, const char* data, int len);
extern std::string get_mge_sched_data(long hostid);
extern DEVICE_STATUS get_last_device_status(long hostid);
extern void mge_log(const char* format, ...);
#endif
