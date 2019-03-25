create table device_status (
    host_id                     integer not null default 0,
    last_update_time            double not null default 0,
    on_ac_power                 tinyint not null default 0,
    on_usb_power                tinyint not null default 0,
    battery_charge_pct          double not null default 0,
    battery_state               integer not null default 0,
    battery_temperature_celsius double not null default 0,
    wifi_online                 tinyint not null default 0,
    user_active                 tinyint not null default 0,    
    device_name                 varchar(254) not null default '',
    mge_sched_data              varchar(254) not null default ''
) engine = InnoDB;

alter table device_status add unique(host_id);

alter table device_status
    add index device_host (host_id);

alter table result 
    add column init_battery_pct double not null default 0,
    add column init_battery_temp double not null default 0,
    add column final_battery_pct double not null default 0,
    add column final_battery_temp double not null default 0;
