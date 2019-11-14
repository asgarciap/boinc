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

#ifndef BOINC_SCHED_MGE_API_H
#define BOINC_SCHED_MGE_API_H

#include "sched_send.h"
#include "sched_mge.h"
#include "sched_msgs.h"

extern void send_work_host(SCHEDULER_REQUEST* sreq, WORK_REQ* wreq, WU_RESULT wu_results[], int nwus);
extern void calc_workunit_replicas(SCHEDULER_REQUEST* sreq, WORKUNIT* wu, int& reps, int& quorum, int max_reps);

#endif
