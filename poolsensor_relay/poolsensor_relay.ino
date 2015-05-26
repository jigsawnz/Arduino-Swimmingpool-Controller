/*
// Poolsensor_relay.ino by George Timmermans
// This sketch contains code from Adafruit, Arduino and Stephanie Maks(chronodot library)
// The program reads several sensors on a regular interval, averages the results and determines the position of a valve and
// if the pump is allowed to run. 
//
// for more info go to www.georgetimmermans.com
*/

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
int RELAYVALVEPIN = 4; 
int RELAYPUMPPIN = 3; 

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
#define NUMSAMPLES 10
char filename[] = "LogAD_00.csv"; // filename must be 6 char 2 zeros
File dataFile;

//DHT temperature sensor
#define DHTTYPE DHT21   // DHT 21 (AM2301)
DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(0x27,20,4);  // set the LCD address to 0x27 for a 20 chars and 4 line display
 
const uint8_t totalSensors = ANALOGSENSORS + DIGITALSENSORS;
double average[totalSensors];
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
  delay(2000);
  
  /*if (! RTC.isrunning()) {
    Serial.println(F("RTC is NOT running!"));
    // following line sets the RTC to the date & time this sketch was compiled
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }
  */
  
  RTC.adjust(DateTime(__DATE__, __TIME__));
  
  lcd.clear();
 
  
  // see if the card is present and can be initialized:
  if (!SD.begin(CHIPSELECT)) {
    Serial.println("Card failed, or not present");
    lcd.print("Card failed, or not present");
  }
  else {
    Serial.println("card initialized.");
    lcd.print("card initialized.");
  }

  // create a new file name for each reset/start
  for (uint8_t i = 0; i < 100; i++) {
    filename[6] = i/10 + '0';
    filename[7] = i%10 + '0';
    if (! SD.exists(filename))
      break; // leave the loop!
  }
  if (!SD.open(filename, FILE_WRITE)) {
    Serial.println("SD file open failed!");
    lcd.clear();
    lcd.print("SD file open failed!");
  }
  dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile) {
    //print header
    dataFile.println(F("Date,Time,Pool,SolarPanel,Brightness,IndoorTemp,IndoorHum,ValveState"));
        
    // close the file:
    dataFile.close();
  } 
  // if the file isn't open, pop up an error:
  else {
    Serial.print(F("Error opening file: "));
    Serial.println(filename);
    lcd.clear();
    lcd.print("Error opening file: ");
    lcd.print(filename);
  }
        
  // clear array;  
  for (uint8_t i = 0; i < totalSensors; i++)
    average[i] = 0;
     
  valveON = false;
  
  pinMode(RELAYVALVEPIN, OUTPUT); 
  pinMode(RELAYPUMPPIN, OUTPUT); 
  digitalWrite(RELAYVALVEPIN, HIGH);
  digitalWrite(RELAYPUMPPIN, HIGH);
}
 
void loop(void) {
  unsigned long currentMillis = millis();
  if(currentMillis - previousMillis >= interval) {
    // save the last time you blinked the LED 
    previousMillis = currentMillis;   

    // Read all the sensors
    if (count < NUMSAMPLES)  {
    //for (uint8_t i = 0; i < ANALOGSENSORS; i++)
      //average[i] += analogRead(analogSensorArray[i]);  
      average[0] += analogRead(A0);
      average[1] += analogRead(A1);
      average[2] += analogRead(A2);
      average[3] += dht.readTemperature();
      average[4] += dht.readHumidity();
      count++;
      Serial.print(count);
      if ( count < NUMSAMPLES)
        Serial.print(F(", "));
    }
    
    // Average the the sensor readings
    if (count == NUMSAMPLES)  {
      count = 0;
      Serial.println();    
      
      averageData();
      controlRelay();
      logToSD();
      printScreen();
      clearData();
    }
  }
}

