#include "rtusw_mk2.h"

RtuSwMk2::RtuSwMk2(PubSubClient *_mqttClient, ModbusMaster *_modbus, int _slaveID, int _numOfSW, ...){//"Battery 12V", "Battery Equalize"

	slaveID = _slaveID;
	numOfSW = _numOfSW;
 	mqttClient = _mqttClient;

	modbus = _modbus;
	// serial = _serial;

	subscribe_size = 0;
	for(int i = 1 ; i < numOfSW+1 ; i++){
		subscribe_list[i-1] = (char*)malloc(sizeof(char)*40);
		sprintf(subscribe_list[i-1], "homeassistant/switch/%s/%d%d/set\0", device_domain, slaveID, i);
		subscribe_size++;
	}

	va_list valist;
	va_start(valist, _numOfSW);
	for(int i = 1 ; i < numOfSW+1 ; i++){
		role[i] = va_arg(valist, char*);
	}
	va_end(valist);
	
	comm_info.rx_pin=13;
	comm_info.tx_pin=12;
	comm_info.baudrate=9600;
	comm_info.slaveID=slaveID;
}

int RtuSwMk2::setup_entity(){
	// return 0;
	for(int i = 1 ; i < numOfSW+1 ; i++){
		char this_topic_sensor[45];
		char this_topic_state[45];
		char this_topic_set[45];
		char this_uniq_id[6];

		sprintf(this_topic_sensor, "homeassistant/switch/%s/%d%d/config\0", device_domain, slaveID, i);
		sprintf(this_topic_state, "homeassistant/switch/%s/%d%d/state\0", device_domain, slaveID, i);
		sprintf(this_topic_set, "homeassistant/switch/%s/%d%d/set\0", device_domain, slaveID, i);
		sprintf(this_uniq_id, "rsw%d%d\0", slaveID, i);
    
    	char msgbuf[1024];

		if(mqtt_publish (this_topic_sensor, "", true)){
			if(mqtt_publish (this_topic_sensor, assemble_discover_switch_message(this_uniq_id, role[i], this_topic_state, this_topic_set, msgbuf), true)){
				Serial.println("success");
			}
		}
	}
	return 0;
}

int RtuSwMk2::publish_switch(){
	if(isSuccess){
		for(int j = 1; j < numOfSW+1; j++){
			char url[100];
			sprintf(url, "%s/%s/%d%d/%s", "homeassistant/switch",device_domain,slaveID, j , "state");
			const char* msgcontent = switch_state[j] == 0 ? "OFF" : "ON";
			Serial.printf("%s -> %s", url, msgcontent);
			Serial.println(msgcontent);
			if(!mqtt_publish(url, msgcontent)){
				return 1;
			}
		}
	}
    return 0;
}
int RtuSwMk2::publish_data(){
    return 0;
}

int RtuSwMk2::update_data(){
    return 0;
}

int RtuSwMk2::update_switch(){
	// prepareSerial();
  	// prepareModbus();
    char nResult;
	modbus->clearResponseBuffer();

	Serial.printf("switch id %d\n", slaveID);

	nResult = modbus->readHoldingRegisters(1, 4);
		
	isSuccess = nResult == modbus->ku8MBSuccess;

	if (nResult == modbus->ku8MBSuccess){
		char response[100];
		int responseValue = modbus->getResponseBuffer(0);

		// int status[4];
		Serial.print("response0 : ");
		Serial.print(modbus->getResponseBuffer(0), BIN);
		Serial.print(modbus->getResponseBuffer(1), BIN);
		Serial.print(modbus->getResponseBuffer(2), BIN);
		Serial.println(modbus->getResponseBuffer(3), BIN);

		for(int j = 0; j < 4; j++){
			switch_state[j+1] = modbus->getResponseBuffer(j) != 0;
		}
	}else{
		Serial.printf("Failed request on %04X~%04X : ERR(%03d)\n", 0, 8, nResult);
	}
	return isSuccess;
}


int RtuSwMk2::change_switch(const char* switch_name, const char* onoff){
	// prepareSerial();
  	// prepareModbus();
	int add= atoi(switch_name);
	int rtuDevNo = add/10;
	int rtuSWNo = add%10;
	int sendvalue = 0;

	Serial.printf("my slaveId : %d, incomming id : %d\n", slaveID, rtuDevNo);
	if(rtuDevNo != slaveID){
		return 0;
	}
	
	if(strcmp(onoff, "ON")==0){
		sendvalue = 0x0100;
	}else{
		sendvalue = 0x0200;
	}

	Serial.printf("rtuDevNo : %d", rtuDevNo);
	Serial.printf(" rtuChNo : %d\n", rtuSWNo);
	
	// modbus->begin(rtuDevNo, serial);
	int retVal;
	int count;
	while(retVal = modbus->writeSingleRegister(rtuSWNo,sendvalue) == modbus->ku8MBSuccess){
		if(count > 5){
			break;
		}
		count++;
	}

	
	char topic_address[100] = {0,};
	sprintf(topic_address, "homeassistant/switch/%s/%d%d/state", device_domain, rtuDevNo, rtuSWNo);

	const char* msgcontent;

	bool isSuccessRequest = retVal == modbus->ku8MBSuccess;
	
	if(isSuccessRequest){
		msgcontent = strcmp(onoff, "ON")==0 ? "ON" : "OFF";
		switch_state[rtuSWNo] = strcmp(onoff, "ON")==0;
		mqtt_publish(topic_address, msgcontent);
	}else{
		msgcontent = switch_state[rtuSWNo] ? "ON" : "OFF";
		mqtt_publish(topic_address, msgcontent);
	}

	// if(strcmp(onoff, "ON")==0){
	// 	msgcontent = retVal ? "OFF" : "ON";
	// }else{
	// 	msgcontent = retVal ? "ON" : "OFF";
	// }
	

	return retVal;

}
const char* RtuSwMk2::getDeviceName(){
	return "rtusw_mk2";
}
