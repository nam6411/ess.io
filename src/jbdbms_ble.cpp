#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include "jbdbms_ble.h"
#include "common.h"


class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{ //this is called by some underlying magic
    /**
    * Called for each advertising BLE server.
    */
   public : 
    MyAdvertisedDeviceCallbacks(BLEUUID* _serviceUUID, BLEAdvertisedDevice* _myDevice, boolean* _doConnect, boolean* _doScan){
        serviceUUID = _serviceUUID;
        doConnect = _doConnect;
        doScan = _doScan;
        myDevice = _myDevice;
    }
    private : 
    BLEUUID* serviceUUID;
    boolean* doConnect;
    boolean* doScan;
    BLEAdvertisedDevice* myDevice;


    void onResult(BLEAdvertisedDevice advertisedDevice)
    {
        Serial.print("BLE Advertised Device found: ");
        Serial.println(advertisedDevice.toString().c_str());

        // We have found a device, let us now see if it contains the service we are looking for.
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(*serviceUUID))
        {
            BLEDevice::getScan()->stop();
            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            memset(doConnect, true, 1);
            memset(doScan, true, 1);

        } // Found our server
    }     // onResult
};        // MyAdvertisedDeviceCallbacks
class MyClientCallback : public BLEClientCallbacks
{ //this is called on connect / disconnect by some underlying magic+

    public : 
    MyClientCallback(boolean* _BLE_client_connected){
        BLE_client_connected = _BLE_client_connected;
    }

    private : 
    boolean* BLE_client_connected;
    void onConnect(BLEClient *pclient)
    {
    }

    void onDisconnect(BLEClient *pclient)
    {
        memset(BLE_client_connected, false, 1);
        Serial.println("onDisconnect");
    }
};

static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) //this is called when BLE server sents data via notofication
{
    //hexDump((char*)pData, length);
    // bleCollectPacket((char *)pData, length);
    //response
}


void Jbdbms_ble::bleStartup()
{
    // TRACE;
    BLEDevice::init("");

    // Retrieve a Scanner and set the callback we want to use to be informed when we
    // have detected a new device.  Specify that we want active scanning and start the
    // scan to run for 5 seconds.
    BLEScan *pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(&serviceUUID, myDevice, &doConnect, &doScan));
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(5, true);
}

bool Jbdbms_ble::connect_to_ble_server(){
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    BLEClient *pClient = BLEDevice::createClient();
    Serial.println(" - Created client");
    pClient->setClientCallbacks(new MyClientCallback(&BLE_client_connected));

    // Connect to the remove BLE Server.
    pClient->connect(myDevice); // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");
    // Obtain a reference to the service we are after in the remote BLE server.
    //BLERemoteService*
    pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr)
    {
        Serial.print("Failed to find our service UUID: ");
        Serial.println(serviceUUID.toString().c_str());
        pClient->disconnect();
        return false;
    }
    Serial.println(" - Found our service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID_rx);
    if (pRemoteCharacteristic == nullptr)
    {
        Serial.print("Failed to find our characteristic UUID: ");
        Serial.println(charUUID_rx.toString().c_str());
        pClient->disconnect();
        return false;
    }
    Serial.println(" - Found our characteristic");
    // Read the value of the characteristic.
    if (pRemoteCharacteristic->canRead())
    {
        std::string value = pRemoteCharacteristic->readValue();
        Serial.print("The characteristic value was: ");
        Serial.println(value.c_str());
    }

    if (pRemoteCharacteristic->canNotify())
        pRemoteCharacteristic->registerForNotify(notifyCallback);

    BLE_client_connected = true;
}


void Jbdbms_ble::sendCommand(uint8_t *data, uint32_t dataLen)
{
    // TRACE;

    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID_tx);

    if (pRemoteCharacteristic)
    {
        pRemoteCharacteristic->writeValue(data, dataLen);
        //commSerial.println("bms request sent");
    }
    else
    {
        Serial.println("Remote TX characteristic not found");
    }
}

