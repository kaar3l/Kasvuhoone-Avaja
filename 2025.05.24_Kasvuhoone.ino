//TODO: Muuda wifi nimi ja parool+
//TODO: Muuda sammude arv 700 peale+
//TODO: Countdown õigeks+
#include <WiFi.h>
#include <PubSubClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <OneWire.h>
//#include <DallasTemperature.h>
const char compile_date[] = __DATE__ " " __TIME__;
#include <math.h>

#include "SPI.h"
#include "TFT_eSPI.h"
TFT_eSPI tft = TFT_eSPI();

int cwPin = 13; //Kinni ,out
int ccwPin = 17; //Lahti ,out
const int countPin = 33; //RevCounter ,in
const int homeSwitchPin = 32; //Homeswitchi pin , in

const int revOut=22;
const int homeOut=21;

int lcdWidth=240;
int lcdHeight=135;

int buttonPushCounter = 0;   // counter for the number of button presses
int buttonState = 0;         // current state of the button
int lastButtonState = 0;     // previous state of the button
int countDownTimer=600; //600 on okei


int homeSwitchState = 0;     // variable to store the read value
float lowTemp = 26; //Temperatuur millest hakkab uks ennast timmima
float highTemp = 32; //Temperatuur millest ylespoole on 100% lahti uks
float lastDoorState=0; //Ukse hetke asukoht
float maxDoorState=700; //Maksimum ukse asukoht 750 aga siis painutab. 700 panen
uint32_t errorLockOutTime=1000*300; //5min lockout, measured full close travel = 250sec
uint32_t pulseStallTimeout=1000*10; //countPin should toggle ~every 2.8sec while motor runs; no pulse for 10sec = jammed

float tempC=0;

volatile bool inErrorLockout = false;
volatile bool resetLockoutRequested = false;

unsigned long ota_progress_millis = 0;


#include "secrets.h"  //defines ssid & password, gitignored

AsyncWebServer server(80);
WiFiClient espClient;

OneWire ds(25);  // on pin 10 (a 4.7K resistor is necessary)
byte addr[8];

void setup(void) {
  Serial.begin(115200);
  ds.search(addr);

  startup();
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(2);
  
  //display.println("SETUP WIFI");
  setup_wifi();
  //display.setCursor(10,10);
  //display.println("Starting OTA server");
  tft.println("Starting OTA server");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String hellotext="Hi! I am LiliyGo T-Diplay ESP32. (Kasvuhoone kontroller) \nBuild date: ";
    String builddate=String(compile_date);
    hellotext.concat(builddate);
    request->send(200, "text/plain", hellotext);
  });

  server.on("/lockout", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head><title>Kasvuhoone lockout</title></head><body>";
    html += "<h1>Kasvuhoone error lockout</h1>";
    html += "<p>Status: ";
    html += inErrorLockout ? "LOCKED OUT" : "OK";
    html += "</p>";
    if (inErrorLockout) {
      html += "<form method='POST' action='/lockout/reset'><button type='submit'>Reset lockout</button></form>";
    }
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  server.on("/lockout/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    resetLockoutRequested = true;
    request->send(200, "text/html", "<html><body><p>Reset requested.</p><p><a href='/lockout'>Back</a></p></body></html>");
  });

  ElegantOTA.begin(&server);    // Start ElegantOTA
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
  server.begin();
  
  pinMode(cwPin, OUTPUT);
  pinMode(ccwPin, OUTPUT);
  pinMode(countPin, INPUT);
  pinMode(homeSwitchPin, INPUT);

  pinMode(homeOut, OUTPUT);
  pinMode(revOut, OUTPUT);
  //pinMode(ledOut, OUTPUT);

  //Turn pins to low
  digitalWrite(cwPin, LOW);
  digitalWrite(ccwPin, LOW);
  //Put door to home position
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(2);
  homing();
}




void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
  // <Add your own code here>
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
  // <Add your own code here>
}




void startup(){
  tft.init();
  tft.fillScreen(TFT_BLACK);
  //tft.setViewport(10,10,220,300);
  for(int i=0;i<lcdWidth/2;i++){
    tft.fillCircle(lcdHeight/2,lcdWidth/2,i,TFT_WHITE);
    delay(5);
  }
  for(int i=0;i<lcdWidth/2-3;i++){
    tft.fillCircle(lcdHeight/2,lcdWidth/2,i,TFT_BLACK);
    delay(5);
  }

  tft.setRotation(1);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.println(" ");
  tft.println("  KASVUHOONE");
  tft.println("  CONTROLLER");
  delay(1000);
}

