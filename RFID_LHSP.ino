//////LOGISTICS//////////////LOGISTICS//////////////LOGISTICS//////////////LOGISTICS////////
/*
  Data logging sketch for the ETAG RFID Reader
    Updated by L.U.T. for integrated LCD screen, simplified memory writing to SD card
  
  PROCESSOR SAMD21JXXX
  USE BOARD DEFINITION: ETAG RFID V2 D21
  
  Adapted from 
    ETAG READER Version 10.0 by Wilhelm et al.
*/
#include "RV3129.h"          // include library for the real time clock - must be installed in libraries folder
#include <Wire.h>            // include the standard wire library - used for I2C communication with the clock
#include <SD.h>              // include the standard SD card library
#include <SPI.h>             // include standard SPI library
#include "Manchester.h"
#include <LiquidCrystal_I2C.h>

//////USER VARIABLES//////////////SET ME//////////////SET ME//////////////SET ME////////

// Device ID
char DEVICE_ID[5] = "TEST";

// Tag reading rates
const unsigned long pauseTime = 100;                    // How long in milliseconds to wait between reading attempts
const byte checkTime = 30;                              // How long in milliseconds to check to see if a tag is present (Tag is only partially read during this time -- This is just a quick way of detirmining if a tag is present or not
const unsigned int pollTime1 = 200;                     // How long in milliseconds to try to read a tag if a tag was initially detected (applies to both RF circuits, but that can be changed)
const unsigned int delayTime = 1000;                    // How long in milliseconds to avoid recording the same tag twice in a row

// Tag Type
uint8_t ISO = 0;                                        // set to 1 to read ISO11874/5 tags, 0 = EM4100 tags

// Serial Monitor settings
unsigned int cycleCount = 0;          // counts read cycles
unsigned int stopCycleCount = 5000;   // How many read cycles to maintain serial comminications
byte writingSerial = 1;

//////VARIABLES//////////////VARIABLES//////////////VARIABLES//////////////VARIABLES////////

#define serial SerialUSB       // Designate the USB connection as the primary serial comm port - note lowercase "serial"
#define SD_FAT_TYPE         3  // Type 3 Reads all card formats
#define SPI_SPEED          SD_SCK_MHZ(4) .  //Finds fastest speed.
//#define DEMOD_OUT_1      41  // (PB22) this is the target pin for the raw RFID data from RF circuit 1
//#define DEMOD_OUT_2      42  // (PB23) this is the target pin for the raw RFID data from RF circuit 2
//#define SHD_PINA         48  // (PB16) Setting this pin high activates RFID circuit 1
//#define SHD_PINB         49  // (PB17) Setting this pin high activates RFID circuit 2
#define SDselect           46  // (PA21) Chip select for SD card. Make this pin low to activate the SD card for SPI communication
#define SDon               45  // (PA06) Provides power to the SD via a high side mosfet. Make this pin low to power on the SD card
#define FlashCS            44  // (PA05) Chip select for flash memory. Make this pin low to activate the flash memory for SPI communication
#define LED_RFID           43  // (PA27) Pin to control the LED indicator.
#define INT1               47  // (PA20) Clock interrupt for alarms and timers on the RTC
#define MOTR               2   // used for sleep function (need to investigate this). 
RV3129 rtc;                                 // Initialize an instance for the RV3129 real time clock library.

// LCD Added L.U.T. 2025-06-18
LiquidCrystal_I2C lcd(0x27, 16, 2); 
unsigned long lastTimeRec = 0;
const unsigned long TIMERECINT = 5000;
 
// Clock intervals
unsigned long now = millis();
unsigned long thenClock = millis();
unsigned long thenTag = millis();

int hours;
int minutes;
int seconds;
int lastHour;

byte SDOK = 0;
File outFile;
String fName;
char outLine[40];

uint32_t currRFID;                // stores current RFID number and RF circuit - for use with delayTime
uint16_t currRFID2;               // stores current RFID number and RF circuit - for use with delayTime
uint32_t pastRFID = 0xFFFFFFFF;   // stores past RFID number and RF circuit - for use with delayTime
uint16_t pastRFID2 = 0xFFFF;      // stores past RFID number and RF circuit - for use with delayTime

uint8_t RFcircuit = 1;            // Used to determine which RFID circuit is active. 1 = primary circuit, 2 = secondary circuit.
uint8_t pastCircuit = 0xFF;       // Used for repeat reads

char lcdTime[40];

