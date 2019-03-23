#include "sched_mge_api.h"
#include "boinc_db.h"

#include <map>

void decode_sched_data(const char* data, double& avg, int &samples, double& start_time, double &dr, double& nextupdate)
{
    avg = 0.0;
    samples = 0;
    start_time = time(0);
    dr = 0;
    std::vector<std::string> vdata = split(data, ';');
    if(vdata.size() >= 5)
    {
        avg = atof(vdata[0].c_str());
        samples = atoi(vdata[1].c_str());
        start_time = atof(vdata[2].c_str());
        dr = atof(vdata[3].c_str());
        nextupdate = atof(vdata[4].c_str());
    }
}

std::string encode_sched_data(double uptimeavg, int samples, double start_time, double dr, double nextupdate)
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
    return mge_data;
}

double time_inprogress(SCHEDULER_REQUEST* sreq)
{
    double total=0.0;
    for(std::size_t i=0; i<sreq->ip_results.size(); i++)
        total += sreq->ip_results[i].estimated_completion_time;
    return total;
}

// next function implements the sched mge api using SEAS algorithm
void send_work_host(SCHEDULER_REQUEST* sreq, WU_RESULT wu_results[], int nwus)
{
    std::string data(get_mge_sched_data(sreq->host));
    double avg, start_time, dr, nextupdate;
    int samples;
    decode_sched_data(data.c_str(),avg,samples,start_time, dr, nextupdate);
    DEVICE_STATUS ds = get_last_device_status(sreq->host);
    
    //estimate discharging rate if there were a change in battery charge percentage
    double newcharge = sreq->host.battery_charge_pct;
    double oldcharge = ds.battery_charge_pct;
    double newtime = sreq->host.device_status_time;
    double oldtime = ds.last_update_time;
    double delta = 0.0f;
    double uptimeavg = 0.0f;
    double newuptime = 0.0f;
    double tewd = 0; //total amount of time or work sent to the device (in seconds)
    
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
    
    if(newuptime > 0) {
        double inprogress = time_inprogress(sreq);
        if(inprogress < newuptime) {
            for(int i=0; i<nwus; i++) {
                WU_RESULT wu = wu_results[i];
                BEST_APP_VERSION* bavp = get_best_app_version(&wu.workunit);
                if(bavp) {
                    double ewd = estimate_duration(wu.workunit, *bavp);
                    // the job can't be finish before the battery dies
                    if((tewd+ewd+inprogress > newuptime) && !sreq->host.on_ac_power && !sreq->host.on_usb_power) {
                        mge_log("[seas_sched] [HOST#%lu] [RESULT#%lu] The device can't finish the job before running out of batteries.\n",
                            sreq->host.id, wu.resultid);
                        continue;
                    }else {
                        mge_log("[seas_sched] [HOST#%lu] [RESULT#%lu] Trying to assing job to device. Estimated job duration: %f seconds. Total job assigned: %f\n",sreq->host.id, wu.resultid, ewd, (tewd+ewd));
                        if(add_result_to_reply(&wu.workunit, bavp) == 0)
                            tewd += ewd;
                    }
                }
            }
        }
        else
        {
            mge_log("[seas_sched] [HOST#%lu] Host has too much in progress jobs. Not sending new jobs. Total time until finish current jobs: %ld  seconds",sreq->host.id, inprogress);
        }
    }
    else 
    {
        mge_log("[seas_sched] [HOST#%lu] Host is about to lost connection because of battery power. Not sending jobs",sreq->host.id);
    }
    
    //we will expect for the next report, twice the total of work sent (it should come before that time)
    if(tewd > 0)
        nextupdate = (sreq->host.device_status_time)+(tewd*2);
    else
        nextupdate = (sreq->host.device_status_time)+120;
    
    std::string md = encode_sched_data(uptimeavg, samples, start_time, dr, nextupdate);
    save_mge_sched_data(sreq->host, md.c_str(), (int) md.size());
}

// next function implements the sched mge api replication using reinforcement learning approach
void calc_workunit_replicas(SCHEDULER_REQUEST* sreq, WORKUNIT wu, int& reps, int& quorum)
{
    //always use quorum=1
    quorum = 1;
    int K_FACTOR=10;
    int K_WASTED_ENERGY_IMPACT = 7;
    double EXPLORATIVE_PROB = 0.2; //20% of the times we use an explorative strategy
    
    std::string data(get_mge_sched_data(sreq->host));
    double avg, start_time, dr, nextupdate;
    int samples;
    decode_sched_data(data.c_str(),avg,samples,start_time, dr, nextupdate);
    if(dr <= 0) dr = 1;
    
    //check last replicas results from 1 to 10 max.
    DB_WORKUNIT dbwu;
    char where[256];
    std::map<int,double> repl_reward;
   
    BEST_APP_VERSION* bavp = get_best_app_version(&wu);
    double estimate_wu_duration = estimate_duration(wu, *bavp);
    int max_repls = 0;
    for(int i=1; i <= 10; ++i) {
        sprintf(where, "where target_nresults=%d and id <> %lu order by create_time desc", i, wu.id);
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
                
                //we need
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
        }else {
            break;
        }
    }
    
    double val = (double) rand()/RAND_MAX;
    
    if(val < EXPLORATIVE_PROB) {
        //choose a random number between 1 and max_repls
        reps = (rand() % max_repls)+1;
    }else {
        // sort repl_reward and take the one with the higher reward (exploitative)
        reps = 1;
        double v = 0;
        for(auto it=repl_reward.begin(); it != repl_reward.end(); ++it) {
            if(it->second > v) {
                v = it->second;
                reps = it->first;
            }
        }
    }
}