//Finding home switch
void homing(){
  //Time calculation
  uint32_t start = millis();
  uint32_t workTime;
  //HOMING:
  homeSwitchState=digitalRead(homeSwitchPin);
  //Home led:
  digitalWrite(homeOut,homeSwitchState);

  
  Serial.print("Door switch:");
  Serial.print(homeSwitchState); //Mis asendis on ukse lüliti
  if(homeSwitchState==0){ //Uks on lahti, seega tuleks kinni panna
    //display.clearDisplay();
    //display.println("SULGEN UST");
    tft.println("SULGEN UST");
    //display.display();
    Serial.println("Panen ukse kinni");
    digitalWrite(cwPin, HIGH); //Mootor kinni minema
    int lastPulseState=digitalRead(countPin);
    uint32_t lastPulseTime=millis();
    while(homeSwitchState < 1){ //Vaatame kas uks tuleb kinni:
      ElegantOTA.loop();
      rotateOut();
      homeSwitchState=digitalRead(homeSwitchPin);   // Vaatame pinni, millal saab ukse kinni
      Serial.print("Waiting for door switch. ");
      Serial.print("Door switch: ");
      Serial.println(homeSwitchState); //Mis asendis on ukse lüliti
      Serial.print("Time: ");
      workTime=millis()-start;
      if(workTime>errorLockOutTime){
        Serial.println("ERROR");
        errorLockOut();
        digitalWrite(cwPin, HIGH); //errorLockOut() turns motor off, re-enable it to actually keep closing
        start = millis(); //reset timeout window after lockout is cleared via web, give it a fresh try
        lastPulseTime = millis();
      }
      int pulseState=digitalRead(countPin);
      if(pulseState!=lastPulseState){
        lastPulseState=pulseState;
        lastPulseTime=millis();
      } else if(millis()-lastPulseTime>pulseStallTimeout){
        Serial.println("ERROR: countPin not pulsing, motor jammed");
        errorLockOut();
        digitalWrite(cwPin, HIGH); //errorLockOut() turns motor off, re-enable it to actually keep closing
        start = millis();
        lastPulseTime = millis();
      }
      delay(10);
    }
    digitalWrite(cwPin, LOW); //Mootor välja lülitada
    Serial.println("Door closed, switching off motor.");
    //Ka led põlema
    digitalWrite(homeOut,homeSwitchState);
    lastDoorState=0;
    //display.clearDisplay();
    //display.println("UKS KINNI");
    tft.println("UKS KINNI");
    delay(1000);
  }
  createGui();
}

//WIFIGA ÜHENDAMISE OSA
void setup_wifi() {
  //display.setCursor(0, 12);  
  Serial.println("Connecting to ");
  tft.println("Connecting to ");
  tft.print(ssid);
  //display.print("Connecting to ");
  //display.print(ssid);
  //display.println("");
  //display.display();

  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int TryCount = 0;

  while (WiFi.status() != WL_CONNECTED) {
    TryCount++;
    delay(1000);
    Serial.print(".");
    //display.print(".");
    //display.display();
    if ( TryCount == 10 )
    {
      break;
      //ESP.restart();
    }
  }
  if(WiFi.status() == WL_CONNECTED){
    Serial.println("");
    //display.println("");
    //Serial.println("WiFi connected..!");
    tft.println("WiFi connected..!");
    
    //display.println("Connected");
    tft.println("Connected");
    //display.display();
    //display.clearDisplay();
    Serial.print("Got IP: ");  Serial.println(WiFi.localIP());
    //display.println("");
    //display.setCursor(0, 0);  
    //display.print("IP:");
    tft.print("IP:");
    tft.println(WiFi.localIP());
    //display.println(WiFi.localIP());
    //display.display();
    delay(5000);
  }else{
    tft.println("Connected");
    tft.println("Connection Failed");
    tft.println("Skipping wifi connection");
    //display.setCursor(0, 0);  
    //display.println("Connection Failed");
    //display.println("Skipping wifi connection");
    delay(5000);
  }
}

