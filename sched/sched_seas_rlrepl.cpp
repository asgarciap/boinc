#include "sched_mge_api.h"
#include "boinc_db.h"
#include <map>
#include <cmath>

//This file implements both, the model based strategy to asign jobs to a specific mobile device
//and the replication strategy to calculate the number of replicas to be generated for a particular workunit.
//The first one is implemented in the send_work_host function and the second one in the calc_workunit_replicas.
//Several auxiliar functions are implemented to help within them.
//@author Arturo Garcia / MsC Computer Science 2019 / PUJ

static inline void decode_sched_data(std::string data, double& avg, int &samples, double& start_time, double &dr, double& lastcharge, double& lastupdate, int& tot_cpus)
{
    avg = 0.0;
    samples = 0;
    start_time = 0.0f;
    dr = 0.0f;
    lastupdate = 0;
    lastcharge = -1;
    tot_cpus = 0;
    if(!data.size()) return;
    std::vector<std::string> vdata = split(data, ';');
    if(vdata.size() >= 7)
    {
        avg = atof(vdata[0].c_str());
        samples = atoi(vdata[1].c_str());
        start_time = atof(vdata[2].c_str());
        dr = atof(vdata[3].c_str());
        lastcharge = atof(vdata[4].c_str());
        lastupdate = atof(vdata[5].c_str());
        tot_cpus = atoi(vdata[6].c_str());
    }
}

static inline std::string encode_sched_data(double uptimeavg, int samples, double start_time, double dr, double lastcharge, double lastupdate, int tot_cpus)
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
    mge_data.append(std::to_string(lastcharge));
    mge_data.append(";");
    mge_data.append(std::to_string(lastupdate));
    mge_data.append(";");
    mge_data.append(std::to_string(tot_cpus));
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
    const double epsilon = 0.001f;
    return std::abs(x-y) <= epsilon*std::abs(x);
}

//get the current discharge rate %/sec for a given hostid
//0.0f means there is no data available
double get_host_drs(long hostid)
{
    std::string data = get_mge_sched_data(hostid);
    double u,st,dr,lc,lu;
    int s,c;
    decode_sched_data(data,u,s,st,dr,lc,lu,c);
    if(dr > 0.0f) return double(1/dr);
    return 0.0f;
}

