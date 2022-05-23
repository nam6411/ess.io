#include "common.h"
#include "device.h"
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif
Device::Device(){
  #if defined(ESP8266)
  sprintf(device_id, "%06X", ESP.getChipId());
  #elif defined(ESP32)
  uint32_t id = 0;
  for(int i=0; i<17; i=i+8) {
    id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  sprintf(device_id, "%06X", id);
  #endif
  
  
}

bool Device::mqtt_publish(String topic, String message, bool retain){
//  bool Device::mqtt_publish(const char* topic, const char* message, bool retain){
//  Serial.printf("%s -> %s %d\n", topic, message, retain);
  Serial.println(topic + "-> " + message + " retain : " +retain);
  delay(100);
  if(mqttClient->connected()){
    return mqttClient->publish((char*)topic.c_str(), (char*)message.c_str(), retain);
  }else{
    Serial.printf("mqtt not connected\n");
    Serial.printf("mqtt not connected\n");
    ESP.restart();
    return 0;
  }
  
}
int Device::subscribe(){
    Serial.printf("subscribe size : %d\n", subscribe_size);
    for(int i = 0 ; i < subscribe_size ; i++){
      Serial.print("subscribe : ");
        mqttClient->subscribe(subscribe_list[i]);
      Serial.println(subscribe_list[i]);
      mqttClient->subscribe(subscribe_list[i]);
    }
    return 0;
}

comm_info_t Device::getCommInfo(){
  return comm_info;
};

void Device::prepareSerial(){
  // serial->begin(comm_info.baudrate);
  if(serial->baudRate() != comm_info.baudrate){
    Serial.printf("Change baudrate : %d\n", comm_info.baudrate);
    // serial->begin(comm_info.baudrate);
    serial->begin(comm_info.baudrate, SWSERIAL_8N1, comm_info.rx_pin, comm_info.tx_pin, false, 256);
    delay(100);
    // serial->flush();
  }
};

void Device::prepareModbus(){
  Serial.printf("Change modbus slave ID : %d\n", comm_info.slaveID);
  modbus->begin(comm_info.slaveID, *serial);
  // modbus->setSlave(comm_info.slaveID);
  // modbus->clearResponseBuffer();
  // modbus->clearTransmitBuffer();
  // available
};

char* Device::assemble_device(char* buf){
//  char buf[1000];
  sprintf(buf, "{\"identifiers\":[\"%s\"],\"connections\":[[\"mac\",\"%s\"]],\"name\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\",\"manufacturer\":\"%s\"}", device_id, WiFi.macAddress().c_str(), "Battery", "ESP8266", CURRENT_VERSION, "nam6411");
  return buf;
}

char* Device::assemble_discover_switch_message(char* uniq_id, char* name, char* state_topic, char* command_topic, char* buf){

  char device_buf[1000];

  sprintf(buf, "{\"device\":%s, \"uniq_id\":\"%s_%s\",  \"name\": \"%s\", \"state_topic\": \"%s\", \"command_topic\": \"%s\" }", assemble_device(device_buf), device_id, uniq_id, name, state_topic, command_topic);
//   "{ \"uniq_id\":\"93069C_\",  \"name\": \"Inverter\", \"command_topic\": \"homeassistant/switch/epever/inverter/set\", \"state_topic\": \"homeassistant/switch/epever/inverter/state\"}"
  return buf;
}

char* Device::assemble_discover_sensor_message(char* uniq_id, char* name, char* unit, char* device_class, char* state_topic, char* value_json, char* buf){

 char device_buf[1000];

  sprintf(buf, "{\"device_class\": \"%s\", \"device\":%s, \"uniq_id\":\"%s_%s\",  \"name\": \"%s\", \"state_topic\": \"%s\", \"unit_of_measurement\": \"%s\", \"value_template\": \"{{ value_json.%s}}\" }", \
  device_class, assemble_device(device_buf), device_id, uniq_id, name, state_topic, unit, value_json);
  return buf;
}

String Device::assemble_device(){
  String retVal = "{\"identifiers\":[\""+String(device_id);
  retVal += +"\"],\"connections\":[[\"mac\",\""+String(WiFi.macAddress().c_str())
  +"\"]],\"name\":\""+"Battery"
  +"\",\"model\":\""+"ESP32"
  +"\",\"sw_version\":\""+CURRENT_VERSION
  +"\",\"manufacturer\":\""+"nam6411"
  +"\"}";
  return retVal;
}

String Device::assemble_discover_switch_message(String uniq_id, String name, String state_topic, String command_topic){
  String retVal = "{\"device\":"+assemble_device()
  +", \"uniq_id\":\""+String(device_id);
  retVal +="_"+uniq_id
  +"\",  \"name\": \""+name
  +"\", \"state_topic\": \""+state_topic
  +"\", \"command_topic\": \""+command_topic
  +"\" }";
  return retVal;
}

String Device::assemble_discover_sensor_message(String uniq_id, String name, String unit, String device_class, String state_topic, String value_json){

  String retVal = "{\"device_class\": \""+device_class
  +"\", \"device\":"+assemble_device()
  +", \"uniq_id\":\""+String(device_id);
  retVal +="_"+uniq_id
  +"\",  \"name\": \""+name
  +"\", \"state_topic\": \""+state_topic
  +"\", \"unit_of_measurement\": \""+unit
  +"\", \"value_template\": \"{{ value_json."+value_json
  +"}}\" }";
  
  return retVal;
}



int Device::mqtt_register(const char *name, const char *device_class){
    //name = "Discharge Voltage"
    //topic_name = "discharge_voltage"
    //uniq_name = "jbdbms_01_discharge_voltage"
    String unit;
    bool isSensor = true;
    
    String topic;
    String message;
    String entity;
    String topic_type;
    char device[256];

    char device_no[5];
    sprintf(device_no, "%d", comm_info.slaveID);
    

    DynamicJsonDocument doc(1024);
    DynamicJsonDocument device_property(256);
    
    Serial.printf("device_class : %s %d\n", device_class, strcmp(device_class, "power"));

    if(strcmp(device_class, "power") == 0){
        unit = "W";
    }else if(strcmp(device_class, "current") == 0){
        unit = "A";
    }else if(strcmp(device_class, "voltage") == 0){
        unit = "V";
    }else if(strcmp(device_class, "energy") == 0){
        unit = "Ah";
    }else if(strcmp(device_class, "temperature") == 0){
        unit = "℃";
    }else if(strcmp(device_class, "humidity") == 0){
        unit = "%";
    }else if(strcmp(device_class, "battery") == 0){
        unit = "%";
    }else if(strcmp(device_class, "ohm") == 0){
        unit = "Ω";
    }else if(strcmp(device_class, "switch") == 0){
        unit = "";
        isSensor = false;
    }else{
        unit = "";
    }
    
    entity = String(name);
    entity.replace(' ', '_');
    entity.toLowerCase();
    Serial.println(entity);
    Serial.println(device_id);
    

    device_property["identifiers"][0] = device_id;
    device_property["connections"][0][0] = "mac";
    device_property["connections"][0][1] = WiFi.macAddress();
    device_property["name"] = "Battery";
    device_property["model"] = "ESP8266";
    device_property["sw_version"] = CURRENT_VERSION;
    device_property["manufacturer"] = "nam6411";
    
    doc["device"]=device_property;
    doc["uniq_id"]  = entity + "_" + device_no + "_" + device_id;
    doc["name"] = name;

    if(isSensor){
        topic_type = "sensor";
        doc["device_class"]=device_class;
        doc["unit_of_measurement"]=unit;
        doc["value_template"]=String("{{ value_json.")+ entity +"}}";
        doc["state_topic"]=String("homeassistant/") + topic_type + "/" + device_name + "_" + device_no + "/" + device_name +"/state";
    }else{
        topic_type = "switch";
        doc["command_topic"]=String("homeassistant/") + topic_type + "/" + device_name + "_" + device_no +"/" + entity + "/set";
        doc["state_topic"]=String("homeassistant/") + topic_type + "/" + device_name + "_" + device_no +"/" + entity + "/state";
    }

    //serialize topic
    topic = String("homeassistant/") + topic_type + "/" + device_name + "_" + device_no +"/" + entity +"/config";

    //serialize message
    serializeJson(doc, message);

    mqtt_publish (topic.c_str(), "", true);
    mqtt_publish (topic.c_str(), message.c_str(), true);
    
    return 0;
    
}



int Device::mqtt_register(String name, String device_class){
    //name = "Discharge Voltage"
    //topic_name = "discharge_voltage"
    //uniq_name = "jbdbms_01_discharge_voltage"
    String unit;
    bool isSensor = true;
    
    String topic;
    String message;
    String entity;
    String topic_type;
    char device[256];

    char device_no[5];
    sprintf(device_no, "%d", comm_info.slaveID);
    

    DynamicJsonDocument doc(1024);
    DynamicJsonDocument device_property(256);
    
    Serial.println("device_class : " + device_class + " " + device_class.equals("power"));

    if(device_class.equals("power")){
        unit = "W";
    }else if(device_class.equals("current")){
        unit = "A";
    }else if(device_class.equals("voltage")){
        unit = "V";
    }else if(device_class.equals("energy")){
        unit = "Ah";
    }else if(device_class.equals("temperature")){
        unit = "℃";
    }else if(device_class.equals("humidity")){
        unit = "%";
    }else if(device_class.equals("battery")){
        unit = "%";
    }else if(device_class.equals("ohm")){
        unit = "Ω";
    }else if(device_class.equals("switch")){
        unit = "";
        isSensor = false;
    }else{
        unit = "";
    }
    
    entity = name;
    entity.replace(' ', '_');
    entity.toLowerCase();
    Serial.println(entity);
    Serial.println(device_id);
    

    device_property["identifiers"][0] = device_id;
    device_property["connections"][0][0] = "mac";
    device_property["connections"][0][1] = WiFi.macAddress();
    device_property["name"] = "Battery";
    device_property["model"] = "ESP8266";
    device_property["sw_version"] = CURRENT_VERSION;
    device_property["manufacturer"] = "nam6411";
    
    doc["device"]=device_property;
    doc["uniq_id"]  = entity + "_" + device_no + "_" + device_id;
    doc["name"] = name;

    if(isSensor){
        topic_type = "sensor";
        doc["device_class"]=device_class;
        doc["unit_of_measurement"]=unit;
        doc["value_template"]=String("{{ value_json.")+ entity +"}}";
        doc["state_topic"]=String("homeassistant/") + topic_type + "/" + device_name + "_" + device_no + "/" + device_name +"/state";
    }else{
        topic_type = "switch";
        doc["command_topic"]=String("homeassistant/") + topic_type + "/" + device_name + "_" + device_no +"/" + entity + "/set";
        doc["state_topic"]=String("homeassistant/") + topic_type + "/" + device_name + "_" + device_no +"/" + entity + "/state";
    }

    //serialize topic
    topic = String("homeassistant/") + topic_type + "/" + device_name + "_" + device_no +"/" + entity +"/config";

    //serialize message
    serializeJson(doc, message);

    mqtt_publish (topic.c_str(), "", true);
    mqtt_publish (topic.c_str(), message.c_str(), true);
    
    return 0;
    
}



String Device::getFullTopic(char* topic, char* topic_end){
    String retVal;
    retVal = String("homeassistant/switch/") + device_name + "_" + device_name + "_" + comm_info.slaveID +"_"+topic + "/" + topic_end;
    return retVal;
}

String Device::getSensorTopicName(char* topic, char* topic_end){
    String retVal;
    retVal = String("homeassistant/sensor/") + device_name + "_" + comm_info.slaveID + "/" + device_name + "/" + topic_end;
    return retVal;
}

String Device::getSwitchTopicName(char* topic, char* topic_end){
    String retVal;
    retVal = String("homeassistant/switch/") + device_name + "_" + comm_info.slaveID +"/"+topic + "/" + topic_end;
    return retVal;
}




String Device::getFullTopic(String topic, String topic_end){
    String retVal;
    retVal = String("homeassistant/switch/") + device_name + "_" + device_name + "_" + comm_info.slaveID +"_"+topic + "/" + topic_end;
    return retVal;
}

String Device::getSensorTopicName(String topic, String topic_end){
    String retVal;
    retVal = String("homeassistant/sensor/") + device_name + "_" + comm_info.slaveID + "/" + device_name + "/" + topic_end;
    return retVal;
}

String Device::getSwitchTopicName(String topic, String topic_end){
    String retVal;
    retVal = String("homeassistant/switch/") + device_name + "_" + comm_info.slaveID +"/"+topic + "/" + topic_end;
    return retVal;
}
