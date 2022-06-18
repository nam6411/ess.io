#include "upower.h"
#include "common.h"
#include <ModbusMaster.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

double round_range(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

Upower::Upower(PubSubClient *_mqttClient, ModbusMaster *_modbus/*, SoftwareSerial *_serial*/){
  mqttClient = _mqttClient;

	modbus = _modbus;
//	serial = _serial;
  printf("modbus : %p", modbus);
  subscribe_size = 4 ;
	subscribe_list[0] = (char*)"homeassistant/switch/upower/inverter/set";
	subscribe_list[1] = (char*)"homeassistant/switch/upower/solar_charge/set";
	subscribe_list[2] = (char*)"homeassistant/switch/upower/grid_charge/set";
	subscribe_list[3] = (char*)"homeassistant/switch/upower/gridout_prio/set";
	//subscribe_list[4];' = (char*)"homeassistant/switch/upower/storage/set";
  // configuration();

  comm_info.rx_pin=13;
  comm_info.tx_pin=12;
  comm_info.baudrate=115200;
  comm_info.slaveID=10;
//  prepareSerial();
//  prepareModbus();
}

int Upower::setup_entity(){
  
	mqtt_publish ("homeassistant/switch/upower/inverter/config", "");
	mqtt_publish ("homeassistant/switch/upower/grid/config", "");
	mqtt_publish ("homeassistant/switch/upower/solar/config", "");
	mqtt_publish ("homeassistant/switch/upower/bypass/config", "");
// mqtt_publish ("homeassistant/switch/upower/storage/config", "");

	mqtt_publish ("homeassistant/sensor/upower/utility_charging_current/config", "");
	mqtt_publish ("homeassistant/sensor/upower/utility_charging_voltage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/utility_charging_wattage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/pv_charging_current/config", "");
	mqtt_publish ("homeassistant/sensor/upower/pv_charging_voltage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/pv_charging_wattage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/battery_temp/config", "");
	mqtt_publish ("homeassistant/sensor/upower/battery_soc/config", "");
	mqtt_publish ("homeassistant/sensor/upower/battery_voltage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/inverter_voltage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/inverter_current/config", "");
	mqtt_publish ("homeassistant/sensor/upower/inverter_wattage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/inverter_frequency/config", "");
	mqtt_publish ("homeassistant/sensor/upower/bypass_voltage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/bypass_current/config", "");
	mqtt_publish ("homeassistant/sensor/upower/bypass_wattage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/utility_voltage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/utility_current/config", "");
	mqtt_publish ("homeassistant/sensor/upower/utility_wattage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/pv_voltage/config", "");
	mqtt_publish ("homeassistant/sensor/upower/pv_current/config", "");
	mqtt_publish ("homeassistant/sensor/upower/pv_wattage/config", "");


  //remove lagacy
  mqtt_publish ("homeassistant/switch/epever/inverter/config", "");
  mqtt_publish ("homeassistant/switch/epever/grid_charge/config", "");
  mqtt_publish ("homeassistant/switch/epever/solar_charge/config", "");
  mqtt_publish ("homeassistant/switch/epever/gridout_prio/config", "");
  mqtt_publish ("homeassistant/sensor/epever/utility_charging_current/config", "");
  mqtt_publish ("homeassistant/sensor/epever/utility_charging_voltage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/utility_charging_wattage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/pv_charging_current/config", "");
  mqtt_publish ("homeassistant/sensor/epever/pv_charging_voltage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/pv_charging_wattage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/battery_temp/config", "");
  mqtt_publish ("homeassistant/sensor/epever/battery_soc/config", "");
  mqtt_publish ("homeassistant/sensor/epever/battery_voltage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/inverter_voltage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/inverter_current/config", "");
  mqtt_publish ("homeassistant/sensor/epever/inverter_wattage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/inverter_frequency/config", "");
  mqtt_publish ("homeassistant/sensor/epever/bypass_voltage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/bypass_current/config", "");
  mqtt_publish ("homeassistant/sensor/epever/bypass_wattage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/utility_voltage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/utility_current/config", "");
  mqtt_publish ("homeassistant/sensor/epever/utility_wattage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/pv_voltage/config", "");
  mqtt_publish ("homeassistant/sensor/epever/pv_current/config", "");
  mqtt_publish ("homeassistant/sensor/epever/pv_wattage/config", "");

  // char msgbuf[1024];


  mqtt_publish ("homeassistant/switch/upower/inverter/config", assemble_discover_switch_message("isw", "Inverter", "homeassistant/switch/upower/inverter/state", "homeassistant/switch/upower/inverter/set"), true);
	mqtt_publish ("homeassistant/switch/upower/solar_charge/config", assemble_discover_switch_message("ssw", "Solar Charge", "homeassistant/switch/upower/solar_charge/state", "homeassistant/switch/upower/solar_charge/set"), true);
	mqtt_publish ("homeassistant/switch/upower/grid_charge/config", assemble_discover_switch_message("gsw", "Grid Charge", "homeassistant/switch/upower/grid_charge/state", "homeassistant/switch/upower/grid_charge/set"), true);
	mqtt_publish ("homeassistant/switch/upower/gridout_prio/config", assemble_discover_switch_message("bsw", "Grid Output Priority", "homeassistant/switch/upower/gridout_prio/state", "homeassistant/switch/upower/gridout_prio/set"), true);
	//mqtt_publish ("homeassistant/switch/upower/storage/config", assemble_discover_switch_message("stsw", "Storage Mode", "homeassistant/switch/upower/storage/state", "homeassistant/switch/upower/storage/set"), true);
  
  mqtt_publish ("homeassistant/sensor/upower/grid_in_current/config", assemble_discover_sensor_message("gic", "Grid In Current", "A", "energy", "homeassistant/sensor/upower/grid/state", "inCurrent"), true);
  mqtt_publish ("homeassistant/sensor/upower/grid_in_wattage/config", assemble_discover_sensor_message("giw", "Grid In Wattage", "W", "energy", "homeassistant/sensor/upower/grid/state", "inWattage"), true);	
	mqtt_publish ("homeassistant/sensor/upower/grid_in_voltage/config", assemble_discover_sensor_message("giv", "Grid In Voltage", "V", "energy", "homeassistant/sensor/upower/grid/state", "inVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/grid_charge_current/config", assemble_discover_sensor_message("goc", "Grid Out Current", "A", "energy", "homeassistant/sensor/upower/grid/state", "outCurrent"), true);
	mqtt_publish ("homeassistant/sensor/upower/grid_charge_wattage/config", assemble_discover_sensor_message("gow", "Grid Out Wattage", "W", "energy", "homeassistant/sensor/upower/grid/state", "outWattage"), true);
  mqtt_publish ("homeassistant/sensor/upower/grid_charge_voltage/config", assemble_discover_sensor_message("gov", "Grid Out Voltage", "V", "energy", "homeassistant/sensor/upower/grid/state", "outVoltage"), true);
	mqtt_publish ("homeassistant/sensor/upower/grid_charge_accumulate/config", assemble_discover_sensor_message("goa", "Grid Out Accumulate", "kWh", "energy", "homeassistant/sensor/upower/grid/state", "accumulate"), true);
  mqtt_publish ("homeassistant/sensor/upower/grid_temperature/config", assemble_discover_sensor_message("got", "Grid Temperature", "℃", "temperature", "homeassistant/sensor/upower/grid/state", "temperature"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_in_voltage/config", assemble_discover_sensor_message("piv", "PV In Voltage", "V", "energy", "homeassistant/sensor/upower/pv/state", "inVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_in_current/config", assemble_discover_sensor_message("pic", "PV In Current", "A", "energy", "homeassistant/sensor/upower/pv/state", "inCurrent"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_in_wattage/config", assemble_discover_sensor_message("piw", "PV In Wattage", "W", "energy", "homeassistant/sensor/upower/pv/state", "inWattage"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_charge_voltage/config", assemble_discover_sensor_message("pov", "PV Out Voltage", "V", "energy", "homeassistant/sensor/upower/pv/state", "outVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_charge_current/config", assemble_discover_sensor_message("poc", "PV Out Current", "A", "energy", "homeassistant/sensor/upower/pv/state", "outCurrent"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_charge_wattage/config", assemble_discover_sensor_message("pow", "PV Out Wattage", "W", "energy", "homeassistant/sensor/upower/pv/state", "outWattage"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_charge_accumulate/config", assemble_discover_sensor_message("poa", "PV Out Accumulate", "kWh", "energy", "homeassistant/sensor/upower/pv/state", "accumulate"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_temperature/config", assemble_discover_sensor_message("pot", "PV Temperature", "℃", "temperature", "homeassistant/sensor/upower/pv/state", "temperature"), true);
	mqtt_publish ("homeassistant/sensor/upower/inverter_in_voltage/config", assemble_discover_sensor_message("iiv", "Inverter In Voltage", "V", "energy", "homeassistant/sensor/upower/inverter/state", "inVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/inverter_out_voltage/config", assemble_discover_sensor_message("iov", "Inverter Out Voltage", "V", "energy", "homeassistant/sensor/upower/inverter/state", "outVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/inverter_out_current/config", assemble_discover_sensor_message("ioc", "Inverter Out Current", "A", "energy", "homeassistant/sensor/upower/inverter/state", "outCurrent"), true);
  mqtt_publish ("homeassistant/sensor/upower/inverter_out_wattage/config", assemble_discover_sensor_message("iow", "Inverter Out Wattage", "W", "energy", "homeassistant/sensor/upower/inverter/state", "outWattage"), true);
  mqtt_publish ("homeassistant/sensor/upower/inverter_out_frequency/config", assemble_discover_sensor_message("iof", "Inverter Out Frequency", "Hz", "energy", "homeassistant/sensor/upower/inverter/state", "outFrequency"), true);
 // mqtt_publish ("homeassistant/sensor/upower/inverter_temperature/config", assemble_discover_sensor_message("iot", "Inverter Temperature", "℃", "temperature", "homeassistant/sensor/upower/inverter/state", "temperature"), true);
	mqtt_publish ("homeassistant/sensor/upower/bypass_out_voltage/config", assemble_discover_sensor_message("byov", "Bypass In Voltage", "V", "energy", "homeassistant/sensor/upower/bypass/state", "inVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/bypass_out_current/config", assemble_discover_sensor_message("byoc", "Bypass In Current", "A", "energy", "homeassistant/sensor/upower/bypass/state", "inCurrent"), true);
  mqtt_publish ("homeassistant/sensor/upower/bypass_out_wattage/config", assemble_discover_sensor_message("oybw", "Bypass In Wattage", "W", "energy", "homeassistant/sensor/upower/bypass/state", "inWattage"), true);
  mqtt_publish ("homeassistant/sensor/upower/battery_out_voltage/config", assemble_discover_sensor_message("bov", "Battery Out Voltage", "V", "energy", "homeassistant/sensor/upower/battery/state", "outVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/battery_temperature/config", assemble_discover_sensor_message("bot", "Battery Temperature", "℃", "temperature", "homeassistant/sensor/upower/battery/state", "temperature"), true);

  return 0;
}

int Upower::publish_switch(){
  return mqtt_publish("homeassistant/switch/upower/inverter/state", switch_state[INVERTER]?"ON":"OFF") &&
  mqtt_publish("homeassistant/switch/upower/solar_charge/state", switch_state[SOLAR_CHARGE]?"ON":"OFF") &&
  mqtt_publish("homeassistant/switch/upower/grid_charge/state", switch_state[GRID_CHARGE]?"ON":"OFF") &&
  mqtt_publish("homeassistant/switch/upower/gridout_prio/state", switch_state[GRID_PRIO]?"ON":"OFF");
}
int Upower::publish_data(){
  char buf[1024];

  DynamicJsonDocument pv_data(1024);
  pv_data["inVoltage"] = round_range(pv_in.voltage);
  pv_data["inCurrent"] = round_range(pv_in.current);
  pv_data["inWattage"] = round_range(pv_in.wattage);
  pv_data["outVoltage"] = round_range(pv_charge.voltage);
  pv_data["outCurrent"] = round_range(pv_charge.current);
  pv_data["outWattage"] = round_range(pv_charge.wattage);
  pv_data["accumulate"] = round_range(pv_charge.accumulate);
  pv_data["temperature"] = pv_charge.temp;
  pv_data["state"] = pv_charge.state;

  DynamicJsonDocument grid_data(1024);
  grid_data["inVoltage"] = round_range(grid_in.voltage);
  grid_data["inCurrent"] = round_range(grid_in.current);
  grid_data["inWattage"] = round_range(grid_in.wattage);
  grid_data["outVoltage"] = round_range(grid_charge.voltage);
  grid_data["outCurrent"] = round_range(grid_charge.current);
  grid_data["outWattage"] = round_range(grid_charge.wattage);
  grid_data["temperature"] = grid_charge.temp;
  grid_data["accumulate"] = round_range(grid_charge.accumulate);

  DynamicJsonDocument inverter_data(1024);
  inverter_data["inVoltage"] = round_range(inverter_in.voltage);
  inverter_data["outVoltage"] = round_range(inverter_out.voltage);
  inverter_data["outCurrent"] = round_range(inverter_out.current);
  inverter_data["outWattage"] = round_range(inverter_out.wattage);
  inverter_data["outFrequency"] = inverter_out.freq;
  inverter_data["temperature"] = inverter_out.temp;

  DynamicJsonDocument bypass_data(1024);
  bypass_data["inVoltage"] = round_range(bypass.voltage);
  bypass_data["inCurrent"] = round_range(bypass.current);
  bypass_data["inWattage"] = round_range(bypass.wattage);

  DynamicJsonDocument battery_data(1024);
  battery_data["outVoltage"] = round_range(battery.voltage);
  battery_data["temperature"] = battery.temp;


  serializeJson(grid_data, buf);
  mqtt_publish("homeassistant/sensor/upower/grid/state", buf);

  serializeJson(pv_data, buf);
  mqtt_publish("homeassistant/sensor/upower/pv/state", buf);
  
  serializeJson(inverter_data, buf);
  mqtt_publish("homeassistant/sensor/upower/inverter/state", buf);

  
  serializeJson(bypass_data, buf);
  mqtt_publish("homeassistant/sensor/upower/bypass/state", buf);

  serializeJson(battery_data, buf);
  mqtt_publish("homeassistant/sensor/upower/battery/state", buf);

  return 0;
}

unsigned char Upower::getUpowerStateFrom(int addr, int size, uint16_t *buf){
  int nResult = modbus->readInputRegisters(addr, size);
  if(nResult == modbus->ku8MBSuccess ){
    for(int i = 0 ; i < size ; i++){
      buf[i] = modbus->getResponseBuffer(i);
    }
    Serial.printf("Success request on %04X~%04X : RES(%03d)\n", addr, addr+size, nResult);
  }else{
    Serial.printf("Failed request on %04X~%04X : ERR(%03d)\n", addr, addr+size, nResult);
  }
  
//  for(int i = 0 ; i < size ; i++){
//    Serial.printf("%04x %02d ", addr+i, buf[i]);
//  }
//  Serial.printf("\n");
  
  return nResult;
}

int Upower::update_data(){
//    prepareSerial();
//    prepareModbus();
    // prepareRS485();
    uint16_t ucValue3500[19]={0,};
    uint16_t ucValue3519[20]={0,};
    uint16_t ucValue352F[13]={0,};
    uint16_t ucValue354C[16]={0,};

    unsigned char nResult3500;
    unsigned char nResult3519;
    unsigned char nResult352F;
    unsigned char nResult354C;


    nResult3500 = getUpowerStateFrom(0x3500, 19, ucValue3500);
    nResult3519 = getUpowerStateFrom(0x3519, 20, ucValue3519);
    nResult352F = getUpowerStateFrom(0x352F, 13, ucValue352F);
    nResult354C = getUpowerStateFrom(0x354C, 16, ucValue354C);

    if(nResult3500 == modbus->ku8MBSuccess){
        grid_charge.voltage=(float)ucValue3500[5]/100;
        grid_charge.current=(float)ucValue3500[6]/100;
        grid_charge.wattage=(float)ucValue3500[7]/100 + (float)ucValue3500[8]*256*256/100;
        grid_charge.accumulate=(float)ucValue3500[15]/100 + (float)ucValue3500[16]*256*256/100;
        grid_charge.temp=(float)((int16_t)ucValue3500[18])/100;
        
                
    }
    if(nResult3519 == modbus->ku8MBSuccess){
        pv_in.voltage=(float)ucValue3519[0]/100;
        pv_in.current=(float)ucValue3519[1]/100;
        pv_in.wattage=(float)ucValue3519[2]/100 + (float)ucValue3519[3]*256*256/100;

        pv_charge.voltage=(float)ucValue3519[4]/100;
        pv_charge.current=(float)ucValue3519[5]/100;
        pv_charge.wattage=(float)ucValue3519[6]/100 + (float)ucValue3519[7]*256*256/100;
        pv_charge.accumulate=(float)ucValue3519[14]/100 + (float)ucValue3519[15]*256*256/100;
        pv_charge.state=(ucValue3519[16] >> 1) && 3;// 00 : No Charging, 01 : Float Charging, 10 : Boost Charging, 11 : Equalization
        pv_charge.temp=(float)((int16_t)ucValue3519[19])/100;
    }
    if(nResult352F == modbus->ku8MBSuccess){
        inverter_in.voltage=(float)ucValue352F[0]/100;
        inverter_out.voltage=(float)ucValue352F[4]/100;
        inverter_out.current=(float)ucValue352F[5]/100;
        inverter_out.wattage=(float)ucValue352F[7]/100 + (float)ucValue352F[8]*256*256/100;
        inverter_out.freq=(float)ucValue352F[12]/100;
    }
    if(nResult354C == modbus->ku8MBSuccess){
        battery.voltage=(float)ucValue354C[0]/100;
        battery.temp=(float)((int16_t)ucValue354C[3])/100;
        battery.soc=ucValue354C[4];
        battery.state=ucValue354C[7];

        bypass.voltage=(float)ucValue354C[12]/100;
       	bypass.current=(float)ucValue354C[13]/100;
        bypass.wattage=(float)ucValue354C[14]/100; + (float)ucValue354C[15]*256*256/100;
    }

    if(nResult3500 == modbus->ku8MBSuccess && nResult354C == modbus->ku8MBSuccess){
	      grid_in.voltage=(float)ucValue3500[0]/100;
        grid_in.current=grid_charge.current + bypass.current;
        grid_in.wattage=grid_charge.wattage + bypass.wattage;
    }

    return nResult3500 | nResult3519 | nResult352F | nResult354C;
}

int Upower::update_switch(){
  //  prepareSerial();
  //  prepareModbus();
    
    unsigned char results[NUM_OF_SWITCH];

    // delay(1000);
    
    results[INVERTER] = modbus->readCoils(0x0106, 1);
    if(results[INVERTER] == modbus->ku8MBSuccess){
      switch_state[INVERTER] = modbus->getResponseBuffer(0) & (1 << 0);
    }

    results[GRID_PRIO] = modbus->readCoils(0x0104, 1);
    if(results[GRID_PRIO] == modbus->ku8MBSuccess){
      switch_state[GRID_PRIO] = modbus->getResponseBuffer(0) & (1 << 0);
    }

    results[SOLAR_CHARGE] = modbus->readCoils(0x010B, 1);
    if(results[SOLAR_CHARGE] == modbus->ku8MBSuccess){
      switch_state[SOLAR_CHARGE] = modbus->getResponseBuffer(0) & (1 << 0);
    }
    
    results[GRID_CHARGE] = modbus->readCoils(0x010C, 1);
    if(results[GRID_CHARGE] == modbus->ku8MBSuccess){
      switch_state[GRID_CHARGE] = modbus->getResponseBuffer(0) & (1 << 0);
    }

    //results[STORAGE_MODE] = modbus->readHoldingRegisters(0x960D, 3);
    //if(results[STORAGE_MODE] == modbus->ku8MBSuccess){
    //  switch_state[STORAGE_MODE] = modbus->getResponseBuffer(0) == STORAGE_MODE_BCV;
    //}
    

    return results[GRID_PRIO] | results[SOLAR_CHARGE] | results[GRID_CHARGE] | results[INVERTER];// | results[STORAGE_MODE];
}


const char* getStringSwitchName(switchType switch_type){
  const char *retVal[] = {"inverter", "gridout_prio", "solar_charge", "grid_charge"};//, "storage_mode"};
    return retVal[switch_type];
}

switchType getStringSwitchEnum(const char* switch_name){
    if (strcmp(switch_name, "inverter")==0){
      return INVERTER;
    }else if (strcmp(switch_name, "gridout_prio")==0){
      return GRID_PRIO;
    }else if (strcmp(switch_name, "solar_charge")==0){
      return SOLAR_CHARGE;
    }else{
      return GRID_CHARGE;
    //}else{
    //  return STORAGE_MODE;
    }
}

int Upower::change_switch(const char* switch_name, const char* onoff){  
//  prepareSerial();
//  prepareModbus();
  Serial.printf("%s turn %s\n", switch_name, onoff);

  int addr = 0;
  int value = 0;
  char topic[50];
  int result=-1;
//  int bcv;
//  int fcv;
//  int bvr;
  int switch_type=-1;
  
  sprintf(topic, "homeassistant/switch/upower/%s/state", switch_name);
  
  if(strcmp(onoff, "ON") == 0){
    value = 0xff;
  }

  if(strcmp(switch_name, "inverter") == 0){
    addr = 0x0106;
    switch_type = INVERTER;
  }else if(strcmp(switch_name, "solar_charge") == 0){
    addr = 0x010B;
    switch_type = SOLAR_CHARGE;
  }else if(strcmp(switch_name, "grid_charge") == 0){
    addr = 0x010C;
    switch_type = GRID_CHARGE;
  }else if(strcmp(switch_name, "gridout_prio") == 0){
    addr = 0x0104;
    switch_type = GRID_PRIO;
//  }else if(strcmp(switch_name, "storage") == 0){
//    addr = 0x960D;
//    switch_type = STORAGE_MODE;
//    if(strcmp(onoff, "ON") == 0){
//      //storage mode
//      bcv = STORAGE_MODE_BCV;//2920
//      fcv = STORAGE_MODE_FCV;//2760
//      bvr = STORAGE_MODE_BVR;//2680
//    }else{
//      //full mode
//      bcv = FULL_MODE_BCV;
//      fcv = FULL_MODE_FCV;
//      bvr = FULL_MODE_BVR;
//    }
  }


  int try_count = 0;
  int mqttResult = 0;
  while(result != modbus->ku8MBSuccess){
//    if(strcmp(switch_name, "storage") == 0){
//      modbus->setTransmitBuffer(0, (bcv>>8)&255);
//      modbus->setTransmitBuffer(1, (bcv   )&255);//bcv
//      modbus->setTransmitBuffer(2, (fcv>>8)&255);
//      modbus->setTransmitBuffer(3, (fcv   )&255);//fcv
//      modbus->setTransmitBuffer(4, (bvr>>8)&255);
//      modbus->setTransmitBuffer(5, (bvr   )&255);//bvr
  
    
//      Serial.printf("writeMultipleRegisters @%x => %d %d %d\n", addr,bcv, fcv, bvr);
//      result = modbus->writeMultipleRegisters(addr, 3);
//    }else{
    Serial.printf("writeSingleCoil @%x => %x\n", addr,value);
    result = modbus->writeSingleCoil(addr,value);
//    }
    
    if(result == modbus->ku8MBSuccess){
      Serial.printf("Success Switch %s\n", onoff);
      switch_state[switch_type] = strcmp(onoff, "ON") == 0;
      
      mqttResult = mqtt_publish(topic, switch_state[switch_type]?"ON":"OFF");

    }else{    
      Serial.printf("Failed to Switch %s as %d\n", onoff, result);
    }
    
    if(try_count > 5){
      Serial.printf("Failed to Switch %d times. break up \n", try_count);
      break;
    }
    try_count++;
    delay(100);
  }

  return result && !mqttResult;
}

const char* Upower::getDeviceName(){
  return "upower";
}
