#include "upower.h"
#include "common.h"
#include <ModbusMaster.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

Upower::Upower(PubSubClient *_mqttClient, ModbusMaster *_modbus/*, SoftwareSerial *_serial*/){
  mqttClient = _mqttClient;

	modbus = _modbus;
//	serial = _serial;
  printf("modbus : %p", modbus);
  subscribe_size = 4 ;
	subscribe_list[0] = (char*)"homeassistant/switch/upower/inverter/set";
	subscribe_list[1] = (char*)"homeassistant/switch/upower/solar/set";
	subscribe_list[2] = (char*)"homeassistant/switch/upower/grid/set";
	subscribe_list[3] = (char*)"homeassistant/switch/upower/bypassoff/set";
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
	mqtt_publish ("homeassistant/switch/upower/bypassoff/config", "");
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
  mqtt_publish ("homeassistant/switch/epever/grid/config", "");
  mqtt_publish ("homeassistant/switch/epever/solar/config", "");
  mqtt_publish ("homeassistant/switch/epever/bypassoff/config", "");
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
	mqtt_publish ("homeassistant/switch/upower/solar/config", assemble_discover_switch_message("ssw", "Solar Charge", "homeassistant/switch/upower/solar/state", "homeassistant/switch/upower/solar/set"), true);
	mqtt_publish ("homeassistant/switch/upower/grid/config", assemble_discover_switch_message("gsw", "Grid Charge", "homeassistant/switch/upower/grid/state", "homeassistant/switch/upower/grid/set"), true);
	mqtt_publish ("homeassistant/switch/upower/bypassoff/config", assemble_discover_switch_message("bsw", "Bypass Off", "homeassistant/switch/upower/bypassoff/state", "homeassistant/switch/upower/bypassoff/set"), true);
	//mqtt_publish ("homeassistant/switch/upower/storage/config", assemble_discover_switch_message("stsw", "Storage Mode", "homeassistant/switch/upower/storage/state", "homeassistant/switch/upower/storage/set"), true);
  
  mqtt_publish ("homeassistant/sensor/upower/grid_in_current/config", assemble_discover_sensor_message("gic", "Grid In Current", "A", "power", "homeassistant/sensor/upower/grid/state", "inCurrent"), true);
  mqtt_publish ("homeassistant/sensor/upower/grid_in_wattage/config", assemble_discover_sensor_message("giw", "Grid In Wattage", "W", "power", "homeassistant/sensor/upower/grid/state", "inWattage"), true);	
	mqtt_publish ("homeassistant/sensor/upower/grid_in_voltage/config", assemble_discover_sensor_message("giv", "Grid In Voltage", "V", "power", "homeassistant/sensor/upower/grid/state", "inVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/grid_out_current/config", assemble_discover_sensor_message("goc", "Grid Out Current", "A", "power", "homeassistant/sensor/upower/grid/state", "outCurrent"), true);
	mqtt_publish ("homeassistant/sensor/upower/grid_out_wattage/config", assemble_discover_sensor_message("gow", "Grid Out Wattage", "W", "power", "homeassistant/sensor/upower/grid/state", "outWattage"), true);
  mqtt_publish ("homeassistant/sensor/upower/grid_out_voltage/config", assemble_discover_sensor_message("gov", "Grid Out Voltage", "V", "power", "homeassistant/sensor/upower/grid/state", "outVoltage"), true);
	mqtt_publish ("homeassistant/sensor/upower/grid_out_accumulate/config", assemble_discover_sensor_message("goa", "Grid Out Accumulate", "Kw", "power", "homeassistant/sensor/upower/grid/state", "accumulate"), true);
  mqtt_publish ("homeassistant/sensor/upower/grid_temperature/config", assemble_discover_sensor_message("got", "Grid Temperature", "℃", "power", "homeassistant/sensor/upower/grid/state", "temperature"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_in_voltage/config", assemble_discover_sensor_message("piv", "PV In Voltage", "V", "power", "homeassistant/sensor/upower/pv/state", "inVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_in_current/config", assemble_discover_sensor_message("pic", "PV In Current", "A", "power", "homeassistant/sensor/upower/pv/state", "inCurrent"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_in_wattage/config", assemble_discover_sensor_message("piw", "PV In Wattage", "W", "power", "homeassistant/sensor/upower/pv/state", "inWattage"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_out_voltage/config", assemble_discover_sensor_message("pov", "PV Out Voltage", "V", "power", "homeassistant/sensor/upower/pv/state", "outVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_out_current/config", assemble_discover_sensor_message("poc", "PV Out Current", "A", "power", "homeassistant/sensor/upower/pv/state", "outCurrent"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_out_wattage/config", assemble_discover_sensor_message("pow", "PV Out Wattage", "W", "power", "homeassistant/sensor/upower/pv/state", "outWattage"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_out_accumulate/config", assemble_discover_sensor_message("poa", "PV Out Accumulate", "Kw", "power", "homeassistant/sensor/upower/pv/state", "accumulate"), true);
  mqtt_publish ("homeassistant/sensor/upower/pv_temperature/config", assemble_discover_sensor_message("pot", "PV Temperature", "℃", "power", "homeassistant/sensor/upower/pv/state", "temperature"), true);
	mqtt_publish ("homeassistant/sensor/upower/inverter_in_voltage/config", assemble_discover_sensor_message("iiv", "Inverter In Voltage", "V", "power", "homeassistant/sensor/upower/inverter/state", "inVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/inverter_out_voltage/config", assemble_discover_sensor_message("iov", "Inverter Out Voltage", "V", "power", "homeassistant/sensor/upower/inverter/state", "outVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/inverter_out_current/config", assemble_discover_sensor_message("ioc", "Inverter Out Current", "A", "power", "homeassistant/sensor/upower/inverter/state", "outCurrent"), true);
  mqtt_publish ("homeassistant/sensor/upower/inverter_out_wattage/config", assemble_discover_sensor_message("iow", "Inverter Out Wattage", "W", "power", "homeassistant/sensor/upower/inverter/state", "outWattage"), true);
  mqtt_publish ("homeassistant/sensor/upower/inverter_out_frequency/config", assemble_discover_sensor_message("iof", "Inverter Out Frequency", "Hz", "power", "homeassistant/sensor/upower/inverter/state", "outFrequency"), true);
  mqtt_publish ("homeassistant/sensor/upower/inverter_temperature/config", assemble_discover_sensor_message("iot", "Inverter Temperature", "℃", "power", "homeassistant/sensor/upower/inverter/state", "temperature"), true);
	mqtt_publish ("homeassistant/sensor/upower/bypass_out_voltage/config", assemble_discover_sensor_message("byov", "Bypass In Voltage", "V", "power", "homeassistant/sensor/upower/bypass/state", "inVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/bypass_out_current/config", assemble_discover_sensor_message("byoc", "Bypass In Current", "A", "power", "homeassistant/sensor/upower/bypass/state", "inCurrent"), true);
  mqtt_publish ("homeassistant/sensor/upower/bypass_out_wattage/config", assemble_discover_sensor_message("oybw", "Bypass In Wattage", "W", "power", "homeassistant/sensor/upower/bypass/state", "inWattage"), true);
  mqtt_publish ("homeassistant/sensor/upower/battery_out_voltage/config", assemble_discover_sensor_message("bov", "Battery Out Voltage", "V", "power", "homeassistant/sensor/upower/battery/state", "outVoltage"), true);
  mqtt_publish ("homeassistant/sensor/upower/battery_temperature/config", assemble_discover_sensor_message("bot", "Battery Temperature", "℃", "power", "homeassistant/sensor/upower/battery/state", "temperature"), true);

  return 0;
}

int Upower::publish_switch(){
  return mqtt_publish("homeassistant/switch/upower/inverter/state", switch_state[INVERTER]?"ON":"OFF") &&
  mqtt_publish("homeassistant/switch/upower/solar/state", switch_state[SOLAR_CHARGE]?"ON":"OFF") &&
  mqtt_publish("homeassistant/switch/upower/grid/state", switch_state[GRID_CHARGE]?"ON":"OFF") &&
  mqtt_publish("homeassistant/switch/upower/bypassoff/state", switch_state[BYPASS]?"ON":"OFF");
}
double round2(double value) {
   return (int)(value * 100 + 0.5) / 100.0;
}
int Upower::publish_data(){
  char buf[1024];

  DynamicJsonDocument pv_data(1024);
  pv_data["inVoltage"] = round2(pv_in.voltage);
  pv_data["inCurrent"] = round2(pv_in.current);
  pv_data["inWattage"] = round2(pv_in.wattage);
  pv_data["outVoltage"] = round2(pv_out.voltage);
  pv_data["outCurrent"] = round2(pv_out.current);
  pv_data["outWattage"] = round2(pv_out.wattage);
  pv_data["temperature"] = round2(pv_out.temp);
  pv_data["accumulate"] = round2(pv_out.accumulate);
  pv_data["state"] = round2(pv_out.state);

  DynamicJsonDocument grid_data(1024);
  grid_data["inVoltage"] = round2(grid_in.voltage);
  grid_data["inCurrent"] = round2(grid_in.current);
  grid_data["inWattage"] = round2(grid_in.wattage);
  grid_data["outVoltage"] = round2(grid_out.voltage);
  grid_data["outCurrent"] = round2(grid_out.current);
  grid_data["outWattage"] = round2(grid_out.wattage);
  grid_data["temperature"] = round2(grid_out.temp);
  grid_data["accumulate"] = round2(grid_out.accumulate);

  DynamicJsonDocument inverter_data(1024);
  inverter_data["inVoltage"] = round2(inverter_in.voltage);
  inverter_data["outVoltage"] = round2(inverter_out.voltage);
  inverter_data["outCurrent"] = round2(inverter_out.current);
  inverter_data["outWattage"] = round2(inverter_out.wattage);
  inverter_data["outFrequency"] = round2(inverter_out.freq);
  inverter_data["temperature"] = round2(inverter_out.temp);

  DynamicJsonDocument bypass_data(1024);
  bypass_data["inVoltage"] = round2(bypass.voltage);
  bypass_data["inCurrent"] = round2(bypass.current);
  bypass_data["inWattage"] = round2(bypass.wattage);

  DynamicJsonDocument battery_data(1024);
  battery_data["outVoltage"] = round2(battery.voltage);
  battery_data["temperature"] = round2(battery.temp);


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
        grid_out.voltage=(float)ucValue3500[5]/100;
        grid_out.current=(float)ucValue3500[6]/100;
        grid_out.wattage=(float)ucValue3500[7]/100 + (float)ucValue3500[8]*256*256/100;
        grid_out.accumulate=(float)ucValue3500[15]/100 + (float)ucValue3500[16]*256*256/100;
        grid_out.temp=(float)((int16_t)ucValue3500[18])/100;
        
        grid_in.voltage=(float)ucValue3500[0]/100;
        grid_in.wattage=grid_out.wattage;
        grid_in.current=grid_in.wattage == 0 ? 0 : (float)grid_out.wattage/(float)grid_in.voltage;
        
    }
    if(nResult3519 == modbus->ku8MBSuccess){
        pv_in.voltage=(float)ucValue3519[0]/100;
        pv_in.current=(float)ucValue3519[1]/100;
        pv_in.wattage=(float)ucValue3519[2]/100 + (float)ucValue3519[3]*256*256/100;

        pv_out.voltage=(float)ucValue3519[4]/100;
        pv_out.current=(float)ucValue3519[5]/100;
        pv_out.wattage=(float)ucValue3519[6]/100 + (float)ucValue3519[7]*256*256/100;
        pv_out.accumulate=(float)ucValue3519[14]/100 + (float)ucValue3519[15]*256*256/100;
        pv_out.accumulate=(float)ucValue3519[14]/100 + (float)ucValue3519[15]*256*256/100;
        pv_out.state=(ucValue3519[16] >> 1) && 3;// 00 : No Charging, 01 : Float Charging, 10 : Boost Charging, 11 : Equalization
        pv_out.temp=(float)((int16_t)ucValue3519[19])/100;
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
        battery.state=ucValue354C[4];
        bypass.voltage=(float)ucValue354C[12]/100;
        bypass.current=(float)ucValue354C[13]/100;
        bypass.wattage=(float)ucValue354C[14]/100; + (float)ucValue354C[15]*256*256/100;
    }

    // if(nResult3500 == modbus->ku8MBSuccess){
    //     grid_out.voltage=(float)ucValue3500[5]/100;
    //     grid_out.current=(float)ucValue3500[6]/100;
    //     grid_out.wattage=(float)ucValue3500[7]/100 + (float)ucValue3500[8]*256*256/100;
    //     grid_out.accumulate=(float)ucValue3500[15]/100 + (float)ucValue3500[16]*256*256/100;
    //     grid_out.temp=(float)ucValue3500[18]/100;
        
    //     grid_in.voltage=(float)ucValue3500[0]/100;
    //     grid_in.current=grid_out.wattage/grid_in.voltage;
    //     grid_in.wattage=grid_out.wattage;
        
    // }
    // if(nResult3519 == modbus->ku8MBSuccess){
    //     pv_in.voltage=(float)ucValue3519[0]/100;
    //     pv_in.current=(float)ucValue3519[1]/100;
    //     pv_in.wattage=(float)ucValue3519[2]/100 + (float)ucValue3519[3]*256*256/100;

    //     pv_out.voltage=(float)ucValue3519[4]/100;
    //     pv_out.current=(float)ucValue3519[5]/100;
    //     pv_out.wattage=(float)ucValue3519[6]/100 + (float)ucValue3519[7]*256*256/100;
    //     pv_out.accumulate=(float)ucValue3519[14]/100 + (float)ucValue3519[15]*256*256/100;
    //     pv_out.accumulate=(float)ucValue3519[14]/100 + (float)ucValue3519[15]*256*256/100;
    //     pv_out.state=(ucValue3519[16] >> 1) && 3;// 00 : No Charging, 01 : Float Charging, 10 : Boost Charging, 11 : Equalization
    //     pv_out.temp=(float)ucValue3519[19]/100;
    // }
    // if(nResult352F == modbus->ku8MBSuccess){
    //     inverter_in.voltage=(float)ucValue352F[0]/100;
    //     inverter_out.voltage=(float)ucValue352F[4]/100;
    //     inverter_out.current=(float)ucValue352F[5]/100;
    //     inverter_out.wattage=(float)ucValue352F[7]/100 + (float)ucValue352F[8]*256*256/100;
    //     inverter_out.freq=(float)ucValue352F[12]/100;
    // }
    // if(nResult354C == modbus->ku8MBSuccess){
    //     battery.voltage=(float)ucValue354C[0]/100;
    //     battery.temp=(float)ucValue354C[3]/100;
    //     battery.state=ucValue354C[4];
    //     bypass.voltage=(float)ucValue354C[12]/100;
    //     bypass.current=(float)ucValue354C[13]/100;
    //     bypass.wattage=(float)ucValue354C[14]/100; + (float)ucValue354C[15]*256*256/100;
    // }
    // return nResult3500 | nResult3519 | nResult352F | nResult354C;
    return nResult3500 | nResult3519 | nResult352F | nResult354C;
}

int Upower::update_switch(){
  //  prepareSerial();
  //  prepareModbus();
    
    unsigned char results[NUM_OF_SWITCH];

    // delay(1000);
    
    results[INVERTER] = modbus->readCoils(addresses[INVERTER], 1);
    if(results[INVERTER] == modbus->ku8MBSuccess){
      Serial.printf("Inverter returns : %d\n", modbus->getResponseBuffer(0));
      switch_state[INVERTER] = modbus->getResponseBuffer(0);
    }

    

    results[BYPASS] = modbus->readCoils(addresses[BYPASS], 1);
    if(results[BYPASS] == modbus->ku8MBSuccess){
      switch_state[BYPASS] = modbus->getResponseBuffer(0);
    }

    results[SOLAR_CHARGE] = modbus->readCoils(addresses[SOLAR_CHARGE], 1);
    if(results[SOLAR_CHARGE] == modbus->ku8MBSuccess){
      switch_state[SOLAR_CHARGE] = modbus->getResponseBuffer(0);
    }
    results[GRID_CHARGE] = modbus->readHoldingRegisters(addresses[GRID_CHARGE], 1);
    if(results[GRID_CHARGE] == modbus->ku8MBSuccess){
      Serial.printf("grid charging returns : %d\n", modbus->getResponseBuffer(0));
      switch_state[GRID_CHARGE] = modbus->getResponseBuffer(0) != 8;
    }


    // results[STORAGE_MODE] = modbus->readHoldingRegisters(0x960D, 3);
    // if(results[STORAGE_MODE] == modbus->ku8MBSuccess){
    //   switch_state[STORAGE_MODE] = modbus->getResponseBuffer(0) == STORAGE_MODE_BCV;
    // }
    

    return results[BYPASS] | results[SOLAR_CHARGE] | results[GRID_CHARGE] | results[INVERTER];// | results[STORAGE_MODE];
}


const char* getStringSwitchName(switchType switch_type){
  const char *retVal[] = {"inverter", "bypass", "solar_charge", "grid_charge", "storage_mode"};
    return retVal[switch_type];
}

switchType getStringSwitchEnum(const char* switch_name){
    if (strcmp(switch_name, "inverter")==0){
      return INVERTER;
    }else if (strcmp(switch_name, "bypass")==0){
      return BYPASS;
    }else if (strcmp(switch_name, "solar_charge")==0){
      return SOLAR_CHARGE;
    }else if (strcmp(switch_name, "grid_charge")==0){
      return GRID_CHARGE;
    // }else{
    //   return STORAGE_MODE;
    }
    return NUM_OF_SWITCH;
}

int Upower::change_switch(const char* switch_name, const char* onoff){  
//  prepareSerial();
//  prepareModbus();
  Serial.printf("%s turn %s\n", switch_name, onoff);

  int addr = 0;
  int value = 0;
  char topic[50];
  int result=-1;
  int bcv;
  int fcv;
  int bvr;
  int switch_type=-1;



  int try_count = 0;
  int mqttResult = 0;
  
  sprintf(topic, "homeassistant/switch/upower/%s/state", switch_name);
  
  if(strcmp(onoff, "ON") == 0){
    value = 0xff;//? 1??
    // value = 0x1;
  }

  if(strcmp(switch_name, "inverter") == 0){
    switch_type = INVERTER;
  }else if(strcmp(switch_name, "solar") == 0){
    switch_type = SOLAR_CHARGE;
  }else if(strcmp(switch_name, "grid") == 0){
    switch_type = GRID_CHARGE;
    if(strcmp(onoff, "ON") == 0){
      value = 4;
      // value = 0x1;
    }else{
      value = 8;
    }
    while(result != modbus->ku8MBSuccess){
      Serial.printf("writeMultipleRegisters @%x => %x\n", addresses[switch_type],value);
      result = modbus->writeMultipleRegisters(addresses[switch_type],value);
      
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
  }else if(strcmp(switch_name, "bypassoff") == 0){
    switch_type = BYPASS;
  }

  while(result != modbus->ku8MBSuccess){
    Serial.printf("writeSingleCoil @%x => %x\n", addresses[switch_type],value);
    result = modbus->writeSingleCoil(addresses[switch_type],value);
    
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
