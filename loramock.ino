#include <Arduino.h>

#include <ctype.h>

// LoraWan Libraries
#include <lmic.h> // https://doc.sm.tc/mac/programming.html
#include <hal/hal.h>
#include <SPI.h>

// OLED Bildschirm
#include <SSD1306.h>

// Server
#include <ArduinoJson.h>
#include <map>
#include <functional>
#include <UrlTokenBindings.h>
#include <RichHttpServer.h>

// **********************************************
// Pin Definitionen
// **********************************************

// Pins wo LoRa Modul angeschlossen sind
#define LORA_PIN_NSS 18
#define LORA_PIN_RXTX LMIC_UNUSED_PIN
#define LORA_PIN_RST 23
#define LORA_PIN_DIO0 26
#define LORA_PIN_DIO1 33
#define LORA_PIN_DIO2 32

// Pin der grünen LED
#define LEDPIN 25

// Pins und Adresse des OLED Screens
#define OLED_I2C_ADDR 0x3C
#define OLED_SDA 21
#define OLED_SCL 22
SSD1306 display (OLED_I2C_ADDR, OLED_SDA, OLED_SCL);


// NTP Zeit
#include "time.h"
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200; // UTC+2h
const int   daylightOffset_sec = 0; // Soomer/Winterzeit

// Programmversion
#define VERSION "1.0"


// OTA  
#include "OTA.h"
unsigned long entry;


// **********************************************
// Webserver Credentials
// **********************************************
// Wifi
#include <credentials.h>
const char* WIFI_SSID     = mySSID;
const char* WIFI_PASSWORD = myPASSWORD;
TaskHandle_t threadWebserver; // für multithread (webserver läuft auf core 0)

//using namespace::placeholders;

// Define shorthand for common types
using RichHttpConfig = RichHttp::Generics::Configs::EspressifBuiltin;
using RequestContext = RichHttpConfig::RequestContextType;

// Use the default auth provider
SimpleAuthProvider authProvider;

// Use the builtin server (ESP8266WebServer for ESP8266, WebServer for ESP32).
// Listen on port 80.
RichHttpServer<RichHttpConfig> server(80, authProvider);

static osjob_t handleServerRequestsJob;

unsigned int counter = 0;
char TTN_response[30];


// Downlink Messages

std::map<size_t, std::map<char*, String>> downloadlinkMsg;
size_t nextId = 1;


#include "credentials_lora1.h"

void os_getDevEui (u1_t* buf) {
  memcpy_P(buf, DEVEUI, 8);
}

void os_getArtEui (u1_t* buf) {
  memcpy_P(buf, APPEUI, 8);
}

void os_getDevKey (u1_t* buf) {
  memcpy_P(buf, APPKEY, 16);
}


static uint8_t LORAWAN_PORT = 1;

static osjob_t sendjob;

// Zwischenspeicher für uplink payload/message
static uint8_t message[52] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"; // 51 bytes; letztes ist "\0"
static int mLength = 51;


// Lora Pin mapping
const lmic_pinmap lmic_pins = {
  .nss = LORA_PIN_NSS,
  .rxtx = LORA_PIN_RXTX,
  .rst = LORA_PIN_RST,
  .dio = {LORA_PIN_DIO0, LORA_PIN_DIO1, LORA_PIN_DIO2}
};


// helper zum heruasfinden, ob ein String ein Integer ist
inline bool isInteger(const String & s) {
  if (((!isdigit(s[0])) && (s[0] != '-') && (s[0] != '+'))) return false;
  char * p;
  strtol(s.c_str(), &p, 10);
  return (*p == 0);
}



// Sendet ein Uplink
// Kopiert die Message in den Payload
void do_send(osjob_t* j) {
  Serial.println("Sending Message...");
  uint8_t messagePayload[mLength + 1];
  memcpy(messagePayload, message, mLength);

  // Sendet nicht, wenn schon eine Transaktion läuft
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  }
  else {
    // Data Format ist: uint8_t[]
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    //
    LMIC_setTxData2(LORAWAN_PORT, messagePayload, sizeof(messagePayload) - 1, 0);
    Serial.println(F("Sending uplink packet..."));
    digitalWrite(LEDPIN, HIGH);
    display.clear();
    display.drawString (0, 0, "Sending uplink packet...");
    display.drawString (0, 50, String (++counter));
    display.display ();
  }
  // Next TX is scheduled after TX_COMPLETE event.
}