Jbdbms_ble::Jbdbms_ble(PubSubClient *_mqttClient, String _BLE_SERVICE_UUID, 
    String _BLE_TX_UUID, 
    String _BLE_RX_UUID, int slave_id, int _cell_series){

    BLE_SERVICE_UUID = _BLE_SERVICE_UUID;
    BLE_TX_UUID = _BLE_TX_UUID;
    BLE_RX_UUID = _BLE_RX_UUID;
    
    serviceUUID = BLEUUID(BLE_SERVICE_UUID.c_str()); //xiaoxiang bms original module
    charUUID_tx = BLEUUID(BLE_TX_UUID.c_str()); //xiaoxiang bms original module
    charUUID_rx = BLEUUID(BLE_RX_UUID.c_str()); //xiaoxiang bms original module
    

    mqttClient = _mqttClient;

    strcpy(device_name, "jbdbms");
    sprintf(call_name, "jbdbms_%d", slave_id);
    
    comm_info.slaveID=slave_id;

    subscribe_size = 3;
    subscribe_list[0] = (char*)malloc(sizeof(char)*60);
    subscribe_list[1] = (char*)malloc(sizeof(char)*60);
    subscribe_list[2] = (char*)malloc(sizeof(char)*60);
    strcpy(subscribe_list[0], (char*)getSwitchTopicName("charge_mosfet", "set").c_str());
    strcpy(subscribe_list[1], (char*)getSwitchTopicName("discharge_mosfet", "set").c_str());
    strcpy(subscribe_list[2], (char*)getSwitchTopicName("sleep_mode", "set").c_str());

    fVoltageAll = 0.0;
    bms_soc = 0;
    cell_data[30] = {0.0, };
    cell_resi[30] = {0.0, };
    temp[10] = {0.0, };
    battery_capacity = 0.0;
    battery_current = 0.0;
    
    // comm_info.rx_pin=5;
    // comm_info.tx_pin=16;
//    comm_info.rx_pin=rx_pin;
//    comm_info.tx_pin=tx_pin;
//    comm_info.baudrate=9600;
    
    charge_mosfet_status = 0;
    discharge_mosfet_status = 0;
    sleep_mode = false;

    cell_series = _cell_series;

//    serial->begin(comm_info.baudrate, SWSERIAL_8N1, comm_info.rx_pin, comm_info.tx_pin, false, 256);

    // setup_entity();

}


int Jbdbms_ble::setup_entity(){
    mqtt_register("Discharge Voltage", "voltage");
    mqtt_register("Discharge Current", "current");
    mqtt_register("Discharge Wattage", "energy");

    mqtt_register("Capacity", "energy");
    mqtt_register("SoC", "battery");

    mqtt_register("Cycle", "energy");

    mqtt_register("Cell Diff", "voltage");

    mqtt_register("Charge Mosfet", "switch");
    mqtt_register("Discharge Mosfet", "switch");
    mqtt_register("Sleep Mode", "switch");

	for(int i = 1 ; i <= cell_series; i++){
    mqtt_register(String("Cell ")+i, "voltage");
    mqtt_register(String("Cell Resist ")+i, "voltage");
	}
 return 0;
}



int Jbdbms_ble::clearSerial(){
	int retVal = 0;
	while(serial->available()){
		Serial.printf("%02x ", serial->read());
		retVal++;
	}
	return retVal;
}

int Jbdbms_ble::flushBMSSerial(){
	int size=0;
	while(serial->available()){
		serial->read();
		size++;
	}
	return size;
}
int Jbdbms_ble::checksumCalc(char* buffer_data, int size){
    //체크섬 계산

    //1차 헤더 체크
	if(!(buffer_data[0] == 0xDD && buffer_data[size-1] == 0x77)){
        Serial.printf("\nBMS Header not matching : %d\n",size);
        return -1;
    }

    //2차 헤더 체크
    if(!(buffer_data[2] == 0x0)){
        Serial.printf("\nBMS Header not matching : %d\n",size);
        return -1;
    }

    //길이 체크
    if(buffer_data[3] != size-7){
        Serial.printf("\nBMS Header not matching : %d\n",size);
        return -1;
    }
    
	int checksum = 65536;

    for(int i = 0 ; i < size ; i++){
        // Serial.printf("%x ", buffer_data[i]);
        if( i > 1 && i < size-3){
            checksum -= buffer_data[i];
        }
    }
    // Serial.printf("\n");

    uint8_t calc_checksum_u = (uint8_t)(checksum >> 8);
    uint8_t calc_checksum_l = (uint8_t)(checksum     );

    // Serial.printf("%02X %02X (%04X)\n", calc_checksum_u, calc_checksum_l, checksum);

    uint8_t sent_checksum_u = buffer_data[size-3];
    uint8_t sent_checksum_l = buffer_data[size-2];

    //체크섬 체크
    if(sent_checksum_u == calc_checksum_u && sent_checksum_l == calc_checksum_l){
        return 0;
    }else{
        Serial.printf("\nBMS Checksum FAILED!!\n");
        return -1;
    }

}

