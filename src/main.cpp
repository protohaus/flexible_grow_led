#include <Arduino.h>
#include <PCA9685.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include <Bounce2.h> 
#include <OneWire.h>
#include <DallasTemperature.h>

//================================Settings================================

// PCA9685 PWM Generator settings
const int fI2c_adr = 0x43;
float fPwm_freq = 800.0;
PCA9685 pwm_gen = PCA9685(fI2c_adr, PCA9685_MODE_N_DRIVER, fPwm_freq);

//JSON Parser
StaticJsonDocument<200> doc_in;
StaticJsonDocument<2048> doc_out;

// Wifi/MQTT settings
const char *cUuid = "e49ee006-b5f1-4a57-813b-dd339f91ace6";

// Update these with values suitable for your network.
const char *ssid = "ssid";
const char *password = "password";
const char *mqtt_server = "ip or adress";

WiFiClient espClient;
PubSubClient client(espClient);

//Info-LED Settings
#define NUM_LEDS 1
#define DATA_PIN 5
CRGB leds[NUM_LEDS];

// Temp Sensor DS18b20
#define ONE_WIRE_BUS D4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float fTemp = 20.0;

// Fan
uint16_t FanSpeed = 0;
float fUpperLimit = 0;
float fLowerLimit = 0;

// Button
Bounce debouncer = Bounce();
bool bButton = false;
bool bButton_old = false;

// Timer Leselampe
unsigned long previousMillis_whiteMode = 0;
const long interval_whiteMode = 60000; //60s
bool bActive = false;
// Timer MQTT Update
unsigned long previousMillis_MQTT = 0;
const long interval_MQTT = 300000; //300s
// Timer Temp + Fan Update
unsigned long previousMillis_Fan = 0;
const long interval_Fan = 30000; //30s

//================================LED Functions================================
void set_LEDs(String sColor, float fBrightness)
{
  uint8_t iColor = 15;
  if (sColor == "white")
    iColor = 0;
  if (sColor == "royal_blue")
    iColor = 1;
  if (sColor == "blue")
    iColor = 2;
  if (sColor == "green")
    iColor = 3;
  if (sColor == "amber")
    iColor = 4;
  if (sColor == "red_orange")
    iColor = 5;
  if (sColor == "photo_red")
    iColor = 6;
  if (sColor == "far_red")
    iColor = 7;
  if (!bButton)
  {
    int iBrightness = map(fBrightness, 0.0, 100.0, 0, 4095);
    pwm_gen.getPin(iColor).setValueAndWrite(iBrightness);
  }
}

