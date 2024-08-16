#include <M5StickCPlus2.h>

/*
This is a fork of the original work by Aron N. Het Lam where I've added support for the onboard OLED display of the wifi kit 8 by heltec.
*/

// #define DEBUG_LED_STRIP
#define FASTLED_ESP8266_DMA

//Include libraries:
#if ESP32
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#else
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#endif

#include <EEPROM.h>
#include <TallyServer.h>

#define VERSION 0.2

//Define states
#define STATE_STARTING                  0
#define STATE_CONNECTING_TO_WIFI        1
#define STATE_CONNECTING_TO_SWITCHER    2
#define STATE_RUNNING                   3

//Define modes of operation
#define MODE_NORMAL                     1
#define MODE_PREVIEW_STAY_ON            2
#define MODE_PROGRAM_ONLY               3
#define MODE_ON_AIR                     4

#define TALLY_FLAG_OFF                  0
#define TALLY_FLAG_PROGRAM              1
#define TALLY_FLAG_PREVIEW              2

int numTallyLEDs;
int numStatusLEDs;


unsigned long previousMillis = 0;
const long interval = 4000;
unsigned long currentMillis;

//Initialize global variables
#if ESP32
WebServer server(80);
#else
ESP8266WebServer server(80);
#endif

TallyServer tallyServer;

uint8_t state = STATE_STARTING;

//Define sturct for holding tally settings (mostly to simplify EEPROM read and write, in order to persist settings)
struct Settings {
    char tallyName[32] = "";
    uint8_t tallyNo;
    uint8_t tallyModeLED1;
    uint8_t tallyModeLED2;
    bool staticIP;
    IPAddress tallyIP;
    IPAddress tallySubnetMask;
    IPAddress tallyGateway;
    IPAddress switcherIP;
    uint16_t neopixelsAmount;
    uint8_t neopixelStatusLEDOption;
    uint8_t NeopixelBrightness;
};

Settings settings;

bool firstRun = true;

int tallyFlag = TALLY_FLAG_OFF;

//Perform initial setup on power on
void setup() {

  auto cfg = M5.config();
    StickCP2.begin(cfg);
    
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setTextColor(WHITE);
    
    StickCP2.Display.setRotation(1);    
    StickCP2.Display.setTextDatum(middle_center);
    StickCP2.Display.setTextFont(&fonts::DejaVu18);

    StickCP2.Display.setCursor(0, 10);
    StickCP2.Display.print(VERSION);
    StickCP2.Display.printf("Loading... \n");

    //Start Serial
    StickCP2.Display.printf("serial... ");
    Serial.begin(115200);
    Serial.println("########################");
    Serial.println("Serial started");  
    StickCP2.Display.printf("OK\n");

    Serial.println(settings.tallyName);
    //Serial.println(sizeof(settings)); //Check size of settings struct
    StickCP2.Display.printf("WiFi... ");


    if (settings.staticIP) {
        WiFi.config(settings.tallyIP, settings.tallyGateway, settings.tallySubnetMask);
    }

    //Put WiFi into station mode and make it connect to saved network
    WiFi.mode(WIFI_STA);
    WiFi.begin();
#if ESP32
    WiFi.setHostname(settings.tallyName);
#else
    WiFi.hostname(settings.tallyName);
#endif
    WiFi.setAutoReconnect(true);

    Serial.println("------------------------");
    Serial.println("Connecting to WiFi...");
    Serial.println("Network name (SSID): " + getSSID());

    StickCP2.Display.printf("OK\n");

    // Initialize and begin HTTP server for handeling the web interface
    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    tallyServer.begin();

    //Wait for result from first attempt to connect - This makes sure it only activates the softAP if it was unable to connect,
    //and not just because it hasn't had the time to do so yet. It's blocking, so don't use it inside loop()
    WiFi.waitForConnectResult();

    //Set state to connecting before entering loop
    changeState(STATE_CONNECTING_TO_WIFI);

    tallyServer.setTallySources(40);
}