void Jbdbms_ble::parseBasicData(char* bms_data, int size){


    fVoltageAll = 0.0f;

    battery_all_voltage = ((int)bms_data[4] << 8 | (int)bms_data[5])/100.0f;
    battery_current     = (int16_t)((int16_t)bms_data[6] << 8 | (int16_t)bms_data[7])/100.0f;
    battery_capacity    = ((int)bms_data[8] << 8 | (int)bms_data[9])/100.0f;
    battery_full_capacity = ((int)bms_data[10] << 8 | (int)bms_data[11])/100.0f;
    battery_cycle       = ((int)bms_data[12] << 8 | (int)bms_data[13]);
    bms_soc = (int)bms_data[23];

    discharge_mosfet_status = (int)((bms_data[24] >> 1 ) & 1);
    charge_mosfet_status = (int)(bms_data[24] & 1);

    // Serial.printf("orig %d and %d shift right %d  sft+and %d \n", bms_data[24], bms_data[24] & 1, bms_data[24] >> 1, bms_data[24] >> 1 & 1);

    // Serial.printf("chg %d dis %d \n", charge_mosfet_status, discharge_mosfet_status);

    number_of_temp_sensor = (int)bms_data[26];
    Serial.printf("number_of_temp_sensor : %d\n" + number_of_temp_sensor);
    
    for(int i = 0 ; i < number_of_temp_sensor ; i++){
        temp[i]         = (((int)bms_data[27+i*2] << 8 | (int)bms_data[28+i*2])-2731)/10;
    }
    
}


void Jbdbms_ble::parseVoltageData(char* bms_data, int size){
    
    //parse Cell voltage
    float min_voltage = 5.0f;
    float max_voltage = 0.0f;
    // fVoltageAll = 0.0f;
    int num_of_cell=bms_data[3];

    for(int i=1 ; i < cell_series+1 ; i++){
        float cell_voltage = ((int)bms_data[2+i*2] << 8 | (int)bms_data[3+i*2])/1000.0f;

        float cell_internal_resist = (cell_data[i] - cell_voltage) / battery_current * 1000;
        cell_resi[i] = cell_internal_resist;
        cell_data[i] = cell_voltage;

        if(min_voltage > cell_voltage){
            min_voltage = cell_voltage;
        }

        if(max_voltage < cell_voltage ){
            max_voltage = cell_voltage;
        }
    }

    cell_diff = max_voltage - min_voltage;
}
int Jbdbms_ble::asking(char* requestMessage, int sent_size, int min_length){
    
    //clear read buffer before request
	while(serial->available()){
		serial->read();
	}
	serial->write(requestMessage, sent_size);
	delay(100);

	int buffer_size = serial->readBytes(bms_data, 50);

    // Serial.printf("---------- sent : -----------\n");
    // for(int i = 0 ; i < sent_size; i++){
    //     if(i%5 == 0)
    //         Serial.printf("\n%d : ", i);
    //     Serial.printf("%02X ", requestMessage[i]);
    // }
    // Serial.printf("\n------------------");

    // Serial.printf("---------- got  : -----------\n");
    // for(int i = 0 ; i < buffer_size; i++){
    //     if(i%5 == 0)
    //         Serial.printf("\n%d : ", i);
    //     Serial.printf("%02X ", bms_data[i]);
    // }
    // Serial.printf("\n------------------");

    if( buffer_size < min_length){
        Serial.printf("min_length : %d, return length : %d\n" , min_length, buffer_size);
        return -3;
    }

    if(checksumCalc(bms_data, buffer_size) < 0){
        Serial.printf("checksum failed\n");
        return -4;
    }
    return buffer_size;
}

int Jbdbms_ble::update_data(){
//     Serial.println("getBMSAttribute");
	
	int cnt = 0;
    int buffer_size;
    char askBasic[] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};
    char askVoltage[] = {0xDD, 0xA5, 0x04, 0x00, 0xFF, 0xFC, 0x77};

    buffer_size = asking(askBasic, 7, 34);
    Serial.println(buffer_size);
    if(buffer_size > 0 ){
        parseBasicData(bms_data, buffer_size);
    }
    buffer_size = asking(askVoltage, 7, cell_series*2+7);
    Serial.println(buffer_size);
    if(buffer_size > 0 ){
        parseVoltageData(bms_data, buffer_size);
    }
    return 0;
}

