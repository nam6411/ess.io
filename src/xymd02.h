#ifndef XYMD02_H
#define XYMD02_H

#include "common.h"
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include "device.h"

struct humidity{
  float temperature;
  float humidity;
};

#define ZSCORE_LAG 5
#define ZSCORE_THRESHOLD 3.5
#define ZSCORE_INFLUENCE 0.5

class XYMD02 : public Device { 
    private: 
        struct humidity humidity_state;
        int slaveID;//0xb1

        char topic_state[55];

        char topic_sensor1[55];
        char uniqid1[5];
        char entityname1[20];
        
        char topic_sensor2[55];
        char uniqid2[5];
        char entityname2[20];

        int zscore_index_humi;
        float zscore_data_humi[ZSCORE_LAG];
        float zscore_filter_humi;

        int zscore_index_temp;
        float zscore_data_temp[ZSCORE_LAG];
        float zscore_filter_temp;

        int zscore_status;

        bool isSuccess;

        int zscore(float value, float* zscore_data, float* zscore_filter, int* zscore_index);

    public: 
        #ifdef ESP8266
        XYMD02(PubSubClient *mqttClient, ModbusMaster *_modbus, SoftwareSerial *_serial, int _slaveId);
        #else
        XYMD02(PubSubClient *mqttClient, ModbusMaster *_modbus, int _slaveId);
        #endif
        virtual int publish_switch();
        virtual int publish_data();
        
        virtual int update_data();//updateRealtimeState
        virtual int update_switch();//updateSwitchState

        virtual int change_switch(const char* switch_name, const char* onoff);//putSwitchState
        virtual const char* getDeviceName();
        virtual int setup_entity();
};

#endif
