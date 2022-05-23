#ifndef ANTBMS_H
#define ANTBMS_H

#include "common.h"
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include "device.h"


class Antbms  : public Device { 
    private: 
        float fVoltageAll;
        int bms_soc;
        int charge_mosfet_status;
        int discharge_mosfet_status;
        const char* charge_mosfet_status_describe;
        const char* discharge_mosfet_status_describe;
        const char* balance_status_describe;
        char bms_data[140];
        float cell_data[30];
        float cell_resi[30];
        float battery_capacity, battery_current;
        float temp_mosfet, temp_balance, temp_cell1, temp_cell5 ;
        float cell_diff;
        int clearSerial();
        int flushBMSSerial();
        int get_param(char addr);
        int set_param(char addr, int data);
        int check_sleep_mode();
        bool slow_charge;
        bool sleep_mode;
        float battery_full_capacity;
        float battery_all_voltage;
        float battery_cycle;
        uint32_t battery_running_seconds;
    public: 
        Antbms(PubSubClient *_mqttClient, SoftwareSerial *_serial);
        virtual int publish_switch();
        virtual int publish_data();
        
        virtual int update_data();//updateRealtimeState
        virtual int update_switch();//updateSwitchState

        virtual int change_switch(const char* switch_name, const char* onoff);//putSwitchState
        virtual const char* getDeviceName();
        void parseBMSData(char* bms_data, int size);
        virtual int setup_entity();
};

#endif
