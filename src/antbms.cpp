#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include "antbms.h"
#include "common.h"


const char* reason_err_charge[] = {
    "is turn off", 
    "is turn on", 
    "over charge protect", 
    "over current protect", 
    "battery is full charged", 
    "the total voltage of battery pack is over", 
    "battery over temperature", 
    "the MOSFET over temperature", 
    "Abnormal current", 
    "Balanced string out (a battery is not detected)", 
    "Motherboard over temperature", 
    "Discharge MOSFET abnormality", 
    "Manually turn off"
};

const char* reason_err_discharge[] = {
	"turn off",
	"turn on",
	"over discharge protect (single battery)",
	"over current protect",
	"over discharge protect ( battery pack)",
	"battery over temperature",
	"the MOSFET over temperature",
	"Abnormal current",
	"Balanced string out (a battery is not detected)",
	"Motherboard over temperature",
	"charge MOSFET turn on",
	"short protect",
	"Discharge MOSFET abnormality",
	"Start exception",
	"Manually turn off"
};

const char* reason_err_balance[] = {
	"turn off",
	"Exceeding limit trigger balance",
	"When charging, the voltage difference is too big, trigger balance ",
	"balance over temperature",
	"Automatic balance",
	"Motherboard over temperature"
};


Antbms::Antbms(PubSubClient *_mqttClient, SoftwareSerial *_serial){
    // SoftwareSerial serial;
    // serial->begin(19200, SWSERIAL_8N1, 5, 16, false, 256);
    
	// modbus = _modbus;
	serial = _serial;
    mqttClient = _mqttClient;

    subscribe_size = 3;
    subscribe_list[0] = "homeassistant/switch/antbms/charge_mosfet/set";
    subscribe_list[1] = "homeassistant/switch/antbms/discharge_mosfet/set";
    subscribe_list[2] = "homeassistant/switch/antbms/sleep_mode/set";


    fVoltageAll = 0.0;
    bms_soc = 0;
    charge_mosfet_status = 0;
    discharge_mosfet_status = 0;
    charge_mosfet_status_describe = "";
    discharge_mosfet_status_describe = "";
    balance_status_describe = "";
    cell_data[30] = {0.0, };
    // cell_resi[30] = {0.0, };
    battery_capacity = 0.0;
    battery_current = 0.0;
    temp_mosfet = 0.0;
    temp_balance = 0.0;
    temp_cell1 = 0.0;
    temp_cell5 = 0.0;
    // configuration();

    comm_info.rx_pin=5;
    comm_info.tx_pin=16;
    comm_info.baudrate=19200;
    comm_info.slaveID=0;
    sleep_mode = false;
}