// onEvent ist eine Funktion von LMIC und wird aufgerufen
// wenn durch LoRa ein Event auftritt
void onEvent (ev_t ev) {
  Serial.print("LMIC running on core ");
  Serial.println(xPortGetCoreID());
  Serial.print(os_getTime());
  Serial.print(": ");
  display.drawString (64, 22, String(ev));
  switch (ev) {
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
      display.clear();
      display.drawString (0, 0, "EV_TXCOMPLETE event!");

      if (LMIC.txrxFlags & TXRX_ACK) {
        Serial.println(F("Received ack"));
        display.drawString (0, 20, "Received ACK.");
      }

      Serial.print("LMIC.dataBeg: "); Serial.println(LMIC.dataBeg);
      Serial.print("LMIC.dataLen: "); Serial.println(LMIC.dataLen);
      Serial.print("LMIC.frame: ");  Serial.println((char*)LMIC.frame);
      Serial.print("LMIC.rssi: "); Serial.println(LMIC.rssi);
      Serial.print("TXRX_PORT: "); Serial.println(TXRX_PORT);

      if (LMIC.dataLen) {
        // data received in rx slot after tx
        Serial.print(F("Data Received: "));
        Serial.write(LMIC.frame + LMIC.dataBeg, LMIC.dataLen);
        Serial.println();
        Serial.println(LMIC.rssi);


        display.drawString (0, 9, "Received DATA.");

        // Hex-String in Hex-Format umwandeln
        String string = "";
        char const hex_chars[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
        for ( int i = 0; i < LMIC.dataLen; ++i ) {
          char const byte = LMIC.frame[LMIC.dataBeg + i];
          string += hex_chars[ ( byte & 0xF0 ) >> 4 ];
          string += hex_chars[ ( byte & 0x0F ) >> 0 ];
        }

        display.drawString (0, 22, string);
        display.drawString (0, 32, String(LMIC.rssi));
        display.drawString (64, 32, String(LMIC.snr));

        size_t id = nextId++;
        if (nextId > 3) nextId = 0;
        downloadlinkMsg[id]["payload"] = string;
        downloadlinkMsg[id]["rssi"] = String(LMIC.rssi);
        downloadlinkMsg[id]["snr"] = String(LMIC.snr);
        downloadlinkMsg[id]["timestamp"] = String(time(NULL));
        downloadlinkMsg[id]["port"] = String(LMIC.frame[LMIC.dataBeg - 1]);

      }
      digitalWrite(LEDPIN, LOW);
      display.drawString (0, 50, String (counter));
      break;
    case EV_JOINING:
      Serial.println(F("EV_JOINING: -> Joining..."));
      display.drawString(0, 16 , "OTAA joining....");
      break;
    case EV_JOINED: {
        Serial.println(F("EV_JOINED"));
        display.clear();
        display.drawString(0 , 0 ,  "Joined!");
        LMIC_setLinkCheckMode(0);
      }
      break;
    case EV_RXCOMPLETE:
      // data received in ping slot
      Serial.println(F("EV_RXCOMPLETE"));
      break;
    case EV_LINK_DEAD:
      Serial.println(F("EV_LINK_DEAD"));
      break;
    case EV_LINK_ALIVE:
      Serial.println(F("EV_LINK_ALIVE"));
      break;
    default:
      Serial.println(F("Unknown event"));
      break;
  }
  display.display ();
}


static void handleServerRequests (osjob_t* j) {
  server.handleClient();
}


// Webserver Auth
void handleAuth(RequestContext& request) {
  JsonObject obj = request.getJsonBody().as<JsonObject>();

  if (obj.containsKey("username") && obj.containsKey("password")) {
    authProvider.requireAuthentication(obj["username"], obj["password"]);
  } else {
    authProvider.disableAuthentication();
  }

  request.response.json["success"] = true;
}

void handleStaticResponse(const char* response) {
  server.send(200, "text/plain", response);
}

// Server Route für /about
void handleAbout(RequestContext& request) {
  request.response.json["name"] = "Lora Mock REST API";
  request.response.json["version"] = VERSION;
  request.response.json["localIP"] = WiFi.localIP().toString();
  request.response.json["free_heap"] = ESP.getFreeHeap();
  request.response.json["endpoints"] = "GET / > this info \n POST /uplink uplink mit JSON {port:port,payload:payload} \n GET /downlink returns JSON[{payload,port,timestamp}] \n ";
}


// Server Route für /about
void handleLmicReset(RequestContext& request) {
  os_setCallback(&sendjob, initfunc);
  request.response.json["success"] = true;

}




// Server Route für POST /uplink
//
// behandelt ein Payload Uplink, welcher via POST request geschickt wird
// Input ist ein JSON Objekt mit folgenden Keys:
// "port"          : Port auf welchen uplink erfolgt; default=1
// "payload"       : payload als String (kein HEX; String wird vor dem senden in HEX umgewandelt)
//
// return ist ein JSON Objekt mit den Werten:
// "received_port"          : Port auf welchen uplink erfolgt ist
// "received_payload"       : gesendeter payload als String

// fehlt der "payload" wird code 400 mit der Fehlermeldung zurückgegeben

void handleSendPayload(RequestContext& request) {
  Serial.println("receiving payload on POST...");
  request.server.sendHeader("Access-Control-Allow-Origin", "*");
  request.server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  JsonObject body = request.getJsonBody().as<JsonObject>();
  String sPayload;
  const char* port;

  // Liest den Port, sofern angegeben (default=1)
  if (! body["port"].isNull()) {
    port = body["port"].as<char*>();
    LORAWAN_PORT = atoi(port);
  }
  else {
    LORAWAN_PORT = 1;
  }

  // Liest payload, sofern angegeben und triggerd uplink
  if (! body["payload"].isNull()) {
    const char* payload = body["payload"].as<char*>();
    sPayload = payload;
    int str_len = sPayload.length() + 1;
    mLength = str_len - 1;
    uint8_t m[str_len];
    int i = 0;
    for (i; i < sPayload.length(); i++) {
      message[i] = sPayload[i];
    }
    Serial.println(payload);

    // Payload wird sofort versendet
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(0), do_send);

    // Response JSON
    request.response.json["received_payload"] = sPayload;
    request.response.json["received_port"] = port;
  }
  else {
    // Bei unvollständigem POST request
    request.response.setCode(400);
    request.response.json["error"] = "Key mit dem Namen ´payload´ erforderlich";
  }
}