// Global variable for tag codes
char RFIDstring[10];              // Stores the TagID as a character array (10 character string)
char ISOstring[14];               // Country code, period, and 10 characters ("003.03B3AB35D9")
uint16_t RFIDtagUser = 0;         // Stores the first (most significant) byte of a tag ID (user number)
unsigned long RFIDtagNumber = 0;  // Stores bytes 1 through 4 of a tag ID (user number)
uint8_t RFIDtagArray[6];          // Stores the five or 6 (ISO) individual bytes of a tag ID.
uint16_t IDCRC;                   // CRC calculated to determine repeat reads
uint16_t pastCRC;                 // used to determine repeat reads
uint16_t countryCode;
uint8_t tagTemp;                      // Temperature byte from temperature sensing tags.

//////SETUP//////////////SETUP//////////////SETUP//////////////SETUP////////

void setup() {  // setup code goes here, it is run once before anything else

  // ********************SET UP INPUT & OUTPUT PINS*******************************

  pinMode(LED_RFID, OUTPUT);      // pin for controlling the on-board LED
  digitalWrite(LED_RFID, LOW);   // turn the LED off (LOW turns it on)
  pinMode(SDselect, OUTPUT);      // Chip select pin for SD card must be an output
  pinMode(SDon, OUTPUT);          // Chip select pin for SD card must be an output
  pinMode(FlashCS, OUTPUT);       // Chip select pin for Flash memory
  digitalWrite(SDon, LOW);        // turns on the SD card.
  digitalWrite(SDselect, HIGH);   // Make both chip selects high (not selected)
  digitalWrite(FlashCS, HIGH);    // Make both chip selects high (not selected)
  pinMode(SHD_PINA, OUTPUT);      // Make the primary RFID shutdown pin an output.
  digitalWrite(SHD_PINA, HIGH);   // turn the primary RFID circuit off (LOW turns on the EM4095)
  pinMode(SHD_PINB, OUTPUT);      // Make the secondary RFID shutdown pin an output.
  digitalWrite(SHD_PINB, HIGH);   // turn the secondary RFID circuit off (LOW turns on the EM4095)
  pinMode(INT1, INPUT);           // Make the alarm pin an input

  // ********************EXECUTED STARTUP CODE *******************************

  // Initialize Serial Comms
  blinkLED(LED_RFID, 4, 400);     // blink LED to provide a delay for serial comms to come online
  
  Wire.begin(); //Start I2C bus to communicate with clock.
  serial.begin(9600);
  serial.println(); serial.println(); serial.println();
  lcd.begin(16,2);
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("ID: " + String(DEVICE_ID));
  delay(1000);

  // Initialize Clock
  if (!rtc.begin()) {  // Try initiation and report if something is wrong
    serial.println("Something wrong with clock");
    lcd.setCursor(0,0);
    lcd.print("                    ");
    lcd.setCursor(0,0);
    lcd.print("CLOCK BROKE STOP");
    delay(1000);
  } else {
    rtc.updateTime();
    serial.print("Clock working -- ");
    serial.println(rtc.stringTimeStamp());
    lcd.setCursor(0,0);
    lcd.print("                    ");
    lcd.setCursor(0,0);
    lcd.print("CLOCK OK");
  }

  // Open connection to output data file

  digitalWrite(SDon, LOW);       // Power to the SD card
  delay(20);
  digitalWrite(SDselect, LOW);   // SD card turned on
  SD.begin(SDselect);

  int mm = rtc.getMonth();
  int dd = rtc.getDate();
  String MM = (mm < 10 ? "0" : "") + String(mm);
  String DD = (dd < 10 ? "0" : "") + String(dd);
  fName = "RF_" + MM + "_" + DD + ".txt";
  serial.println("Will write to file " + fName);

  outFile = SD.open(fName, FILE_WRITE);           // Open file

  if (outFile) {
    serial.println("SD card detected.");                 

    snprintf(outLine, sizeof(outLine), "STARTUP,%s,%s", DEVICE_ID, rtc.stringTimeStamp());
    outFile.println(outLine);
    outFile.flush();
    
    lcd.setCursor(0,0);
    lcd.print("                    ");
    lcd.setCursor(0,0);
    lcd.print("SD OK");
    delay(1000);

    SDOK = 1;                                            
  } else {
    serial.println("No SD card detected.");              // error message
    lcd.setCursor(0,0);
    lcd.print("                    ");
    lcd.setCursor(0,0);
    lcd.print("SD MISS STOP");
    blinkLED(LED_RFID, 100, 400);     // blink LED to provide a delay for serial comms to come online
    delay(100000);
  }

  lastHour = rtc.getHours();

  RFcircuit = 1;
  blinkLED(LED_RFID, 3, 100);
}

