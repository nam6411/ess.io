#include "ds1603da.h"
#include "common.h"
#include <ModbusMaster.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

DS1603DA::DS1603DA(PubSubClient *_mqttClient, ModbusMaster *_modbus, SoftwareSerial *_serial, int _slaveID){
  slaveID = _slaveID;
  mqttClient = _mqttClient;

	modbus = _modbus;
	serial = _serial;

  subscribe_size = 0;
  
  int levelerId = slaveID - 0xb0;
  sprintf(topic_sensor, "homeassistant/sensor/ds1603da/gas%d/config\0", levelerId);
  sprintf(topic_state, "homeassistant/sensor/ds1603da/gas%d/state\0", levelerId);
  sprintf(uniqid, "gas%d\0", levelerId);
  sprintf(entityname, "Gas%d Level\0", levelerId);

  isSuccess = false;

  comm_info.rx_pin=13;
  comm_info.tx_pin=12;
  comm_info.baudrate=9600;
  comm_info.slaveID=slaveID;
}

int DS1603DA::setup_entity(){
  

  char msgbuf[1024];
	mqtt_publish (topic_sensor, "");
  mqtt_publish (topic_sensor, assemble_discover_sensor_message(uniqid, entityname, "mm", "power", topic_state, "level", msgbuf));

  return 0;
}

int DS1603DA::publish_switch(){
  return 0;
}
int DS1603DA::publish_data(){

  if(!isSuccess){
    return 0;
  }

  char buf[1024];
  DynamicJsonDocument levelData(1024);
  levelData["level"] = gas_state.level;
  levelData["percentage"] = gas_state.percentage;
  
  serializeJson(levelData, buf);
  
  return mqtt_publish(topic_state, buf);
}

int DS1603DA::update_data(){
  prepareSerial();
  prepareModbus();
  int nResult = modbus->readHoldingRegisters(0x00, 0x02);
  if(nResult == modbus->ku8MBSuccess ){
    gas_state.level = modbus->getResponseBuffer(0);
    isSuccess = true;
    Serial.printf("Success request on %04X~%04X : RES(%03d)\n", 0x00, 0x02, nResult);
  }else{
    Serial.printf("Failed request on %04X~%04X : ERR(%03d)\n", 0x00, 0x02, nResult);
  }
  return nResult;
}

int DS1603DA::update_switch(){
    return 0;
}

int DS1603DA::change_switch(const char* switch_name, const char* onoff){  
  return 0;
}

const char* DS1603DA::getDeviceName(){
  return "ds1603da";
}