// Server Route für GET /downlink
//
// Listet alle Downlink-Messages und gibt diese als JSON Array im response zurück
//
void handleDownlink(RequestContext& request) {
  JsonArray arr = request.response.json.createNestedArray("downlink_messages");

  std::map<size_t, std::map<char*, String>>::iterator it;
  std::map<char*, String>::iterator itsub;
  for (it = downloadlinkMsg.begin(); it != downloadlinkMsg.end(); ++it) {
    JsonObject obj = arr.createNestedObject();
    obj["id"] = it->first;
    std::map<char*, String> &dataElement = it->second;
    for (itsub = dataElement.begin(); itsub != dataElement.end(); ++itsub) {
      if (isInteger(itsub->second)) {
        obj[itsub->first] = String(itsub->second).toInt();
      }
      else {
        obj[itsub->first] = itsub->second;
      }
    }
  }
}


static void initfunc (osjob_t* j) {
  LMIC_reset();
  LMIC_setClockError(5 * MAX_CLOCK_ERROR / 1000);
  LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);      // g-band
  LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
  LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK,  DR_FSK),  BAND_MILLI);      // g2-band
  LMIC.dn2Dr = DR_SF12;
  LMIC_startJoining();
}



// bootfunktion
void setup() {
  Serial.begin(115200);
  delay(2500);                      // Give time to the serial monitor to pick up
  Serial.println(F("Starting..."));


  ArduinoOTA.setHostname("LoraMock");
  setupOTA();


  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  server
  .buildHandler("/uplink")
  .setDisableAuthOverride()
  .on(HTTP_POST, handleSendPayload);
  server
  .buildHandler("/downlink")
  .setDisableAuthOverride()
  .on(HTTP_GET, handleDownlink);
  server
  .buildHandler("/lmic/reset")
  .setDisableAuthOverride()
  .on(HTTP_GET, handleLmicReset);
  server
  .buildHandler("/")
  .setDisableAuthOverride()
  .on(HTTP_GET, handleAbout);
  server.clearBuilders();
  server.begin();


  // Use the green pin to signal transmission.
  pinMode(LEDPIN, OUTPUT);


  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);


  // DISPLAY
  display.init ();
  display.flipScreenVertically ();
  display.setFont (ArialMT_Plain_10);

  display.setTextAlignment (TEXT_ALIGN_LEFT);

  display.drawString (0, 0, "Starting....");
  display.display ();

  // Pin webserver to cpu core 1 (0/1)
  xTaskCreatePinnedToCore(
                    threadWebserverCode,   /* Task function. */
                    "threadWebserver",     /* name of task. */
                    10000,       /* Stack size of task */
                    (void *)1,        /* parameter of the task */
                    1,           /* priority of the task */
                    &threadWebserver,/* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */
                    

  // start LMIC
  os_init();
  os_setCallback(&sendjob, initfunc);
  os_runloop();
}

void threadWebserverCode( void * pvParameters ){
  vTaskDelay(5000);
  for(;;){
    // Webserver requests handeln
    server.handleClient();
    ArduinoOTA.handle();
    vTaskDelay(1);
    //    Serial.print("Webserver running on core ");
    //    Serial.println(xPortGetCoreID());
  }
}

// loop bootfunktion
// Keine Verwendung, da mit LMIC Callbacks gearbeitet wird
void loop() {
}