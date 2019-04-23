#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include "RTClib.h"
#include "LowPower.h"

// Derived form adafruit light-temperature logger https://github.com/adafruit/Light-and-Temp-logger
// Merged with bat wifi script ESP8266_Arduino_MegaBatchTelnet
// Record time and millis and feeding port photosensor state for multiple feeding ports.
// Log to a SD Card
// Modifications
// ---- updated 2017/08/12: 
// -------- File name should start with date... not LOGGER
// -------- extend time between sd writes. Went fomr 5 sec to 30 seconds

// Set for Uno with adafruit rev 2 data logger
// Changes needed:
// ---- Power saving ideas
// -------- Make SD reader sleep 
// -------- fewer indicator lights: I have reduced to 1
// -------- if it is daylight hours turn off either by using the clock or a photosensor. 
// ---------add 2 VCNL4010 sensors on an interupt line. Would not scan unless tis trigers 
// ---------and then record data for .. maybe 5 minutes then sleep to be awoken again when 
// ---------the proximity sensor fires again. (http://www.instructables.com/id/PIR-Motion-Detector-With-Arduino-Operated-at-Lowes/)

// how many milliseconds between grabbing data and logging it. 1000 ms is once a second
// This version runs constantly so there is no sleep to be very precise about 
#define LOG_INTERVAL  1000 // mills between entries (reduce to take more/faster data)

// how many milliseconds before writing the logged data permanently to disk
// set it to the LOG_INTERVAL to write each time (safest)
// set it to 10*LOG_INTERVAL to write all data every 10 datareads, you could lose up to 
// the last 10 reads if power is lost but it uses less power and is much faster!
#define SYNC_INTERVAL 30000 // mills between calls to flush() - to write data to the card
uint32_t syncTime = 0; // time of last sync()

#define ECHO_TO_SERIAL   1 // echo data to serial port
#define WAIT_TO_START    0 // Wait for serial input in setup()
#define NIGHT_ONLY 0 // 1 means operate only at night

#ifdef NIGHT_ONLY
int NightStartHour = 19;
int NightStartMinute = 00;
int DayStartHour = 5;
int DayStartMinute = 45;
#endif

// the digital pins that connect to the LEDs
#define redLEDpin 8   // Pin 8 might work... need to check ???
#define greenLEDpin 9
int CountofTonguesIn; // 0 if no beams broken... in case there is more than one at a time.

// The analog pins that connect to the sensors
#define photocellPin 0           // analog 0
#define tempPin 1                // analog 1
#define BANDGAPREF 14            // special indicator that we want to measure the bandgap

#define aref_voltage 3.3         // we tie 3.3V to ARef and measure it with a multimeter!
#define bandgap_voltage 1.1      // this is not super guaranteed but its not -too- off

RTC_DS1307 RTC; // define the Real Time Clock object
//RTC_PCF8523 RTC;
String BoardID = "A";



// for the data logging shield, we use digital pin 10 for the SD cs line
const int chipSelect = 10;  

// the logging file
File logfile;

// The hummingbird sensor ring includes 5 sensors. The Bat version has 6
// It is necessary to define these counts.
// Bypass broken sensors by updating the sensor list and number of sensors
// Sensors are numbered 1-6 clockwise from the connector
// Each sensor connects to an arduino Digital Pin
#define NUMBEROFSENSORS 6
//int DigitalInputPin[] = {3, 5, 7, 6, 4, 2 }; // Digital ports connected to sensor array C
//int DigitalInputPin[] = {3, 4, 7, 6, 5, 2 }; // Digital ports connected to sensor array New B
int DigitalInputPin[] = {6, 4, 2, 3, 5, 7 }; // Digital ports connected to sensor array A

int ToneFrequency[] = {50, 200, 500, 1000, 2000, 3000};
int SensorValue[NUMBEROFSENSORS]; // update of you have more tnan 10 sensors;
// 1 means beam is broken

