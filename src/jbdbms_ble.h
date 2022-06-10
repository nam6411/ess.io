#ifndef JBDBMS_BLE_H
#define JBDBMS_BLE_H

#include "common.h"
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include "device.h"
#include <BLEDevice.h>


class Jbdbms_ble  : public Device { 
    private: 
        float fVoltageAll = 0;
        int bms_soc = 0;
        int charge_mosfet_status;
        int discharge_mosfet_status;
        const char* charge_mosfet_status_describe;
        const char* discharge_mosfet_status_describe;
        const char* balance_status_describe;
        char bms_data[140] = {0,};
        float cell_data[30] = {0,};
        float cell_resi[30] = {0,};
        float temp[10] = {0,};
        float battery_capacity = 0, battery_current = 0;
        float temp_mosfet = 0, temp_balance = 0, temp_cell1 = 0, temp_cell5 = 0 ;
        float cell_diff = 0;
        int clearSerial();
        int flushBMSSerial();
        // int get_param(char addr);
        // int set_param(char addr, int data);
        // int check_sleep_mode();
        bool slow_charge = false;
        bool sleep_mode = false;
        float battery_full_capacity = 0;
        float battery_all_voltage = 0;
        float battery_cycle = 0;
        uint32_t battery_running_seconds = 0;
        int cell_series = 0;
        int number_of_temp_sensor = 0;
   
        BLERemoteCharacteristic *pRemoteCharacteristic;
        BLEAdvertisedDevice *myDevice;
        BLERemoteService *pRemoteService;
        // The remote service we wish to connect to. Needs check/change when other BLE module used.
        String BLE_SERVICE_UUID;
        String BLE_TX_UUID;
        String BLE_RX_UUID;

        boolean doConnect = false;
        boolean BLE_client_connected = false;
        boolean doScan = false;
        bool newPacketReceived = false;
        bool connect_to_ble_server();
        void bleStartup();
        boolean sendCommand(uint8_t *data, uint32_t dataLen);
        static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify);
        BLEUUID serviceUUID;
        BLEUUID charUUID_tx;
        BLEUUID charUUID_rx;


    public: 
        // Jbdbms_ble(PubSubClient *_mqttClient, SoftwareSerial *_serial, int slave_id, int _cell_series);
        Jbdbms_ble(PubSubClient *_mqttClient, String _BLE_SERVICE_UUID, String _BLE_TX_UUID, String _BLE_RX_UUID, int slave_id, int _cell_series);
        virtual int publish_switch();
        virtual int publish_data();
        
        virtual int update_data();//updateRealtimeState
        virtual int update_switch();//updateSwitchState

        virtual int change_switch(const char* switch_name, const char* onoff);//putSwitchState
        virtual const char* getDeviceName();
        void parseBasicData(char* bms_data, int size);
        void parseVoltageData(char* bms_data, int size);
        virtual int setup_entity();
        int asking(char* requestMessage, int sent_size, int min_length);
        int check_sleep_mode();
        int set_charge_mosfet(bool val);
        int set_discharge_mosfet(bool val);
        int checksumCalc(char* buffer_data, int size);
};

#endif
