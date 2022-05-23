#include "xymd02.h"
#include "common.h"
#include <ModbusMaster.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>




int XYMD02::zscore(float value, float* zscore_data, float* zscore_filter, int* zscore_index){
    float avg = 0;
    float std = 0;
    int signal = 0;

    for(int i = 0 ; i < ZSCORE_LAG ; i++){
        if (zscore_data[i] == -128.0f) {
            zscore_data[*zscore_index] = value;
            *zscore_index = (*zscore_index + 1) % ZSCORE_LAG;
            return 0;
        }
        avg += zscore_data[i];
    }
    avg = avg / ZSCORE_LAG;

    for(int i = 0 ; i < ZSCORE_LAG ; i++){
        std += (zscore_data[i] - avg) * (zscore_data[i] - avg);
    }
    std = (float)sqrt((double)std) / ZSCORE_LAG;
    
    if (abs(value - avg) > ZSCORE_THRESHOLD * std){
        if (value - avg > 0){
            signal = 1;
        } else {
            signal = -1;
        }

        *zscore_filter = value * ZSCORE_INFLUENCE + *zscore_filter*(1-ZSCORE_INFLUENCE);
    } else {
        signal = 0;
        *zscore_filter = value;
    }

    zscore_data[*zscore_index] = value;
    *zscore_index = (*zscore_index + 1) % ZSCORE_LAG;
    return signal;
}


XYMD02::XYMD02(PubSubClient *_mqttClient, ModbusMaster *_modbus, SoftwareSerial *_serial, int _slaveID){
  slaveID = _slaveID;
  mqttClient = _mqttClient;

	modbus = _modbus;
	serial = _serial;

  subscribe_size = 0;
  
  int levelerId = slaveID - 0xb3;
  sprintf(topic_sensor1, "homeassistant/sensor/xymd02/humidity%d/config\0", levelerId);
  sprintf(topic_sensor2, "homeassistant/sensor/xymd02/temperature%d/config\0", levelerId);

  sprintf(topic_state, "homeassistant/sensor/xymd02/humidity%d/state\0", levelerId);

  sprintf(uniqid1, "humidity%d\0", levelerId);
  sprintf(uniqid2, "temperature%d\0", levelerId);

  sprintf(entityname1, "humidity%d\0", levelerId);
  sprintf(entityname2, "temperature%d \0", levelerId);

  isSuccess = false;

  comm_info.rx_pin=13;
  comm_info.tx_pin=12;
  comm_info.baudrate=9600;
  comm_info.slaveID=slaveID;

  zscore_index_humi = 0;
  zscore_index_temp = 0;

  for(int i = 0 ; i < ZSCORE_LAG; i++){
    zscore_data_humi[i] = -128.0f;
    zscore_data_temp[i] = -128.0f;
  }
  zscore_filter_humi = 0.0f;
  zscore_filter_temp = 0.0f;

  zscore_status = 0;
}

int XYMD02::setup_entity(){
  
  // 0xb3 0x04 0x00 0x01 0x00 0x02
  char msgbuf[1024];
	mqtt_publish (topic_sensor1, "");
  mqtt_publish (topic_sensor2, "");
  mqtt_publish (topic_sensor1, assemble_discover_sensor_message(uniqid1, entityname1, "%", "power", topic_state, "humidity", msgbuf));
  mqtt_publish (topic_sensor2, assemble_discover_sensor_message(uniqid2, entityname2, "℃", "power", topic_state, "temperature", msgbuf));


  mqtt_publish ("homeassistant/sensor/xymd02/humidity0/config", assemble_discover_sensor_message("humidity0", "humidity0", "%", "power", "homeassistant/sensor/xymd02/humidity0/state", "humidity", msgbuf));
  mqtt_publish ("homeassistant/sensor/xymd02/temperature0/config", assemble_discover_sensor_message("temperature0", "temperature0 ", "℃", "power", "homeassistant/sensor/xymd02/humidity0/state", "temperature", msgbuf));

  return 0;
}

int XYMD02::publish_switch(){
  return 0;
}
int XYMD02::publish_data(){

  if(!isSuccess){
    return 0;
  }

  if(!zscore_status){
    return 0;
  }

  char buf[1024];
  DynamicJsonDocument levelData(1024);
  levelData["temperature"] = humidity_state.temperature;
  levelData["humidity"] = humidity_state.humidity;
  
  serializeJson(levelData, buf);
  
  return mqtt_publish(topic_state, buf);
}

int XYMD02::update_data(){
  prepareSerial();
  prepareModbus();
  int nResult = modbus->readInputRegisters(0x01, 0x02);
  if(nResult == modbus->ku8MBSuccess ){

    humidity_state.temperature = (float)((int16_t)modbus->getResponseBuffer(0))/10.0f;
    humidity_state.humidity = (float)((int16_t)modbus->getResponseBuffer(1))/10.0f;

    int zscore_status_temp = zscore(humidity_state.temperature, zscore_data_temp, &zscore_filter_temp, &zscore_index_temp);
    int zscore_status_humi = zscore(humidity_state.humidity, zscore_data_humi, &zscore_filter_humi, &zscore_index_humi);

    if(zscore_status_temp != 0 && zscore_status_humi != 0){
      zscore_status = 0;
    }else{
      zscore_status = 1;
    }

    isSuccess = true;
    Serial.printf("Success request on %04X~%04X : RES(%03d)\n", 0x01, 0x02, nResult);
  }else{
    Serial.printf("Failed request on %04X~%04X : ERR(%03d)\n", 0x01, 0x02, nResult);
  }
  return nResult;
}

int XYMD02::update_switch(){
    return 0;
}

int XYMD02::change_switch(const char* switch_name, const char* onoff){  
  return 0;
}

const char* XYMD02::getDeviceName(){
  return "xymd02";
}