boolean IsBroken[NUMBEROFSENSORS]; // also needs to change if more than 10 sensors 
DateTime now;
int counter; // fortesting
//_________________________________________________________________________________
void error(const char *str, int errorCode)
{
  /* Fatal Error Patterns:
  red blinking - card failure - Maybe card missing or not FAT16 or FAT32 format
  green blinking - RealTime Clock failure - maybe onboard battery problem
*/

  // if code 1 blink red
  // if code 2 blink green
  // if code 3 blink red and green together
  int blinkFreq = 333;
  Serial.print("error: ");
  Serial.println(str);
  if (errorCode == 1) {
    // red LED indicates error
    while(1) {
      digitalWrite(redLEDpin, HIGH);
      delay (blinkFreq);
      digitalWrite(redLEDpin, LOW);
      delay (blinkFreq);
      } 
  } else if (errorCode == 2) {
      while(1) {
      digitalWrite(greenLEDpin, HIGH);
      delay (blinkFreq);
      digitalWrite(greenLEDpin, LOW);
      delay (blinkFreq);
      }
      } else {
        while(1) {
        digitalWrite(greenLEDpin, HIGH);
        digitalWrite(redLEDpin, HIGH);
        delay (blinkFreq);
        digitalWrite(greenLEDpin, LOW);
        digitalWrite(redLEDpin, LOW);
        delay (blinkFreq);
      }
      }
}

void setup(void)
{
  Serial.begin(57600);
  Serial.println();
  
  // use debugging LEDs
  pinMode(redLEDpin, OUTPUT);
  pinMode(greenLEDpin, OUTPUT);

 // initialize the sensor array and onboard LED.
 // int i;
  for (int i = 0; i < NUMBEROFSENSORS; i = i + 1) { 
    //Serial.println(myPins[i]);
    pinMode(DigitalInputPin[i], INPUT);
    IsBroken[i] = true;
  }
  CountofTonguesIn = NUMBEROFSENSORS; // The count must take into account the initialization cycle
  digitalWrite(redLEDpin, LOW);
  digitalWrite(greenLEDpin, LOW);

#if WAIT_TO_START
  Serial.println("Type any character to start");
  while (!Serial.available());
#endif //WAIT_TO_START

  // initialize the SD card
#if ECHO_TO_SERIAL
   Serial.print("Initializing SD card...");
#endif
  // make sure that the default chip select pin is set to
  // output, even if you don't use it:
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH); // davekw7x: If it's low, the Wiznet chip corrupts the SPI bus
  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    error("Card failed, or not present", 1);
  }
 #if ECHO_TO_SERIAL
  Serial.println("card initialized.");
 #endif


  // connect to RTC
  Wire.begin();  
  if (!RTC.begin()) {
    error("Couldn't find RTC", 2);
  }
#if ECHO_TO_SERIAL
  Serial.println("Found RTC");
#endif
  if (! RTC.isrunning()) {
    Serial.println("RTC is NOT running!");
  
    // following line sets the RTC to the date & time this sketch was compiled
    Serial.print ("Compile date:");
    Serial.print (__DATE__);
    Serial.print (":");
    Serial.println (__TIME__);
    RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // RTC.adjust(DateTime(2016, 11, 10, 21, 14, 0));
  }
  if (! RTC.isrunning()) {
    Serial.println("RTC STILL is NOT running!");
  }


  // If you want to set the aref to something other than 5v
  analogReference(EXTERNAL);
    // fetch the time
    now = RTC.now();
      // create a new file. Name based on the date
    /****
    logfile.print(now.unixtime()); // seconds since 1/1/1970
    logfile.print(", ");
    //logfile.print('"');
    logfile.print(now.year(), DEC);
    logfile.print("/");
    logfile.print(now.month(), DEC);
    logfile.print("/");
    logfile.print(now.day(), DEC);
    logfile.print(" ");
    logfile.print(now.hour(), DEC);
    logfile.print(":");
    logfile.print(now.minute(), DEC);
    logfile.print(":");
    logfile.print(now.second(), DEC);
    */
  // File name format [A-Z]Data board designator + last digit of year eg. "7" for 2017 + 2 digit month eg. "09" is Septmber + 
  // 2 digit day of month eg. "01" is first day of month + 2 digit count 0-99 to allow board to restate and not overwrite prior files.
  String filename;
  String sYear;
  sYear = now.year();
  sYear = BoardID + sYear.substring(3,4); // ??? FileNameLimited to 8 charcaters

  //String sMonth;
  int iiMonth = now.month();
  if (iiMonth < 10) {
    filename = sYear + "0" + iiMonth; 
    } else {
      filename = sYear + iiMonth;
    }

  int iDay;
  //String sDay;
  iDay = now.day();
   if (iDay < 10) {
    filename = filename + "0" + iDay + "00.CSV"; 
    } else {
      filename = filename + iDay + "00.CSV";
    }
    char charFilename[16];
    filename.toCharArray(charFilename, 16);
