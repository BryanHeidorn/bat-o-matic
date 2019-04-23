/*
The Bat-O-Matic

File: FeederMonitor.ino
Authors:
  Bryan Heidorn
  Rafael Lopez

Description:
  Executes the data logging for the Bat-O-Matic using hardware interrupts to
  conserve power.
*/
#include <SD.h>
#include "RTClib.h"
#include "LowPower.h"

// Enables Debugging mode.
#define DEBUG

// Toggles DEBUG behavior using macros to avoid macros within functions.
#ifdef DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// Prevent startup until user input is detected via Serial.
const bool WAIT_TO_START = true;

// Logging Interval in seconds -  The time between reading data and logging it.
const int SYNC_INTERVAL = 1;
// The amount of time in seconds to wait before entering Power Saving mode.
const int TIMEOUT = 5;


// Night Mode Settings
const bool NIGHT_ONLY = true;
const int NIGHT_START_HOUR = 19;
const int NIGHT_START_MIN = 00;
const int DAY_START_HOUR = 5;
const int DAY_START_MIN = 45;


// Pin 10 for the chip select (CS) pin.
const int SD_CHIP_SELECT = 10;
// Hardware interrupt used for sensors.
const int INTERRUPT_PIN = 2; 

// Number of sensors for the device.
const int NUMBER_OF_SENSORS = 6;

// Sensors are numbered 1-NUMBER_OF_SENSORS clockwise from the connector.
const int sensorInput[NUMBER_OF_SENSORS] = {3, 4, 5, 6, 7, 8};
// Stores previous sensor state to detect when to write changes.
int sensorState[NUMBER_OF_SENSORS];

// Data Shield L1 and L2 Pins.
const int RED_LED = 12;
const int GREEN_LED = 13;

// Tracks when to enter LowPower mode.
volatile bool standby = false;

// Stores RTC time at the start of the main loop iteration.
DateTime now;
// Stores the time of the last trigger from the RTC;
DateTime lastTriggerTime;
// Stores the last time contents were written to the file.
DateTime lastSyncTime;

// File to write to on the data shield.
File logFile;

// Uncomment the appropriate RTC object for your data shield. 
// NOTE: If it doesn't say PCF8523 it's the DS1307.
RTC_DS1307 RTC;
// RTC_PCF8523 RTC;

// Asssigned Board ID (A-Z).
const char BOARD_ID = 'A';


/* Main setup for the Bat-O-Matic.
*/
void setup() {
  // Initialize Serial Port
  Serial.begin(9600);

  // Enable DataShield LEDs.
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  // LEDs off by default.
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);

  // Enable Photo-interrupters.
  for (int i = 0; i < NUMBER_OF_SENSORS; i++) {
    pinMode(sensorInput[i], INPUT);
    // Set default sensor value to false.
    sensorState[i] = LOW;
  }

  // Enable the interrupt pin with the OR Gate Output as the input value.
  pinMode(INTERRUPT_PIN, INPUT);

  // Initialize RTC.
  DEBUG_PRINTLN("Initializing RTC...");

  // Dispatch RTC error if it can't be found.
  if (! RTC.begin()) {
    error("Couldn't find RTC", 2);
  }
  // Set RTC from date when compiling if RTC is not yet running.
  if (! RTC.isrunning()) {
    // Set RTC to the system date/time at the time of compillation.
    RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));

    // DEBUG Statements for date time set on the RTC.
    DEBUG_PRINTLN("RTC not running! RTC Time Set.");
    DEBUG_PRINT("Compile Date: ");
    DEBUG_PRINT(__DATE__);
    DEBUG_PRINT(" ");
    DEBUG_PRINTLN(__TIME__);
  }

  now = RTC.now();
  lastTriggerTime = now;
  DEBUG_PRINTLN("RTC Initialized.");

  // Initialize SD card.

  DEBUG_PRINTLN("Initializing SD Card...");
  // Dispatch SD Card error if there is a problem.
  if (!SD.begin(SD_CHIP_SELECT)) {
    error("Card failed or is not present.", 1);
  }
  DEBUG_PRINTLN("SD Card Initialized.");

  DEBUG_PRINTLN("Creating Log File.");
  create_logFile();
  DEBUG_PRINTLN("LogFile Opened.");

  DEBUG_PRINTLN("Writing CSV column headers.");
  logFile.println("Unix Time,DateTime,Feeder,Liters Consumed");
  
  // Populate Header data.
  logFile.print(now.unixtime()); // seconds since 1/1/1970
  logFile.print(",");
  logFile.print(now.year(), DEC);
  logFile.print("/");
  logFile.print(now.month(), DEC);
  logFile.print("/");
  logFile.print(now.day(), DEC);
  logFile.print(" ");
  logFile.print(now.hour(), DEC);
  logFile.print(":");
  logFile.print(now.minute(), DEC);
  logFile.print(":");
  logFile.print(now.second(), DEC);
  logFile.print(",");
  logFile.print(BOARD_ID);
  logFile.println();
  logFile.println();
  logFile.println("time,sensor");
  logFile.flush();

  delay(2000);
}


