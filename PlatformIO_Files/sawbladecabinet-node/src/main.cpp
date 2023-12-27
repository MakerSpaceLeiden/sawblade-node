/*
      Copyright 2015-2018 Dirk-Willem van Gulik <dirkx@webweaving.org>
                          Stichting Makerspace Leiden, the Netherlands.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef ESP32
#error "The Sawblade Cabinet uses an ESP32 based board (Olimex ESP32-PoE)!"
#endif

// An Olimex ESP32-PoE is used (default is POESP board (Aart board))
#define ESP32_PoE

#include <Arduino.h>
#include <PowerNodeV11.h>
#include <ACNode.h>
#include <RFID.h>   // NFC version
#include <SIG2.h>
#include <Cache.h>
#include <EEPROM.h>

#include "passwd.h"
/* If using WiFi instead of a fixed ethernet connection, and/or OTA:

Add Passwd.h to the sawbladecabinet-node/src directoy containing the following information

#pragma once

#define WIFI_NETWORK "YOUR_SSID"
#define WIFI_PASSWD "YOUR_PASSWD"

#define OTA_PASSWD "YOUR_OTA_PASSWD"
*/

#define MACHINE "sawbladecabinet"
#define DOORLOCK1 "sawbladecabinet"
#define DOORLOCK2 "sawbladecabinetplus"

#define DOORLOCK1_RELAIS      (4)
#define DOORLOCK2_RELAIS      (2)
#define SYSTEM_LED            (5)

// Introduced by alex - 2020-01-8

// Clear EEProm + Cache button
// Press BUT1 on Olimex ESP32 PoE module before (re)boot of node
// keep BUT1 pressed for at least 5 s
// After the release of BUT1 node will restart with empty EEProm and empty cache
#define CLEAR_EEPROM_AND_CACHE_BUTTON         (34)
#define CLEAR_EEPROM_AND_CACHE_BUTTON_PRESSED (LOW)
#define MAX_WAIT_TIME_BUTTON_PRESSED          (4000)  // in ms

#define DOOR_OPEN_TIME                          (250) // in ms
#define CHECK_NFC_READER_AVAILABLE_TIME_WINDOW  (10000) // in ms 
#define GPIOPORT_I2C_RECOVER_TRANSISTOR         (15)

//#define OTA_PASSWD "MyPassWoord"
//ACNode node = ACNode(MACHINE, WIFI_NETWORK, WIFI_PASSWD

#ifdef WIFI_NETWORK
ACNode node = ACNode(MACHINE, WIFI_NETWORK, WIFI_PASSWD);
#else
ACNode node = ACNode(MACHINE);
#endif

#define USE_CACHE_FOR_TAGS true
#define USE_NFC_RFID_CARD true

RFID reader = RFID(USE_CACHE_FOR_TAGS, USE_NFC_RFID_CARD); // use tags are stored in cache, to allow access in case the MQTT server is down; also use NFC RFID card

MqttLogStream mqttlogStream = MqttLogStream();
TelnetSerialStream telnetSerialStream = TelnetSerialStream();

#ifdef OTA_PASSWD
OTA ota = OTA(OTA_PASSWD);
#endif

LED aartLed = LED(SYSTEM_LED);    // defaults to the aartLed - otherwise specify a GPIO.

typedef enum {
  BOOTING, OUTOFORDER,      // device not functional.
  REBOOT,                   // forcefull reboot
  TRANSIENTERROR,           // hopefully goes away level error
  NOCONN,                   // sort of fairly hopeless (though we can cache RFIDs!)
  WAITINGFORCARD,           // waiting for card.
  CHECKINGCARD,
  CLEARSTATUS,
  APPROVED,
  REJECTED,
  LOCKOPEN,
  LOCKCLOSED,
} machinestates_t;

#define NEVER (0)