// This loop checks to see the last file name for this day from 0-99.
  for (uint8_t i = 0; i < 100; i++) {
    charFilename[6] = i/10 + '0';
    charFilename[7] = i%10 + '0';
    if (! SD.exists(charFilename)) {
      // only open a new file if it doesn't exist
      Serial.println("Just before open");
        Serial.println(charFilename);

      logfile = SD.open(charFilename, FILE_WRITE); 
      break;  // leave the loop!
    }
  }

  if (! logfile) {
    error("couldn't create file", 3);
  }
  
#if ECHO_TO_SERIAL
  Serial.print("Logging to: ");
  Serial.println(charFilename);
#endif
  logfile.println("millis,unixtime,datetime,RTC_DS1307,Feeder A,Liters Drank");  // Remember to change feeder ID
#if ECHO_TO_SERIAL
  Serial.println("millis,unixtime,datetime,RTC_DS1307,Feeder A,Liters Drank");
#endif //ECHO_TO_SERIAL
    uint32_t m = millis();
    logfile.print(m);           // milliseconds since start
    logfile.print(", ");    
#if ECHO_TO_SERIAL
    Serial.print(m);         // milliseconds since start
    Serial.print(", ");  
#endif

    // log time
    logfile.print(now.unixtime()); // seconds since 1/1/1970
    logfile.print(", ");
    //logfile.print('"');
    logfile.print(now.year(), DEC);
    logfile.print("/");
    logfile.print(now.month(), DEC);
    logfile.print("/");
    logfile.print(now.day(), DEC);
    logfile.print(" ");
    logfile.print(now.hour(), DEC);
    logfile.print(":");
    logfile.print(now.minute(), DEC);
    logfile.print(":");
    logfile.print(now.second(), DEC);
    logfile.print(",");
    logfile.print(BoardID);
#if ECHO_TO_SERIAL
    Serial.print(now.unixtime()); // seconds since 1/1/1970
    Serial.print(", ");
    //Serial.print('"');
    Serial.print(now.year(), DEC);
    Serial.print("/");
    Serial.print(now.month(), DEC);
    Serial.print("/");
    Serial.print(now.day(), DEC);
    Serial.print(" ");
    Serial.print(now.hour(), DEC);
    Serial.print(":");
    Serial.print(now.minute(), DEC);
    Serial.print(":");
    Serial.print(now.second(), DEC);
    Serial.print(",");
    Serial.print(BoardID);
    //Serial.print('"');
#endif //ECHO_TO_SERIAL 
   
    logfile.println();
#if ECHO_TO_SERIAL
    Serial.println();
#endif // ECHO_TO_SERIAL
  logfile.println("millis,Sensor");    
#if ECHO_TO_SERIAL
  Serial.println("millis,Sensor");
#endif //ECHO_TO_SERIAL
  digitalWrite(redLEDpin, LOW);
  digitalWrite(greenLEDpin, LOW);

// If the NIGHT_ONLY flag is set and it is day get the time until night and sleep until then
#if NIGHT_ONLY
  //delay(1000); // give time for buffers to clear
  int DebugTimeCounter = 16;
  while (!night()) {
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    //LowPower.idle(SLEEP_8S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_OFF, SPI_OFF, USART0_OFF, TWI_OFF);
    //LowPower.idle(SLEEP_4S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_ON, SPI_OFF, USART0_OFF, TWI_OFF);
     //Serial.print("Sleeping... ");
     if (DebugTimeCounter > 15) {
         delay(1000);
    now = RTC.now();
#if ECHO_TO_SERIAL
    Serial.print(now.unixtime()); // seconds since 1/1/1970
    Serial.print(", ");
    //Serial.print('"');
    Serial.print(now.year(), DEC);
    Serial.print("/");
    Serial.print(now.month(), DEC);
    Serial.print("/");
    Serial.print(now.day(), DEC);
    Serial.print(" ");
    Serial.print(now.hour(), DEC);
    Serial.print(":");
    Serial.print(now.minute(), DEC);
    Serial.print(":");
    Serial.println(now.second(), DEC);
    delay(500);
#endif
    DebugTimeCounter = 0;
     } // end if
     else { DebugTimeCounter++; }
  }
  delay(1000); // give time for buffers to clear