int Jbdbms_ble::check_sleep_mode(){
    if(sleep_mode){
      Serial.println("sleep mode");
        //soc over 50% && not enabled slow_charged
        if(bms_soc > 50 && !charge_mosfet_status){
            //enable slow mode
            Serial.println("slow charge set");
            set_charge_mosfet(false);
        }
        //soc less than 50% && enabled slow_charged
        else if(bms_soc < 50 && slow_charge){
            //disable slow mode
            Serial.println("fast charge set");
            set_charge_mosfet(true);
        }
    }else{
        Serial.println("Not sleep mode");
        if(charge_mosfet_status){
            Serial.println("now slow mode, so set fast mode");
            set_charge_mosfet(true);
        }
    }
    return 0;
}

int Jbdbms_ble::update_switch(){
    return 0;
}

int Jbdbms_ble::publish_data(){
    char buf[1000];
    sprintf(buf, "{");
    sprintf(&buf[strlen(buf)], "\"discharge_voltage\":%2.2f,",battery_all_voltage);
    sprintf(&buf[strlen(buf)], "\"discharge_current\":%2.2f,",battery_current);
    sprintf(&buf[strlen(buf)], "\"discharge_wattage\":%3.2f,",battery_all_voltage * battery_current);
    sprintf(&buf[strlen(buf)], "\"capacity\":%3.2f,",battery_capacity);
    sprintf(&buf[strlen(buf)], "\"cycle\":\"%f\",", battery_cycle);
    for(int i = 1 ; i <= cell_series ; i++){
        sprintf(&buf[strlen(buf)], "\"cell_%d\":%1.3f,", i, cell_data[i]);
        // sprintf(&buf[strlen(buf)], "\"cell_resist_%d\":%1.3f,", i, cell_resi[i]);  
    }
    for(int i = 0 ; i < number_of_temp_sensor ; i++){
        sprintf(&buf[strlen(buf)], "\"bms_temp_%d\":%1.3f,", i, temp[i]); 
    }
    
    sprintf(&buf[strlen(buf)], "\"soc\":%d,", bms_soc);
    sprintf(&buf[strlen(buf)], "\"cell_diff\":%1.3f,", cell_diff);
    // Serial.println(buf);
    sprintf(&buf[strlen(buf)-1], "}");

    Serial.printf("%s\n", buf);

    mqtt_publish (getSensorTopicName("bms", "state"), String(buf));
    return 0;
}


int Jbdbms_ble::publish_switch(){
    
    String topic;
    
    topic = getSwitchTopicName("charge_mosfet", "state");
    if(charge_mosfet_status){
        mqtt_publish(topic.c_str(), "ON");//"homeassistant/switch/jbdbms/charge_mosfet/state"
    }else{
        mqtt_publish(topic.c_str(), "OFF");
    }
    
    topic = getSwitchTopicName("discharge_mosfet", "state");
    if(discharge_mosfet_status){
        mqtt_publish(topic.c_str(), "ON");
    }else{
        mqtt_publish(topic.c_str(), "OFF");
    }
    topic = getSwitchTopicName("sleep_mode", "state");
    if(sleep_mode){
        mqtt_publish(topic.c_str(), "ON");
    }else{
        mqtt_publish(topic.c_str(), "OFF");
    }
    return 0;
}

int Jbdbms_ble::set_discharge_mosfet(bool val){
    int mosfet_val = !val*2 + !charge_mosfet_status;
    char askMosfet[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, (char)mosfet_val, 0xFF, 0x1D-(char)mosfet_val, 0x77};
    asking(askMosfet, 9, 5);
    return 0;
}

int Jbdbms_ble::set_charge_mosfet(bool val){
    int mosfet_val = !discharge_mosfet_status*2 + !val;
    char askMosfet[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, (char)mosfet_val, 0xFF, 0x1D-(char)mosfet_val, 0x77};
    asking(askMosfet, 9, 5);
    return 0;
}

int Jbdbms_ble::change_switch(const char* switch_name, const char* onoff){
    
    if(strcmp(switch_name, "discharge_mosfet") == 0){
        set_discharge_mosfet((strcmp(onoff, "ON")==0));
    }else if(strcmp(switch_name, "charge_mosfet") == 0){
        set_charge_mosfet((strcmp(onoff, "ON")==0));
    }else if(strcmp(switch_name, "sleep_mode") == 0){
        sleep_mode = (strcmp(onoff, "ON")==0);
    }
    return 0;
}

const char* Jbdbms_ble::getDeviceName(){
    return call_name;
}
