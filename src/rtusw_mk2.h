#include "common.h"
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <stdarg.h>
#include "device.h"

class RtuSwMk2  : public Device { 
    private: 
        const char* device_domain = "rtusw_mk2";
        bool switch_state[NUM_RTU_MK2_SW+1];

        int slaveID;
        int numOfSW;

        char* role[NUM_RTU_MK2_SW+1];
        bool isSuccess;
    public: 
        RtuSwMk2(PubSubClient *_mqttClient, ModbusMaster *_modbus, SoftwareSerial *_serial, int _slaveID, int _numOfSW, ...);
        
        virtual int publish_switch();
        virtual int publish_data();
        
        virtual int update_data();//updateRealtimeState
        virtual int update_switch();//updateSwitchState

        virtual int change_switch(const char* switch_name, const char* onoff);//putSwitchState
        virtual const char* getDeviceName();
        virtual int setup_entity();
};
