/*
 * EnergyEye PowerCounter
 * 
 * Read analog voltage from IR sensor and publish to web
 * 
 */

#include "secrets.h"
#include <SoftwareSerial.h>

#define PUSH_INTERVAL 60000   // New Values are published every minute
#define PULSE_THRESHOLD 380   // An analog reading below 300 will be registeres as a pulse

SoftwareSerial serialDisplay(6, 7); //Rx=6 Tx+7 Rx is not used, as the display doesn't respond.
SoftwareSerial serialESP8266(8, 9); //Rx=8 Tx=9

char tempString[10];
byte brightness = 0;

int LED_r = 3;
int LED_g = 4;
int LED_b = 5;

boolean wifi_connected = false;
boolean last_push_ok   = false;

int sensor_power = 12;
int sensor_ground = 13;
int sensor_analog =  0;

int sensor_digital = 10;

int fadeValue = 0;

// Sparkfun server and channel info
String server_name  = "data.sparkfun.com";
String server_ip    = "54.86.132.254";
String stream_id    = "9JxQdlMzvXT8gLvAj81x";
String private_key  = SFPRIVATEKEY;

String send_string  = "";

unsigned long currentMillis  = 0;
unsigned long previousMillis = 0;
unsigned long pulse_time     = 0;
unsigned long pulse_time_old = 0;
unsigned long timer          = PUSH_INTERVAL;

boolean pulse_registered = false;

long pulse_count = 0;

double energy = 0;


// The setup routine runs once when you press reset
void setup() {
  
  // Configure input and output pins
  pinMode(LED_r, OUTPUT);
  pinMode(LED_g, OUTPUT);
  pinMode(LED_b, OUTPUT);
  pinMode(sensor_digital, INPUT);
  
  pinMode(sensor_power, OUTPUT);
  digitalWrite(sensor_power, HIGH);
  pinMode(sensor_ground, OUTPUT);
  digitalWrite(sensor_ground, LOW);
  
  // Initialize serial communication at 9600 bits per second
  Serial.begin(9600);
  
  // Initialize serial display
  serialDisplay.begin(9600);
  serialDisplay.write(0x76);
  serialDisplay.write(0x7A);
  serialDisplay.write(brightness);
  serialDisplay.write(0x76);
  serialDisplay.write(0x7A);
  serialDisplay.write(brightness);
  serialDisplay.write(0x76);
  serialDisplay.print("-HI-");
  //delay(1000);
  
  // Initialize serial connection to ESP8266
  serialESP8266.begin(9600);
  initESP();
  
}

// Main loop runs continuously
void loop() {

  // Get a currentMillis for the reading
  currentMillis = millis();
  
  // Read the IR sensor input on analog pin 0
  int irSensorValue = analogRead(A0);
  
  // Print out the read value
  Serial.print(currentMillis);
  Serial.print(" - ");
  Serial.print(pulse_time);
  Serial.print(" - ");
  Serial.print("IR Sensor: ");
  Serial.print(" - ");
  Serial.print(irSensorValue);
  
  if(currentMillis - previousMillis >= PUSH_INTERVAL)
  {
    previousMillis = currentMillis;
    Serial.print(" - Interval has passed. Resetting pulse count.");
    
    energy = (((double) pulse_count) * (1.0/600.0)) * 1000.0;
    Serial.print(" - Consumed energy: ");
    Serial.print(energy);
    Serial.print(" Wh");

    last_push_ok = updateStream(pulse_count);
    
    if (last_push_ok)
    {
      digitalWrite(LED_g, HIGH);
    }
    else
    {
      digitalWrite(LED_g, LOW);
      delay(1000);
      initESP();
      delay(1000);
      updateStream(pulse_count);
    }
    
    pulse_count = 0;

  }   
  
  if (irSensorValue >= PULSE_THRESHOLD)
  {

    digitalWrite(LED_r, HIGH);
    
    if (pulse_registered == false)
    {
      
      // Register the current pulse and increment the pulse counter
      pulse_registered = true;
      pulse_count = pulse_count + 1;
      pulse_time_old = pulse_time;
      pulse_time  = currentMillis;
      Serial.print(" - ");
      Serial.print("Pulse!");
      Serial.print(" - ");
      Serial.print("Pulsecount: ");
      Serial.print(pulse_count);
    }
    else
    {
      // Do nothing as the pulse has already been registered
      Serial.print(" - ");
      Serial.print("Pulse already registered.");
    }
    
  }
  else
  {

    digitalWrite(LED_r, LOW);
    
    if (pulse_registered == true)
    {
      // Reset the pulse registered flag and reset the previousMillis
      pulse_registered = false;
      Serial.print(" - ");
      Serial.print("Pulse reset.");
    }
  
  }

  Serial.println();
  
  
  
  
  // Build and send the value to the serial display
  sprintf(tempString, "%4d", irSensorValue);
  serialDisplay.write(tempString);
  
  // Convert the sensor value to a fading value for the LED
  fadeValue = irSensorValue/4;
  /*  
  Serial.print(currentMillis);
  Serial.print(" - ");
  Serial.print("FadeValue: ");
  Serial.print(" - ");
  Serial.println(fadeValue);
  */
  //analogWrite(led_r, fadeValue );
  
  // Read the digital input value from the humidity sensor board
  sensor_analog = digitalRead(sensor_digital);
  // Switch the Green LED accordingly
  if (sensor_analog>0)
  {
    //digitalWrite(led_g, LOW);
  }
  else
  {
    //digitalWrite(led_g, HIGH);
  }
  
  // Loop delay
  delay(100);
  
}


