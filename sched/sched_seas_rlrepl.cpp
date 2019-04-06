#include "sched_mge_api.h"
#include "boinc_db.h"
#include <map>
#include <cmath>

static inline void decode_sched_data(std::string data, double& avg, int &samples, double& start_time, double &dr, double& nextupdate, double& lastcharge, double& lastupdate)
{
    avg = 0.0;
    samples = 0;
    start_time = time(0);
    dr = 0;
    nextupdate = time(0)+300;
    lastupdate = 0;
    lastcharge = -1;
    if(!data.size()) return;
    std::vector<std::string> vdata = split(data, ';');
    if(vdata.size() >= 7)
    {
        avg = atof(vdata[0].c_str());
        samples = atoi(vdata[1].c_str());
        start_time = atof(vdata[2].c_str());
        dr = atof(vdata[3].c_str());
        nextupdate = atof(vdata[4].c_str());
        lastcharge = atof(vdata[5].c_str());
        lastupdate = atof(vdata[6].c_str());
    }
}

static inline std::string encode_sched_data(double uptimeavg, int samples, double start_time, double dr, double nextupdate, double lastcharge, double lastupdate)
{
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
    mge_data.append(std::to_string(lastcharge));
    mge_data.append(";");
    mge_data.append(std::to_string(lastupdate));
    mge_data.append(";");
    return mge_data;
}

static inline double time_inprogress_op(SCHEDULER_REQUEST* sreq)
{
    //total time to complete results from others projects
    double total=0.0;
    for(std::size_t i=0; i<sreq->ip_results.size(); i++)
        total += sreq->ip_results[i].estimated_completion_time;
    return total;
}

static inline bool isNearlyEqual(double x, double y)
{
    const double epsilon = 0.0001f;
    return std::abs(x-y) <= epsilon*std::abs(x);
}

