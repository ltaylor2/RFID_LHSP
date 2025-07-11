#include "RV3129.h"          // include library for the real time clock - must be installed in libraries folder

#define serial SerialUSB       // Designate the USB connection as the primary serial comm port - note lowercase "serial"
RV3129 rtc;   //Initialize an instance for the RV3129 real time clock library.

char inputArray[40];
byte printTime = 0;

void setup () {
  delay(2000);

  Wire.begin();
  rtc.begin();
  rtc.set24Hour();

  serial.begin(9600);
  serial.println(); serial.println(); serial.println();
  serial.println("Enter mmddyyhhmmss (or enter 'C' to check time)");   // Ask for user input
  
  byte timeInput = getInputString(15000); // Get a string of data and supply time out value
  if (timeInput == 12) {                  // If the input string is the right length, then process it
    byte mo = (inputArray[0] - 48) * 10 + (inputArray[1] - 48); //Convert two ascii characters into a single decimal number
    byte da = (inputArray[2] - 48) * 10 + (inputArray[3] - 48); //Convert two ascii characters into a single decimal number
    byte yr = (inputArray[4] - 48) * 10 + (inputArray[5] - 48); //Convert two ascii characters into a single decimal number
    byte hh = (inputArray[6] - 48) * 10 + (inputArray[7] - 48); //Convert two ascii characters into a single decimal number
    byte mm = (inputArray[8] - 48) * 10 + (inputArray[9] - 48); //Convert two ascii characters into a single decimal number
    byte ss = (inputArray[10] - 48) * 10 + (inputArray[11] - 48); //Convert two ascii characters into a single decimal number
    if (rtc.setTime(ss, mm, hh, da, mo, yr + 2000, 1) == false) {     // attempt to set clock with input values
      serial.println("Something went wrong setting the time");        // error message
    } else {
      printTime = 1;
      serial.println("Time set ok");
    }
  } else if (timeInput == 1 & inputArray[0]=='C') {
    printTime = 1;
    serial.println("Will print time");
  } else {
    serial.println("Time entry error");           // error message if string is the wrong lenth
  }
}

void loop () {
  if (printTime) {
    rtc.updateTime();
    serial.println(rtc.stringTimeStamp());
  }
}

byte getInputString(uint32_t timeOut) {               // Get a character array from the user and put in global array variale. Return the number of bytes read in
  uint32_t sDel = 0;                                  // Counter for timing how long to wait for input
  byte charCnt = 0;
  while (serial.available() == 0 && sDel < timeOut) { // Wait for a user response
    delay(1);                                         // Delay 1 ms
    sDel++;                                           // Add to the delay count
  }
  if (serial.available()) {                           // If there is a response then read in the data
    delay(40);                                        // long delay to let all the data sink into the buffer
    while (serial.available()) {
      byte R1 = serial.read();                          // read the entry from the user
      if (R1 > 47) {
        serial.println(R1, DEC);
        inputArray[charCnt] = R1;                          // ignore punctuation, line returns, etc
        charCnt++;
      }
    }
  }
  return charCnt;                                     //Return the number of characters recieved. It will be zero if nothing is read in
}