int Antbms::setup_entity(){
    
    char *strbuf = (char*)malloc(sizeof(char)*400);

    char msgbuf[1024];

    for(int i = 1 ; i <= NUM_MAX_CELL; i++){
    sprintf(strbuf, "homeassistant/sensor/antbms/cell%d/config\0", i, true);
    mqtt_publish(strbuf, "");
    }
    mqtt_publish ("homeassistant/sensor/antbms/temp_mosfet/config", "", true);
    mqtt_publish ("homeassistant/sensor/antbms/temp_balance/config", "", true);
    mqtt_publish ("homeassistant/sensor/antbms/temp_cell1/config", "", true);
    mqtt_publish ("homeassistant/sensor/antbms/temp_cell5/config", "", true);
    mqtt_publish ("homeassistant/sensor/antbms/battery_discharging_voltage/config", "", true);
    mqtt_publish ("homeassistant/sensor/antbms/battery_discharging_current/config", "", true);
    mqtt_publish ("homeassistant/sensor/antbms/battery_discharging_wattage/config", "", true);
    mqtt_publish ("homeassistant/sensor/antbms/battery_capacity/config", "", true);

    mqtt_publish ("homeassistant/sensor/antbms/bms_soc/config", "", true);

    mqtt_publish ("homeassistant/switch/antbms/charge_mosfet/config", "", true);
    mqtt_publish ("homeassistant/switch/antbms/discharge_mosfet/config", "", true);
    mqtt_publish ("homeassistant/switch/antbms/cell_diff/config", "", true);
    mqtt_publish ("homeassistant/switch/antbms/sleep_mode/config", "", true);
    
  
    for(int i = 1 ; i <= NUM_MAX_CELL; i++){
        sprintf(strbuf, "homeassistant/sensor/antbms/cell%d/config\0", i);
        mqtt_publish(strbuf, "", true);
        sprintf(strbuf, "homeassistant/sensor/antbms/cell_ohm%d/config\0", i);
        mqtt_publish(strbuf, "", true);
    }
	mqtt_publish ("homeassistant/sensor/antbms/temp_mosfet/config", "", true);
	mqtt_publish ("homeassistant/sensor/antbms/temp_balance/config", "", true);
	mqtt_publish ("homeassistant/sensor/antbms/temp_cell1/config", "", true);
	mqtt_publish ("homeassistant/sensor/antbms/temp_cell5/config", "", true);
	mqtt_publish ("homeassistant/sensor/antbms/battery_discharging_voltage/config", "", true);
	mqtt_publish ("homeassistant/sensor/antbms/battery_discharging_current/config", "", true);
	mqtt_publish ("homeassistant/sensor/antbms/battery_discharging_wattage/config", "", true);
	mqtt_publish ("homeassistant/sensor/antbms/battery_capacity/config", "", true);

	mqtt_publish ("homeassistant/sensor/antbms/bms_soc/config", "", true);

	mqtt_publish ("homeassistant/switch/antbms/charge_mosfet/config", "", true);
	mqtt_publish ("homeassistant/switch/antbms/discharge_mosfet/config", "", true);
    mqtt_publish ("homeassistant/switch/antbms/cell_diff/config", "", true);

    
	mqtt_publish ("homeassistant/switch/antbms/charge_mosfet/config", assemble_discover_switch_message("chm", "Charge Mosfet", "homeassistant/switch/antbms/charge_mosfet/state", "homeassistant/switch/antbms/charge_mosfet/set", msgbuf), true);
	mqtt_publish ("homeassistant/switch/antbms/discharge_mosfet/config", assemble_discover_switch_message("dim", "Discharge Mosfet", "homeassistant/switch/antbms/discharge_mosfet/state", "homeassistant/switch/antbms/discharge_mosfet/set", msgbuf), true);
	mqtt_publish ("homeassistant/switch/antbms/sleep_mode/config", assemble_discover_switch_message("slpm", "Sleep Mode", "homeassistant/switch/antbms/sleep_mode/state", "homeassistant/switch/antbms/sleep_mode/set", msgbuf), true);

	mqtt_publish ("homeassistant/sensor/antbms/temp_mosfet/config", assemble_discover_sensor_message("tm", "Temp Mosfet", "°C", "temperature", "homeassistant/sensor/antbms/state", "temp_mosfet", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/temp_balance/config", assemble_discover_sensor_message("tb", "Temp Balance", "°C", "temperature", "homeassistant/sensor/antbms/state", "temp_balance", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/temp_cell1/config", assemble_discover_sensor_message("tc1", "Temp Cell1", "°C", "temperature", "homeassistant/sensor/antbms/state", "temp_cell1", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/temp_cell5/config", assemble_discover_sensor_message("tc5", "Temp Cell5", "°C", "temperature", "homeassistant/sensor/antbms/state", "temp_cell5", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/battery_discharging_voltage/config", assemble_discover_sensor_message("bdv", "Battery Dischg Voltage", "V", "power", "homeassistant/sensor/antbms/state", "battery_discharging_voltage", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/battery_discharging_current/config", assemble_discover_sensor_message("bdc", "Battery Dischg Current", "A", "power", "homeassistant/sensor/antbms/state", "battery_discharging_current", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/battery_discharging_wattage/config", assemble_discover_sensor_message("bdw", "Battery Dischg Wattage", "W", "power", "homeassistant/sensor/antbms/state", "battery_discharging_wattage", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/battery_capacity/config", assemble_discover_sensor_message("btc", "Battery Capacity", "Ah", "battery", "homeassistant/sensor/antbms/state", "battery_capacity", msgbuf), true);

	mqtt_publish ("homeassistant/sensor/antbms/charge_mosfet_status_describe/config", assemble_discover_sensor_message("cms", "Charge Mosfet Status", ".", "power", "homeassistant/sensor/antbms/state", "charge_mosfet_status_describe", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/discharge_mosfet_status_describe/config", assemble_discover_sensor_message("dms", "Discharge Mosfet Status", ".", "power", "homeassistant/sensor/antbms/state", "discharge_mosfet_status_describe", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/bms_soc/config", assemble_discover_sensor_message("bsoc", "BMS SoC", "%", "power", "homeassistant/sensor/antbms/state", "bms_soc", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/balance_status_describe/config", assemble_discover_sensor_message("bals", "Balance Status", ".", "power", "homeassistant/sensor/antbms/state", "balance_status_describe", msgbuf), true);
	mqtt_publish ("homeassistant/sensor/antbms/cell_diff/config", assemble_discover_sensor_message("diff", "Cell Diff", "V", "power", "homeassistant/sensor/antbms/state", "cell_diff", msgbuf), true);
  mqtt_publish ("homeassistant/sensor/antbms/battery_cycle/config", assemble_discover_sensor_message("batc", "Battery Cycle", "Ah", "power", "homeassistant/sensor/antbms/state", "battery_cycle", msgbuf), true);
  mqtt_publish ("homeassistant/sensor/antbms/battery_running_seconds/config", assemble_discover_sensor_message("Battery Running Seconds", "batrs", "s", "power", "homeassistant/sensor/antbms/state", "battery_running_seconds", msgbuf), true);

//	free(strbuf);
	
	for(int i = 1 ; i <= NUM_MAX_CELL; i++){
		sprintf(strbuf, "homeassistant/sensor/antbms/cell%d/config\0", i);
		char strbuf2[20];
		char strbuf3[20];
		char strbuf4[20];
		Serial.printf("start");
		sprintf(strbuf2, "cell%d", i);
		sprintf(strbuf3, "Cell %d", i);
		sprintf(strbuf4, "cell%d", i);
		mqtt_publish(strbuf, assemble_discover_sensor_message(strbuf2, strbuf3, "V", "power", "homeassistant/sensor/antbms/state", strbuf4, msgbuf), true);
	}

//   for(int i = 1 ; i <= NUM_MAX_CELL; i++){
//     sprintf(strbuf, "homeassistant/sensor/antbms/cell_ohm%d/config\0", i);
//     char strbuf2[20];
//     char strbuf3[20];
//     char strbuf4[20];
//     Serial.printf("start");
//     sprintf(strbuf2, "cellohm%d", i);
//     sprintf(strbuf3, "Cell Ohm %d", i);
//     sprintf(strbuf4, "cellohm%d", i);
//     mqtt_publish(strbuf, assemble_discover_sensor_message(strbuf2, strbuf3, "mΩ", "power", "homeassistant/sensor/antbms/state", strbuf4, msgbuf), true);
//   }
    return 0;
}



int Antbms::clearSerial(){
	int retVal = 0;
	while(serial->available()){
		Serial.printf("%02x ", serial->read());
		retVal++;
	}
	return retVal;
}

int Antbms::flushBMSSerial(){
	int size=0;
	while(serial->available()){
		serial->read();
		size++;
	}
	return size;
}
int checksumCalc(char* buffer_data, int size){
    //체크섬 계산
    
	uint8_t checksum_l = 0;
	uint8_t checksum_u = 0;
    for(int i = 0 ; i < 140 ; i++){
        if(i%5 == 0){
            Serial.printf("\n %d :", i);
        }
        Serial.printf("%02x ", buffer_data[i]);
        if( i > 3 && i < 138){
            uint16_t tmpVal = (checksum_l + buffer_data[i]);
            checksum_l = tmpVal % 256;
            checksum_u += (uint8_t)(tmpVal / 256);
        }
    }
    Serial.printf("\n");


    uint8_t sent_checksum_u = buffer_data[138];
    uint8_t sent_checksum_l = buffer_data[139];

    //체크섬 체크
    if(sent_checksum_u == checksum_u && sent_checksum_l == checksum_l){
        return 0;
    }else{
        return -1;
    }
    return 0;
}

void Antbms::parseBMSData(char* bms_data, int size){
    
	if(size >= 140){
    
        //parse Cell voltage
        int cell_index=1;
        float min_voltage = 5.0f;
        float max_voltage = 0.0f;
        fVoltageAll = 0.0f;

        battery_all_voltage = ((int)bms_data[4] * 8 + (int)bms_data[5])/1000.0f;
        int tmp_battery_current = 0 | (bms_data[70] << 24) | (bms_data[71]&0xFF) << 16 | (bms_data[72]&0xFF) << 8  | (bms_data[73]&0xFF);
        Serial.printf("battery_current : %d\n", tmp_battery_current);
        battery_current = (double)tmp_battery_current;
        battery_current = battery_current/10;
        battery_current = battery_current*-1;
        
        for(int i=6 ; i <70 ; i+=2){
            float cell_voltage = ((int)bms_data[i] << 8 | (int)bms_data[i+1])/1000.0f;
            // float cell_internal_resist = (cell_data[cell_index] - cell_voltage) / battery_current * 1000;
            // cell_resi[cell_index] = cell_internal_resist;
            cell_data[cell_index] = cell_voltage;
            fVoltageAll += cell_data[cell_index];
            if(min_voltage > cell_data[cell_index] && cell_data[cell_index] > 2){
                min_voltage = cell_data[cell_index];
            }
    
            if(max_voltage < cell_data[cell_index] ){
                max_voltage = cell_data[cell_index];
            }
    
                cell_index++;
        }
    
        cell_diff = max_voltage - min_voltage;
  
    		bms_soc = (uint16_t)bms_data[74];
        battery_full_capacity = (double)((int)bms_data[75] << 24 | (int)bms_data[76] << 16 | (int)bms_data[77] << 8	| (int)bms_data[78])/1000000;
        battery_capacity = (double)((int)bms_data[79] << 24 | (int)bms_data[80] << 16 | (int)bms_data[81] << 8	| (int)bms_data[82])/1000000;

        battery_cycle = (double)((int)bms_data[83] << 24 | (int)bms_data[84] << 16 | (int)bms_data[85] << 8	| (int)bms_data[86])/1000;
        battery_running_seconds = (double)((int)bms_data[87] << 24 | (int)bms_data[88] << 16 | (int)bms_data[89] << 8	| (int)bms_data[90]);
		

        
        temp_mosfet = (double)((int16_t)bms_data[91] << 8      | (int16_t)bms_data[92]);
        temp_balance = (double)((int32_t)bms_data[93] << 8     | (int16_t)bms_data[94]);
        temp_cell1 = (double)((int16_t)bms_data[95] << 8       | (int16_t)bms_data[96]);
        temp_cell5 = (double)((int16_t)bms_data[97] << 8       | (int16_t)bms_data[98]);

		charge_mosfet_status_describe = reason_err_charge[(int)(bms_data[103])];
        discharge_mosfet_status_describe = reason_err_discharge[(int)(bms_data[104])];
        balance_status_describe = reason_err_discharge[(int)(bms_data[105])];

		charge_mosfet_status=((uint16_t)bms_data[103]) == 1;
		discharge_mosfet_status=((uint16_t)bms_data[104]) == 1;
        
	}else{
        Serial.printf("short data size : %d\n", size);
    }
}

int Antbms::update_data(){
//    prepareSerial();
    Serial.println("getBMSAttribute");
	
	
	int cnt = 0;

    //clear read buffer before request
	while(serial->available()){
		serial->read();
	}
    char requestMessage[] = {0x5A, 0x5A, 0x00, 0x00, 0x01, 0x01};
	serial->write(requestMessage, sizeof(requestMessage));
	delay(100);

    
	int buffer_size = serial->readBytes(bms_data, 140);

	//1차 헤더 체크
	if(!(bms_data[0] == 0xAA && bms_data[1] == 0x55 && bms_data[2] == 0xAA && bms_data[3] == 0xFF)){
        Serial.printf("\nBMS Header not matching : %d\n",buffer_size);
        return -1;
    }

    //check if shorter than 140 bytes
    if(buffer_size < 140){
		Serial.printf("\nBMS Data Length is too short : %d\n",buffer_size);
        return -2;
    }

    //check if longer than 140 bytes
    if(serial->available()){
		Serial.printf("\nBMS buffer size over: %d\n",buffer_size);
        return -3;
    }

    if(checksumCalc(bms_data, buffer_size) < 0){
        Serial.printf("\nBMS Checksum FAILED!!\n");
        return -4;
    }
        // Serial.printf("aaa %d", buffer_size);

    parseBMSData(bms_data, buffer_size);
        // Serial.printf("aaa ");

    // slow_charge = get_param(9) == 10;
    // Serial.printf("slow_charge : %d\n", slow_charge);
    // check_sleep_mode();

    return 0;
}



int Antbms::check_sleep_mode(){
    if(sleep_mode){
      Serial.println("sleep mode");
        //soc over 50% && not enabled slow_charged
        if(bms_soc > 50 && !slow_charge){
            //enable slow mode
            Serial.println("slow charge set");
            set_param(9, 10);
        }
        //soc less than 50% && enabled slow_charged
        else if(bms_soc < 50 && slow_charge){
            //disable slow mode
            Serial.println("fast charge set");
            set_param(9, 1000);
        }
    }else{
        // Serial.println("Not sleep mode");
        // if(slow_charge){
        //     Serial.println("now slow mode, so set fast mode");
        //     set_param(9, 1000);
        // }
    }
    return 0;
}


int Antbms::get_param(char addr){
    
    int cnt = 0;
    char bms_param[6];
    //clear read buffer before request
	while(serial->available()){
		serial->read();
	}
    char requestMessage2[] = {0x5A, 0x5A, addr, 0x00, 0x00, addr};
	serial->write(requestMessage2, sizeof(requestMessage2));
	delay(100);
    
	serial->readBytes(bms_param, 6);

    for(int i = 0 ; i < 6 ; i++){
        Serial.printf("%x ", bms_param[i]);
    }
    Serial.printf("\n");

    if(bms_param[2] + bms_param[3] + bms_param[4] != bms_param[5]){
        Serial.printf("\nBMS Checksum FAILED!!\n");
        return -1;
    }
    
    return (int)((int)bms_param[3] << 8 | bms_param[4]);
    return 0;
}

int Antbms::set_param(char addr, int data){
    
    int cnt = 0;
    char bms_param[6];
    //clear read buffer before request
	while(serial->available()){
		serial->read();
	}

    char val[2];
    val[0] = data >> 8 & 255;
    val[1] = data      & 255;
    char checksum = (char)((addr + val[0] + val[1]) & 255);

    char requestMessage2[] = {0xA5, 0xA5, addr, val[0], val[1], checksum};
	serial->write(requestMessage2, sizeof(requestMessage2));
	delay(100);
    
	serial->readBytes(bms_param, 6);

    for(int i = 0 ; i < 6 ; i++){
        Serial.printf("%x ", bms_param[i]);
    }
    Serial.printf("\n");
    
    return checksum;
}

int Antbms::update_switch(){



    return 0;
}
//MQTT를 여기서 처리할지 말지..
//그냥 부모한테 구현하고 부모보고 처리 하라고 할까.. ㅅㅂ

int Antbms::publish_data(){
    char buf[1000];
    sprintf(buf, "{\"battery_discharging_voltage\":%2.2f, \"battery_discharging_current\":%2.2f, \"battery_discharging_wattage\":%3.2f, \"battery_capacity\":%3.2f, ",fVoltageAll,battery_current, (fVoltageAll * battery_current), battery_capacity	);
    sprintf(&buf[strlen(buf)], "\"temp_mosfet\":%3.2f, \"temp_balance\":%3.2f, \"temp_cell1\":%3.2f, \"temp_cell5\":%3.2f,", temp_mosfet, temp_balance, temp_cell1, temp_cell5);
    sprintf(&buf[strlen(buf)], "\"charge_mosfet_status_describe\":\"%S\", \"discharge_mosfet_status_describe\":\"%S\", \"balance_status_describe\":\"%S\", ", charge_mosfet_status_describe, discharge_mosfet_status_describe, balance_status_describe);
    sprintf(&buf[strlen(buf)], "\"battery_running_seconds\":\"%d\", \"battery_cycle\":\"%f\",", battery_running_seconds, battery_cycle);

    for(int i = 1 ; i <= NUM_MAX_CELL ; i++){
        sprintf(&buf[strlen(buf)], "\"cell%d\":%1.3f,", i, cell_data[i]);	
    }

    // for(int i = 1 ; i <= NUM_MAX_CELL ; i++){
    //     sprintf(&buf[strlen(buf)], "\"cell_ohm%d\":%1.3f,", i, cell_resi[i]);  
    // }

    sprintf(&buf[strlen(buf)], "\"bms_soc\":%d, \"cell_diff\":%1.3f}", bms_soc, cell_diff);

    Serial.println(buf);
    // char* retVal = (char*)malloc(sizeof(char)*(strlen(buf)+1));
    // memcpy(buf, retVal, strlen(buf));

    // Serial.printf("mqtt send : %s", buf);
    mqtt_publish ("homeassistant/sensor/antbms/state", buf);
    return 0;
}


int Antbms::publish_switch(){
    char buf[1000];
    if(charge_mosfet_status){
        mqtt_publish("homeassistant/switch/antbms/charge_mosfet/state", "ON");
    }else{
        mqtt_publish("homeassistant/switch/antbms/charge_mosfet/state", "OFF");
    }

    if(discharge_mosfet_status){
        mqtt_publish("homeassistant/switch/antbms/discharge_mosfet/state", "ON");
    }else{
        mqtt_publish("homeassistant/switch/antbms/discharge_mosfet/state", "OFF");
    }

    if(sleep_mode){
        mqtt_publish("homeassistant/switch/antbms/sleep_mode/state", "ON");
    }else{
        mqtt_publish("homeassistant/switch/antbms/sleep_mode/state", "OFF");
    }

    return 0;
}

int Antbms::change_switch(const char* switch_name, const char* onoff){
//    prepareSerial();
    char bms_control[6];
    Serial.println(switch_name);

    if(strcmp(switch_name, "discharge_mosfet") == 0){
      if(strcmp(onoff, "ON")==0){
        //discharge on
        bms_control[0]=0xa5;//-91:A5
        bms_control[1]=0xa5;//-91
        bms_control[2]=0xf9;//-7:F9
        bms_control[3]=0x0;//0
        bms_control[4]=0x1;//1
        bms_control[5]=0xfa;//-6:FA
//        bms_control[6]=0x24;//?
      }else{
        //discharge off
        bms_control[0]=0xa5;//-91
        bms_control[1]=0xa5;//-91
        bms_control[2]=0xf9;//-7
        bms_control[3]=0x0;//0
        bms_control[4]=0x0;//0
        bms_control[5]=0xf9;//-7:F9
//        bms_control[6]=0xe4;
      }
      
    }else if(strcmp(switch_name, "charge_mosfet") == 0){
      if(strcmp(onoff, "ON")==0){
        //charge on
        bms_control[0]=0xa5;//-91
        bms_control[1]=0xa5;//-91
        bms_control[2]=0xfa;//-6:FA
        bms_control[3]=0x0;//1
        bms_control[4]=0x1;//1
        bms_control[5]=0xfb;//-5:FB
//        bms_control[6]=0x24;
      }else{
        //charge off
        bms_control[0]=0xa5;//-91
        bms_control[1]=0xa5;//-91
        bms_control[2]=0xfa;//-6
        bms_control[3]=0x0;//1
        bms_control[4]=0x0;//0
        bms_control[5]=0xfa;//-6
//        bms_control[6]=0xe4;
      }
    }else if(strcmp(switch_name, "sleep_mode") == 0){
        Serial.printf("sleep mode : %d -> %d\n", sleep_mode, (onoff == "ON"));
        sleep_mode = (strcmp(onoff, "ON")==0);
        return 0;
    }

    int retVal = serial->write(bms_control, sizeof(bms_control));
    Serial.printf("bms response Result : %d", retVal);
    return retVal;
}

const char* Antbms::getDeviceName(){
  return "antbms";
}