//////MAIN//////////////MAIN//////////////MAIN//////////////MAIN////////

void loop() {

  rtc.updateTime(); // Get an update from the real time clock
  now = millis();

  hours = rtc.getHours();
  minutes = rtc.getMinutes();
  seconds = rtc.getSeconds();

  if (now - thenClock >= TIMERECINT) {
    sprintf(lcdTime, "Now: %02d%02d %02d%02d%02d",
            rtc.getMonth(), rtc.getDate(),
            hours, minutes, seconds);
    lcd.setCursor(0,0);
    lcd.print(lcdTime);
    thenClock = millis();
  }

  if (minutes == 0 & seconds == 0 & hours != lastHour) {
    sprintf(outLine, "RUNNING,%s,%s\0",
            DEVICE_ID, rtc.stringTimeStamp());
    outFile.println(outLine);
    outFile.flush();
    lastHour = hours;
  }

  ////// Read Tags 
  //Try to read tags - if a tag is read and it is not a recent repeat, write the data to the SD card
  bool readSuc = 0;
  if (ISO == 1) {
    readSuc = ISOFastRead(RFcircuit, checkTime, pollTime1);
  }
  if (ISO == 0) {
    readSuc = FastRead(RFcircuit, checkTime, pollTime1);
  }

  if (readSuc) {

    if (ISO == 0) {
      processTag(RFIDtagArray, RFIDstring, RFIDtagUser, &RFIDtagNumber);            // Parse tag data into string and hexidecimal formats
    }
    if (ISO == 1) {
      processISOTag(RFIDtagArray, RFIDstring, &countryCode, &tagTemp, &RFIDtagNumber);
    }
    //Put RFID code and Circuit into two variable to identify repeats
    currRFID = (RFIDtagArray[0] << 24) + (RFIDtagArray[1] << 16) + (RFIDtagArray[2] << 8) + (RFIDtagArray[3]);  
    currRFID2 = (RFIDtagArray[4] << 8 + RFcircuit);       

    if ((currRFID != pastRFID) | (currRFID2 != pastRFID2) | (now - thenTag >= delayTime)) {                   // See if the tag read is a recent repeat
      sprintf(outLine, "%s,%d,%s\0",
               RFIDstring, RFcircuit, rtc.stringTimeStamp());

      outFile.println(outLine);
      outFile.flush();
      
      if (writingSerial) {
        serial.println(outLine);
      }

      pastRFID = currRFID;            //First of three things to identify repeat reads
      pastRFID2 = currRFID2;          //Second of three things to identify repeat reads
      thenTag = millis();

      char last3[4];
      char line[40];     
      size_t len = strlen(RFIDstring);
      if (len >= 3) {
        memcpy(last3, RFIDstring + len - 3, 3);  // copy last 3 chars
        last3[3] = '\0';                        // null-terminate
      } else {
        // if too short, copy what’s available
        strcpy(last3, RFIDstring);
      }
      snprintf(line, sizeof(line),
               "%s  %02d%02d %02d%02d%02d",
               last3, rtc.getMonth(), rtc.getDate(), 
               rtc.getHours(), rtc.getMinutes(), rtc.getSeconds());
      lcd.setCursor(0,1);
      lcd.print(line);
    } else if (writingSerial) {
      serial.println("Repeat - Data not logged");                            // Message to indicate repeats (no data logged)
    }
    blinkLED(LED_RFID, 1, 100);
  }

  if (cycleCount < stopCycleCount) { // Pause between read attempts with delay or a sleep timer
    cycleCount++;                    // Advance the counter
    serial.println(rtc.stringTimeStamp());
  } else {
    writingSerial = 0;
  }

  // Alternate between circuits (comment out to stay on one circuit).
  RFcircuit == 1 ? RFcircuit = 2 : RFcircuit = 1;

  // After each read attempt execute a pause using a simple delay
  delay(pauseTime);
}

/////FUNCTIONS/////////FUNCTIONS/////////FUNCTIONS/////////FUNCTIONS/////////

//repeated LED blinking function
void blinkLED(uint8_t ledPin, uint8_t repeats, uint16_t duration) { //Flash an LED or toggle a pin
  pinMode(ledPin, OUTPUT);             // make pin an output
  for (int i = 0; i < repeats; i++) {  // loop to flash LED x number of times
    digitalWrite(ledPin, HIGH);         // turn the LED on (LOW turns it on)
    delay(duration);                   // pause again
    digitalWrite(ledPin, LOW);        // turn the LED off (HIGH turns it off)
    delay(duration);                   // pause for a while
  }                                    // end loop
}                                      // End function