void averageData()  {
  for (uint8_t i = 0; i < totalSensors; i++) {
    average[i] /= NUMSAMPLES;
    /*
    Serial.print(sensors[i]); 
    Serial.print(F(" = "));
    Serial.println(average[i]);  
    */
  }
  
  // Map to make it fit the graph in Excel better
  average[2] = map(average[2], 0, 1023, 50, 250);
      
  // Convert analog values to temperature in degrees celsius.
  calcThermistorTemp(0);
  calcThermistorTemp(1);
  
  for (uint8_t i = 0; i < totalSensors; i++) {
    Serial.print(sensors[i]); 
    Serial.print(F(" = "));
    Serial.println(average[i]);  
  }
}

// Convert analog values to temperature in degrees celsius.
void calcThermistorTemp(uint8_t i)  {
  average[i] = 1023 / average[i] - 1;
  average[i] = SERIESRESISTOR / average[i];
        
  /*
  Serial.print(F("Thermistor resistance ")); 
  Serial.print(i);
  Serial.print(F(" = "));
  Serial.println(average[i]);
  */
       
  average[i] = average[i] / THERMISTORNOMINAL;     // (R/Ro)
  average[i] = log(average[i]);                  // ln(R/Ro)
  average[i] /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  average[i] += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  average[i] = 1.0 / average[i];                 // Invert
  average[i] -= 273.15;                         // convert to C
     
  /*
  Serial.print(F("Temperature ")); 
  Serial.print(i);
  Serial.print(F(" = "));
  Serial.print(average[i]);
  Serial.println(F(" *C"));
  */
}

// Open or close valve depending on the temperature difference between swimmingpool and solarpanel
void controlRelay()  {
  if (average[1] - average[0] > valveOpen & !valveON)  {
    digitalWrite(RELAYVALVEPIN, LOW);
    digitalWrite(RELAYPUMPPIN, LOW);
    valveON = true;
    Serial.println(F("Valve Open, Pump ON"));
  }
  else if (average[1] - average[0] < valveClosed & valveON)  {
    digitalWrite(RELAYVALVEPIN, HIGH);
    digitalWrite(RELAYPUMPPIN, HIGH); 
    valveON = false;
    Serial.println(F("Valve Closed, Pump OFF"));
  }
  else
  {
    Serial.print(F("Valve is "));
    if (valveON)
      Serial.println(F("Open, Pump ON"));
    else
      Serial.println(F("Closed, Pump OFF"));    
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
  dataFile = SD.open(filename, FILE_WRITE);
        
  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
    // print to the serial port too:
    Serial.println(dataString);
  }
  // if the file isn't open, pop up an error:
  else {
    Serial.print(F("Error opening file: "));
    Serial.println(filename);
  }
}

void printScreen()  {
  /*
  lcd.clear();
  for (uint8_t i = 0; i < 3; i++) { //4 is the maximum rows on the lcd screen.
    lcd.setCursor(0, i);
    lcd.print(sensors[i]); 
    lcd.setCursor(12, i);
    lcd.print("= ");
    lcd.print(average[i]);
  }
  */
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(sensors[0]); 
  lcd.setCursor(12, 0);
  lcd.print(F("= "));
  lcd.print(average[0]);
  
  lcd.setCursor(0, 1);
  lcd.print(sensors[1]);  
  lcd.setCursor(12, 1);
  lcd.print(F("= "));
  lcd.print(average[1]);
  
  lcd.setCursor(0, 2);
  lcd.print(F("difference")); 
  lcd.setCursor(12, 2);
  lcd.print(F("= "));
  lcd.print(average[1] - average[0]);
  
  lcd.setCursor(0, 3);
  lcd.print(F("valveState")); 
  lcd.setCursor(12, 3);
  lcd.print(F("= "));
  lcd.print(valveON);
}

// Clear the array for the next set of data to be sampled
void clearData()  {
  for (uint8_t i = 0; i < totalSensors; i++)
    average[i] = 0;
}

