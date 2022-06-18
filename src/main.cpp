#include <Arduino.h>

#include <ArduinoJson.h>

#include <SoftwareSerial.h>

#include <PubSubClient.h>

#include <ModbusMaster.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#endif

#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <WebSocketsServer.h>


#include "common.h"
#include "upower.h"

#include "device.h"
#include "upower.h"
#include "rtusw_mk1.h"
#include "rtusw_mk2.h"
#include "antbms.h"
#include "jbdbms.h"

#include "ds1603da.h"
#include "xymd02.h"

#include "ds1603da.h"

#if defined(ESP8266)
	ESP8266WebServer server(80);
	String BOARD_NAME = "esp8266";
#elif defined(ESP32)
	WebServer server(80);
	String BOARD_NAME = "esp32";
#endif
WebSocketsServer webSocket = WebSocketsServer(81);

void webRootHandler();
void webSettingsHandler();

void webNotFoundHandler();
void handleSetSSID();
void handleReset();
void handleRestart();

bool detectMultiplePress(unsigned long* releasedTime, unsigned long pressedTimes[], int* pressedIndex, boolean curState);
void setWifiInfo(char* SSID, char* PW,char* MQT_ADR, char* MQT_PORT,char* MQT_ID, char* MQT_PW);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
int setup_wifi();
void read_params();
void setup_mqtt();
enum RS485TYPE{
        UPOWER,
        RTU_SWITCH,
};


char device_id[300];
int loop_count;

long lastMsg = 0;
long lastMsg2 = 0;

long lastMoverButon;
long lastInverterButon;

const int debugFlag=1;

char* ssid;
char* ssid_pw;
char* mqtt_addr;
char* mqtt_port;
char* mqtt_client_id;
char* mqtt_client_pw;

//esp32 pin map
//D1(5) : BMS Rx
//D0(16) : BMS Tx

//D7(13) : RS485 Rx
//D6(12) : RS485 Tx
//Tx : Debugging Log Tx


WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);





void preTransmission() {}
void postTransmission() {}
void getBMSAttributes();
void AddressRegistry_switches();
void messageRouter(char* topic, byte* message, unsigned int length);

int queueMover = -1;
int queueInverter = -1;

Device* devices[10];
int numOfDevice;

RtuSwMk1* devRtuSwitch;
Upower* devUpower;
// RtuSwMk1* devUpower;

SoftwareSerial serialUpower;
SoftwareSerial serialSwitch;
SoftwareSerial serialBms;

ModbusMaster modbusUpower;
// ModbusMaster modbusSwitch;




const int COUNT_RESET_TIME = 1000;

void modbusUpowerPostTransmission(){
	serialUpower.enableIntTx(false);
}

void modbusUpowerPreTransmission(){
	serialUpower.enableIntTx(true);
}

void modbusSwitchPostTransmission(){
  serialSwitch.enableIntTx(false);
}

void modbusSwitchPreTransmission(){
  serialSwitch.enableIntTx(true);
}



void IRAM_ATTR Ext_INVERTER_ISR(){
  	// long now = millis();
  	Serial.printf("IR : %d\n", queueInverter);
  	// lastInverterButon = now;
  	queueInverter++;
//   if(now - lastInverterButon > 100){
//   }
}

void IRAM_ATTR Ext_MOVER_ISR(){
	// long now = millis();
	Serial.printf("MR : %d\n", queueMover);
	// lastMoverButon = now;
	queueMover++;
//   if(now - lastMoverButon > 100){
//   }
}




