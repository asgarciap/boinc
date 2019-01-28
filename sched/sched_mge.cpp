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
#include "sched_mge.h"
#include "sched_main.h"
#include "sched_msgs.h"
#include "sched_send.h"
#include "sched_shmem.h"
#include "sched_types.h"
#include "sched_util.h"
#include "sched_version.h"

void get_sched_data(char* data, double& avg, int &samples, double& start_time, double &dr, double& nextupdate) {
    
    avg = 0.0;
    samples = 0;
    start_time = time(0);
    dr = 0;
    //parses "avg;samples;start_time;"
    std::string data64;
    data64.assign(data);
    std::string datastr = r_base64_decode(data64.data(), data64.size());
    std::vector<std::string> vdata = split(datastr, ';');
    if(vdata.size() >= 5) {
        avg = atof(vdata[0].c_str());
        samples = atoi(vdata[1].c_str());
        start_time = atof(vdata[2].c_str());
        dr = atof(vdata[3].c_str());
        nextupdate = atof(vdata[4].c_str());
    }
    
    log_messages.printf(MSG_NORMAL,
                        "[mge_sched] [HOST#%lu] mge sched data. avg:%f samples:%d start_time:%f dr:%f\n",
                        g_reply->host.id, avg, samples, start_time,dr);
}

void send_work_mge() {  
    char buf[256];

    //SCHED_DB_RESULT result;
    
    // Update battery time estimation.
    DB_DEVICE_STATUS ds;
    int samples = 0;
    double start_time;
    double uptimeavg = 0, delta = 0;
    double newuptime = 0; // remaining connection time
    double dr = 0;
    double nextupdate = 0; //next time we expect (as maximum) the device status report
    double tewd = 0; //total amount of time or work sent to the device (in seconds)
    sprintf(buf, "where host_id=%lu", g_reply->host.id);
    if (!ds.enumerate(buf)) {
        
        ds.end_enumerate();
        
        //get average data
        get_sched_data(ds.mge_sched_data, uptimeavg, samples, start_time,dr,nextupdate);
        
        // -------- SEAS algorithm --------
        //estimate discharging rate if there were a change in battery charge percentage
        double newcharge = g_request->host.battery_charge_pct;
        double oldcharge = ds.battery_charge_pct;
        double newtime = g_request->host.device_status_time;
        double oldtime = ds.last_update_time;
        
        //if the battery charge didn't change or it has been a "long time" since the last update
        //use the last discharge rate saved
        if(oldcharge > newcharge && newtime <= nextupdate)
            dr = (newtime - oldtime) / (oldcharge-newcharge);
        
        //estimate remain time
        double uptime = newtime - start_time + newcharge*dr;
        
        //update average remain time
        samples++;
        delta = uptime - uptimeavg;
        uptimeavg += delta/samples;
        newuptime = uptimeavg - (newtime-start_time);
        
        log_messages.printf(MSG_NORMAL, "[mge_sched] [HOST#%lu] Discharge rate: 1%%/%f secs - Est. uptime (avg): %f until 0%%.\n",
                g_reply->host.id,dr,newuptime
        );
        // -------- end of SEAS algorithm --------
        
    }else {
        //we don't have any device_status record for this host.
        //insert a new empty record to this host
        start_time=time(NULL);
        dr = 600; //default discharge rate is 1% every 10 minutes (arbitrary value) .. check
        DB_DEVICE_STATUS d;
        d.hostid=g_reply->host.id;
        int retval = d.insert();
        if(retval) {
            log_messages.printf(MSG_CRITICAL,
                                "[mge_sched] [HOST#%lu] Error when trying to insert device_status record. %s\n",
                                g_reply->host.id,boincerror(retval)
                               );
        }
    }
        
    //If the node has enough time to process any of the result, send to it
    if(newuptime > 0)
    {
        if (!g_wreq->need_proc_type(PROC_TYPE_CPU)) {
            return;
        }

        int nscan = ssp->max_wu_results;
        int rnd_off = rand() % ssp->max_wu_results;
        if (config.debug_send_scan) {
            log_messages.printf(MSG_NORMAL,
                "[mge_sched] scanning %d slots starting at %d\n", nscan, rnd_off
            );
        }
        bool sema_locked = false;
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
                continue;
            }
            
            double ewd = estimate_duration(wu, *bavp);
            tewd += ewd;
            
            // the job can't be finish before the battery dies
            if((tewd > newuptime) && !g_reply->host.on_ac_power && !g_reply->host.on_usb_power) {
                log_messages.printf(MSG_NORMAL,
                        "[mge_sched] [HOST#%lu] [RESULT#%lu] Device can't finish the job before running out of batteries.\n",
                        g_reply->host.id, wu_result.resultid);
                continue;
            }else {
                log_messages.printf(MSG_NORMAL,
                        "[mge_sched] [HOST#%lu] [RESULT#%lu] Trying to assing job to device. Estimated job duration: %f seconds. Total job assigned: %f\n",
                        g_reply->host.id, wu_result.resultid, ewd, tewd);
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
                    add_result_to_reply(result, wu, bavp, false);

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
        
        if (sema_locked) {
            unlock_sema();
        }

        g_wreq->best_app_versions.clear();
    }else {
        log_messages.printf(MSG_NORMAL,
                        "[mge_sched] [HOST#%lu] can't send jobs to device because remaining battery time (%f seconds) is too short.\n",
                        g_reply->host.id, newuptime);
    }
    
    //we will expect for the next report, twice the total of work sent (it should come before that time)
    if(tewd > 0)
        nextupdate = (g_request->host.device_status_time)+(tewd*2);
    else
        nextupdate = (g_request->host.device_status_time)+120;
    
    //update device_status record in BD
    std::string mge_data("");
    mge_data.append(std::to_string(uptimeavg)); //avg
    mge_data.append(";");
    mge_data.append(std::to_string(samples)); //samples
    mge_data.append(";");
    mge_data.append(std::to_string(start_time)); //start time
    mge_data.append(";");
    mge_data.append(std::to_string(dr)); //discharge rate (seconds)
    mge_data.append(";");
    mge_data.append(std::to_string(nextupdate));
    mge_data.append(";");
    std::string mgedata64 = r_base64_encode(mge_data.data(), mge_data.size());
    //changing g_reply will cause an update in BD
    //this data is not send to the client since is not writen in g_reply.write method.
    strlcpy(g_reply->host.mge_sched_data, mgedata64.data(), sizeof(g_reply->host.mge_sched_data));
    g_reply->host.battery_charge_pct = g_request->host.battery_charge_pct;
    g_reply->host.battery_state = g_request->host.battery_state;
    g_reply->host.battery_temperature_celsius = g_request->host.battery_temperature_celsius;
    log_messages.printf(MSG_NORMAL,
                        "[mge_sched] [HOST#%lu] updating mge sched data. avg uptime:%f samples:%d start_time:%f dr:%f\n",
                        g_reply->host.id, uptimeavg, samples, start_time, dr);
}
