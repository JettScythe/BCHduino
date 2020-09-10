/**************************************************************************************************/
// BCH Watchdog
// Description: 
//   Bot that monitorizes your addresses and alerts you if a movement of funds is detected.
// Disclaimer: This is beta software!
// Created on: 20 may 2020
// Last modified date: 20 may 2020
// Version: 0.1
/**************************************************************************************************/

/* Libraries */

#include <string.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h> //Use V6
#include <utlgbotlib.h>

/**************************************************************************************************/


// SHA1 Fingerprint for Bitcoin.com URL.
const char fingerprint[] PROGMEM= "29 0D E5 3A 2A FA 7C 48 FA CB 4D 3D 97 02 2A 27 0F 31 20 0D";

String serverName = "http://rest.bitcoin.com/v2/address/details/";
//Put here your address or addresses to track them
String addresses[] = {"bitcoincash:qzftvp2d0ketld8uc2y9fryg45e4ll74my9yyxx9kg", "bitcoincash:qzs02v05l7qs5s24srqju498qu55dwuj0cx5ehjm2c", "bitcoincash:ppm39e48x3sjynxlqmhy3pcnhl90spk34ssje8jg6x"};


// WiFi Parameters
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"
#define MAX_CONN_FAIL 50
#define MAX_LENGTH_WIFI_SSID 31
#define MAX_LENGTH_WIFI_PASS 63

// Telegram Bot Token (Get from Botfather)
#define TLG_TOKEN "XXXXXXXX:XXXXXXXXXXXXXXXXXXXXXXXXXXX"

// Enable Bot debug level (0 - None; 1 - Bot Level; 2 - Bot+HTTPS Level)
#define DEBUG_LEVEL_UTLGBOT 0

  
ESP8266WiFiMulti WiFiMulti;

/**************************************************************************************************/

/* Functions Prototypes */

void wifi_init_stat(void);
bool wifi_handle_connection(void);

/**************************************************************************************************/

/* Globals */

// Create Bot object
uTLGBot Bot(TLG_TOKEN);
DynamicJsonDocument myArray(512); //Here will be stored you addresses with each balance
char* chat_ID = NULL;

/**************************************************************************************************/

/* Main Function */

void setup(void)
{
    // Enable Bot debug
    Bot.set_debug(DEBUG_LEVEL_UTLGBOT);

    // Initialize Serial
    Serial.begin(115200);

    // Initialize WiFi station connection
    wifi_init_stat();

    // Wait WiFi connection
    Serial.println("Waiting for WiFi connection.");
    while(!wifi_handle_connection())
    {
        Serial.println(".");
        delay(1000);
    }
    
    StaticJsonDocument<200> doc;
    StaticJsonDocument<64> filter;
    filter["balanceSat"] = true;

    WiFiClient client;

    HTTPClient http;

    //Here, we will fill myArray with the data: addresses and bavlances
    Serial.print("[HTTPS] begin...\n");
    for (byte idx = 0; idx < sizeof(addresses) / sizeof(addresses[0]); idx++){
      if (http.begin(client, serverName + addresses[idx])) {  // HTTPS
        Serial.print("[HTTPS] GET...\n");
        Serial.println(serverName + addresses[idx]);
        // start connection and send HTTP header
        int httpCode = http.GET();
        // httpCode will be negative on error
        if (httpCode > 0) {
          // HTTP header has been send and Server response header has been handled
          Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
          // file found at server
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String payload = http.getString();
            DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
            // Test if parsing succeeds.
            if (error) {
              Serial.print(F("deserializeJson() failed: "));
              Serial.println(error.c_str());
              return;
              }
            long balance = doc["balanceSat"];
            Serial.print("Balance: ");
            Serial.println(balance);
            myArray[addresses[idx]] = balance;
          }
        } else {
          Serial.printf("[HTTPS] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
           }
      http.end();
      }
      else {
        Serial.printf("[HTTPS] Unable to connect\n");
        }
    }
    //Display the data in a fancy mode
    serializeJsonPretty(myArray, Serial);
}