void loop() {
    switch (state) {
        case STATE_CONNECTING_TO_WIFI: {
                if (WiFi.status() == WL_CONNECTED) {
                    WiFi.mode(WIFI_STA); // Disable softAP if connection is successful
                    Serial.println("------------------------");
                    Serial.println("Connected to WiFi:   " + WiFi.SSID());
                    Serial.println("IP:                  " + WiFi.localIP().toString());
                    Serial.println("Subnet Mask:         " + WiFi.subnetMask().toString());
                    Serial.println("Gateway IP:          " + WiFi.gatewayIP().toString());
                    Serial.println("Press enter (\\r) to loop through tally states.");

                    StickCP2.Display.clear();
                    StickCP2.Display.setCursor(10, 10);

                    StickCP2.Display.printf("Connected to WiFi: %s", WiFi.SSID());
                    StickCP2.Display.printf("IP: %s", WiFi.localIP().toString());
                    StickCP2.Display.printf("Subnet Mask: %s", WiFi.subnetMask().toString());
                    StickCP2.Display.printf("Gateway IP: %s", WiFi.gatewayIP().toString());


                    changeState(STATE_RUNNING);
                } else if (firstRun) {
                    firstRun = false;
                    Serial.println("Unable to connect. Serving \"Tally Light setup\" WiFi for configuration, while still trying to connect...");
                    StickCP2.Display.clear();
                    StickCP2.Display.setCursor(10, 10);
                    StickCP2.Display.printf("Started softAP ");

                    WiFi.mode(WIFI_AP_STA); // Enable softAP to access web interface in case of no WiFi
                    WiFi.softAP("Tally Test server setup");                    
                }
            }
            break;

        case STATE_RUNNING:

        currentMillis = millis();

              if (currentMillis - previousMillis >= interval) {
                previousMillis = currentMillis;

                StickCP2.Display.clear();
                
                StickCP2.Display.setCursor(0, 10);
                StickCP2.Display.setTextColor(WHITE);

                StickCP2.Display.print("Tally TEST Server ");  
                StickCP2.Display.println(VERSION);
                StickCP2.Display.println(WiFi.SSID());
                StickCP2.Display.printf("IP: %s\n\n", WiFi.localIP().toString());
              
                           
                switch (tallyFlag) {
                    case TALLY_FLAG_OFF:
                        Serial.println("Off");                        
                        StickCP2.Display.setTextColor(WHITE);
                        StickCP2.Display.println("OFF");                      
                        break;
                    case TALLY_FLAG_PROGRAM:
                        Serial.println("Program");                      
                        StickCP2.Display.setTextColor(RED);
                        StickCP2.Display.println("PGM");                        
                        break;
                    case TALLY_FLAG_PREVIEW:
                        Serial.println("Preview");
                        StickCP2.Display.setTextColor(GREEN);
                        StickCP2.Display.println("PRV");                      
                        break;
                case TALLY_FLAG_PROGRAM | TALLY_FLAG_PREVIEW:
                        Serial.println("Program and preview");                      
                        StickCP2.Display.setTextColor(ORANGE);
                        StickCP2.Display.println("PGM + PRV");                        
                        break;
                    default:
                        Serial.println("Invalid tally state...");
                        break;
                }
              
                

                for(int i = 0; i < 41; i++) {
                    tallyServer.setTallyFlag(i, tallyFlag);
                }
                tallyFlag++;
                tallyFlag %= 4;
            }

            //Handle Tally Server
            tallyServer.runLoop();

            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("------------------------");
                Serial.println("WiFi connection lost...");
                changeState(STATE_CONNECTING_TO_WIFI);

                tallyServer.resetTallyFlags();
            }
            break;

        default:
            break;
    }

    

    //Handle web interface
    server.handleClient();

