#include <Wire.h>
#include "Chronodot.h"
#include <SPI.h>
#include <SD.h>
#include "DHT.h"
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

Chronodot RTC;

// which analog pin to connect
#define THERMISTORPIN1 A0  
#define THERMISTORPIN2 A1 
#define LDRPIN1 A2 
#define DHTPIN 2
#define RELAYVALVEPIN 5 
#define RELAYPUMPPIN 6 

 /* SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 8
 */

String sensors[] {"Pool","SolarPanel","Brightness","IndoorTemp","IndoorHum"};
enum sensorlist {Pool,SolarPanel,Brightness,IndoorTemp,IndoorHum};

// amount of sensors connected to analogpins
#define ANALOGSENSORS 3
#define DIGITALSENSORS 2
#define THERMISTORS 2 // to be changed. not flexible enough.

// Thermistor
// resistance at 25 degrees C
#define THERMISTORNOMINAL 10000      
// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 25   
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3950
// the value of the 'other' resistor
#define SERIESRESISTOR 10000    

// SD shield
#define CHIPSELECT 8
#define SAMPLETIME 30000
#define NUMSAMPLES 30

//DHT temperature sensor
#define DHTTYPE DHT21   // DHT 21 (AM2301)
DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(0x27,20,4);  // set the LCD address to 0x27 for a 20 chars and 4 line display
 
const uint8_t totalSensors = ANALOGSENSORS + DIGITALSENSORS;
uint8_t analogSensorArray[] = {THERMISTORPIN1, THERMISTORPIN2, LDRPIN1};
float average[totalSensors];
uint8_t value = 0;
uint8_t count = 0;
unsigned long previousMillis = 0;
// constants won't change :
const long interval = SAMPLETIME / NUMSAMPLES;    
float valveOpen = 5.0;
float valveClosed = 2.0;
boolean valveON;

 
void setup(void) {
  Serial.begin(9600);
  Serial.println(F("Initializing Chronodot."));
  
  analogReference(EXTERNAL);
  
  dht.begin();
  Wire.begin();
  RTC.begin();
  lcd.init();
  
  lcd.backlight();
  lcd.setCursor(2, 0);
  lcd.print(F("Pool datalogger"));
  lcd.setCursor(6, 1);
  lcd.print(F("Made by"));
  lcd.setCursor(1, 2);
  lcd.print(F("George Timmermans"));
  
  /*if (! RTC.isrunning()) {
    Serial.println(F("RTC is NOT running!"));
    // following line sets the RTC to the date & time this sketch was compiled
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }
  */
  RTC.adjust(DateTime(__DATE__, __TIME__));
  
  Serial.print(F("Initializing SD card..."));
  // make sure that the default chip select pin is set to
  // output, even if you don't use it:
  pinMode(CHIPSELECT, OUTPUT);

  // see if the card is present and can be initialized:
  if (!SD.begin(CHIPSELECT)) {
    Serial.println(F("Card failed, or not present."));
    // don't do anything more:
    return;
  }
  Serial.println(F("Card initialized."));
  
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File dataFile = SD.open("datalog.csv", FILE_WRITE);
        
  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println("Date,Time,Pool,SolarPanel,Brightness,IndoorTemp,IndoorHum,ValveState");
    dataFile.close();
  }
    
  for (uint8_t i = 0; i < totalSensors; i++)
    average[i] = 0;
    
  pinMode(RELAYVALVEPIN, OUTPUT); 
  pinMode(RELAYPUMPPIN, OUTPUT); 
  digitalWrite(RELAYVALVEPIN, HIGH);
  digitalWrite(RELAYPUMPPIN, HIGH);
  valveON = false;
}
 