// next function implements the sched mge api using SEAS algorithm
void send_work_host(SCHEDULER_REQUEST* sreq, WU_RESULT wu_results[], int nwus)
{
    //get current data	
    std::string data = get_mge_sched_data(sreq->hostid);

    double uptimeavg, start_time, dr, nextupdate, lastcharge, lastupdate;
    int samples;
    bool updatemge = true;
    decode_sched_data(data,uptimeavg,samples,start_time, dr, nextupdate, lastcharge, lastupdate);

    //if it had been more than 6 hours since the last measure, start over again
    if(time(0) - lastupdate > 21600.0f) {
        log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] It had been more than 12 hours since last measure. Resetting battery info data\n");
        lastupdate = sreq->host.device_status_time - 300;
        if(sreq->host.battery_charge_pct > 5.0f)
            lastcharge = sreq->host.battery_charge_pct + 5.0f;
        else
            lastcharge = 0.0f;
        start_time = lastupdate;
        uptimeavg = 0;
        samples = 0;
    }

    DEVICE_STATUS ds = get_last_device_status(sreq->hostid);

    //estimate discharging rate if there were a change in battery charge percentage
    double newcharge = sreq->host.battery_charge_pct;
    double oldcharge = ds.battery_charge_pct;
    double newtime = sreq->host.device_status_time;
    double oldtime = ds.last_update_time;

    //we should calculate only when there is a change in the battery charge state
    if(isNearlyEqual(oldcharge,newcharge)) {
	    if(!isNearlyEqual(newcharge,lastcharge))
	    	oldcharge = lastcharge;
	    else
		    updatemge = false; // if the battery charge is the same we have saved, dont update it
	    oldtime = lastupdate;
    }else if(time(0) - oldtime > 21600.0f)
    {
        oldtime = lastupdate;
        oldcharge = lastcharge;
    }

    double delta = 0.0f;
    int newuptime = 0;
    int tewd = 0; //total amount of time or work sent to the device (in seconds)
    
    log_messages.printf(MSG_NORMAL,"[sched_seas] data_decoded - avg:%.3f samples:%d start_time:%.0f dr:%.3f nextupdate:%.0f lastupdate:%.0f currupdate:%.0f lastcharge:%.3f newcharge:%.3f\n",uptimeavg,samples,start_time,dr,nextupdate,oldtime,newtime,oldcharge,newcharge);

    //if the battery charge didn't change or it has been a "long time" since the last update
    //use the last discharge rate saved
    if(oldcharge > newcharge && newtime <= nextupdate && newtime > oldtime)
        dr = (newtime - oldtime) / (oldcharge-newcharge);
        
    //default discharge rate. (number of seconds to discharge 1% of battery)
    if(dr <= 0)
	    dr = 600;

    //discharge rate (amount of % discharged in 1 second)
    double drs = 1/dr;

    //default start time
    if(start_time <= 0)
	    start_time = sreq->host.device_status_time - 300;
   
    //estimate remain time
    double availablecharge = newcharge - sreq->global_prefs.battery_charge_min_pct;
    double uptime = 0.f;
    if(availablecharge > 0.f)
        uptime = newtime - start_time + availablecharge*dr;
    
    //update average remain time
    samples++;
    delta = uptime - uptimeavg;
    uptimeavg += delta/samples;
    newuptime = uptimeavg - (newtime-start_time);
    log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Battery DR: %.3f%%/secs, Available Battery Pct: %.3f%%  Remaining Uptime: %d secs Jobs in progress: %ld\n", sreq->hostid, drs, availablecharge, newuptime, (sreq->ip_results.size()+sreq->other_results.size())); 
    log_messages.printf(MSG_NORMAL,"[sched_seas] data_calculated - avg:%.3f samples:%d start_time:%.0f dr:%.3f nextupdate:%.0f\n",uptimeavg,samples,start_time,dr,nextupdate);
    if(newuptime > 0) {
	//total time to complete jobs from others projects
	//it should be 0 always ideally
        int inprogress = time_inprogress_op(sreq);
	log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Estimated in progress job remaining time: %d seconds\n",sreq->hostid,inprogress);
        if(inprogress < newuptime) {
            for(int i=0; i<nwus; i++) {
                WU_RESULT& wu = wu_results[i];
                BEST_APP_VERSION* bavp = get_best_app_version(&wu.workunit);
                if(bavp) {
                    int ewd = estimate_duration(wu.workunit, *bavp);
                    //other_results is the numbers of results that the client has from this project
                    //we dont know how much time it will take to complete each of them, so we asume
                    //they have a 0% of advance
                    int ewop = ewd*sreq->other_results.size();
                    int tot_busy = (int) (tewd+ewd+inprogress+ewop);
                    log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [RESULT#%ld] Estimated job duration time: %d seconds.\n",wu.resultid,ewd);
                    // the job can't be finish before the battery dies
                    if( tot_busy > newuptime && !sreq->host.on_ac_power && !sreq->host.on_usb_power) {
                        log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] [RESULT#%ld] The device can't finish the job before running out of batteries. Total Time: %d > Available Time: %d\n",
                            sreq->hostid, wu.resultid,tot_busy,newuptime);
                        continue;
                    }else {
                        log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] [RESULT#%ld] Trying to assing job to device. Estimated job duration: %d seconds. Total job assigned: %d\n",sreq->hostid, wu.resultid, ewd, (tewd+ewd));
			int rr = add_result_to_reply(&wu.workunit,bavp);
                        if(rr == 0)
                            tewd += ewd;
                    }
                }
            }
        }
        else
        {
            log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Host has too much in progress jobs. Not sending new jobs. Total time until finish current jobs: %d  seconds\n",sreq->hostid, inprogress);
        }
    }
    else 
    {
        log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Host is about to lost connection because of battery power. Not sending jobs\n",sreq->hostid);
    }
    
    //we will expect for the next report, twice the total of work sent (it should come before that time)
    if(tewd > 0)
        nextupdate = (sreq->host.device_status_time)+(tewd*2);
    else
        nextupdate = (sreq->host.device_status_time)+120;

    std::string md = data;
    if(updatemge) {  
        md = encode_sched_data(uptimeavg, samples, start_time, dr, nextupdate,newcharge,newtime);
    }

    //we always have to call this method, otherwise it will swipe current data in BD
    save_mge_sched_data(sreq->hostid, md.c_str(), (int) md.size());
}