struct {
  const char * label;                   // name of this state
  LED::led_state_t ledState;            // flashing pattern for the aartLED. Zie ook https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1.
  time_t maxTimeInMilliSeconds;         // how long we can stay in this state before we timeout.
  machinestates_t failStateOnTimeout;   // what state we transition to on timeout.
  unsigned long timeInState;
  unsigned long timeoutTransitions;
  unsigned long autoReportCycle;
} state[LOCKCLOSED + 1] =
{
  { "Booting",                LED::LED_ERROR,                   120 * 1000, REBOOT,         0 },
  { "Out of order",           LED::LED_ERROR,                   120 * 1000, REBOOT,         5 * 60 * 1000 },
  { "Rebooting",              LED::LED_ERROR,                   120 * 1000, REBOOT,         0 },
  { "Transient Error",        LED::LED_ERROR,                     5 * 1000, WAITINGFORCARD, 5 * 60 * 1000 },
  { "No network",             LED::LED_FLASH,                        NEVER, NOCONN,         0 },
  { "Waiting for card",       LED::LED_IDLE,                         NEVER, WAITINGFORCARD, 0 },
  { "Checking card",          LED::LED_PENDING,                   5 * 1000, REJECTED,       0 },
  { "Clear status",           LED::LED_PENDING,                      NEVER, WAITINGFORCARD, 0 },
  { "Approved card",          LED::LED_PENDING,                  60 * 1000, CLEARSTATUS,    0 },
  { "Rejected",               LED::LED_ERROR,                     5 * 1000, CLEARSTATUS,    0 },
  { "Door lock is open",      LED::LED_ON,                  DOOR_OPEN_TIME, LOCKCLOSED,     0 },
  { "Door lock is closed",    LED::LED_ON,                           NEVER, WAITINGFORCARD, 0 },
};

unsigned long laststatechange = 0, lastReport = 0;
static machinestates_t laststate = OUTOFORDER;
machinestates_t machinestate = BOOTING;

const char * membertag;
static bool pendingapprovaldoorlock2;

// to handle onconnect only once (only after reboot)
static bool firstOnConnectTime = true;

unsigned long lastCheckNFCReaderTime = 0;

void checkClearEEPromAndCacheButtonPressed(void) {
  unsigned long ButtonPressedTime;
  unsigned long currentSecs;
  unsigned long prevSecs;
  bool firstTime = true;

  // check CLEAR_EEPROM_AND_CACHE_BUTTON pressed
  pinMode(CLEAR_EEPROM_AND_CACHE_BUTTON, INPUT);
  // check if button is pressed for at least 3 s
  Log.println("Checking if the button is pressed for clearing EEProm and cache");
  ButtonPressedTime = millis();  
  prevSecs = MAX_WAIT_TIME_BUTTON_PRESSED / 1000;
  Log.print(prevSecs);
  Log.print(" s");
  while (digitalRead(CLEAR_EEPROM_AND_CACHE_BUTTON) == CLEAR_EEPROM_AND_CACHE_BUTTON_PRESSED) {
    if (millis() >= MAX_WAIT_TIME_BUTTON_PRESSED) {
      if (firstTime == true) {
        Log.print("\rPlease release button");
        firstTime = false;
      }
    } else {
      currentSecs = (MAX_WAIT_TIME_BUTTON_PRESSED - millis()) / 1000;
      if ((currentSecs != prevSecs) && (currentSecs >= 0)) {
        Log.print("\r");
        Log.print(currentSecs);
        Log.print(" s");
        prevSecs = currentSecs;
      }
    }
  }
  if (millis() >= (ButtonPressedTime + MAX_WAIT_TIME_BUTTON_PRESSED)) {
    Log.print("\rButton for clearing EEProm and cache was pressed for more than ");
    Log.print(MAX_WAIT_TIME_BUTTON_PRESSED / 1000);
    Log.println(" s, EEProm and Cache will be cleared!");
    // Clear EEPROM
    EEPROM.begin(1024);
    wipe_eeprom();
    Log.println("EEProm cleared!");
    // Clear cache
    prepareCache(true);
    Log.println("Cache cleared!");
    // wait until button is released, than reboot
    while (digitalRead(CLEAR_EEPROM_AND_CACHE_BUTTON) == CLEAR_EEPROM_AND_CACHE_BUTTON_PRESSED) {
      // do nothing here
    }
    Log.println("Node will be restarted");
    // restart node
    ESP.restart();
  } else {
    Log.println("\rButton was not (or not long enough) pressed to clear EEProm and cache");
  }
}

