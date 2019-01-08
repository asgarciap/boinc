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
    remain_connection_time      integer not null default 0
) engine = InnoDB;

alter table device_status add unique(host_id);

alter table device_status
    add index device_host (host_id);