//ERRORLOCKOUT
void errorLockOut(){
  //Just a while loop and error message
  //display.clearDisplay();
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  //myGLCD.print("SULGEN UST", CENTER, 24);
  tft.println("SULGEN UST");
  //display.println("SULGEN UST");
  //Turn pins to low
  digitalWrite(cwPin, LOW);
  digitalWrite(ccwPin, LOW);
  inErrorLockout = true;
  resetLockoutRequested = false;
  while(!resetLockoutRequested){ //blink until reset via http POST /lockout/reset
    ElegantOTA.loop();
    delay(250);
    digitalWrite(homeOut, HIGH);
    digitalWrite(revOut, HIGH);
    //digitalWrite(ledOut, HIGH);
    delay(250);
    digitalWrite(homeOut, LOW);
    digitalWrite(revOut, LOW);
    //digitalWrite(ledOut, LOW);
  }
  Serial.println("Lockout reset via web, retrying homing");
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println("LOCKOUT RESET");
  inErrorLockout = false;
  resetLockoutRequested = false;
}

//MEASURETEMPERATURE
float getTemperature(){

  float temp; 
  byte busStatus;
  byte data[12];
  ds.reset();       //bring 1-Wire into idle state
  ds.select(addr); //slect with DS-1 with address addr1
  ds.write(0x44);    //conversion command
  //DS18B20 12-bit conversion takes max 750ms; bail out instead of hanging if sensor is gone/stuck
  uint32_t conversionStart = millis();
  do
  {
    busStatus = ds.read();
    if (millis() - conversionStart > 1000) {
      Serial.println("DS18B20 conversion timeout, sensor not responding");
      return -127.0;
    }
  }
  while (busStatus != 0xFF);
  //---------------------------
  ds.reset();
  ds.select(addr);  //selectimg the desired DS18B20
  ds.write(0xBE);    //Function command to read Scratchpad Memory (9Byte)
  ds.read_bytes(data, 9); //data comes from DS and are saved into buffer data[8]
  //---------------------------------
  if (OneWire::crc8(data, 8) != data[8]) {
    Serial.println("DS18B20 CRC mismatch, discarding reading");
    return -127.0;
  }

  int16_t raw = (data[1] << 8) | data[0]; //---data[0] and data[1] contains temperature data : 12-bit resolution-----
  temp = (float)raw / 16.0;  //12-bit resolution default
  return (temp);
}




//Output rotation signal, take in rotation signal
void rotateOut(){
  digitalWrite(revOut, digitalRead(countPin));
}

//Countdown
void countDown(){
  Serial.println("Starting countdown");
  //Timer=600*1000ms=600sec=10min
  int counter=countDownTimer;
  while(counter>0){
    ElegantOTA.loop();
    counter--;
    Serial.println(counter);
    writetolcd(counter);
    //tft.fillRect(0, 100, lcdWidth, 30,TFT_RED);
    //tft.setCursor(92, 105);
    //tft.setTextSize(3);
    //tft.print(countDownTimer);
    delay(1000);
  }
}

//EKRAANI ALUMISSE OSSA KIRJUTAJA
void writetolcd(int info){
    tft.fillRect(0, 100, lcdWidth, 30,TFT_RED);
    tft.setCursor(5, 105);
    tft.setTextSize(3);
    tft.print(info);
}
void writetolcd(String info){
    tft.fillRect(0, 100, lcdWidth, 30,TFT_RED);
    tft.setCursor(5, 105);
    tft.setTextSize(3);
    tft.print(info);
}

//Countdown
void createGui(){
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 10);
  tft.print("TEMP:");
  tft.setCursor(2, 40);
  tft.print("MINTEMP:");
  tft.setCursor(2, 70);
  tft.print("MAXTEMP:");
}