void checkNFCReaderAvailable() {
  if (USE_NFC_RFID_CARD) {
    // Error in communication with RFID reader, try resetting communication
    pinMode(RFID_CLK_PIN, OUTPUT);
    digitalWrite(RFID_CLK_PIN, 0);
    pinMode(RFID_SDA_PIN, OUTPUT);
    digitalWrite(RFID_SDA_PIN, 0);

    pinMode(GPIOPORT_I2C_RECOVER_TRANSISTOR, OUTPUT);
    digitalWrite(GPIOPORT_I2C_RECOVER_TRANSISTOR, 1);

    delay(500);

    pinMode(GPIOPORT_I2C_RECOVER_TRANSISTOR, OUTPUT);
    digitalWrite(GPIOPORT_I2C_RECOVER_TRANSISTOR, 0);
    reader.begin();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  // for recovery relay I2C
  pinMode(GPIOPORT_I2C_RECOVER_TRANSISTOR, OUTPUT);
  digitalWrite(GPIOPORT_I2C_RECOVER_TRANSISTOR, 0);

  pinMode(DOORLOCK1_RELAIS, OUTPUT);
  pinMode(DOORLOCK2_RELAIS, OUTPUT);
  // the relays are the low triggered relay type
  digitalWrite(DOORLOCK1_RELAIS, 1);
  digitalWrite(DOORLOCK2_RELAIS, 1);
  
  checkClearEEPromAndCacheButtonPressed();

  node.set_mqtt_prefix("ac");
  node.set_master("master");
 
  node.onConnect([]() {
    Log.println("Connected");
    if (firstOnConnectTime == true) {
      firstOnConnectTime = false;
      machinestate = WAITINGFORCARD;
    }
  });

  node.onDisconnect([]() {
    Log.println("Disconnected");
    machinestate = NOCONN;
  });
  node.onError([](acnode_error_t err) {
    Log.printf("Error %d\n", err);
    machinestate = WAITINGFORCARD;

  });

  node.onApproval([](const char *machine) {
      if ((machinestate == WAITINGFORCARD) || (machinestate == CHECKINGCARD)) {  
        // approval for outer-door-lock 
          if (strcmp(machine, DOORLOCK1) == 0) {
              Debug.print("Got approve for machine: ");
              Debug.println(machine);    
              digitalWrite(DOORLOCK1_RELAIS, 0); // Unlock the outer-door-lock
              pendingapprovaldoorlock2 = true;
              machinestate = LOCKOPEN; // Update machine state as needed
          } 
          // approval for inner-door-lock    
          else if (strcmp(machine, DOORLOCK2) == 0) {
              Debug.print("Got approve for machine: ");
              Debug.println(machine);       
              digitalWrite(DOORLOCK2_RELAIS, 0); // Unlock the inner-door-lock
              pendingapprovaldoorlock2 = false;
              machinestate = LOCKOPEN; // Update machine state as needed
          }
      }
  });
  
  node.onDenied([](const char * machine) {
    Debug.println("Got denied");
    if (machinestate > REJECTED) {
      Debug.println("Denied ingnored, one of the doors is already open");
    } else {
      machinestate = REJECTED;
    }
  });

  node.set_report_period(20 * 1000);
  node.onReport([](JsonObject  & report) {
    report["state"] = state[machinestate].label;
    #ifdef OTA_PASSWD
      report["ota"] = true;
    #else
      report["ota"] = false;
    #endif
    });

  reader.onSwipe([](const char * tag) -> ACBase::cmd_result_t {
    // avoid swithing messing with the door swipe process
    if (machinestate > CHECKINGCARD) {
      Debug.printf("Ignoring a normal swipe - as we're still in some open process.");
      checkNFCReaderAvailable();
      return ACBase::CMD_CLAIMED;
    }

    // We'r declining so that the core library handle sending
    // an approval request, keep state, and so on.
    //
    Debug.printf("Detected a normal swipe.\n");
    membertag = tag;
    machinestate = CHECKINGCARD;    
    return ACBase::CMD_DECLINE;
  });

  // This debug setting reports things such as FW version of the card; which can 'wedge' it. So we
  // disable it unless we absolutely need that information.
  //
  reader.set_debug(false);
  node.addHandler(&reader);
  
  #ifdef OTA_PASSWD
    node.addHandler(&ota);
  #endif

  Log.addPrintStream(std::make_shared<MqttLogStream>(mqttlogStream));
  auto t = std::make_shared<TelnetSerialStream>(telnetSerialStream);
  Log.addPrintStream(t);
  Debug.addPrintStream(t);

  //   node.set_debug(true);
  //   node.set_debugAlive(true);

  // if Olimex ESP32-PoE board is used
  #ifdef ESP32_PoE  
    node.begin(BOARD_OLIMEX);
  #endif

  // if default, board (POESP, board Aart) is used
  #ifndef ESP32_PoE
    node.begin();
  #endif
    Log.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );
}