//================================WiFi/MQTT Functions================================
void setup_wifi()
{

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  WiFi.hostname("LED-Panel_ESP");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void wifi_check()
{
  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 5)
  {
    wifi_retry++;
    Serial.println("WiFi not connected. Try to reconnect");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    WiFi.hostname("LED-Panel_ESP");
  }
  if (wifi_retry >= 5)
  {
    Serial.println("\nReboot");
    ESP.restart();
  }
}
// MQTT
void callback(char *topic, byte *payload, int length)
{

  DeserializationError error = deserializeJson(doc_in, payload, length);
  // Test if parsing succeeds.
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  //serializeJsonPretty(doc_in, Serial);
  set_LEDs(doc_in["enable"]["color"], doc_in["enable"]["power_percent"]);
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-e49ee006-b5f1-4a57-813b-dd339f91ace6";
    // Attempt to connect
    if (client.connect(clientId.c_str()))
    {
      Serial.println("connected");
      // ... and resubscribe
      client.subscribe("action/e49ee006-b5f1-4a57-813b-dd339f91ace6/led");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

//================================Setup & Loop================================
void setup()
{
  // setup GPIOs
  pinMode(D6, OUTPUT);

  // Info-LED and Fan Test
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS); //Info-LED
  leds[0] = CRGB::Red;
  FastLED.show();
  analogWrite(D6, 512); //Fan 10%
  delay(500);
  leds[0] = CRGB::Green;
  FastLED.show();
  delay(500);
  leds[0] = CRGB::Blue;
  FastLED.show();
  delay(500);
  leds[0] = CRGB::Black;
  FastLED.show();
  analogWrite(D6, 0); //Fan 0%

  debouncer.attach(D8);
  debouncer.interval(5); // interval in ms

  // initialize serial for debug output
  Serial.begin(9600);
  // initialize TwoWire communication
  Wire.begin();
  // setup the PCA9685
  pwm_gen.setup();

  // reset all LEDs
  for (uint8_t i = 0; i < 15; i++)
  {
    pwm_gen.getPin(i).setValueAndWrite(10);
  }

  // setup Temp-Sensor
  sensors.begin();

  setup_wifi();
  /*__________________OTA Begin_______________*/

  ArduinoOTA.setPassword("admin");
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  /*__________________OTA END_______________*/
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop()
{
  wifi_check();
  ArduinoOTA.handle();

  unsigned long currentMillis = millis();

  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  debouncer.update();
  if (debouncer.rose())
  {
    bButton = !bButton;
  }

  // Leselampe D8
  if (bButton && !bActive)
  {
    // White for 60s
    for (uint8_t i = 1; i < 15; i++) //All Black
    {
      pwm_gen.getPin(i).setValueAndWrite(0);
    }
    pwm_gen.getPin(0).setValueAndWrite(2048); // White 50%
    leds[0] = CRGB::White;
    FastLED.show();
    bActive = true;
  }

  // Restore Old Values
  if (currentMillis - previousMillis_whiteMode >= interval_whiteMode || (!bButton && bButton_old))
  {
    previousMillis_whiteMode = currentMillis;
    bButton = false;
    bActive = false;
    client.publish("action/e49ee006-b5f1-4a57-813b-dd339f91ace6/request_update", "1");

    leds[0] = CRGB::Black;
    FastLED.show();
  }

  // Normal MQTT Update
  if (currentMillis - previousMillis_MQTT >= interval_MQTT && !bButton)
  {
    previousMillis_MQTT = currentMillis;

    client.publish("action/e49ee006-b5f1-4a57-813b-dd339f91ace6/request_update", "1"); //Send Update Request
  }

  // Normal int Update
  if (currentMillis - previousMillis_Fan >= interval_Fan)
  {
    previousMillis_Fan = currentMillis;

    // read Temp-Sensor
    sensors.requestTemperatures();
    fTemp = sensors.getTempCByIndex(0);
    Serial.println(fTemp);

    // Lüfterdrehzahl anpassen
    if (fTemp > 1 && fTemp < 30)
      FanSpeed = 0;

    else if (fTemp >= 30 && fTemp < 40)
      FanSpeed = map(fTemp, 30, 40, 419, 1023);

    else
      FanSpeed = 1023;

    analogWrite(D6, FanSpeed); // Set Fan Speed

    // Json doc_out für MQTT State
    //String sHostname = WiFi.hostname();
    //doc_out["Hostname"] = sHostname.c_str();
    //String sIpAdress = WiFi.localIP().toString();
    //doc_out["WLAN_IP"] = sIpAdress.c_str();
    doc_out["Temp"] = fTemp;
    doc_out["Fan"] = FanSpeed;

    char sTemp[6];
    dtostrf(fTemp, 2, 2, sTemp);
    client.publish("action/e49ee006-b5f1-4a57-813b-dd339f91ace6/temp", sTemp); //Send Temp

    char sFanSpeed[4];
    dtostrf(FanSpeed, 4, 0, sFanSpeed);
    client.publish("action/e49ee006-b5f1-4a57-813b-dd339f91ace6/fan", sFanSpeed); //Send Fan Speed

    // debug

    char sWhiteValue[4];
    dtostrf(pwm_gen.getPin(0).getValue(), 4, 0, sWhiteValue);
    client.publish("action/e49ee006-b5f1-4a57-813b-dd339f91ace6/debug_white", sWhiteValue); //Send White Value
    doc_out["debug_white_Value"] = pwm_gen.getPin(0).getValue();

   /* byte error;
    Wire.beginTransmission(112);
    error = Wire.endTransmission();

    if (error == 0)
      doc_out["i2c"] = true;
    else
      doc_out["i2c"] = false;
*/
    String sJsonOut;
    serializeJson(doc_out, sJsonOut);
    client.publish("action/e49ee006-b5f1-4a57-813b-dd339f91ace6/debug", sJsonOut.c_str());
    // debug_end
  }

  bButton_old = bButton;
}