// next function implements the sched mge api using SEAS algorithm
void send_work_host(SCHEDULER_REQUEST* sreq, WORK_REQ* wreq, WU_RESULT wu_results[], int nwus)
{
    //get current data	
    std::string data = get_mge_sched_data(sreq->hostid);

    double uptimeavg, start_time, dr, lastcharge, lastupdate;
    int samples, tot_cpus;
    bool updatemge = true;
    decode_sched_data(data,uptimeavg,samples,start_time, dr, lastcharge, lastupdate, tot_cpus);
    double origlastupdate = 0.0f;
    //total current available CPUs than can run a job simultaneously
    int numavlcpus = wreq->req_instances[PROC_TYPE_CPU];
    if(numavlcpus > tot_cpus) tot_cpus = numavlcpus;

    //if it had been more than 6 hours since the last measure, start over again
    if(time(0) - lastupdate > 21600.0f) 
    {
        log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] More than 6 hours have passed since last measure. Resetting battery info data\n");
        origlastupdate = lastupdate;
        lastupdate = sreq->host.device_status_time;
        lastcharge = sreq->host.battery_charge_pct;
        start_time = lastupdate;
        uptimeavg = 0;
        samples = 0;
    }

    DEVICE_STATUS ds = get_last_device_status(sreq->hostid);
    (void) ds; //avoid unused var warnning
    //estimate discharging rate if there were a change in battery charge percentage
    double newcharge = sreq->host.battery_charge_pct;
    double oldcharge = lastcharge;
    double newtime = sreq->host.device_status_time;
    double oldtime = lastupdate;

    //we should calculate only when there is a change in the battery charge state
    if(isNearlyEqual(oldcharge,newcharge) && origlastupdate > 0.0f) 
		updatemge = false; // if the battery charge is the same we have saved, dont update it

    double delta = 0.0f;
    int newuptime = 0;
    //int tewd = 0; //total amount of time or work sent to the device (in seconds)
    
    log_messages.printf(MSG_NORMAL,"[sched_seas] data_decoded - avg:%.3f samples:%d start_time:%.0f dr:%.3f lastupdate:%.0f currupdate:%.0f lastcharge:%.3f newcharge:%.3f\n",uptimeavg,samples,start_time,dr,oldtime,newtime,oldcharge,newcharge);

    //if the battery charge didn't change or it has been a "long time" since the last update
    //use the last discharge rate saved
    if(!isNearlyEqual(oldcharge,newcharge) && newtime > oldtime)
        dr = (newtime - oldtime) / (oldcharge-newcharge);
        
    //default discharge rate. (number of seconds to discharge 1% of battery)
    if(dr <= 0)
	    dr = 300;

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
    if(!isNearlyEqual(oldcharge,newcharge))
    {
        samples++;
        delta = uptime - uptimeavg;
        uptimeavg += delta/samples;
    }

    if(uptimeavg > 1.0f)
    {
        newuptime = uptimeavg - (newtime-start_time);
    }
    else
    {
        newuptime = uptime;
    }

    log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Battery DR: %.3f%%/secs, Available Battery Pct: %.3f%%  Remaining Uptime: %d secs Jobs in progress: %ld\n", sreq->hostid, drs, availablecharge, newuptime, (sreq->ip_results.size()+sreq->other_results.size())); 
    log_messages.printf(MSG_NORMAL,"[sched_seas] data_calculated - avg:%.3f samples:%d start_time:%.0f dr:%.3f \n",uptimeavg,samples,start_time,dr);
    if(newuptime > 0) {
	    //total time to complete jobs from others projects
	    //it should be 0 always ideally
        int inprogress = time_inprogress_op(sreq);
	    log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Estimated in progress job remaining time: %d seconds. Available CPUs for computing: %d out of %d\n",sreq->hostid,inprogress,numavlcpus,tot_cpus);
        int numsentjobs = 0;
        bool ewd_estimated = true; //indicates if we estimate the job duration (true) or if it is taken from avg_turnaround (acurate)
        //other_results is the number of results that the client has from this project
        //we dont know how much time it will take to complete each of them, so we asume
        //they have a 0% of progress
        int currentjobs = sreq->other_results.size();
        if(inprogress < newuptime && (numavlcpus > 0 || (tot_cpus && (currentjobs % tot_cpus > 0)) || currentjobs == tot_cpus)) {
            for(int i=0; i<nwus; i++) {
                if(!work_needed(false)) {
                    log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Enough work sent on this RPC.\n",sreq->hostid);
                    break;
                }
                WU_RESULT& wu = wu_results[i];
                BEST_APP_VERSION* bavp = get_best_app_version(&wu.workunit);
                if(bavp) {
                    int ewd = estimate_duration(wu.workunit, *bavp);
                    //if we have real data from previous jobs, use it since is more acourate
                    if((int) avg_turnaround_time() > 0) {
                        ewd = ceil(avg_turnaround_time());
                        ewd_estimated = false;
                    }else if(((int)sreq->other_results.size() >= tot_cpus || numsentjobs >= tot_cpus) && !numavlcpus){
                        log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Waiting for host to report its first finished jobs to compute estimated turnaround\n",sreq->hostid);
                        break;
                    }

                    int ewop = ewd*(ceil((currentjobs+numsentjobs)/tot_cpus));
                    int tot_busy = (currentjobs+numsentjobs) % tot_cpus == 0 ? (int) (ewd+inprogress+ewop) : (int) (inprogress+ewop);
                    if(tot_busy > wu.workunit.delay_bound && numavlcpus <= 0) {
                        log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] The device has too much jobs in progress (%d). It won't finish next result within its delay bound. Not sending jobs. Total remaining jobs time: %d secs\n",sreq->hostid,currentjobs,tot_busy);
                        break;
                    }
                    log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [RESULT#%ld] Estimated job duration time: %d secs. avg turnaround:%.2f secs\n",wu.resultid,ewd,avg_turnaround_time());
                    // the job can't be finish before the battery dies
                    if(tot_busy > newuptime && !sreq->host.on_ac_power && !sreq->host.on_usb_power && !ewd_estimated) {
                        log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] [RESULT#%ld] The device can't finish the job before running out of batteries. Total Time: %d > Available Time: %d\n",
                            sreq->hostid, wu.resultid,tot_busy,newuptime);
                        break;
                    }else {
                        log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] [RESULT#%ld] Trying to assign job to device. Estimated job duration: %d seconds. Total jobs time assigned: %d in %d jobs running in %d CPUs\n",sreq->hostid, wu.resultid, ewd, tot_busy, (currentjobs+numsentjobs), tot_cpus);
                        int rr = add_result_to_reply(&wu.workunit,bavp);
                        if(rr == 0) {
                            numsentjobs++;
                            numavlcpus--;
                        } else {
                            log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] [RESULT#%ld] Can't assign job to device. Error: %s\n",sreq->hostid,wu.resultid, boincerror(rr));
                            //check next result in the list
                            continue;
                        }
                    }
                }
            }
        }
        else if(numavlcpus)
        {           
            log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Host has too much in progress jobs. Trying to send 1 job to avoid client backoff. Total time until finish current jobs: %d  seconds\n",sreq->hostid, inprogress);
            WU_RESULT& wu = wu_results[0];
            BEST_APP_VERSION* bavp = get_best_app_version(&wu.workunit); 
            int rr = add_result_to_reply(&wu.workunit,bavp);
            if(rr != 0)
            {
                log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] [RESULT#%ld] Can't assign job to device. Error: %s\n",sreq->hostid,wu.resultid, boincerror(rr));
            }
        }
        else
        {
            log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Host doesn't have enough CPU availables for computing. %d out of %d CPUs are avaible for computing\n",sreq->hostid, numavlcpus, tot_cpus);
        }
    }
    else 
    {
        log_messages.printf(MSG_NORMAL,"[mge_sched] [seas_sched] [HOST#%ld] Host is about to lost connection because of battery power. Not sending jobs\n",sreq->hostid);
    }
    
    std::string md = data;
    if(updatemge) {  
        md = encode_sched_data(uptimeavg, samples, start_time, dr, newcharge,newtime,tot_cpus);
    }

    //we always have to call this method, otherwise it will swipe current data in BD
    save_mge_sched_data(sreq->hostid, md.c_str(), (int) md.size());
}