void setup()
{
  
	Serial.begin(115200);
	EEPROM.begin(4096);

	Serial.printf("Start!!\n");

	// pinMode(MOVER_BUTTON_PIN, INPUT_PULLUP);
	// pinMode(INVERTER_BUTTON_PIN, INPUT_PULLUP);

	// pinMode(INVERTER_STATE_PIN, OUTPUT);
	// pinMode(MOVER_STATE_PIN, OUTPUT);

	// attachInterrupt(MOVER_BUTTON_PIN, Ext_MOVER_ISR, FALLING);
	// attachInterrupt(INVERTER_BUTTON_PIN, Ext_INVERTER_ISR, FALLING);

	
	mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
	mqttClient.setCallback(messageRouter);
	ssid = (char*)malloc(sizeof(char)*30);
	ssid_pw = (char*)malloc(sizeof(char)*30);
	mqtt_addr = (char*)malloc(sizeof(char)*30);
	mqtt_port = (char*)malloc(sizeof(char)*30);
	mqtt_client_id = (char*)malloc(sizeof(char)*30);
	mqtt_client_pw = (char*)malloc(sizeof(char)*30);

	read_params();

	setup_wifi();
	
	server.on("/", webRootHandler);
	server.on("/settings", webSettingsHandler);
	server.on("/setssid", HTTP_POST, handleSetSSID);
	server.on("/restart", handleRestart);
	server.on("/reset", handleReset);
	server.onNotFound(webNotFoundHandler);
	server.begin();
	Serial.println("HTTP server started");
	if (MDNS.begin(BOARD_NAME.c_str())) {
		Serial.println("mDNS responder started");
	} else {
		Serial.println("Error setting up MDNS responder!");
	}

	loop_count=0;

	#if defined(ESP8266)
		sprintf(device_id, "%06X", ESP.getChipId());
	#elif defined(ESP32)
		uint32_t id = 0;
		for(int i=0; i<17; i=i+8) {
		id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
		}
		sprintf(device_id, "%06X", id);
	#endif

	Serial.println(device_id);
	

	webSocket.onEvent(webSocketEvent);
	webSocket.begin();

	Serial.println("set websocket");



	Serial.println("Start Devices initialize");
	#if defined(ESP8266)
	serialUpower.begin(115200, SWSERIAL_8N1, 13, 12, false, 256);//upower 라인 : 22, 23
	serialBms.begin(19200, SWSERIAL_8N1, 5, 16, false, 256);//bms 라인 : 18, 19
	// serialSwitch.begin(9600, SWSERIAL_8N1, 13, 12, false, 256);//sw 라인 : 16, 17
	#elif defined(ESP32)
	// serialUpower.begin(115200, SWSERIAL_8N1, 22, 23, false, 256);//sw 라인 : 16, 17
	serialBms.begin(9600, SWSERIAL_8N1, 16, 17, false, 256);//bms 라인 : 18, 19
	serialSwitch.begin(9600, SWSERIAL_8N1, 18, 19, false, 256);//sw 라인 : 16, 17
	#endif
	



	delay(100);



	delay(100);
  
	numOfDevice=0;
	#ifdef ESP8266


	modbusUpower.postTransmission(modbusUpowerPostTransmission);
	modbusUpower.preTransmission(modbusUpowerPreTransmission);
	// modbusSwitch.postTransmission(modbusSwitchPostTransmission);
	// modbusSwitch.preTransmission(modbusSwitchPreTransmission);
	
	modbusUpower.begin(10, serialUpower);
	// modbusSwitch.begin(255, serialUpower);

	//for 572UT
	devices[numOfDevice++] = new Upower(&mqttClient,  &modbusUpower, &serialUpower);
	devices[numOfDevice++] = new Antbms(&mqttClient, &serialBms);
	// devices[numOfDevice++] = new RtuSwMk1(&mqttClient, &modbusUpower, &serialUpower, 1, 1, "Equalize");
	// devices[numOfDevice++] = new RtuSwMk2(&mqttClient, &modbusUpower, &serialUpower, 2, 3, "Water drain1", "Fridge", "Around View");
	// devices[numOfDevice++] = new RtuSwMk2(&mqttClient, &modbusUpower, &serialUpower, 3, 4, "Water drain2", "External Pump", "Fill", "Heating Preheat");
	// devices[numOfDevice++] = new XYMD02(&mqttClient, &modbusUpower, &serialUpower, 0xb3);
	// devices[numOfDevice++] = new XYMD02(&mqttClient, &modbusUpower, &serialUpower, 0xb4);
	#endif

	#ifdef ESP32
	modbusUpower.postTransmission(modbusUpowerPostTransmission);
	modbusUpower.preTransmission(modbusUpowerPreTransmission);
	modbusSwitch.postTransmission(modbusSwitchPostTransmission);
	modbusSwitch.preTransmission(modbusSwitchPreTransmission);
	
	modbusUpower.begin(10, serialUpower);
	modbusSwitch.begin(255, serialSwitch);

	devUpower = new Upower(&mqttClient, &modbusUpower /*&modbus, &serialUpower*/);
	devRtuSwitch = new RtuSwMk1(&mqttClient, &modbusSwitch , /*&modbus, &serialSwitch, */ 0, 8, "Equalizer", "Plumbing Drain", "Tank Drain", "Whale to Fill", "Aroundview", "Mover", "12v Charger");
	devices[numOfDevice++] = devUpower;
	devices[numOfDevice++] = new Jbdbms(&mqttClient, &serialBms,0, 16);
  	devices[numOfDevice++] = devRtuSwitch;
	#endif
	setup_mqtt();
}



