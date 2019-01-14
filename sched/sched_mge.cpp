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

void get_sched_data(char* data, double& avg, int &samples, double& start_time) {
    
    avg = 0.0;
    samples = 0;
    start_time = time(0);
    //parses "avg;samples;start_time;"
    std::string data64;
    data64.assign(data);
    std::string datastr = r_base64_decode(data64.data(), data64.size());
    std::size_t pos = datastr.find(";");
    if(pos != std::string::npos) {
        avg = atof(datastr.substr(0,pos).c_str());
        std::size_t pos2 = datastr.find(";",pos+1);
        if(pos2 != std::string::npos) {
            samples = atoi(datastr.substr(pos+1,(pos2-pos+1)).c_str());
            std::size_t pos3 = datastr.find(";",pos2+1);
            if(pos3 != std::string::npos)
                start_time = atof(datastr.substr(pos2+1,(pos3-pos2+1)).c_str());
        }
    }
    
    log_messages.printf(MSG_NORMAL,
                        "[mge_sched] [HOST#%lu] mge sched data. avg:%f samples:%d start_time:%f\n",
                        g_reply->host.id, avg, samples, start_time);
}

void send_work_mge() {  
    char buf[256];

    SCHED_DB_RESULT result;
    
    // Update battery time estimation.
    DB_DEVICE_STATUS ds;
    int samples = 0;
    double start_time;
    double uptimeavg = 0, delta = 0;
    double newuptime = 0; // remaining connection time
    sprintf(buf, "where host_id=%lu", g_reply->host.id);
    if (!ds.enumerate(buf)) {
        
        ds.end_enumerate();
        
        //get average data
        get_sched_data(ds.mge_sched_data, uptimeavg, samples, start_time);
        
        // -------- SEAS algorithm --------
        //estimate discharging rate if there were a change in battery charge percentage
        double newcharge = g_request->host.battery_charge_pct;
        double oldcharge = ds.battery_charge_pct;
        double newtime = g_request->host.device_status_time;
        double oldtime = ds.last_update_time;
        if(oldcharge != newcharge) {
            double dr = (newtime - oldtime) / (oldcharge-newcharge);
            
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
            
        }
        // -------- end of SEAS algorithm --------
        
    }else {
        //we don't have any device_status record for this host.
        //insert a new empty record to this host
        start_time=time(NULL);
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
    
    std::string mge_data("");
    mge_data.append(std::to_string(uptimeavg)); //avg
    mge_data.append(";");
    mge_data.append(std::to_string(samples)); //samples
    mge_data.append(";");
    mge_data.append(std::to_string(start_time)); //start time
    mge_data.append(";");
    std::string mgedata64 = r_base64_encode(mge_data.data(), mge_data.size());
    //changing g_reply will cause an update in BD
    //this data is not send to the client since is not writen in g_reply.write method.
    strlcpy(g_reply->host.mge_sched_data, mgedata64.data(), sizeof(g_reply->host.mge_sched_data));
    g_reply->host.battery_charge_pct = g_request->host.battery_charge_pct;
    g_reply->host.battery_state = g_request->host.battery_state;
    g_reply->host.battery_temperature_celsius = g_request->host.battery_temperature_celsius;
    log_messages.printf(MSG_NORMAL,
                        "[mge_sched] [HOST#%lu] updating mge sched data. avg uptime:%f samples:%d start_time:%f\n",
                        g_reply->host.id, uptimeavg, samples, start_time);
    
    //If the node has enough time to process any of the result, send to it
    if(newuptime > 0)
    {
        
    }
    
    //TODO
    //Se debe usar el metodo add_result_to_reply(...) que esta en sched_send para agregar los resultados.
    
//     int i, nsent, nfiles, j;
// 
//     // seed the random number generator
//     unsigned int seed=time(0)+getpid();
//     srand(seed);
// 
//     // file names are used in SQL queries throughout; escape them now
//     // (this breaks things if file names legitimately contain ', but they don't)
//     //
//     for (unsigned int k=0; k<g_request->file_infos.size(); k++) {
//         FILE_INFO& fi = g_request->file_infos[k];
//         escape_string(fi.name, sizeof(fi.name));
//     }
// 
// #ifdef EINSTEIN_AT_HOME
//     std::vector<FILE_INFO> eah_copy = g_request->file_infos;
//     g_request->file_infos.clear();
//     g_request->files_not_needed.clear();
//     nfiles = (int) eah_copy.size();
//     for (i=0; i<nfiles; i++) {
//         char *fname = eah_copy[i].name;
// 
//         if (is_workunit_file(fname)) {
//             // these are files that we will use for locality scheduling and
//             // to search for work
//             //
//             g_request->file_infos.push_back(eah_copy[i]);
//         } else if (is_sticky_file(fname)) {  // was if(!data_files)
//             // these files MIGHT be deleted from host if we need to make
//             // disk space there
//             //
//             g_request->file_delete_candidates.push_back(eah_copy[i]);
//             if (config.debug_locality) {
//                 log_messages.printf(MSG_NORMAL,
//                     "[locality] [HOST#%lu] removing file %s from file_infos list\n",
//                     g_reply->host.id, fname
//                 );
//             }
//         } else {
//             // these files WILL be deleted from the host
//             //
//             g_request->files_not_needed.push_back(eah_copy[i]);
//             if (config.debug_locality) {
//                 log_messages.printf(MSG_NORMAL,
//                     "[locality] [HOST#%lu] adding file %s to files_not_needed list\n",
//                     g_reply->host.id, fname
//                 );
//             }
//         }
//     }
// #endif // EINSTEIN_AT_HOME
// 
//     nfiles = (int) g_request->file_infos.size();
//     for (i=0; i<nfiles; i++)
//         if (config.debug_locality) {
//             log_messages.printf(MSG_NORMAL,
//                 "[locality] [HOST#%lu] has file %s\n",
//                 g_reply->host.id, g_request->file_infos[i].name
//             );
//         }
// 
//     // send old work if there is any. send this only to hosts which have
//     // high-bandwidth connections, since asking dial-up users to upload
//     // (presumably large) data files is onerous.
//     //
//     if (config.locality_scheduling_send_timeout && g_request->host.n_bwdown>100000) {
//         int until=time(0)-config.locality_scheduling_send_timeout;
//         int retval_sow=send_old_work(INT_MIN, until);
//         if (retval_sow) {
//             log_messages.printf(MSG_NORMAL,
//                 "[locality] send_old_work() returned %d\n", retval_sow
//             );
//         }
//         if (!work_needed(true)) return;
//     }
// 
//     // Look for work in order of increasing file name, or randomly?
//     //
//     if (config.locality_scheduling_sorted_order) {
//         sort(g_request->file_infos.begin(), g_request->file_infos.end(), file_info_order);
//         j = 0;
//     } else {
//         if (!nfiles) nfiles = 1;
//         j = rand()%nfiles;
//     }
// 
//     // send work for existing files
//     //
//     for (i=0; i<(int)g_request->file_infos.size(); i++) {
//         int k = (i+j)%nfiles;
//         int retval_srff;
// 
//         if (!work_needed(true)) break;
//         FILE_INFO& fi = g_request->file_infos[k];
//         retval_srff = send_results_for_file(
//             fi.name, nsent, false
//         );
// 
//         if (retval_srff==ERR_NO_APP_VERSION || retval_srff==ERR_INSUFFICIENT_RESOURCE) return;
// 
//         // if we couldn't send any work for this file, and we STILL need work,
//         // then it must be that there was no additional work remaining for this
//         // file which is feasible for this host.  In this case, delete the file.
//         // If the work was not sent for other (dynamic) reason such as insufficient
//         // cpu, then DON'T delete the file.
//         //
//         if (nsent == 0 && work_needed(true) && config.file_deletion_strategy == 1) {
//             g_reply->file_deletes.push_back(fi);
//             if (config.debug_locality) {
//                 log_messages.printf(MSG_NORMAL,
//                     "[locality] [HOST#%lu]: delete file %s (not needed)\n",
//                     g_reply->host.id, fi.name
//                 );
//             }
// #ifdef EINSTEIN_AT_HOME
//             // For name matching patterns h1_
//             // generate corresponding l1_ patterns and delete these also
//             //
//             if (   /* files like h1_0340.30_S6GC1 */
//                    (   strlen(fi.name) == 16 &&
//                        !strncmp("h1_", fi.name, 3) &&
//                        !strncmp("_S6GC1", fi.name + 10, 6)
//                    ) ||
//                    /* files like h1_0000.00_S6Directed */
//                    (   strlen(fi.name) == 21 &&
//                        !strncmp("h1_", fi.name, 3) &&
//                        !strncmp("_S6Directed", fi.name + 10, 11)
//                    ) ||
//                    /* files like h1_0000.00_S6Direct */
//                    (   strlen(fi.name) == 19 &&
//                        !strncmp("h1_", fi.name, 3) &&
//                        !strncmp("_S6Direct", fi.name + 10, 9)
//                    )
//                ) {
//                 FILE_INFO fil;
//                 fil=fi;
//                 fil.name[0]='l';
//                 g_reply->file_deletes.push_back(fil);
//                 if (config.debug_locality) {
//                     log_messages.printf(MSG_NORMAL,
//                         "[locality] [HOST#%lu]: delete file %s (accompanies %s)\n",
//                         g_reply->host.id, fil.name, fi.name
//                     );
//                 }
//             } else if ( /* for files like h1_XXXX.XX_S5R4 */
//                    (   strlen(fi.name) == 15 &&
//                        !strncmp("h1_", fi.name, 3) &&
//                        !strncmp("_S5R4", fi.name + 10, 5)
//                    )
//                ) {
//                 FILE_INFO fil4,fil7,fih7;
//                 fil4=fi;
//                 fil4.name[0]='l';
//                 fil7=fil4;
//                 fil7.name[14]='7';
//                 fih7=fi;
//                 fih7.name[14]='7';
//                 g_reply->file_deletes.push_back(fil4);
//                 g_reply->file_deletes.push_back(fil7);
//                 g_reply->file_deletes.push_back(fih7);
//                 if (config.debug_locality) {
//                     log_messages.printf(MSG_NORMAL,
//                         "[locality] [HOST#%lu]: delete files %s,%s,%s (accompanies %s)\n",
//                         g_reply->host.id, fil4.name,fil7.name,fih7.name, fi.name
//                     );
//                 }
//             }
// #endif
//         } // nsent==0
//     } // loop over files already on the host
}

// Explanation of the logic of this scheduler:

// (1) If there is an (one) unsent result which is older than
// (1) config.locality_scheduling_send_timeout (7 days) and is
// (1) feasible for the host, and host has a fast network
// (1) connection (>100kb/s) then send it.

// (2) If we did send a result in the previous step, then send any
// (2) additional results that are feasible for the same input file.
// (2) Note that step 1 above is the ONLY place in the code where we
// (2) can send a result that is NOT of the locality name-type
// (2) FILENAME__other_stuff.

// (3) If additional results are needed, step through input files on
// (3) the host.  For each, if there are results that are feasible for
// (3) the host, send them.  If there are no results that are feasible
// (3) for the host, delete the input file from the host.

// (4) If additional results are needed, send the oldest result
// (4) created between times A and B, where
// (4) A=random time between locality_scheduling_send timeout and
// (4) locality_timeout/2 in the past, and B=locality_timeout/2 in
// (4) the past.

// (5) If we did send a result in the previous step, then send any
// (5) additional results that are feasible for the same input file.

// (6) If additional results are needed, select an input file name at
// (6) random from the current input file working set advertised by
// (6) the WU generator.  If there are results for this input file
// (6) that are feasible for this host, send them.  If no results
// (6) were found for this file, then repeat this step 6 another nine
// (6) times.

// (7) If additional results are needed, carry out an expensive,
// (7) deterministic search for ANY results that are feasible for the
// (7) host.  This search starts from a random filename advertised by
// (7) the WU generator, but continues cyclicly to cover ALL results
// (7) for ALL files. If a feasible result is found, send it.  Then
// (7) send any additional results that use the same input file.  If
// (7) there are no feasible results for the host, we are finished:
// (7) exit.

// (8) If addtional results are needed, return to step 4 above.

// const char *BOINC_RCSID_238cc1aec4 = "$Id$"; //NOTE what it this for?