unsigned long now;

void loop() {

  node.loop();

  if ((USE_NFC_RFID_CARD) && (!pendingapprovaldoorlock2)) {
    now = millis();
    if ((now - lastCheckNFCReaderTime) > CHECK_NFC_READER_AVAILABLE_TIME_WINDOW) {
      lastCheckNFCReaderTime = now;
      Serial.print("Check Reader Available\n\r");
      checkNFCReaderAvailable();
    }
  }
  
  if (laststate != machinestate) {
    Debug.printf("Changed from state <%s> to state <%s>\n",
                 state[laststate].label, state[machinestate].label);

    state[laststate].timeInState += (millis() - laststatechange) / 1000;
    laststate = machinestate;
    laststatechange = millis();
    return;
  }

  if (state[machinestate].maxTimeInMilliSeconds != NEVER &&
      (millis() - laststatechange > state[machinestate].maxTimeInMilliSeconds))
  {
    state[machinestate].timeoutTransitions++;

    laststate = machinestate;
    machinestate = state[machinestate].failStateOnTimeout;

    Log.printf("Time-out; transition from <%s> to <%s>\n",
               state[laststate].label, state[machinestate].label);
    return;
  };

  if (state[machinestate].autoReportCycle && \
      millis() - laststatechange > state[machinestate].autoReportCycle && \
      millis() - lastReport > state[machinestate].autoReportCycle)
  {
    Log.printf("State: %s now for %lu seconds", state[laststate].label, (millis() - laststatechange) / 1000);
    lastReport = millis();
  };

  aartLed.set(state[machinestate].ledState);

  switch (machinestate) {
    case REBOOT: 
      node.delayedReboot();   
      break;
    case WAITINGFORCARD:
    case CHECKINGCARD:
       break;   
     case CLEARSTATUS:
      machinestate = WAITINGFORCARD;
      break;     
    case REJECTED:
      machinestate = WAITINGFORCARD;
      break;
    case LOCKOPEN:
      // keep the lock open
      break;
    case LOCKCLOSED:
      // If outer-door-lock is opened and  approval for inner-door-lock is pending; 
      if (pendingapprovaldoorlock2 == true) {
          digitalWrite(DOORLOCK1_RELAIS, 1); // Lock the outer-door-lock
          digitalWrite(DOORLOCK2_RELAIS, 1); // Lock the inner-door-lock     
          node.request_approval(membertag, "energize", DOORLOCK2); // request approval for inner-door-lock
          membertag = "none"; // after using it, set the membertag value back to none
          machinestate = CHECKINGCARD;
          pendingapprovaldoorlock2 = false;
          }
      else {
          digitalWrite(DOORLOCK1_RELAIS, 1); // Lock the outer-door-lock
          digitalWrite(DOORLOCK2_RELAIS, 1); // Lock the inner-door-lock
          machinestate = WAITINGFORCARD;
          }
    case BOOTING:
    case OUTOFORDER:
    case TRANSIENTERROR:
    case NOCONN:
      break;
  };
}