void updateStates(int deviceNumber){
	Serial.printf("device %d - switch update\n", deviceNumber);
	devices[deviceNumber]->update_switch();
	Serial.printf("device %d - data update\n", deviceNumber);
	devices[deviceNumber]->update_data();
	
	// Serial.printf("Inverter : %d, Mover : %d \n", devUpower->switch_state[0], devRtuSwitch->getSwitchState(6));
    // digitalWrite(INVERTER_STATE_PIN, devUpower->switch_state[0] == 1 ? HIGH:LOW);
    // digitalWrite(MOVER_STATE_PIN, devRtuSwitch->getSwitchState(6) == 1 ? HIGH:LOW);


}
void pushMQTT(int deviceNumber){
	
    Serial.printf("device %d - push switch mqtt\n", deviceNumber);
	devices[deviceNumber]->publish_switch();
    Serial.printf("device %d - push data mqtt\n", deviceNumber);
	devices[deviceNumber]->publish_data();
	
}

void setup_mqtt_device(){

  for(int i = 0 ; i < numOfDevice ; i++){
		Serial.printf("%d setup mqtt\n", i);
		devices[i]->setup_entity();
	 }
}



int setup_wifi() {

	if(WiFi.status() == WL_CONNECTED){
		return 0;
	}

	bool isSTA=false;
	

	int count = 0;
	
	if(strlen(ssid) > 0 && strlen(ssid_pw) > 0){
		while (WiFi.status() != WL_CONNECTED) {
			WiFi.begin(ssid, ssid_pw);	
			Serial.printf("Trying to connecting : %s ", ssid);
			for(int i = 0 ; i < 20 ; i++){
				delay(500);
				Serial.printf(".");
			}
			if(count > 3){
				break;
			}
			count++;
		}
		if(WiFi.status() == WL_CONNECTED){
			Serial.println("WiFi connected");
			Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
		}else{
			isSTA=true;
		}
	}else{
		Serial.println("SSID is not set. Working STA mode only");
		WiFi.mode(WIFI_AP_STA);
  		WiFi.softAP(BOARD_NAME.c_str(), "12341234");
		isSTA=true;
	}

	if(isSTA){
		Serial.print("IP address for network ");
		Serial.print(BOARD_NAME);
		Serial.print(" : ");
		Serial.print(WiFi.softAPIP());
	}else{
		Serial.print("IP address for network ");
		Serial.print(ssid);
		Serial.print(" : ");
		Serial.println(WiFi.localIP());
	}
	
	

	return WiFi.status();
}

void(* resetFunc) (void) = 0;//declare reset function at address 0



