#ifndef UPOWER_H
#define UPOWER_H

#include "common.h"
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include "device.h"

struct power{
  float voltage;
  float current;
  float wattage;
  float temp;
  float accumulate;
  float freq;
  int state;
};

#define FULL_MODE_BCV NUM_MAX_CELL*365
#define FULL_MODE_FCV NUM_MAX_CELL*345
#define FULL_MODE_BVR NUM_MAX_CELL*338
#define STORAGE_MODE_BCV NUM_MAX_CELL*340
#define STORAGE_MODE_FCV NUM_MAX_CELL*330
#define STORAGE_MODE_BVR NUM_MAX_CELL*320


enum switchType{
    INVERTER, 
    BYPASS,//Turn On/Off Bypass Power
    SOLAR_CHARGE,//Turn On/Off Solar charge
    GRID_CHARGE,//Turn On/Off Grid charge
    STORAGE_MODE,//Turn On : For Battery cell life, Limit charge percentage up to 70%, Turn Off : Full charge
    NUM_OF_SWITCH
};

class Upower  : public Device { 
    private: 
        struct power pv_in = {};
        struct power pv_out = {};
        struct power inverter_out = {};
        struct power inverter_in = {};
        struct power grid_in = {};
        struct power grid_out = {};
        struct power bypass = {};
        struct power battery = {};
        unsigned char getUpowerStateFrom(int addr, int size, uint16_t *buf);
        const char* device_domain = "upower";
    public: 
        bool switch_state[NUM_OF_SWITCH];

//        Upower(PubSubClient *_mqttClient, ModbusMaster *_modbus, SoftwareSerial *_serial);
        Upower(PubSubClient *_mqttClient, ModbusMaster *_modbus);
        
        virtual int publish_switch();
        virtual int publish_data();
        
        virtual int update_data();//updateRealtimeState
        virtual int update_switch();//updateSwitchState

        virtual int change_switch(const char* switch_name, const char* onoff);//putSwitchState
        virtual const char* getDeviceName();
        virtual int setup_entity();
};

#endif
