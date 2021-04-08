#include <ArduinoHttpClient.h>
#include <WiFiNINA.h>
#include "arduino_secrets.h"

#include <Arduino_LSM6DS3.h>
#include <SparkFun_Bio_Sensor_Hub_Library.h>
#include <Wire.h>
#include <Arduino.h>
#include <TinyGPS++.h>

//pin assignment

int HRresPin = 4;
int HRmfioPin = 5;
int tempSensorPin = A3;  // S tab of sensor to A3

//object creation
 
SparkFun_Bio_Sensor_Hub bioHub(HRresPin, HRmfioPin);
bioData body;
TinyGPSPlus gps;
float gpsLat = 0;
float gpsLong = 0;
int alertDelay = 30000;

// wifi setup //
//enter sensitive data in the Secret tab/arduino_secrets.h

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
char serverAddress[] = "ptsv2.com";  // test server address
//char serverAddress[] = "us-central1-health-band-staff.cloudfunctions.net";
int port = 80;
WiFiClient wifi;
HttpClient client = HttpClient(wifi, serverAddress, port);
int status = WL_IDLE_STATUS;


void setup() {
  Serial.begin(57600);
  Serial1.begin(9600); //gps setup
  wifiSetup();
  accelSetup();
  heartRateSetup();
  tempSetup();
}

void loop() {
  accelLoop();
  String hrOxygen = heartRateLoop();
  String temp = tempLoop();
  String gps = gpsLoop();

  String patientName = "Manpreet";
  String HR = getValue(hrOxygen, ',', 0);
  String oxygen = getValue(hrOxygen, ',', 1);
  String lati = getValue(gps, ',', 0);
  String lon = getValue(gps, ',', 1);
  
  String postData = "{\"name\":\""+ patientName + "\",\"hr\":" + HR + ",\"o2\":" + oxygen + ",\"temp\":" + temp + ",\"lat\":" + lati +",\"lon\":"+ lon + "}";
  Serial.println(postData);
  wifiLoop(postData);
  delay(5000);
}

void wifiSetup() {
  while ( status != WL_CONNECTED) {
    Serial.print("Attempting to connect to Network named: ");
    Serial.println(ssid); // print the network name (SSID);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);
  }
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
}

void accelSetup() {
  while (!Serial);
  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }
  Serial.print("Accelerometer sample rate = ");
  Serial.print(IMU.accelerationSampleRate());
  Serial.println(" Hz");
  Serial.println("Acceleration in G's");
}

void heartRateSetup() {
  Wire.begin();
  int result = bioHub.begin();
  if (result == 0) // Zero errors!
    Serial.println("Biometric sensor started!");
  else
    Serial.println("Could not communicate with the biometric!");
  Serial.println("Configuring Biometric Sensor....");
  int error = bioHub.configBpm(MODE_ONE); // Configuring just the BPM settings.
  if (error == 0) { // Zero errors!
    Serial.println("Sensor configured.");
  }
  else {
    Serial.println("Error configuring biometric sensor.");
    Serial.print("Error: ");
    Serial.println(error);
  }
  // Data lags a bit behind the sensor, if you're finger is on the sensor when
  // it's being configured this delay will give some time for the data to catch
  // up.
  Serial.println("Loading up the biometric sensor buffer with data....");
  delay(1000);
}

void tempSetup() {
  pinMode(tempSensorPin, INPUT);
}


String accelLoop() {
  float x, y, z;
  String accel;
  if (IMU.accelerationAvailable()) {
    IMU.readAcceleration(x, y, z);
    Serial.print("Acceleration: ");
    accel = "x:  " + String(x) + "  y:  " + String(y) + "  z:  " + String(z);
    Serial.println(accel);
  }

  //Fall Detection
  if (abs(x) > 2 || abs (y) > 2 || abs(z) > 2){
    Serial.println("Oops Ive falled");
    sendAlert("acceleration too fast, fall detected!");
    Serial.println("");
    delay(alertDelay);
  }
  
  return accel;
}

 String heartRateLoop() {
  body = bioHub.readBpm();
  Serial.print("Heartrate: ");
  Serial.println(body.heartRate);
  Serial.print("Confidence: ");
  Serial.println(body.confidence);
  Serial.print("Oxygen: ");
  Serial.println(body.oxygen);
  Serial.print("Biometric Sensor Status: ");
  Serial.println(body.status);
  // Slow it down or your heart rate will go up trying to keep up
  // with the flow of numbers
  //delay(250);

  //body.heartRate = 40;

  //Bradycardia warning
  if (body.heartRate != 0 && body.heartRate < 50 ) {
    Serial.println("Heart Rate too low");
    sendAlert("heart rate too low!");
    Serial.println("");
    delay(alertDelay);
  }
  
   //Tachycardia warning
  if (body.heartRate != 0 && body.heartRate > 100 ) {
    Serial.println("Heart Rate too high");
    sendAlert("heart rate too high!");
    Serial.println("");
    delay(alertDelay);
  }
  
  String hrOxygen = String(body.heartRate) + "," + String(body.oxygen);
  return hrOxygen;
}