void setup_mqtt() {
	int retry_count = 0;

	Serial.println("WiFi check.");

	if(WiFi.status() != WL_CONNECTED){
		Serial.println("WiFi Not connected. Stopping MQTT Connection.");
		return;
	}

	int valid_mqtt_connection_info = strlen(mqtt_addr) > 0 && strlen(mqtt_port) > 0 && strlen(mqtt_client_id) > 0 && strlen(mqtt_client_pw) > 0;

	if(!valid_mqtt_connection_info){
		return;
	}

	Serial.printf("MQTT server @%s:%d\n", mqtt_addr, atoi(mqtt_port));
	mqttClient.setServer(mqtt_addr, atoi(mqtt_port));

	Serial.println("Attempting MQTT connection...");
	char mqtt_client_name[20];
	sprintf(mqtt_client_name, "%s-%s", MQTT_CLIENT_NAME, device_id);
    Serial.println(mqtt_client_name);
		
    Serial.printf("MQTT name : %s account : %s:%s\n", mqtt_client_name, mqtt_client_id, mqtt_client_pw);
		int mqtt_connection_result=mqttClient.connect(mqtt_client_name, mqtt_client_id, mqtt_client_pw);

		if (mqtt_connection_result) {
			Serial.println("connected");
			retry_count=0;
			
			Serial.printf("subscribe start");
			mqttClient.subscribe("homeassistant/switch/reset/set");  
			mqttClient.subscribe("homeassistant/switch/restart/set");  
			for(int i = 0 ; i < numOfDevice ; i++){
				Serial.printf("device %d subscribe start", i);
				devices[i]->subscribe();
			}
		}else {
			Serial.printf("failed, rc= %d, try again in 5 seconds\n", mqttClient.state());
			retry_count++;
		}
}

void device_cycle(int seq) {
	updateStates(seq);
	if(WiFi.status() == WL_CONNECTED){
		if(mqttClient.connected()){
			pushMQTT(seq);
		}else{
			setup_mqtt();
		}
		
	}else{
		setup_wifi();
		setup_mqtt();
	}
	
}


bool detectMultiplePress(unsigned long* releasedTime, unsigned long pressedTimes[], int* pressedIndex, boolean curState){
  bool newState = curState;
      pressedTimes[*pressedIndex] = *releasedTime ;
      *pressedIndex = (*pressedIndex+1)%5;

    long now = millis();
    int validCount = 0;
    for(int i = 0 ; i < 5 ; i++){
      if(now - pressedTimes[i] < COUNT_RESET_TIME){
        validCount++;
      }
    }

    Serial.println(validCount);
    if(validCount > ((curState)? 1 : 2) ){
      newState = !curState;
      Serial.printf("turn %s(%d)\n", curState?"on":"off", validCount);
      for(int i = 0 ; i < 5 ; i++){
        pressedTimes[i]=0;
      }
    }

    return newState;
}

void loop(){
  
	server.handleClient();
	mqttClient.loop();
	webSocket.loop();
	
	long now = millis();
  
	if(queueMover > 5){
		bool curState = devRtuSwitch->getSwitchState(6);
		bool newState = !curState;
		Serial.printf("mover turn %s\n", newState?"ON":"OFF");
		devRtuSwitch->change_switch("06", newState==1?"ON":"OFF");
		queueMover = 0;
		queueInverter = 0;
	}
	if(queueInverter > 5){
		bool curState = devUpower->switch_state[0];
		bool newState = !curState;
		Serial.printf("Inverter turn %s\n", newState?"ON":"OFF");
		devUpower->change_switch("inverter", newState==1?"ON":"OFF");
		queueInverter = 0;
		queueMover = 0;
	}

	if (now - lastMsg > 1000) {
		lastMsg = now;
		printf("set 0 : %d, %d\n",queueMover, queueInverter);
		queueMover = 0;
		queueInverter = 0;
		device_cycle(loop_count++);
		loop_count = loop_count%numOfDevice;
		
	}

	
}





void messageRouter(char* topic, byte* rawMessage, unsigned int length) {
	char message[20]= {0,};
	for(int i = 0 ; i < length ; i++){
		message[i] = rawMessage[i];
	}
	Serial.printf("Message arrived on topic: %s Message: %s", topic, message);
  
	const char* token = "/";

	char* homeassistant = strtok(topic,token);//homeassistant
	char* device_type = strtok(NULL,token);//switch
	char* device_domain = strtok(NULL,token);//upower
	char* switch_name = strtok(NULL,token);//inverter

	Serial.printf("message incomming : %s\n", message);

	for(int i = 0 ; i < numOfDevice; i++){

		Serial.printf("Ask to %d device\n", i);
		if(strcmp(device_domain, devices[i]->getDeviceName()) == 0){
			Serial.printf("Matched %d device\n", i);
			devices[i]->change_switch(switch_name, message);
		}
		
	}

	if(strcmp(device_domain, "restart") == 0){
		Serial.println("all modules reset");
		resetFunc();
	}else if(strcmp(device_domain, "reset") == 0){
		Serial.println("register MQTT Device");
		setup_mqtt_device();
	}
}