#endif // End night only
}

//__________________________________________________________________________
bool night() {
  //before midnight
  if ((now.hour() >= NightStartHour && now.minute() >= NightStartMinute) || (now.hour() <= DayStartHour && now.minute() <= DayStartMinute)) return (1);
  return (0);
}
//__________________________________________________________________________

//__________________________________________________________________________

void loop(void)
{
  int SensorChangeValue;
  int i;
  
#if NIGHT_ONLY
// When day comes go inot sleep mode
  while (!night()) {
    // LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    LowPower.idle(SLEEP_8S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_OFF, 
                SPI_OFF, USART0_OFF, TWI_OFF);
#if ECHO_TO_SERIAL
     delay(1000);
     Serial.print("Sleeping IN MAIN LOOP... ");
#endif
  }
  //delay(1000); // give time for buffers to clear
#endif // End night only


  //
  // delay for the amount of time we want between readings
  // no delay between readings delay((LOG_INTERVAL -1) - (millis() % LOG_INTERVAL));
  for (i = 0; i < NUMBEROFSENSORS; i = i + 1) { 
    SensorValue[i] = digitalRead(DigitalInputPin[i]);
    if(!SensorValue[i]){ // then nothing breaking beam
      if (IsBroken[i]) { //need to flip the state and send open stamp
        IsBroken[i] = false;
        CountofTonguesIn--;
        SensorChangeValue = -i - 1; // 1 based counting to allow negatives and send negative port ID
      }
    }
    else { // something breaking beam
      if (!IsBroken[i]) { //need to flip the state 
        IsBroken[i] = true;
        CountofTonguesIn++;
        SensorChangeValue = i + 1; // 1 based counting to allow negatives and send positive port ID
      }
    }
      // log milliseconds since starting
    if (SensorChangeValue != 0) {
      if (CountofTonguesIn > 0 ) { 
        digitalWrite(greenLEDpin, HIGH); 
        //digitalWrite(redLEDpin, HIGH);
        }
      else { 
        digitalWrite(greenLEDpin, LOW); 
        //digitalWrite(redLEDpin, LOW);
        }
      uint32_t m = millis();
      logfile.print(m);           // milliseconds since start
      logfile.print(", ");    
#if ECHO_TO_SERIAL
      Serial.print(m);         // milliseconds since start
      Serial.print(", ");  
#endif
      logfile.print(SensorChangeValue);    // Sensor Value    
#if ECHO_TO_SERIAL
      Serial.print(SensorChangeValue);     // Sensor Value
      //Serial.print(", ");     // Sensor Value
      //Serial.print(CountofTonguesIn);     // Sensor Value
#endif
    logfile.println();
#if ECHO_TO_SERIAL
      Serial.println();
#endif // ECHO_TO_SERIAL
      SensorChangeValue = 0;
      // Now we write data to disk! Don't sync too often - requires 2048 bytes of I/O to SD card
      // which uses a bunch of power and takes time
      // By putting this here we loose the last data point before the power goes out... 
      // can give instruction to give set of false positives to mark shutdown time
      if ((millis() - syncTime) > SYNC_INTERVAL) {
        syncTime = millis();
        logfile.flush();
      }
    } // end if sensor change
  
    //digitalWrite(greenLEDpin, LOW);
   } // end for loop
   
    // Now we write data to disk! Don't sync too often - requires 2048 bytes of I/O to SD card
    // which uses a bunch of power and takes time
    if ((millis() - syncTime) < SYNC_INTERVAL) return;
      syncTime = millis();
    
      // blink LED to show we are syncing data to the card & updating FAT!
      //digitalWrite(redLEDpin, HIGH);
      logfile.flush();
      //digitalWrite(redLEDpin, LOW);
 
    }
