#ifndef DEVICE_H
#define DEVICE_H

#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>


struct comm_info_t{
    int rx_pin;
    int tx_pin;
    int baudrate;
    int slaveID;
};

class Device  {
    public: 
        Device();
        int subscribe();
        
        comm_info_t getCommInfo();

        virtual int publish_switch() = 0;
        virtual int publish_data() = 0;
        
        virtual int update_data() = 0;//updateRealtimeState
        virtual int update_switch() = 0;//updateSwitchState

        virtual int change_switch(const char* switch_name, const char* onoff) = 0;//putSwitchState
        
        virtual int setup_entity() = 0;

        virtual const char* getDeviceName() = 0;

        void setup_mqtt();
        bool mqtt_publish(String topic, String message, bool retain=false);
        
        void prepareSerial();
        void prepareModbus();
//        bool *switch_state = 0;


    protected :
        char* subscribe_list[20];
        int subscribe_size;
        PubSubClient *mqttClient;
        ModbusMaster *modbus;
        SoftwareSerial *serial;
        comm_info_t comm_info = {0,};
        char device_id[7];
        char device_name[20];
        char call_name[20];
        String device_sensor_root;
        String device_switch_root;
        char* assemble_device(char* buf);
        char* assemble_discover_switch_message(char* uniq_id, char* name, char* state_topic, char* command_topic, char* buf);
        char* assemble_discover_sensor_message(char* uniq_id, char* name, char* unit, char* device_class, char* state_topic, char* value_json, char* buf);
        
        String assemble_device();
        String assemble_discover_switch_message(String uniq_id, String name, String state_topic, String command_topic);
        String assemble_discover_sensor_message(String uniq_id, String name, String unit, String device_class, String state_topic, String value_json);

        int mqtt_register(const char *name, const char *device_class);
        String getFullTopic(char* topic, char* topic_end);
        String getSwitchTopicName(char* topic, char* topic_end);
        String getSensorTopicName(char* topic, char* topic_end);

        int mqtt_register(String name, String device_class);
        String getFullTopic(String topic, String topic_end);
        String getSwitchTopicName(String topic, String topic_end);
        String getSensorTopicName(String topic, String topic_end);

};

#endif