//web service

void webRootHandler(){

}

void webSettingsHandler() {
	String buf = String(F("<head> \
<script> \
	var wSocket = new WebSocket(\"ws://\"+window.location.hostname+\":81/\"); \
	wSocket.onopen = function(e) { \
		alert(\"[open] Connection established\"); \
		alert(\"Sending to server\"); \
		wSocket.send(\"My name is John\"); \
	}; \
	wSocket.onmessage = function(e) { \
		var tag = document.getElementById(\"log\"); \
		var text = document.createElement(\"p\"); \
		text.innerHTML = \"set ssid : \"+e.data; \
		tag.appendChild(text); \
	}; \
	function send(){ \
		wSocket.send(\"aaaa\"); \
	}; \
	wSocket.onerror = function(error) { \
		alert(`[error] ${error.message}`); \
	}; \
</script> \
</head> \
<body> \
	<form action=\"/setssid\"  method=\"post\"> \
		<label for=\"ssid\">ssid:</label><br> \
		<input type=\"text\" id=\"ssid\" name=\"ssid\" value=\"")) + \
		ssid + \
		String(F("\"><br> \
		<label for=\"password\">password:</label><br> \
		<input type=\"text\" id=\"password\" name=\"password\" value=\"")) + 
		ssid_pw + \
		String(F("\"><br> \
	<label for=\"mqtt_address\">mqtt_address:</label><br> \
	<input type=\"text\" id=\"mqtt_address\" name=\"mqtt_address\" value=\"")) + \
	mqtt_addr + \
	String(F("\"><br> \
	<label for=\"mqtt_port\">mqtt_port:</label><br> \
	<input type=\"text\" id=\"mqtt_port\" name=\"mqtt_port\" value=\"")) + \
	mqtt_port + \
	String(F("\"><br> \
	<label for=\"mqtt_id\">mqtt_id:</label><br> \
	<input type=\"text\" id=\"mqtt_id\" name=\"mqtt_id\" value=\"")) + \
	mqtt_client_id + \
	String(F("\"><br> \
	<label for=\"mqtt_password\">mqtt_password:</label><br> \
	<input type=\"text\" id=\"mqtt_password\" name=\"mqtt_password\" value=\"")) + \
	mqtt_client_pw + 
	String(F("\"><br> \
		<input type=\"submit\" value=\"Submit\"> \
	</form> \
	<form action=\"/reset\"> \
		<input type=\"submit\" value=\"Reset\"> \
	</form> \
	<input type=\"button\" value=\"send\" onclick=\"send()\" > \
</body>"));


	server.send(200, "text/html", buf);
}

void handleRestart() {
	server.send(200, "text/html", "<h1>will be restart!</h1>");
	resetFunc();
}

void handleReset() {
	server.send(200, "text/html", "<h1>will be reset all entity!</h1>");
	// delete_params();
	setup_mqtt_device();
}


