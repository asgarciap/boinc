#include "sched_mge_api.h"

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
        for(int i=0; i<nwus; i++) {
            WU_RESULT wu = wu_results[i];
            BEST_APP_VERSION* bavp = get_best_app_version(&wu.workunit);
            if(bavp) {
                
                double ewd = estimate_duration(wu.workunit, *bavp);
                
                // the job can't be finish before the battery dies
                if((tewd+ewd > newuptime) && !sreq->host.on_ac_power && !sreq->host.on_usb_power) {
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
    reps = 1;
    quorum = 1;
}