// Helper functions below


// Set the displays brightness. Should receive byte with the value
//  to set the brightness to
//  dimmest------------->brightest     THIS IS REVERSED FOR SOME REASON!
//     0--------127--------255
void setBrightness(byte value)
{
  serialDisplay.write(0x7A);  // Set brightness command byte
  serialDisplay.write(value);  // brightness data byte
}

// Turn on any, none, or all of the decimals.
//  The six lowest bits in the decimals parameter sets a decimal 
//  (or colon, or apostrophe) on or off. A 1 indicates on, 0 off.
//  [MSB] (X)(X)(Apos)(Colon)(Digit 4)(Digit 3)(Digit2)(Digit1)
void setDecimals(byte decimals)
{
  serialDisplay.write(0x77);
  serialDisplay.write(decimals);
}


void sendDebug(String cmd)
{
  Serial.print("SEND: ");
  Serial.println(cmd);
  serialESP8266.println(cmd);
  /*
  if(serialESP8266.find("OK"))
  {
    Serial.println("RECEIVED: OK");
    return true;
  }
  else
  {
    Serial.println("ERROR: String OK not received");
    return false;
  }
  */

}

void resetESP()
{
  String rcv = "";
  Serial.println("Resetting ESP...");
  serialESP8266.println("AT+RST");
  delay(3000);
  while (serialESP8266.available())
  {
    rcv += serialESP8266.read();
  }
  Serial.println(rcv);

  /*
  if(serialESP8266.find("OK"))
  {
    Serial.println("RECEIVED: OK");
    return true;
  }
  else
  {
    Serial.println("ERROR: String OK not received");
    return false;
  }
  */

}



boolean connectWiFi()
{
  serialESP8266.println("AT+CWMODE=1");
  delay(1000);
  String cmd="AT+CWJAP=\"";
  cmd+=SSID;
  cmd+="\",\"";
  cmd+=PASS;
  cmd+="\"";
  sendDebug(cmd);
  delay(1000);
  if(serialESP8266.find("OK"))
  {
    Serial.println("RECEIVED: OK");
    return true;
  }
  else
  {
    Serial.println("ERROR: Failed to connect to WiFi network");
    return false;
  }
  delay(5000);
}


boolean getMacAddress()
{
  String cmd="AT+CIPSTAMAC?";
  String rcv = "";
  sendDebug(cmd);
  delay(3000);
  while (serialESP8266.available())
  {
    rcv += serialESP8266.read();
  }
  Serial.println(rcv);
  if(serialESP8266.find("OK"))
  {
    Serial.println("RECEIVED: OK");
    return true;
  }
  else
  {
    Serial.println("RECEIVED: Error");
    return false;
  }
}




boolean updateStream(float energy)
{
  String cmd = "AT+CIPSTART=\"TCP\",\"";
  cmd += server_name;
  //cmd += server_ip;
  cmd += "\",80";
  sendDebug(cmd);
  delay(2000);
  if(serialESP8266.find("Error")){
    Serial.print("RECEIVED: Error connecting to data.sparkfun.com");
    return false;
  }
  cmd  = "GET ";
  cmd += "/input/";
  cmd += stream_id;
  cmd += "?";
  cmd += "private_key=";
  cmd += private_key;
  cmd += "&energy=";
  cmd += energy;
  cmd += "\r\n";  
  serialESP8266.print("AT+CIPSEND=");
  serialESP8266.println(cmd.length());
  if(serialESP8266.find(">"))
  {
    Serial.print(">");
    Serial.print(cmd);
    serialESP8266.print(cmd);
  }
  else
  {
    sendDebug("AT+CIPCLOSE");
  }
  if(serialESP8266.find("OK"))
  {
    Serial.println("RECEIVED: OK. Data published.");
    while(serialESP8266.available())
    {
      serialESP8266.read();
    }
    return true;
  }
  else
  {
    Serial.println("RECEIVED: Error publishing data.");
    while(serialESP8266.available())
    {
      serialESP8266.read();
    }
    return false;
  }

  /*
  cmd = GET;
  cmd += tenmpF;
  cmd += "\r\n";
  Serial.print("AT+CIPSEND=");
  Serial.println(cmd.length());
  if(Serial.find(">")){
    monitor.print(">");
    monitor.print(cmd);
    Serial.print(cmd);
  }else{
    sendDebug("AT+CIPCLOSE");
  }
  if(Serial.find("OK")){
    monitor.println("RECEIVED: OK");
  }else{
    monitor.println("RECEIVED: Error");
  }
  */

}

void initESP()
{
  resetESP();  
  sendDebug("AT");
  delay(1000);
  if(serialESP8266.find("OK"))
  {
    Serial.println("RECEIVED: OK");
    Serial.println("Connecting to WiFi network...");
    //getMacAddress();

    wifi_connected = connectWiFi();
    
    while (wifi_connected != true)
    {
      digitalWrite(LED_b, LOW);
      wifi_connected = connectWiFi();
    }
    digitalWrite(LED_b, HIGH);
  }
  else
  {
    Serial.println("ERROR: No OK string received");
  }
}