/* Main loop for the Bat-O-Matic.
*/
void loop() {
  // Sleep if standby mode is enabled.
  if (standby) {
    DEBUG_PRINT("Entering Sleep Mode. ");
    DEBUG_PRINT(now.hour());
    DEBUG_PRINT(":");
    DEBUG_PRINT(now.minute());
    DEBUG_PRINT(":");
    DEBUG_PRINT(now.second());
    DEBUG_PRINT(" ");
    DEBUG_PRINT(now.month());
    DEBUG_PRINT("/");
    DEBUG_PRINT(now.day());
    DEBUG_PRINT("/");
    DEBUG_PRINTLN(now.year());

    // Allow suffictient time for Serial DEBUGS.
    delay(500);

    // Enable interrupt before sleep.
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), wake, HIGH);

    // Enter sleep.
    LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
    
    // Prevent calls to ISR.
    detachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN));

    // Update trigger time from RTC.
    lastTriggerTime = RTC.now();
  }
  // Monitor until all photo interruptors are enganged again.
  else {
    DEBUG_PRINTLN( TIMEOUT + lastTriggerTime.unixtime() - now.unixtime() );

    // Grab the current time from the RTC.
    now = RTC.now();
    
    if (digitalRead(INTERRUPT_PIN) == HIGH) {
      lastTriggerTime = now;
      
      // Check the sensors.
      for (int i = 0; i < NUMBER_OF_SENSORS; i++ ) {

        // Write change if state has changed.
        if(sensorState[i] != digitalRead(sensorInput[i])) {
          sensorState[i] = digitalRead(sensorInput[i]);

          if(sensorState[i] == HIGH) {
            logFile.print(now.unixtime());
            logFile.print(",");
            logFile.println(i + 1);
          }
          else {
            logFile.print(now.unixtime());
            logFile.print(",-");
            logFile.println(i + 1);
          }
        }
      }
    }
    // Enable standby procedure when sensors are enganged and the time since
    // the lastTriggerTime has elapsed.
    else {
      if ((now.unixtime() - lastTriggerTime.unixtime()) > TIMEOUT ) {
        DEBUG_PRINTLN("Time to sleep.");
        standby = true;
        return;
      }
    }
    
    // Log changes to SD.
    if((now.unixtime() - lastSyncTime.unixtime()) > SYNC_INTERVAL) {
      lastSyncTime = now;
      logFile.flush();
    }
  }
}


/* Displays error state the bat-o-matic using the DataShield LEDs.

  LED Fatal Error Patterns:
    Code 1 - Blinking RED
      Card Failure -  The card could be missing or incorrectly formatted.
    Code 2 - Blinking GREEN
      RealTimeClock (RTC) Failure - It is potentially a battery issue.
    Code 3 - Blinking RED and GREEN
      Not yet defined.

Argumments:
  str(char*):
    A pointer to a character array passed as a string for Serial output if
    DEBUG is enabled.
  errorCode(int):
    The integer value for the error code. Current error statuses:
      - Code 1: Blink RED
      - Code 2: Blink GREEN
      - Code 3: Blink RED and GREEN together.
*/
void error(const char *str, int errorCode)
{
  // The frequenct in milliseconds (ms) to blink the status code.
  int blinkFrequency = 333;

  DEBUG_PRINT("error: ");
  DEBUG_PRINTLN(str);

  // Code 1: Blinking RED.
  if (errorCode == 1) {
    while(1) {
      digitalWrite(RED_LED, HIGH);
      delay(blinkFrequency);
      digitalWrite(RED_LED, LOW);
      delay(blinkFrequency);
      } 
  }
  // Code 2: Blinking GREEN.
  else if (errorCode == 2) {
      while(1) {
      digitalWrite(GREEN_LED, HIGH);
      delay(blinkFrequency);
      digitalWrite(GREEN_LED, LOW);
      delay(blinkFrequency);
      }
  // Code 3: Blinking RED and GREEN.
  } else {
    while(1) {
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(RED_LED, HIGH);
      delay(blinkFrequency);
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(RED_LED, LOW);
      delay(blinkFrequency);
    }
  }
}

/* Helper function to quickly determine if it is night time.
*/
bool isNight() {
  // Current time is during night time.
  if (
    (now.hour() >= NIGHT_START_HOUR && now.minute()>= NIGHT_START_MIN) ||
    (now.hour() <= DAY_START_HOUR && now.minute() <= DAY_START_MIN)) {
      return (true);
  }
  return (false);
}


void create_logFile() {
  // Grab the current year.
  int year = now.year() % 10;
  // Get month as an integer value.
  int integerMonth = now.month();
  // Get day as an integer value.
  int integerDay = now.day();

  // Initialize the Log Filename with BOARD_ID and the 1 digit year.
  char filename[16];
  
  for(int i = 0; i < 100; i++) {
    sprintf(
      filename, "%c%d%02d%02d%d%d.csv",
      BOARD_ID, year, integerMonth, integerDay, i / 10, i % 10);

      // Find filename that is not taken.
      if(!SD.exists(filename)) {
        DEBUG_PRINT("Opening File: ");
        DEBUG_PRINTLN(filename);
        logFile = SD.open(filename, FILE_WRITE);
        break;
      }
  }

  if(!logFile) {
    error("Couldn't create a logFile", 3);
  }
}


/* Interrupt Service Routine (ISR) for waking from the low power state.
This method is called when any of the photo-interruptors are triggered and the
interrupt pin is HIGH. The value for standby(bool) is set to true and the 
lastTriggerTime(float) is set to the current millis value.
*/
void wake() {
  standby = false;
}

