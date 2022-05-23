#ifndef COMMON_H
#define COMMON_H
#define CURRENT_VERSION "0.1.1"

#define NUM_MAX_CELL 16
#define NUM_SWITCH 3
#define NUM_RTU_MK1_SW 8
#define NUM_RTU_MK2_SW 4
#define MQTT_CLIENT_NAME "ESP32"

#define MQTT_MAX_PACKET_SIZE 1024


#define INVERTER_STATE_PIN 25
#define MOVER_STATE_PIN 26

#define INVERTER_BUTTON_PIN 12
#define MOVER_BUTTON_PIN 14

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#endif