String tempLoop() {
  long rawTemp;
  float voltage;
  float celsius;
  rawTemp = analogRead(tempSensorPin); // Read the raw 0-1023 value of temperature
  // Calculate the voltage, based on that value.
  // Multiply by maximum voltage (3.3V) and divide by maximum ADC value (1023).
  voltage = rawTemp * (3.3 / 1023.0);
  celsius = (voltage - 0.5) * 100;
  Serial.print("Temperature (celsius): ");
  Serial.println(celsius);

  //Under temperature warning
  if (celsius != 0 && celsius < 22) {
    Serial.println("Temperature too low");
    sendAlert("temperature too low!");
    Serial.println("");
    delay(alertDelay);
  }

  
  //Over temperature warning
  if (celsius > 38) {
    Serial.println("Temperature too high");
    sendAlert("temperature too high!");
    Serial.println("");
    delay(alertDelay);
  } 
  
  String temp = String(celsius);
  return temp;

  delay(500); //remove?
}

String gpsLoop() {
  String latlong;
  // This displays information every time a new sentence is correctly encoded.
  

  bool isGpsValid = false;
  do {
    while (Serial1.available() > 0) {
      char c = byte(Serial1.read());
      if(gps.encode(c)) {
        if (gps.location.isValid()) {
          gpsLat = gps.location.lat();
          gpsLong = gps.location.lng();
          isGpsValid = true;
        }
      }   
    }
  } while (isGpsValid == false);
  
  latlong = displayInfo();
  
  if (millis() > 5000 && gps.charsProcessed() < 10)
  {
    Serial.println(F("No GPS detected: check wiring."));
  }

  Serial.println("");
  return latlong;
}

String displayInfo() {
  String latlong = "null";
  Serial.print(F("Location: ")); 
  Serial.print(gpsLat,6);
  Serial.print(F(","));
  Serial.print(gpsLong, 6);
  latlong = String(gpsLat) +","+ String(gpsLong);

  Serial.print(F("  Date/Time: "));
  if (gps.date.isValid())
  {
    Serial.print(gps.date.month());
    Serial.print(F("/"));
    Serial.print(gps.date.day());
    Serial.print(F("/"));
    Serial.print(gps.date.year());
  }
  else
  {
    Serial.print(F("N/A"));
  }
  Serial.print(F(" "));
  if (gps.time.isValid())
  {
    if (gps.time.hour() < 10) Serial.print(F("0"));
    Serial.print(gps.time.hour());
    Serial.print(F(":"));
    if (gps.time.minute() < 10) Serial.print(F("0"));
    Serial.print(gps.time.minute());
    Serial.print(F(":"));
    if (gps.time.second() < 10) Serial.print(F("0"));
    Serial.print(gps.time.second());
    Serial.print(F("."));
    if (gps.time.centisecond() < 10) Serial.print(F("0"));
    Serial.print(gps.time.centisecond());
  }
  else
  {
    Serial.print(F("N/A"));
  }

  return latlong;
}


void wifiLoop(String postData) {
  Serial.println("");

  client.beginRequest();
  //client.post("/bandWriteInfo");
  //client.sendHeader("Content-Type", "application/json");
  
  client.post("/t/v8tf7-1617593301/post"); // test server 
  client.sendHeader("Content-Type", "application/x-www-form-urlencoded");
  
  client.sendHeader("Content-Length", postData.length());
  client.beginBody();
  client.print(postData);
  client.endRequest();

  // read the status code and body of the response
  int statusCode = client.responseStatusCode();
  String response = client.responseBody();
  Serial.println("");
  Serial.println("Sending to server..");
  Serial.print("Server status code: ");
  Serial.println(statusCode);
  Serial.print("Server Response: ");
  Serial.println(response);
  Serial.println("");
}

void sendAlert(String alertInfo) {

  Serial.println("");
  String patientName = "Manpreet";
  String postData = "{\"patient\":\""+ patientName + "\",\"msg\":\""+ alertInfo + "\"}";
  
  Serial.println(postData);
  client.beginRequest();
  //client.post("/bandSendAlert");
  //client.sendHeader("Content-Type", "application/json");
  
  client.post("/t/v8tf7-1617593301/post"); // test server 
  client.sendHeader("Content-Type", "application/x-www-form-urlencoded");
  
  client.sendHeader("Content-Length", postData.length());
  client.beginBody();
  client.print(postData);
  client.endRequest();

  // read the status code and body of the response
  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  Serial.print("Alert Server Status code: ");
  Serial.println(statusCode);
  Serial.print("Alert Server Response: ");
  Serial.println(response);

  Serial.println("");
}

String getValue(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}