void loop(void) {
  unsigned long currentMillis = millis();
  if(currentMillis - previousMillis >= interval) {
    // save the last time you blinked the LED 
    previousMillis = currentMillis;   

    if (count < NUMSAMPLES)  {
      for (uint8_t i = 0; i < ANALOGSENSORS; i++)
        average[i] += analogRead(analogSensorArray[i]);   
      average[IndoorTemp] += dht.readTemperature();
      average[IndoorHum] += dht.readHumidity();
      count++;
      Serial.print(count);
      if ( count < NUMSAMPLES)
        Serial.print(F(", "));
    }
    if (count == NUMSAMPLES)  {
      count = 0;
      Serial.println();    
      for (uint8_t i = 0; i < totalSensors; i++) {
        average[i] /= NUMSAMPLES;
        Serial.print(F("Average sensor reading ")); 
        Serial.print(i);
        Serial.print(F(" = "));
        Serial.println(average[i]);  
      }
      for (uint8_t i = 0; i < THERMISTORS; i++) {
        // convert the value to resistance
        average[i] = 1023 / average[i] - 1;
        average[i] = SERIESRESISTOR / average[i];
        Serial.print(F("Thermistor resistance ")); 
        Serial.print(i);
        Serial.print(F(" = "));
        Serial.println(average[i]);
      }
      
      for (uint8_t i = 0; i < THERMISTORS; i++) {
        average[i] = average[i] / THERMISTORNOMINAL;     // (R/Ro)
        average[i] = log(average[i]);                  // ln(R/Ro)
        average[i] /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
        average[i] += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
        average[i] = 1.0 / average[i];                 // Invert
        average[i] -= 273.15;                         // convert to C
     
        Serial.print(F("Temperature ")); 
        Serial.print(i);
        Serial.print(F(" = "));
        Serial.print(average[i]);
        Serial.println(F(" *C"));
      }
      
      controlRelay();
      logToSD();
      printScreen();
      clearData();
    }
  }
}

void controlRelay()  {
  if (average[SolarPanel] - average[Pool] > valveOpen & !valveON)  {
    digitalWrite(RELAYVALVEPIN, LOW);
    digitalWrite(RELAYPUMPPIN, LOW);
    valveON = true;
    Serial.println("Valve Open, Pump ON");
  }
  if (average[SolarPanel] - average[Pool] < valveClosed & valveON)  {
    digitalWrite(RELAYVALVEPIN, HIGH);
    digitalWrite(RELAYPUMPPIN, HIGH);
    valveON = false;
    Serial.println("Valve Closed, Pump OFF");
  }
}

void logToSD()  {
  DateTime now = RTC.now();
      
  String dataString = "";
     
  dataString += String(now.year());
  dataString += String(F("/"));      
  if(now.month() < 10) dataString += String(F("0"));
  dataString += String(now.month());
  dataString += String(F("/"));
  if(now.day() < 10) dataString += String(F("0"));
  dataString += String(now.day());
  dataString += String(F(","));
  if(now.hour() < 10) dataString += String(F("0"));
  dataString += String(now.hour());
  dataString += String(F(":"));
  if(now.minute() < 10) dataString += String(F("0"));
  dataString += String(now.minute());
  dataString += String(F(":"));
  if(now.second() < 10) dataString += String(F("0"));
  dataString += String(now.second());
  for (uint8_t i = 0; i < totalSensors; i++) {
    dataString += String(F(","));
    dataString += String(average[i]);
  }
  dataString += String(F(","));
  dataString += String(valveON);
      
               
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File dataFile = SD.open("datalog.csv", FILE_WRITE);
        
  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
    // print to the serial port too:
    Serial.println(dataString);
  }
  // if the file isn't open, pop up an error:
  else {
    Serial.println(F("Error opening datalog.csv"));
  }
}

void printScreen()  {
  lcd.clear();
  for (uint8_t i = 0; i < 4; i++) { //4 is the maximum rows on the lcd screen.
    lcd.setCursor(0, i);
    lcd.print(sensors[i]); 
    lcd.setCursor(12, i);
    lcd.print("= ");
    lcd.print(average[i]);
  }
}

void clearData()  {
  for (uint8_t i = 0; i < totalSensors; i++)
    average[i] = 0;
}