// next function implements the sched mge api replication using reinforcement learning approach
void calc_workunit_replicas(SCHEDULER_REQUEST* sreq, WORKUNIT* wu, int& reps, int& quorum, int max_repls)
{
    //always use quorum=1
    quorum = 1;
    log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] [WORKUNIT#%ld] Checking number of replicas to generate. max replicas: %d\n",wu->id,max_repls);

    int K_FACTOR=10;
    int K_WASTED_ENERGY_IMPACT = 7;
    double EXPLORATIVE_PROB = 0.2; //20% of the times we use an explorative strategy
    
    std::string data = get_mge_sched_data(sreq->hostid);
    double avg, start_time, dr, lastcharge,lastupdate;
    int samples, tot_cpus;
    decode_sched_data(data,avg,samples,start_time, dr, lastcharge,lastupdate, tot_cpus);
    if(dr <= 0) dr = 600;
    double drs = 1/dr;
 
    //check last replicas results from 1 to 10 max.
    DB_WORKUNIT dbwu;
    char where[256];
    std::map<int,double> repl_reward;

    //we will generate replicas from 1 to max_repls only, thats why we only check for pasts workunits
    //with replicas within this range i=[1...max_repls]   
    BEST_APP_VERSION* bavp = get_best_app_version(wu);
    double estimate_wu_duration = 600.0f; //default base duration (10 minutes)
    if(bavp)
        estimate_wu_duration = estimate_duration(*wu, *bavp);
    if(avg_turnaround_time() > 0)
        estimate_wu_duration = avg_turnaround_time();

    for(int i=1; i <= max_repls; ++i) {
	    log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] Checking finished workunits with %d replicas\n",i);
        sprintf(where, "where target_nresults=%d and id <> %lu order by mod_time desc limit 5", i, wu->id);
        //assume we won't find any good replicas for this replication factor
        repl_reward[i] = -K_FACTOR;
        bool check_next_wu = true;
        while (!dbwu.enumerate(where) && check_next_wu) {
	        log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] Checking results for [WORKUNIT#%ld] with %d replicas.\n",dbwu.id,i);
            int delay_bound = dbwu.delay_bound;
            int wuid = dbwu.id;
            //means that this workunit has a current result running and is still on time to finish (so check another workunit)
            bool found_ontime = false;
            DB_RESULT r;
            sprintf(where, "where workunitid=%d order by received_time desc", wuid);
            int nr = 0;
            double wasted_energy = 0;
            bool found_good_replica = false;
            while(!r.enumerate(where)) {
                nr++;
	            log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] Evaluating [RESULT#%ld].\n",r.id);
                //boinc could generate more replicas if it doesn't receive the result from client.
                //if this is the case, then this workunit didnt met QoS quota
                if(nr > i) {
	                log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] [WORKUNIT#%ld] generated more than %d replicas.\n",dbwu.id,i);
                    repl_reward[i] = -K_FACTOR;
                    break;
                }
                
                //we need to know how much time did it take to complete the replica
                //if the roundtrip time is less that twice the delay_bound, then this is
                //a good replica and the workunit met the QoS quota
                int roundtrip_time = r.received_time - r.sent_time;
                if(r.outcome == RESULT_OUTCOME_SUCCESS && roundtrip_time > 0 && roundtrip_time <= delay_bound) {
                    found_good_replica = true;
                }

                if(r.server_state == RESULT_SERVER_STATE_IN_PROGRESS) {
                    int expend_current_time = time(0) - r.sent_time;
                    if(expend_current_time > delay_bound) {
	                    log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] In-progress [RESULT#%ld] took too much time to finish.%d.\n",r.id,expend_current_time);
                        //we couldn't finish the workunit with this number of replicas.
                        check_next_wu = false;
                        repl_reward[i] = -K_FACTOR;
                        break;
                    }
                    else {
                        found_ontime = true;
                    }
                }
                //use a more accurate metric if we have the discharge rate for the host
                double hostdrs = r.hostid == 0 ? get_host_drs(sreq->hostid) : get_host_drs(r.hostid);
                if(hostdrs > 0.0f) {
                    drs = hostdrs;
                }

                double d_wasted_energy = 0.0f;
                //get the total energy wasted as reported in the result
                //be aware that this is not always the real wasted energy as the result
                //could have been waiting for a long time in the host before it started
                if(r.outcome == RESULT_OUTCOME_SUCCESS) d_wasted_energy = (r.initial_battery_charge_pct-r.final_battery_charge_pct);
                else d_wasted_energy = avg_turnaround_time(r.hostid)*drs;

	            log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] Computing Reward for %d replicas. Replica %d wasted energy: %.3f rtt: %d drs: %.3f ewd: %.3f\n",i,nr,d_wasted_energy,roundtrip_time,drs,estimate_wu_duration);
                wasted_energy += d_wasted_energy;
            }

            if(found_good_replica) {
                double hostdrs = get_host_drs(sreq->hostid);
                double t = wasted_energy/(i*estimate_wu_duration*hostdrs);
                double bt;
                
                if(t >= 1) bt = K_WASTED_ENERGY_IMPACT;
                else bt = K_WASTED_ENERGY_IMPACT*t;
                 
                repl_reward[i] = ((100-wasted_energy)/100)*K_FACTOR;
	            log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] Computing Reward for %d replicas. wasted_energy: %.3f t: %.3f bt: %.3f ewd: %.3f rew1: %.3f rew2: %.3f\n",i,wasted_energy,t,bt,estimate_wu_duration,(K_FACTOR-bt),repl_reward[i]);
                check_next_wu = false;
            }
            else if(found_ontime) {
                //there are no finished results for this workunit yet, but they are all on time
                //check next workunit
	            log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] [WORKUNIT#%ld] is not finished yet but its results are still on time to finish.\n",dbwu.id);
                continue; 
            }
            else {
                repl_reward[i] = -K_FACTOR;
            }
            r.end_enumerate();
        }
        dbwu.end_enumerate();
	    log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] Reward for using %d replicas: %.3f\n",i,repl_reward[i]);
    }
   
    //at this point we have the total wasted energy for every number of replicas
    //generated in the past (from 1..10 max), now we have to select the number
    //of replicas to generate for the current workunit
    //we use two approachs, an explorative one, which selects a number of replicas
    //randomly and the exploitative one, which selects the number of replicas
    //that wasted less energy in the past. 
    double val = (double) rand()/RAND_MAX;
    
    if(val < EXPLORATIVE_PROB) {
        //choose a random number between 1 and max_repls
        reps = rand() % max_repls + 1;
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
	    reps = rand() % max_repls + 1;
	    log_messages.printf(MSG_NORMAL,"[mge_sched] [rlrepl_sched] No data available yet to check. Using explorative approach. Number of replicas: %d\n",reps);
    }
}