void loop()
{
    // Check if WiFi is connected
    if(!wifi_handle_connection())
    {
        // Wait 100ms and check again
        delay(100);
        return;
    }
    
    StaticJsonDocument<512> doc;
    StaticJsonDocument<64> filter;
    filter["balanceSat"] = true;
    filter["unconfirmedBalanceSat"] = true;

    //If the ID of the Telegram chat is not available, just send any message to the bot
    if (chat_ID == NULL) {   
      Bot.getMe();
      Bot.getUpdates();
      while(Bot.received_msg.chat.id[0] == '\0'){
        Serial.println("Send a message to start the watchdog!");
        delay(5000);
        Bot.getUpdates();
      } 
      chat_ID = Bot.received_msg.chat.id;
      Bot.disconnect();
      Serial.print("Chat ID is ");
      Serial.println(chat_ID);
    }   
    
    WiFiClient client;
    HTTPClient http;

    //It's time to compare the current balance with the balance registered for each address
    JsonObject obj = myArray.as<JsonObject>();
    for (JsonPair p : obj){
      if (http.begin(client, serverName + p.key().c_str())); {  // HTTPS
        Serial.print("[HTTPS] GET...\n");
        // start connection and send HTTP header
        int httpCode = http.GET();
        // httpCode will be negative on error
        if (httpCode > 0) {
          // HTTP header has been send and Server response header has been handled
          Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
          // file found at server
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String payload = http.getString();
            DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
            // Test if parsing succeeds.
            if (error) {
              Serial.print(F("deserializeJson() failed: "));
              Serial.println(error.c_str());
              delay(1000);
              return;
              } else {
                serializeJsonPretty(doc, Serial);
                long balance = doc["balanceSat"];
                long unconfirmed_balance = doc["unconfirmedBalanceSat"];
                long current_balance = balance + unconfirmed_balance;
                Serial.print("Balance: ");
                Serial.println(balance);
                Serial.print("Current balance: ");
                Serial.println(current_balance);
                Serial.print("Registered balance: "); 
                Serial.println(p.value().as<long>());  
                if (p.value().as<long>() != current_balance){
                  long funds = balance - p.value().as<long>();
                  char URL[128];
                  strcpy(URL, "https://explorer.bitcoin.com/bch/address/");
                  strcat(URL, p.key().c_str());
                  Bot.getMe();
                  Bot.getUpdates();
                  Bot.sendMessage(Bot.received_msg.chat.id, "ALERT! Movement of funds detected");
                  Bot.sendMessage(Bot.received_msg.chat.id, "Check it out now!");
                  Bot.sendMessage(Bot.received_msg.chat.id, URL);
                  myArray[p.key()] = current_balance;
                  yield();
                  Bot.disconnect();
                }
              }
          }
      } else {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      }

      http.end();
      delay(2000);
      }
  }   
    delay(30000);  //Refresh the balance every 30 seconds
}

/**************************************************************************************************/

/* Functions */

// Init WiFi interface
void wifi_init_stat(void)
{
    Serial.println("Initializing TCP-IP adapter...");
    Serial.print("Wifi connecting to SSID: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFiMulti.addAP(WIFI_SSID, WIFI_PASS);
    

    Serial.println("TCP-IP adapter successfuly initialized.");
}

/**************************************************************************************************/

/* WiFi Change Event Handler */

bool wifi_handle_connection(void)
{
    static bool wifi_connected = false;

    // Device is not connected
    if(WiFiMulti.run() != WL_CONNECTED)
    {
        // Was connected
        if(wifi_connected)
        {
            Serial.println("WiFi disconnected.");
            wifi_connected = false;
        }

        return false;
    }
    // Device connected
    else
    {
        // Wasn't connected
        if(!wifi_connected)
        {
            Serial.println("");
            Serial.println("WiFi connected");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());

            wifi_connected = true;
        }

        return true;
    }
}

/**************************************************************************************************/