void handleSetSSID() {
	if( ! server.hasArg("ssid") || ! server.hasArg("password") 
		|| ! server.hasArg("mqtt_address") || ! server.hasArg("mqtt_port") 
		||! server.hasArg("mqtt_id") || ! server.hasArg("mqtt_password") 
		|| server.arg("ssid") == NULL || server.arg("password") == NULL
		|| server.arg("mqtt_address") == NULL || server.arg("mqtt_port") == NULL
		|| server.arg("mqtt_id") == NULL || server.arg("mqtt_password") == NULL) { // If the POST request doesn't have username and password data
	server.send(400, "text/plain", "400: Invalid Request");         // The request is invalid, so send HTTP status 400
	return;
	}

	char ssid[30], password[30];
	char mqtt_id[30], mqtt_password[30];
	char mqtt_address[30], mqtt_port_[30];

	strcpy(ssid, server.arg("ssid").c_str());
	strcpy(password, server.arg("password").c_str());
	strcpy(mqtt_address, server.arg("mqtt_address").c_str());
	strcpy(mqtt_port_, server.arg("mqtt_port").c_str());
	strcpy(mqtt_id, server.arg("mqtt_id").c_str());
	strcpy(mqtt_password, server.arg("mqtt_password").c_str());

	Serial.println(ssid);
	Serial.println(password);

	setWifiInfo(ssid, password, mqtt_address, mqtt_port_, mqtt_id, mqtt_password);
	server.send(200, "text/html", "<h1>Set Success as " + server.arg("ssid") + "!</h1>");

	read_params();

	setup_wifi();
	setup_mqtt();
}

void webNotFoundHandler(){
  server.send(404, "text/plain", "404: Not found");
}


//web socket
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
	// return;
	IPAddress ip;
	switch(type){
		case WStype_DISCONNECTED:
			Serial.printf("[websocket] Disconnected!\n");
			break;
		case WStype_CONNECTED:
			ip = webSocket.remoteIP(num);
			Serial.printf("[websocket] Connected from %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
			break;
		case WStype_TEXT :
			Serial.printf("[websocket] get Text: %s(%d)\n",payload, length);
			break;
		default :
			break;
	}
}




//EEPROM


int read_word(int addr, char* data){
	char readChar;
	int i = addr;
	char checksum = 0;
	int found = 0;

	while ( i < addr+35){
		readChar = char(EEPROM.read(i));

//		Serial.printf("%c(%02x) @%d\r\n", readChar, readChar, i);

		if (readChar == '\0')
		{
			found = 1;
			char written_checksum = char(EEPROM.read(i+1));
			if(written_checksum != checksum){
				data[0] = '\0';
				return -1;
			}
			data[i-addr] = '\0';
			break;
		}

		checksum += readChar;
		data[i-addr] = readChar;
		delay(10);
		i++;
	}
	if(!found){
		data[0] = '\0';
		i = 0;
	}
	return i;
}

int write_word(int addr, char* word2){
	delay(10);
	int str_len = strlen(word2);
	char checksum = 0;

	for (int i = addr; i < str_len + addr; ++i){
		char cur_char = word2[i - addr];
		Serial.printf("%c(%02x) @%d\r\n", cur_char, cur_char, i);
		EEPROM.write(i, cur_char);
		checksum += cur_char;
	}

	EEPROM.write(str_len + addr, '\0');
	EEPROM.write(str_len + addr + 1, checksum);

	Serial.printf("%c(%02x) @%d\r\n", '\0', '\0', addr);
	Serial.printf("%c(%02x) @%d\r\n", checksum, checksum, addr + 1);

	EEPROM.commit();
	return str_len;
}




void read_params(){
	read_word(0, ssid);
	read_word(32,ssid_pw);
	read_word(64,mqtt_addr);
	read_word(96,mqtt_port);
	read_word(128,mqtt_client_id);
	read_word(160,mqtt_client_pw);
}



void delete_params(){
	write_word(0, (char*)"\0");
	write_word(32, (char*)"\0");
	write_word(64, (char*)"\0");
	write_word(96, (char*)"\0");
	write_word(128, (char*)"\0");
	write_word(160, (char*)"\0");
}




void setWifiInfo(char* SSID, char* PW,char* MQT_ADR, char* MQT_PORT,char* MQT_ID, char* MQT_PW){
	write_word(0, SSID);
	write_word(32, PW);
	write_word(64, MQT_ADR);
	write_word(96, MQT_PORT);
	write_word(128, MQT_ID);
	write_word(160, MQT_PW);
}

int getWifiInfo(char* SSID, char* PW){

	
	if(  strlen(ssid)> 0 &&  strlen(ssid_pw)> 0){
		Serial.printf("ssid read from eeprom %s, %s\r\n", SSID, PW);
		return 0;
	}else{
		return 1;
	}
}
