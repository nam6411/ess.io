#include "common.h"
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <stdarg.h>
#include "device.h"

class RtuSwMk1  : public Device { 
    private: 
        const char* device_domain = "rtusw_mk1";

        int slaveID;
        int numOfSW;
        
        char* role[NUM_RTU_MK1_SW+1];
        bool isSuccess;
    public: 
//        RtuSwMk1(PubSubClient *_mqttClient, ModbusMaster *_modbus, SoftwareSerial *_serial, int _slaveID, int _numOfSW, ...);
        RtuSwMk1(PubSubClient *_mqttClient, ModbusMaster *_modbus, int _slaveID, int _numOfSW, ...);
        bool switch_state[NUM_RTU_MK1_SW+1] = {0,};

        bool getSwitchState(int num);

        virtual int publish_switch();
        virtual int publish_data();
        
        virtual int update_data();//updateRealtimeState
        virtual int update_switch();//updateSwitchState

        virtual int change_switch(const char* switch_name, const char* onoff);//putSwitchState
        virtual const char* getDeviceName();
        virtual int setup_entity();
};