// next function implements the sched mge api replication using reinforcement learning approach
void calc_workunit_replicas(SCHEDULER_REQUEST* sreq, WORKUNIT wu, int& reps, int& quorum)
{
    //always use quorum=1
    quorum = 1;
    log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] [WORKUNIT#%ld] Checking number of replicas to generate\n",wu.id);

    int K_FACTOR=10;
    int K_WASTED_ENERGY_IMPACT = 7;
    double EXPLORATIVE_PROB = 0.2; //20% of the times we use an explorative strategy
    
    std::string data = get_mge_sched_data(sreq->hostid);
    double avg, start_time, dr, nextupdate,lastcharge,lastupdate;
    int samples;
    decode_sched_data(data,avg,samples,start_time, dr, nextupdate, lastupdate, lastcharge);
    if(dr <= 0) dr = 600;
    
    //check last replicas results from 1 to 10 max.
    DB_WORKUNIT dbwu;
    char where[256];
    std::map<int,double> repl_reward;

    //we will generate replicas from 1 to 10 only, thats why we only check for pasts workunits
    //with replicas within this range i=[1...10]   
    BEST_APP_VERSION* bavp = get_best_app_version(&wu);
    double estimate_wu_duration = 600.0f; //default base duration (10 minutes)
    if(bavp)
        estimate_wu_duration = estimate_duration(wu, *bavp);
    int max_repls = 0;
    for(int i=1; i <= 10; ++i) {
        sprintf(where, "where target_nresults=%d and (assimilate_state <> 0 or error_mask <> 0) and id <> %lu order by create_time desc", i, wu.id);
        if (!dbwu.enumerate(where)) {
            max_repls = i;
            int delay_bound = dbwu.delay_bound;
            int wuid = dbwu.id;
            dbwu.end_enumerate();
            DB_RESULT r;
            sprintf(where, "where workunitid=%d order by received_time desc", wuid);
            int nr = 0;
            double wasted_energy = 0;
            bool found_good_replica = false;
            while(!r.enumerate(where)) {
                nr++;
                //boinc could generate more replicas if it doesn't receive the result from client.
                //if this is the case, then this workunit didnt met QoS quota
                if(nr > i) {
                    repl_reward[i] = -K_FACTOR;
                    break;
                }
                
                //we need to know how much time did it take to complete the replica
                //if the roundtrip time is less that twice the delay_bound, then this is
                //a good replica and the worunit met the QoS quota
                int roundtrip_time = r.received_time - r.sent_time;
                if(roundtrip_time > 0 && roundtrip_time <= 2*delay_bound) found_good_replica = true;
                
                //if this result reports 0% wasted energy, we use the discharge rate from the current
                //client to calculate an estimated wasted energy using the roundtrip time
                double d_wasted_energy = (r.final_battery_charge_pct - r.initial_battery_charge_pct);
                if(d_wasted_energy <= 0) {
                    if(roundtrip_time > 0)
                        d_wasted_energy = roundtrip_time*dr;
                    else
                        d_wasted_energy = delay_bound*2*dr;
                }
                wasted_energy += d_wasted_energy;
            }
            
            if(found_good_replica) {
                double t = wasted_energy/(i*estimate_wu_duration*dr);
                double bt;
                
                if(t >= 1) bt = K_WASTED_ENERGY_IMPACT;
                else bt = K_WASTED_ENERGY_IMPACT*t;
                
                repl_reward[i] = K_FACTOR-bt;
            }
            else {
                repl_reward[i] = -K_FACTOR;
            }
            r.end_enumerate();
	    log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] Reward for using %d replicas: %.3f\n",i,repl_reward[i]);
        }else {
            break;
        }
    }
   
    //at this point we have the total wasted energy for every number of replicas
    //generated in the past (from 1..10 max), now we have to select the number
    //of replicas to generate for the current workunit
    //we user two approachs, an explorative one, which selects a number of replicas
    //randomly and the exploitative one, which selects the number of replicas
    //that wasted less energy in the past. 
    double val = (double) rand()/RAND_MAX;
    
    if(val < EXPLORATIVE_PROB) {
        //choose a random number between 1 and max_repls
        reps = (rand() % max_repls+1)+1;
	    log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] Using explorative approach. Number of replicas: %d\n",reps);
    }else if(repl_reward.size() > 0){
        // sort repl_reward and take the one with the higher reward (exploitative)
        reps = 1;
        double v = 0;
        for(auto it=repl_reward.begin(); it != repl_reward.end(); ++it) {
            if(it->second > v) {
                v = it->second;
                reps = it->first;
            }
        }
	    log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] Using exploitative approach. Number of replicas: %d\n",reps);
    }else {
	    //if we dont have any data yet, generate only 1 replica
	    reps = 1;
	    log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] No data available yet to check. Starting with 1 replica\n");
    }
}
