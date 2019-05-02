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

// Locality scheduling: assign jobs to clients based on the data files
// the client already has.
//
// Currently this is specific to Einstein@home and is not generally usable.
// There's a generic but more limited version, "limited locality scheduling":
// http://boinc.berkeley.edu/trac/wiki/LocalityScheduling

#include "config.h"

#include <algorithm>
#include <climits>
#include <vector>

#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <glob.h>
#include <sys/stat.h>

#include "boinc_db.h"
#include "error_numbers.h"
#include "filesys.h"
#include "str_util.h"
#include "base64.h"

#include "sched_check.h"
#include "sched_config.h"
#include "sched_mge_api.h"
#include "sched_main.h"
#include "sched_msgs.h"
#include "sched_shmem.h"
#include "sched_types.h"
#include "sched_util.h"
#include "sched_version.h"

void mge_log(const char* format, ...)
{
    char buf[256];
    va_list va;
    va_start(va, format);
    snprintf(buf, sizeof(buf), format, va);
    log_messages.printf(MSG_NORMAL, "[mge_sched] %s", buf);
    va_end(va);
}

BEST_APP_VERSION* get_best_app_version(WORKUNIT* wu)
{
    BEST_APP_VERSION* bavp;
    bavp = get_app_version(*wu, true, false);
    if (!bavp) {
        if (config.debug_send_job) {
            log_messages.printf(MSG_NORMAL,
                "[mge_sched] [WORKUNIT#%lu] no app version available\n",
                wu->id
            );
        }
    }
    return bavp;
}

int estimate_workunit_duration(WORKUNIT* wu, BEST_APP_VERSION* bavp)
{
    return estimate_duration(*wu,*bavp);
}

double avg_turnaround_time()
{
    return g_reply->host.avg_turnaround;
}

int add_result_to_reply(WORKUNIT* workunit, BEST_APP_VERSION* bavp)
{
    bool sema_locked = false;
    int retadd = 0;    
    for (int i=0; i<ssp->max_wu_results; i++) {
        WU_RESULT& wu_result = ssp->wu_results[i];
        if(wu_result.workunit.id == workunit->id) {
            WORKUNIT wu = wu_result.workunit;
            APP* app;
            app = ssp->lookup_app(wu.appid);

            if (!sema_locked) {
                lock_sema();
                sema_locked = true;
            }

            // make sure the job is still in the cache
            // array is locked at this point.
            //
            if (wu_result.state != WR_STATE_PRESENT  && wu_result.state != g_pid) {
                continue;
            }
            
            int retval = wu_is_infeasible_fast(
                wu,
                wu_result.res_server_state, wu_result.res_priority,
                wu_result.res_report_deadline,
                *app,
                *bavp
            );

            if (retval) {
                log_messages.printf(MSG_WARNING,"[mge_sched][RESULT#%lu] Job can't be send to the host. %s\n",wu_result.resultid, infeasible_string(retval));
		retadd = retval;
                continue;
            }
                        
            wu_result.state = g_pid;

            // It passed fast checks.
            // Release sema and do slow checks
            //
            unlock_sema();
            sema_locked = false;

            switch (slow_check(wu_result, app, bavp)) {
                case CHECK_NO_HOST:
                    wu_result.state = WR_STATE_PRESENT;
                    break;
                case CHECK_NO_ANY:
                    wu_result.state = WR_STATE_EMPTY;
                    break;
                default:
                    // slow_check() refreshes fields of wu_result.workunit;
                    // update our copy too
                    //
                    wu.hr_class = wu_result.workunit.hr_class;
                    wu.app_version_id = wu_result.workunit.app_version_id;

                    // mark slot as empty AFTER we've copied out of it
                    // (since otherwise feeder might overwrite it)
                    //
                    wu_result.state = WR_STATE_EMPTY;

                    // reread result from DB, make sure it's still unsent
                    // TODO: from here to end of add_result_to_reply()
                    // (which updates the DB record) should be a transaction
                    //
                    SCHED_DB_RESULT result;
                    result.id = wu_result.resultid;
                    if (result_still_sendable(result, wu)) {
                        retadd = add_result_to_reply(result, wu, bavp, false);
			if(sema_locked) {
			    unlock_sema();	
			}
			return retadd;

                        // add_result_to_reply() fails only in pathological cases -
                        // e.g. we couldn't update the DB record or modify XML fields.
                        // If this happens, don't replace the record in the array
                        // (we can't anyway, since we marked the entry as "empty").
                        // The feeder will eventually pick it up again,
                        // and hopefully the problem won't happen twice.
                    }
                    break;
            }
        }
    }
   
    if(sema_locked) {
        unlock_sema();	    
    } 
    //we could't add the work unit to the reply
    return retadd;
}

DEVICE_STATUS get_last_device_status(long hostid)
{
    DB_DEVICE_STATUS d;
    char buf[256] = {0};
    snprintf(buf, sizeof(buf), "where host_id=%lu", hostid);
    if (!d.enumerate(buf)) {
        d.end_enumerate();
        return d;
    }
    else {
        d.clear();
	    d.hostid=hostid;
        d.insert();
    }
    return d;
}