//    unsigned long  deltaTime = (micros() - lastMicros);
//    microsCounter += deltaTime;
//    if(deltaTime > maxMicros) maxMicros = deltaTime;
//    lastMicros = micros();
//    counter++;
//    if(millis() - lastMillis > 1000) {
//        Serial.print("Loop Time: ");
//        Serial.print((double)microsCounter / counter);
//        Serial.print(" Max Time: ");
//        Serial.println(maxMicros);
//        maxMicros = 0;
//        lastMillis = millis();
//        microsCounter = 0;
//        counter = 0;
//        lastMicros = micros();
//    }
}

void changeState(uint8_t stateToChangeTo) {
    firstRun = true;
    switch (stateToChangeTo) {
        case STATE_CONNECTING_TO_WIFI:
            state = STATE_CONNECTING_TO_WIFI;
            
            break;
        case STATE_RUNNING:
            state = STATE_RUNNING;
            
            break;
    }
}


int getTallyState(uint16_t tallyNo) {
    if (tallyFlag & TALLY_FLAG_PROGRAM) {
        return TALLY_FLAG_PROGRAM;
    } else if (tallyFlag & TALLY_FLAG_PREVIEW) {
        return TALLY_FLAG_PREVIEW;
    } else {
        return TALLY_FLAG_OFF;
    }
}


//Serve setup web page to client, by sending HTML with the correct variables
void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width,initial-scale=1.0\"><title>Tally Test server setup</title></head><script>function switchIpField(e){console.log(\"switch\");console.log(e);var target=e.srcElement||e.target;var maxLength=parseInt(target.attributes[\"maxlength\"].value,10);var myLength=target.value.length;if(myLength>=maxLength){var next=target.nextElementSibling;if(next!=null){if(next.className.includes(\"IP\")){next.focus();}}}else if(myLength==0){var previous=target.previousElementSibling;if(previous!=null){if(previous.className.includes(\"IP\")){previous.focus();}}}}function ipFieldFocus(e){console.log(\"focus\");console.log(e);var target=e.srcElement||e.target;target.select();}function load(){var containers=document.getElementsByClassName(\"IP\");for(var i=0;i<containers.length;i++){var container=containers[i];container.oninput=switchIpField;container.onfocus=ipFieldFocus;}containers=document.getElementsByClassName(\"tIP\");for(var i=0;i<containers.length;i++){var container=containers[i];container.oninput=switchIpField;container.onfocus=ipFieldFocus;}toggleStaticIPFields();}function toggleStaticIPFields(){var enabled=document.getElementById(\"staticIP\").checked;document.getElementById(\"staticIPHidden\").disabled=enabled;var staticIpFields=document.getElementsByClassName('tIP');for(var i=0;i<staticIpFields.length;i++){staticIpFields[i].disabled=!enabled;}}</script><style>a{color:#0F79E0}</style><body style=\"font-family:Verdana;white-space:nowrap;\"onload=\"load()\"><table cellpadding=\"2\"style=\"width:100%\"><tr bgcolor=\"#777777\"style=\"color:#ffffff;font-size:.8em;\"><td colspan=\"3\"><h1>&nbsp;Tally Test server setup</h1><h2>&nbsp;Status:</h2></td></tr><tr><td><br></td><td></td><td style=\"width:100%;\"></td></tr><tr><td>Connection Status:</td><td colspan=\"2\">";
    switch (WiFi.status()) {
        case WL_CONNECTED:
            html += "Connected to network";
            break;
        case WL_NO_SSID_AVAIL:
            html += "Network not found";
            break;
        case WL_CONNECT_FAILED:
            html += "Invalid password";
            break;
        case WL_IDLE_STATUS:
            html += "Changing state...";
            break;
        case WL_DISCONNECTED:
            html += "Station mode disabled";
            break;
        case -1:
            html += "Timeout";
            break;
    }

    html += "</td></tr><tr><td>Network name (SSID):</td><td colspan=\"2\">";
    html += getSSID();
    html += "</td></tr><tr><td><br></td></tr><tr><td>Signal strength:</td><td colspan=\"2\">";
    html += WiFi.RSSI();
    html += " dBm</td></tr>";
    //Commented out for users without batteries
    // html += "<tr><td><br></td></tr><tr><td>Battery voltage:</td><td colspan=\"2\">";
    // html += dtostrf(uBatt, 0, 3, buffer);
    // html += " V</td></tr>";
    html += "<tr><td>Static IP:</td><td colspan=\"2\">";
    html += settings.staticIP == true ? "True" : "False";
    html += "</td></tr><tr><td>Tally Test server IP:</td><td colspan=\"2\">";
    html += WiFi.localIP().toString();
    html += "</td></tr><tr><td>Subnet mask: </td><td colspan=\"2\">";
    html += WiFi.subnetMask().toString();
    html += "</td></tr><tr><td>Gateway: </td><td colspan=\"2\">";
    html += WiFi.gatewayIP().toString();
    html += "</td></tr><tr><td><br></td></tr>";
    html += "<tr><td><br></td></tr><tr bgcolor=\"#777777\"style=\"color:#ffffff;font-size:.8em;\"><td colspan=\"3\"><h2>&nbsp;Settings:</h2></td></tr><tr><td><br></td></tr><form action=\"/save\"method=\"post\"><tr><td>Tally Test server name: </td><td><input type=\"text\"size=\"30\"maxlength=\"30\"name=\"tName\"value=\"";
