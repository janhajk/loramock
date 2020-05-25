#ifndef CREDENTIALS_LORA_h
#define CREDENTIALS_LORA_h
#include <Arduino.h>


// dev EUI in LSB mode.
static const extern u1_t PROGMEM DEVEUI[];

// App EUI in LSB mode
static const extern u1_t PROGMEM APPEUI[];

// App Key in MSB mode
static const extern u1_t PROGMEM APPKEY[];




#endif