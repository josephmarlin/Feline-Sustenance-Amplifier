//Main Cat Feeder Code
//February 9, 2023
//Feline Sustenance Amplifier
//Handles distance sensor, motor, LEDs, and schedule.

#include <ESP8266WiFi.h>
#include <WiFiManager.h>  
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <FastLED.h>
#define NUM_LEDS 126
CRGB leds[NUM_LEDS];

#define TRUE 1
#define FALSE 0
#define SOUND_SPEED 0.034
#define CM_TO_INCH 0.393701 

//Pins for the 12V supply and the motor hbridge
#define PIN_PWR_CTRL    D1
#define PIN_MTR_CTRL_A  D2
#define PIN_MTR_CTRL_B  D3 
#define PIN_LED_CTRL    D4
#define PIN_US_TRIG     D0
#define PIN_US_ECHO     D5

const long CST_OFFSET = -21600;

//8:00AM breakfast
int TIME_BREAKFAST_HRS = 8; //8
int TIME_BREAKFAST_MINUTES = 0; //0
int time_for_breakfast = TRUE;

//6:00PM dinner
int TIME_DINNER_HRS = 18; //18
int TIME_DINNER_MINUTES = 0; //0
int time_for_dinner = TRUE;

//Time in ms to spin the feed wheels for
int FEED_TIME = 20000; //20 seconds

//Threshold for low food warning. If there are this many inches of open air or more between the sensor and the start of food, it will alarm
int LOW_FOOD_THRESH = 10;
int low_food_alert = FALSE;
int disconnected_alert = FALSE;
int no_us_response_alert = FALSE;

WiFiClient espClient;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", CST_OFFSET);

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
}

void check_level() {
  long duration;
  float distance;

  //clear trigger pin
  digitalWrite(PIN_US_TRIG, LOW);
  delayMicroseconds(2);

  //send 50us pulse to command the device to send a wave
  digitalWrite(PIN_US_TRIG, HIGH);
  delayMicroseconds(50);
  digitalWrite(PIN_US_TRIG, LOW);

  // Reads the echo, returns the sound wave travel time in microseconds, 30ms timeout
  duration = pulseIn(PIN_US_ECHO, HIGH, 30000);
  if (duration == 0) {
    no_us_response_alert = TRUE;
    return;
  } 
  no_us_response_alert = FALSE;
  
  // Convert wave travel time to inches. Divide by two because the wave travels to the food and back, but we care only about the one-way distance
  distance = duration * SOUND_SPEED * CM_TO_INCH / 2.0;
  if (distance >= LOW_FOOD_THRESH) {
    low_food_alert = TRUE;
  } else {
    low_food_alert = FALSE;
  }
  
  Serial.print("Remaining food in inches: ");
  Serial.println(distance);
}

void feed() {
  //Spin the wheel to feed
  digitalWrite(PIN_PWR_CTRL, HIGH);
  digitalWrite(PIN_MTR_CTRL_A, LOW);
  digitalWrite(PIN_MTR_CTRL_B, HIGH);
  delay(FEED_TIME);
  digitalWrite(PIN_MTR_CTRL_B, LOW);
  digitalWrite(PIN_PWR_CTRL, LOW);

  check_level();
}

void clear_LEDs() { 
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();
}

void handle_LEDs() {
  if (disconnected_alert) {
    //Handle highest priority alert - no WIFI. This means unreliable feeding, since we don't have a time. Quick blue flashes.
    Serial.println("ERROR: Wifi disconnected alert active.");
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Blue;
    }
    FastLED.show();
    delay(250);
  } else if (no_us_response_alert) {
    //Handle the case where the US sensor doesn't reply, long green blink
    Serial.println("WARNING: Failed distance sensor alert active.");
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Green;
    }
    FastLED.show();
    delay(1000);
  } else if (low_food_alert) {
    //Handle low food alert - light red sequentially
    Serial.println("WARNING: Low food alert active.");
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Red;
      delay(25);
      FastLED.show();
    }
  } else {
    //Everything is ok - show idle state
    Serial.println("LED_CTRL: No active warnings.");
    delay(1000);
  }
  clear_LEDs();
}


void setup() {
  Serial.begin(9600);

  //Only seems to work if you set the pinmode to output, and that must be done after you call addLeds
  FastLED.addLeds<WS2812B, PIN_LED_CTRL, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(127); //Number between 0 and 255 to permanently scale all values brightness. Set to half brightness to avoid excecssive current draw.

  pinMode(PIN_PWR_CTRL, OUTPUT);
  pinMode(PIN_MTR_CTRL_A, OUTPUT);
  pinMode(PIN_MTR_CTRL_B, OUTPUT);
  pinMode(PIN_LED_CTRL, OUTPUT);
  pinMode(PIN_US_TRIG, OUTPUT);
  pinMode(PIN_US_ECHO, INPUT);

  digitalWrite(PIN_PWR_CTRL, LOW);
  digitalWrite(PIN_MTR_CTRL_B, LOW);
  digitalWrite(PIN_MTR_CTRL_A, LOW);

  // handle wifi
  WiFiManager wifiManager;
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  wifiManager.setAPCallback(configModeCallback);
  Serial.println("Starting the connection process.");
  wifiManager.autoConnect("Marlin-ESP8266", "demoncat");
  Serial.println("Connection process compelete.");
  
  timeClient.begin();
  timeClient.update();

  check_level();
  clear_LEDs();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Error: wifi not connected.");
    disconnected_alert = TRUE;
    handle_LEDs();
    return;
  } else {
    disconnected_alert = FALSE;
  }
  
  timeClient.update();  //we can call this freely, because it will only poke the server at most once a minute

  int hrs = timeClient.getHours();
  int mins = timeClient.getMinutes();
  Serial.print(hrs);
  Serial.print(":");
  Serial.print(mins);
  Serial.print(" vs. ");
  Serial.print(TIME_BREAKFAST_HRS);
  Serial.print(":");
  Serial.print(TIME_BREAKFAST_MINUTES);
  Serial.print(" (breakfast) or ");
  Serial.print(TIME_DINNER_HRS);
  Serial.print(":");
  Serial.print(TIME_DINNER_MINUTES);
  Serial.println(" (dinner).");
  
  if (time_for_breakfast && (hrs == TIME_BREAKFAST_HRS) && (mins == TIME_BREAKFAST_MINUTES)) {
    //Stop checking if it is time for breakfast and start checking if it is time for dinner. This prevents us from feeding for the entire breakfast minute
    time_for_breakfast = FALSE;
    time_for_dinner = TRUE;
    Serial.println("NOTICE: Breakfast time - feeding now.");
    feed();
  }
  
  if (time_for_dinner && (hrs == TIME_DINNER_HRS) && (mins == TIME_DINNER_MINUTES)) {
    //Stop checking if it is time for dinner and start checking if it is time for breakfast. This prevents us from feeding for the entire dinner minute
    time_for_dinner = FALSE;
    time_for_breakfast = TRUE;
    Serial.println("NOTICE: Dinner time - feeding now.");
    feed();
  }

  if (time_for_dinner) {
    Serial.println("Currently checking to see if it is dinner yet.");
  }
  if (time_for_breakfast) {
    Serial.println("Currently checking to see if it is breakfast yet.");
  }

  handle_LEDs();
}