#if ESP32
    html += WiFi.getHostname();
#else
    html += WiFi.hostname();
#endif
    html += "\"required/></td></tr><tr><td>Tally Light mode (LED 1):&nbsp;</td><td><select name=\"tModeLED1\"><option value=\"";
    html += (String) MODE_NORMAL + "\"";
    if (settings.tallyModeLED1 == MODE_NORMAL)
        html += "selected";
    html += ">Normal</option><option value=\"";
    html += (String) MODE_PREVIEW_STAY_ON + "\"";
    if (settings.tallyModeLED1 == MODE_PREVIEW_STAY_ON)
        html += "selected";
    html += ">Preview stay on</option><option value=\"";
    html += (String) MODE_PROGRAM_ONLY + "\"";
    if (settings.tallyModeLED1 == MODE_PROGRAM_ONLY)
        html += "selected";
    html += ">Program only</option><option value=\"";
    html += (String) MODE_ON_AIR + "\"";
    if (settings.tallyModeLED1 == MODE_ON_AIR)
        html += "selected";
    html += ">On Air</option></select></td></tr><tr><td>Tally Light mode (LED 2):</td><td><select name=\"tModeLED2\"><option value=\"";
    html += (String) MODE_NORMAL + "\"";
    if (settings.tallyModeLED2 == MODE_NORMAL)
        html += "selected";
    html += ">Normal</option><option value=\"";
    html += (String) MODE_PREVIEW_STAY_ON + "\"";
    if (settings.tallyModeLED2 == MODE_PREVIEW_STAY_ON)
        html += "selected";
    html += ">Preview stay on</option><option value=\"";
    html += (String) MODE_PROGRAM_ONLY + "\"";
    if (settings.tallyModeLED2 == MODE_PROGRAM_ONLY)
        html += "selected";
    html += ">Program only</option><option value=\"";
    html += (String)MODE_ON_AIR + "\"";
    if (settings.tallyModeLED2 == MODE_ON_AIR)
        html += "selected";
    html += ">On Air</option></select></td></tr><tr><td>Amount of Neopixels:</td><td><input type=\"number\"size=\"5\"min=\"0\"max=\"1000\"name=\"neoPxAmount\"value=\"";
    html += settings.neopixelsAmount;
    html += "\"required/></td></tr><tr><td>Neopixel status LED: </td><td><select name=\"neoPxStatus\"><option value=\"";
    html += ">None</option></select></td></tr><tr><td> Neopixel brightness: </td><td><input type=\"number\"size=\"5\"min=\"0\"max=\"255\"name=\"neoPxBright\"value=\"";
    html += settings.NeopixelBrightness;
    html +=  "\"required/></td></tr><tr><td><br></td></tr><tr><td>Network name(SSID): </td><td><input type =\"text\"size=\"30\"maxlength=\"30\"name=\"ssid\"value=\"";
    html += getSSID();
    html += "\"required/></td></tr><tr><td>Network password: </td><td><input type=\"password\"size=\"30\"maxlength=\"30\"name=\"pwd\"pattern=\"^$|.{8,32}\"value=\"";
    if (WiFi.isConnected()) //As a minimum security meassure, to only send the wifi password if it's currently connected to the given network.
        html += WiFi.psk();
    html += "\"/></td></tr><tr><td><br></td></tr><tr><td>Use static IP: </td><td><input type=\"hidden\"id=\"staticIPHidden\"name=\"staticIP\"value=\"false\"/><input id=\"staticIP\"type=\"checkbox\"name=\"staticIP\"value=\"true\"onchange=\"toggleStaticIPFields()\"";
    if (settings.staticIP)
        html += "checked";
    html += "/></td></tr><tr><td>Tally Test server IP: </td><td><input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"tIP1\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallyIP[0];
    html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"tIP2\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallyIP[1];
    html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"tIP3\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallyIP[2];
    html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"tIP4\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallyIP[3];
    html += "\"required/></td></tr><tr><td>Subnet mask: </td><td><input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"mask1\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallySubnetMask[0];
    html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"mask2\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallySubnetMask[1];
    html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"mask3\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallySubnetMask[2];
    html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"mask4\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallySubnetMask[3];
    html += "\"required/></td></tr><tr><td>Gateway: </td><td><input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"gate1\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallyGateway[0];
    html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"gate2\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallyGateway[1];
    html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"gate3\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallyGateway[2];
    html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"gate4\"pattern=\"\\d{0,3}\"value=\"";
    html += settings.tallyGateway[3];
    html += "\"required/></tr><tr><td><br></td></tr><tr><td/><td style=\"float: right;\"><input type=\"submit\"value=\"Save Changes\"/></td></tr></form><tr bgcolor=\"#cccccc\"style=\"font-size: .8em;\"><td colspan=\"3\"><p>&nbsp;&copy; 2023-2024 Francesco Cucchiaro; ATEM software based on the work by <a href=\"https://aronhetlam.github.io/\">Aron N. Het Lam</a></p><p>&nbsp; and <a href=\"https://www.skaarhoj.com/\">SKAARHOJ</a></p></td></tr></table></body></html>";
    server.send(200, "text/html", html);
}