std::string get_mge_sched_data(long hostid)
{   
    char buf[256] = {0};
    DB_DEVICE_STATUS ds;
    snprintf(buf, sizeof(buf), "where host_id=%ld", hostid);
    if (!ds.enumerate(buf)) {
        ds.end_enumerate();
        std::string s(ds.mge_sched_data);
        std::string sd = r_base64_decode(s.c_str(), s.size()).c_str();
        log_messages.printf(MSG_NORMAL,"Getting mge_sched_data. base64: %s - decoded: %s\n",ds.mge_sched_data,sd.c_str());
        return sd;
    }
    log_messages.printf(MSG_NORMAL,"Getting mge_sched_data. No record found for host_id:%ld\n",hostid);
    return "";
}

void save_mge_sched_data(long hostid, const char* data, int len)
{
    std::string mgedata64 = r_base64_encode(data, len);

    log_messages.printf(MSG_NORMAL,"Updating mge_sched_data: %s base64: %s hostid: %ld\n",data,mgedata64.c_str(),hostid);

    //changing g_reply will cause an update in BD
    //this data is not send to the client since is not writen in g_reply.write method.
    strlcpy(g_reply->host.mge_sched_data, mgedata64.c_str(), sizeof(g_reply->host.mge_sched_data));
    g_reply->host.battery_charge_pct = g_request->host.battery_charge_pct;
    g_reply->host.battery_state = g_request->host.battery_state;
    g_reply->host.battery_temperature_celsius = g_request->host.battery_temperature_celsius;
    g_reply->host.on_ac_power = g_request->host.on_ac_power;
    g_reply->host.on_usb_power = g_request->host.on_usb_power;
    g_reply->host.wifi_online = g_request->host.wifi_online;
    g_reply->host.user_active = g_request->host.user_active;
    g_reply->host.device_status_time = time(NULL);
    
    DB_DEVICE_STATUS d;
    d.hostid=hostid;
    char buf[100];
    sprintf(buf, "where host_id=%lu", hostid);
    if (d.enumerate(buf)) 
    {
        int retval = d.insert();
        if(retval) 
        {
            log_messages.printf(MSG_CRITICAL,"[mge_sched] [HOST#%lu] Error when trying to insert device_status record. %s\n",
                            hostid,boincerror(retval));
        }
    }
    d.end_enumerate();
}


void send_work_mge() {
        
    if (!g_wreq->need_proc_type(PROC_TYPE_CPU)) {
        log_messages.printf(MSG_NORMAL,
                "[mge_sched] mge sched only supports CPU scheduling and the request doesn't include CPU processor type on it");
        return;
    }

    int nscan = ssp->max_wu_results;
    int rnd_off = rand() % ssp->max_wu_results;
    if (config.debug_send_scan) {
        log_messages.printf(MSG_NORMAL,
            "[mge_sched] scanning %d slots starting at %d\n", nscan, rnd_off
        );
    }
    
    WU_RESULT sched_results[MAX_WU_RESULTS];
    int nr = 0;
    
    for (int j=0; j<nscan; j++) {
        int i = (j+rnd_off) % ssp->max_wu_results;
        WU_RESULT& wu_result = ssp->wu_results[i];
        if (wu_result.state != WR_STATE_PRESENT  && wu_result.state != g_pid) {
            continue;
        }
        WORKUNIT wu = wu_result.workunit;
        APP* app;
        app = ssp->lookup_app(wu.appid);
        if (app->non_cpu_intensive) {
            if (config.debug_send_job) {
                log_messages.printf(MSG_NORMAL,
                    "[mge_sched] [RESULT#%lu] app is non compute intensive\n",
                    wu_result.resultid
                );
            }
            continue;
        }
        BEST_APP_VERSION* bavp;
        bavp = get_app_version(wu, true, false);
        if (!bavp) {
            if (config.debug_send_job) {
                log_messages.printf(MSG_NORMAL,
                    "[mge_sched] [RESULT#%lu] no app version available\n",
                    wu_result.resultid
                );
            }
            continue;
        }
            
        // check limits on jobs for this (app, processor type)
        //
        if (config.max_jobs_in_progress.exceeded(app, bavp->host_usage.proc_type)) {
            if (config.debug_quota) {
                log_messages.printf(MSG_NORMAL,
                    "[quota] limit for app/proctype exceeded\n"
                );
            }
            continue;
        }
        sched_results[nr] = wu_result;
        nr++;
    }
    
    if(nr > 0) {
	if(!g_request->hostid) 
		g_request->hostid = g_reply->hostid; 
        log_messages.printf(MSG_NORMAL,"[mge_sched] [HOST#%lu] Invoking MGE scheduler.\n",g_request->hostid);
        send_work_host(g_request, sched_results, nr);
        g_wreq->best_app_versions.clear();
    }
    else {
        log_messages.printf(MSG_NORMAL, "[mge_sched] There is no work seandable in the work unit cache\n");
    }
}