void loop(void) {
  ElegantOTA.loop();
  
  float celsius;

  celsius = getTemperature();
  Serial.print("  Temperature = ");
  Serial.print(celsius);
  Serial.println(" ");
  
  tft.setTextSize(2);
  tft.setCursor(170, 10);
  
  tft.fillRect(170, 1, 70, 30,TFT_BLACK);
  tft.print(celsius);
  
  tft.setCursor(170, 40);
  tft.print(lowTemp);
  tft.setCursor(170, 70);
  tft.print(highTemp);
  
  delay(1000);
  
  tempC=celsius;
//  tempC=25;
  uint32_t moveStart = millis(); //stall guard for the door-move loops below
  uint32_t lastPulseTime = millis(); //reset right before each while loop starts, tracks countPin activity
 
//TEMPERATUURI PÕHJAL OTSUSTAMA MIS TOIMUMA HAKKAB
  
//TEMPERATUUR MADAL PANEME KINNI:
  if(tempC<=lowTemp){
    Serial.println("UKS KINNI PANNA(TEMP MADAL)"); //Tuleks panna uks kinni
    Serial.print("HomeSwitchState:");
    Serial.println(digitalRead(homeSwitchPin));
    Serial.print("RotSwitch:");
    Serial.println(digitalRead(countPin));
    if(digitalRead(homeSwitchPin)==0){
      delay(1000);
      digitalWrite(cwPin, HIGH); //Mootor kinni minema
      int lastPulseState=digitalRead(countPin);
      uint32_t lastPulseTime=millis();
      while(digitalRead(homeSwitchPin) < 1){ //Vaatame kas uks tuleb kinni:
        ElegantOTA.loop();
        rotateOut();
        homeSwitchState=digitalRead(homeSwitchPin);   // Vaatame pinni, millal saab ukse kinni
        digitalWrite(homeOut,homeSwitchState);
        Serial.println("Uks peaks sulguma");
        writetolcd("TAISKINNI");
        if(millis()-moveStart>errorLockOutTime){
          Serial.println("ERROR");
          errorLockOut();
          digitalWrite(cwPin, HIGH); //errorLockOut() turns motor off, re-enable it to actually keep closing
          moveStart = millis();
          lastPulseTime = millis();
        }
        int pulseState=digitalRead(countPin);
        if(pulseState!=lastPulseState){
          lastPulseState=pulseState;
          lastPulseTime=millis();
        } else if(millis()-lastPulseTime>pulseStallTimeout){
          Serial.println("ERROR: countPin not pulsing, motor jammed");
          errorLockOut();
          digitalWrite(cwPin, HIGH); //errorLockOut() turns motor off, re-enable it to actually keep closing
          moveStart = millis();
          lastPulseTime = millis();
        }
      }
      digitalWrite(cwPin, LOW); //Mootor välja lülitada
      lastDoorState=0;
    }
  }
  
//TEMPERATUUR KÕRGE TEEME LAHTI:
  else if(tempC>=highTemp){
    Serial.println("UKS TÄITSA LAHTI TEHA(TEMP KÕRGE)");
    delay(2000);
    if(lastDoorState!=maxDoorState){
      digitalWrite(ccwPin, HIGH); //Mootor lahti minema
      writetolcd("TAISLAHTI");
      lastPulseTime = millis();
      while(buttonPushCounter<maxDoorState-lastDoorState){ //Nii kaua kuni oleme 0 juures tagasi
        ElegantOTA.loop();
        buttonState=digitalRead(countPin);
        rotateOut();
        if (buttonState != lastButtonState){ //Countimine
          buttonPushCounter++;
          lastPulseTime = millis();
        }
        lastButtonState = buttonState;
//COUNTING TO DISPLAY

        //dtostrf(buttonPushCounter, 3, 0, doorRotCounter);
        //myGLCD.printNumI(buttonPushCounter, CENTER, 36);
        //display.println("  ");
///COUNTING TO DISPLAY
        Serial.print(buttonPushCounter);
        Serial.print(" -> ");
        Serial.println(maxDoorState-lastDoorState);
        if(millis()-moveStart>errorLockOutTime){
          Serial.println("ERROR");
          errorLockOut();
          digitalWrite(ccwPin, HIGH); //errorLockOut() turns motor off, re-enable it to actually keep opening
          moveStart = millis();
          lastPulseTime = millis();
        } else if(millis()-lastPulseTime>pulseStallTimeout){
          Serial.println("ERROR: countPin not pulsing, motor jammed");
          errorLockOut();
          digitalWrite(ccwPin, HIGH); //errorLockOut() turns motor off, re-enable it to actually keep opening
          moveStart = millis();
          lastPulseTime = millis();
        }
      }
      lastDoorState=maxDoorState;
      digitalWrite(ccwPin, LOW); //Mootor offi
    }
  }
  
//TEMPERATUUR JÄÄB VAHEPEALE:
  else{
    float nextDoorState=round(((tempC-lowTemp)/(highTemp-lowTemp))*maxDoorState);
    float doorDifference=nextDoorState-lastDoorState;
    Serial.println(" ");
    Serial.print("LastDoorState: ");
    Serial.print(lastDoorState);
    Serial.println(" ");
    Serial.print("NextDoorState: ");
    Serial.print(nextDoorState);
    Serial.println(" ");
    Serial.print("DoorDifference: ");
    Serial.print(doorDifference);
    Serial.println(" ");
//        myGLCD.print("->VAHEPEAL", CENTER, 40);

    //Ust on vaja kinnipoole panna:
    if (doorDifference<0){ //Uks kinnipoole
      Serial.println("UST ON VAJA KINNIPOOLE PANNA");
      //myGLCD.print("KINNIPOOLE", CENTER, 40);
      //display.println("KINNIPOOLE");
      writetolcd("KINNIPOOLE");
      delay(2000);
      digitalWrite(cwPin, HIGH); //Mootor kinni minema
      lastPulseTime = millis();
      while(buttonPushCounter<abs(doorDifference)){ //Nii kaua kuni oleme 0 juures tagasi
        ElegantOTA.loop();
        buttonState=digitalRead(countPin);
        rotateOut();
        if (buttonState != lastButtonState){ //Countimine
          buttonPushCounter++;
          lastPulseTime = millis();
        }
        lastButtonState = buttonState;
//COUNTING TO DISPLAY
        //myGLCD.printNumI(buttonPushCounter, CENTER, 36);
        //display.println("  ");
        writetolcd(buttonPushCounter);
///COUNTING TO DISPLAY
        Serial.print(buttonPushCounter);
        Serial.print(" -> ");
        Serial.println(doorDifference);
        if(millis()-moveStart>errorLockOutTime){
          Serial.println("ERROR");
          errorLockOut();
          digitalWrite(cwPin, HIGH); //errorLockOut() turns motor off, re-enable it to actually keep closing
          moveStart = millis();
          lastPulseTime = millis();
        } else if(millis()-lastPulseTime>pulseStallTimeout){
          Serial.println("ERROR: countPin not pulsing, motor jammed");
          errorLockOut();
          digitalWrite(cwPin, HIGH); //errorLockOut() turns motor off, re-enable it to actually keep closing
          moveStart = millis();
          lastPulseTime = millis();
        }
      }
          lastDoorState=nextDoorState;
      digitalWrite(cwPin, LOW); //Mootor offi
    }
    //Ust on lahtipoole teha:
    else if (doorDifference>0){ //Uks lahtipoole
      Serial.println("UST ON VAJA LAHTIPOOLE TEHA");
          writetolcd("LAHTIPOOLE");
      delay(2000);
      digitalWrite(ccwPin, HIGH); //Mootor lahti minema
      lastPulseTime = millis();
      while(buttonPushCounter<doorDifference){ //Nii kaua kuni oleme 0 juures tagasi
        ElegantOTA.loop();
        buttonState=digitalRead(countPin);
        rotateOut();
        if (buttonState != lastButtonState){ //Countimine
          buttonPushCounter++;
          lastPulseTime = millis();
        }
        lastButtonState = buttonState;
//COUNTING TO DISPLAY
        //myGLCD.printNumI(buttonPushCounter, CENTER, 36);
        //display.println("  ");
        writetolcd(buttonPushCounter);
///COUNTING TO DISPLAY
        Serial.print(buttonPushCounter);
        Serial.print(" -> ");
        Serial.println(doorDifference);
        if(millis()-moveStart>errorLockOutTime){
          Serial.println("ERROR");
          errorLockOut();
          digitalWrite(ccwPin, HIGH); //errorLockOut() turns motor off, re-enable it to actually keep opening
          moveStart = millis();
          lastPulseTime = millis();
        } else if(millis()-lastPulseTime>pulseStallTimeout){
          Serial.println("ERROR: countPin not pulsing, motor jammed");
          errorLockOut();
          digitalWrite(ccwPin, HIGH); //errorLockOut() turns motor off, re-enable it to actually keep opening
          moveStart = millis();
          lastPulseTime = millis();
        }
      }
          lastDoorState=nextDoorState;
      digitalWrite(ccwPin, LOW); //Mootor offi
    }
    //Uks on vaja samaks jätta
    else{
      //Ära tee midagi
      Serial.println("UST POLE VAJA LIIGUTADA");
      writetolcd("EI LIIGUTA");
      //myGLCD.print("EI LIIGUTA", CENTER, 40);
      //display.println("  ");
      delay(2000);
    }
  }
  buttonState = digitalRead(countPin); //Loeme herkoni asendit
  //Turn pins off
  digitalWrite(cwPin, LOW);
  digitalWrite(ccwPin, LOW);
  delay(2000);
  buttonPushCounter=0;
  lastButtonState=0;
  Serial.println("DONE");
  writetolcd("TEHTUD");
  delay(2000);
  countDown();
}
//////////////////////////////////////////////////////