//Save new settings from client in EEPROM and restart the ESP8266 module
void handleSave() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/html", "<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1.0\"><title>Tally Test server setup</title></head><body style=\"font-family:Verdana;\"><table bgcolor=\"#777777\"border=\"0\"width=\"100%\"cellpadding=\"1\"style=\"color:#ffffff;font-size:.8em;\"><tr><td><h1>&nbsp;Tally Test server setup</h1></td></tr></table><br>Request without posting settings not allowed</body></html>");
    } else {
        String ssid;
        String pwd;
        bool change = false;
        for (uint8_t i = 0; i < server.args(); i++) {
            change = true;
            String var = server.argName(i);
            String val = server.arg(i);

            if (var == "tName") {
                val.toCharArray(settings.tallyName, (uint8_t)32);
            } else if (var == "tModeLED1") {
                settings.tallyModeLED1 = val.toInt();
            } else if (var == "tModeLED2") {
                settings.tallyModeLED2 = val.toInt();
            } else if (var == "neoPxAmount") {
                settings.neopixelsAmount = val.toInt();
            } else if (var == "neoPxStatus") {
                settings.neopixelStatusLEDOption = val.toInt();
            } else if (var == "neoPxBright") {
                settings.NeopixelBrightness = val.toInt();
            } else if (var == "tNo") {
                settings.tallyNo = val.toInt() - 1;
            } else if (var == "ssid") {
                ssid = String(val);
            } else if (var == "pwd") {
                pwd = String(val);
            } else if (var == "staticIP") {
                settings.staticIP = (val == "true");
            } else if (var == "tIP1") {
                settings.tallyIP[0] = val.toInt();
            } else if (var == "tIP2") {
                settings.tallyIP[1] = val.toInt();
            } else if (var == "tIP3") {
                settings.tallyIP[2] = val.toInt();
            } else if (var == "tIP4") {
                settings.tallyIP[3] = val.toInt();
            } else if (var == "mask1") {
                settings.tallySubnetMask[0] = val.toInt();
            } else if (var == "mask2") {
                settings.tallySubnetMask[1] = val.toInt();
            } else if (var == "mask3") {
                settings.tallySubnetMask[2] = val.toInt();
            } else if (var == "mask4") {
                settings.tallySubnetMask[3] = val.toInt();
            } else if (var == "gate1") {
                settings.tallyGateway[0] = val.toInt();
            } else if (var == "gate2") {
                settings.tallyGateway[1] = val.toInt();
            } else if (var == "gate3") {
                settings.tallyGateway[2] = val.toInt();
            } else if (var == "gate4") {
                settings.tallyGateway[3] = val.toInt();
            } else if (var == "aIP1") {
                settings.switcherIP[0] = val.toInt();
            } else if (var == "aIP2") {
                settings.switcherIP[1] = val.toInt();
            } else if (var == "aIP3") {
                settings.switcherIP[2] = val.toInt();
            } else if (var == "aIP4") {
                settings.switcherIP[3] = val.toInt();
            }
        }

        if (change) {
            EEPROM.put(0, settings);
            EEPROM.commit();

            server.send(200, "text/html", (String)"<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1.0\"><title>Tally Test server setup</title></head><body><table bgcolor=\"#777777\"border=\"0\"width=\"100%\"cellpadding=\"1\"style=\"font-family:Verdana;color:#ffffff;font-size:.8em;\"><tr><td><h1>&nbsp;Tally Test server setup</h1></td></tr></table><br>Settings saved successfully.</body></html>");

            // Delay to let data be saved, and the response to be sent properly to the client
            server.close(); // Close server to flush and ensure the response gets to the client
            delay(100);

            // Change into STA mode to disable softAP
            WiFi.mode(WIFI_STA);
            delay(100); // Give it time to switch over to STA mode (this is important on the ESP32 at least)

            if (ssid && pwd) {
                WiFi.persistent(true); // Needed by ESP8266
                // Pass in 'false' as 5th (connect) argument so we don't waste time trying to connect, just save the new SSID/PSK
                // 3rd argument is channel - '0' is default. 4th argument is BSSID - 'NULL' is default.
                WiFi.begin(ssid.c_str(), pwd.c_str(), 0, NULL, false);
            }

            // Delay to apply settings before restart
            delay(100);
            ESP.restart();
        }
    }
}

//Send 404 to client in case of invalid webpage being requested.
void handleNotFound() {
    server.send(404, "text/html", "<!DOCTYPE html> <html> <head> <meta charset=\"ASCII\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Tally Test server setup</title> </head> <body style=\"font-family:Verdana;\"> <table bgcolor=\"#777777\" border=\"0\" width=\"100%\" cellpadding=\"1\" style=\"color:#ffffff;font-size:12px;\"> <tr> <td> <h1>&nbsp Tally Test server setup</h1> </td> </tr> </table><br>404 - Page not found</body></html>");
}

String getSSID() {
#if ESP32
    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    return String(reinterpret_cast<const char *>(conf.sta.ssid));
#else
    return WiFi.SSID();
#endif
}
