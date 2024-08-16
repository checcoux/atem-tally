#include <M5StickCPlus2.h>

#if ESP32
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#else
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#endif

#include <EEPROM.h>
#include <ATEMmin.h>
#include <TallyServer.h>


#define VERSION 0.45


//Define states
#define STATE_STARTING 0
#define STATE_CONNECTING_TO_WIFI 1
#define STATE_CONNECTING_TO_SWITCHER 2
#define STATE_RUNNING 3

//Define LED colors
#define LED_OFF 0
#define LED_RED 1
#define LED_GREEN 2
#define LED_BLUE 3
#define LED_YELLOW 4
#define LED_PINK 5
#define LED_WHITE 6
#define LED_ORANGE 7

//Define modes of operation
#define MODE_NORMAL 1
#define MODE_PREVIEW_STAY_ON 2
#define MODE_PROGRAM_ONLY 3
#define MODE_ON_AIR 4

#define TALLY_FLAG_OFF 0
#define TALLY_FLAG_PROGRAM 1
#define TALLY_FLAG_PREVIEW 2

//Initialize global variables
#if ESP32
WebServer server(80);
#else
ESP8266WebServer server(80);
#endif

ATEMmin atemSwitcher;
TallyServer tallyServer;
uint8_t state = STATE_STARTING;

uint8_t currentState = 0;


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
  uint8_t ledBrightness;
};

Settings settings;

bool firstRun = true;

//Perform initial setup on power on
void setup() {

  auto cfg = M5.config();
  StickCP2.begin(cfg);

  StickCP2.Display.begin();
  StickCP2.Display.setEpdMode(epd_mode_t::epd_fastest);

  StickCP2.Display.setTextSize(1);
  StickCP2.Display.setTextColor(WHITE);

  StickCP2.Display.setRotation(1);
  StickCP2.Display.setTextDatum(middle_center);
  StickCP2.Display.setTextFont(&fonts::DejaVu18);

  StickCP2.Display.setCursor(0, 10);
  StickCP2.Display.print(VERSION);
  StickCP2.Display.printf(" Loading... \n");

  //Start Serial
  StickCP2.Display.printf("serial... ");
  Serial.begin(115200);
  Serial.println("########################");
  Serial.println("Serial started");
  StickCP2.Display.setTextColor(TFT_GREEN);
  StickCP2.Display.printf("OK\n");
  StickCP2.Display.setTextColor(TFT_WHITE);

  Serial.println(settings.tallyName);

  //MANUAL SETTINGS
  settings.switcherIP[0] = 192;
  settings.switcherIP[1] = 168;
  settings.switcherIP[2] = 1;
  settings.switcherIP[3] = 21;

  //String tName = "A";
  //tName.toCharArray(settings.tallyName, (uint8_t)32);
  //settings.tallyNo = 1;


  //Serial.println(sizeof(settings)); //Check size of settings struct
  StickCP2.Display.printf("WiFi init... ");


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

  StickCP2.Display.setTextColor(TFT_GREEN);
  StickCP2.Display.printf("OK\n");
  StickCP2.Display.setTextColor(TFT_WHITE);

  EEPROM.begin(sizeof(settings));
  EEPROM.get(0, settings);

  Serial.println(settings.tallyName);

  // Initialize and begin HTTP server for handeling the web interface
  StickCP2.Display.printf("HTTP server... ");
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
  StickCP2.Display.setTextColor(TFT_GREEN);
  StickCP2.Display.printf("OK\n");
  StickCP2.Display.setTextColor(TFT_WHITE);


  StickCP2.Display.printf("Tally client... ");
  tallyServer.begin();
  WiFi.waitForConnectResult();
  changeState(STATE_CONNECTING_TO_WIFI);
  StickCP2.Display.setTextColor(TFT_GREEN);
  StickCP2.Display.printf("OK\n");
  StickCP2.Display.setTextColor(TFT_WHITE);
}

void loop() {
  switch (state) {
    case STATE_CONNECTING_TO_WIFI:
      if (WiFi.status() == WL_CONNECTED) {
        WiFi.mode(WIFI_STA);  // Disable softAP if connection is successful
        Serial.println("------------------------");
        Serial.println("Connected to WiFi:   " + getSSID());
        Serial.println("IP:                  " + WiFi.localIP().toString());
        Serial.println("Subnet Mask:         " + WiFi.subnetMask().toString());
        Serial.println("Gateway IP:          " + WiFi.gatewayIP().toString());

        StickCP2.Display.clear();
        StickCP2.Display.setCursor(0, 10);
        StickCP2.Display.println("Network:");
        StickCP2.Display.println(WiFi.SSID());
        StickCP2.Display.printf("M5 IP: %s\n", WiFi.localIP().toString());        
        StickCP2.Display.printf("GW. IP: %s\n\n", WiFi.gatewayIP().toString());
        delay(500);


        changeState(STATE_CONNECTING_TO_SWITCHER);
      } else if (firstRun) {
        firstRun = false;
        Serial.println("Unable to connect, SoftAP.");
        WiFi.softAP("TALLY-SETUP");

        StickCP2.Display.clear();
        StickCP2.Display.setCursor(0, 10);
        StickCP2.Display.printf("Started softAP ");

        WiFi.mode(WIFI_AP_STA);  // Enable softAP to access web interface in case of no WiFi
      }
      break;

    case STATE_CONNECTING_TO_SWITCHER:
      // Initialize a connection to the switcher:
      if (firstRun) {
        atemSwitcher.begin(settings.switcherIP);
        //atemSwitcher.serialOutput(0xff); //Makes Atem library print debug info
        Serial.println("------------------------");
        Serial.println("Searching Tally server...");
        Serial.println((String) "Switcher IP:         " + settings.switcherIP[0] + "." + settings.switcherIP[1] + "." + settings.switcherIP[2] + "." + settings.switcherIP[3]);

        StickCP2.Display.print("Tally server... ");     
        /*
        StickCP2.Display.printf("SW IP: %s.", settings.switcherIP[0]);
        StickCP2.Display.printf("%s.", settings.switcherIP[1]);
        StickCP2.Display.printf("%s.", settings.switcherIP[2]);
        StickCP2.Display.printf("%.", settings.switcherIP[3]);
        */
        

        //StickCP2.Display.printf("M5 IP: %s\n\n", WiFi.localIP().toString());
        firstRun = false;
      }
      atemSwitcher.runLoop();
      if (atemSwitcher.isConnected()) {
        changeState(STATE_RUNNING);
        Serial.println("Connected to tally server");
        StickCP2.Display.setTextColor(TFT_GREEN);
        StickCP2.Display.printf("OK\n");
        StickCP2.Display.setTextColor(TFT_WHITE);
      }
      break;

    case STATE_RUNNING:
      //Handle data exchange and connection to swithcher
      atemSwitcher.runLoop();

      int tallySources = atemSwitcher.getTallyByIndexSources();
      tallyServer.setTallySources(tallySources);
      for (int i = 0; i < tallySources; i++) {
        tallyServer.setTallyFlag(i, atemSwitcher.getTallyByIndexTallyFlags(i));
      }

      //Handle Tally Server
      tallyServer.runLoop();

      //StickCP2.Display.clear();
      int color = getLedColor(settings.tallyModeLED1, settings.tallyNo);
      screenMain(color);


      //Switch state if ATEM connection is lost...
      if (!atemSwitcher.isConnected()) {  // will return false if the connection was lost
        Serial.println("------------------------");
        Serial.println("Connection to tally server lost...");
        changeState(STATE_CONNECTING_TO_SWITCHER);

        StickCP2.Display.clearDisplay();
        StickCP2.Display.setFont(&fonts::DejaVu18);
        StickCP2.Display.setCursor(2, 10);    
        StickCP2.Display.print("Lost connection with ");

        //Reset tally server's tally flags, so clients turn off their lights.
        tallyServer.resetTallyFlags();
      }
      break;
  }

  //Switch state if WiFi connection is lost...
  if (WiFi.status() != WL_CONNECTED && state != STATE_CONNECTING_TO_WIFI) {
    Serial.println("------------------------");
    Serial.println("WiFi connection lost...");
    changeState(STATE_CONNECTING_TO_WIFI);

    StickCP2.Display.clearDisplay();
    StickCP2.Display.setFont(&fonts::DejaVu18);
    StickCP2.Display.setCursor(2, 10);    
    StickCP2.Display.print("WiFi connection lost");

    //Force atem library to reset connection, in order for status to read correctly on website.
    atemSwitcher.begin(settings.switcherIP);
    atemSwitcher.connect();

    //Reset tally server's tally flags, They won't get the message, but it'll be reset for when the connectoin is back.
    tallyServer.resetTallyFlags();
  }


  //Handle web interface
  server.handleClient();
}


//Function to print the display status once we are connected to the wifi and to the switcher
void screenMain(int color) {
  if (color != currentState) {       
    if (!StickCP2.Display.displayBusy()) {  
          
      String status = "";
      int line = TFT_BLACK;
      if (color == 1) {
        StickCP2.Display.fillScreen(RED);
        StickCP2.Display.setTextColor(TFT_WHITE);
        line = TFT_WHITE;
        status = "PGM";
      } else if (color == 2) {
        StickCP2.Display.fillScreen(GREEN);
        StickCP2.Display.setTextColor(TFT_BLACK);
        line = TFT_BLACK;
        status = "PRV";
      } else {
        status = "";
        StickCP2.Display.fillScreen(BLACK);
        StickCP2.Display.setTextColor(TFT_WHITE);
        line = TFT_WHITE;
      }

      //status
      StickCP2.Display.setFont(&fonts::DejaVu18);
      StickCP2.Display.setCursor(195, 10);
      StickCP2.Display.println(status);
      
      //tally name
      StickCP2.Display.setCursor(2, 70);    
      StickCP2.Display.setFont(&fonts::DejaVu56);
      StickCP2.Display.println(settings.tallyName);

      //cam
      StickCP2.Display.setFont(&fonts::DejaVu18);
      StickCP2.Display.setCursor(2, 10);
      int cam = settings.tallyNo + 1;
      StickCP2.Display.printf("CAM %d", cam);

      //info bar
      StickCP2.Display.drawLine(0, 122, 240, 122, line);

      //batt
      StickCP2.Display.setCursor(2, 130);
      int vol = StickCP2.Power.getBatteryLevel();
      StickCP2.Display.setFont(&fonts::DejaVu12);
      StickCP2.Display.printf("%d%%", vol);

      //sensors
      StickCP2.Display.setCursor(183, 130);
      int rssi = WiFi.RSSI();
      StickCP2.Display.setFont(&fonts::DejaVu12);
      StickCP2.Display.printf("%ddBm", rssi);

    }
    currentState = color;
  }
}



//Handle the change of states in the program
void changeState(uint8_t stateToChangeTo) {
  firstRun = true;
  switch (stateToChangeTo) {
    case STATE_CONNECTING_TO_WIFI:
      state = STATE_CONNECTING_TO_WIFI;

      break;
    case STATE_CONNECTING_TO_SWITCHER:
      state = STATE_CONNECTING_TO_SWITCHER;

      break;
    case STATE_RUNNING:
      state = STATE_RUNNING;

      break;
  }
}



int getTallyState(uint16_t tallyNo) {
  if (tallyNo >= atemSwitcher.getTallyByIndexSources()) {  //out of range
    return TALLY_FLAG_OFF;
  }

  uint8_t tallyFlag = atemSwitcher.getTallyByIndexTallyFlags(tallyNo);
  if (tallyFlag & TALLY_FLAG_PROGRAM) {
    return TALLY_FLAG_PROGRAM;
  } else if (tallyFlag & TALLY_FLAG_PREVIEW) {
    return TALLY_FLAG_PREVIEW;
  } else {
    return TALLY_FLAG_OFF;
  }
}

int getLedColor(int tallyMode, int tallyNo) {
  if (tallyMode == MODE_ON_AIR) {
    if (atemSwitcher.getStreamStreaming()) {
      return LED_RED;
    }
    return LED_OFF;
  }

  int tallyState = getTallyState(tallyNo);

  if (tallyState == TALLY_FLAG_PROGRAM) {  //if tally live
    return LED_RED;
  } else if ((tallyState == TALLY_FLAG_PREVIEW       //if tally preview
              || tallyMode == MODE_PREVIEW_STAY_ON)  //or preview stay on
             && tallyMode != MODE_PROGRAM_ONLY) {    //and not program only
    return LED_GREEN;
  } else {  //if tally is neither
    return LED_OFF;
  }
}

//Serve setup web page to client, by sending HTML with the correct variables
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width,initial-scale=1.0\"><title>Tally Light setup</title></head><script>function switchIpField(e){console.log(\"switch\");console.log(e);var target=e.srcElement||e.target;var maxLength=parseInt(target.attributes[\"maxlength\"].value,10);var myLength=target.value.length;if(myLength>=maxLength){var next=target.nextElementSibling;if(next!=null){if(next.className.includes(\"IP\")){next.focus();}}}else if(myLength==0){var previous=target.previousElementSibling;if(previous!=null){if(previous.className.includes(\"IP\")){previous.focus();}}}}function ipFieldFocus(e){console.log(\"focus\");console.log(e);var target=e.srcElement||e.target;target.select();}function load(){var containers=document.getElementsByClassName(\"IP\");for(var i=0;i<containers.length;i++){var container=containers[i];container.oninput=switchIpField;container.onfocus=ipFieldFocus;}containers=document.getElementsByClassName(\"tIP\");for(var i=0;i<containers.length;i++){var container=containers[i];container.oninput=switchIpField;container.onfocus=ipFieldFocus;}toggleStaticIPFields();}function toggleStaticIPFields(){var enabled=document.getElementById(\"staticIP\").checked;document.getElementById(\"staticIPHidden\").disabled=enabled;var staticIpFields=document.getElementsByClassName('tIP');for(var i=0;i<staticIpFields.length;i++){staticIpFields[i].disabled=!enabled;}}</script><style>a{color:#0F79E0}</style><body style=\"font-family:Verdana;white-space:nowrap;\"onload=\"load()\"><table cellpadding=\"2\"style=\"width:100%\"><tr bgcolor=\"#777777\"style=\"color:#ffffff;font-size:.8em;\"><td colspan=\"3\"><h1>&nbsp;Tally Light setup</h1><h2>&nbsp;Status:</h2></td></tr><tr><td><br></td><td></td><td style=\"width:100%;\"></td></tr><tr><td>Connection Status:</td><td colspan=\"2\">";
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
  html += "<tr><td>Software VERSION:</td><td colspan=\"2\">";
  html += (String) VERSION;
  html += "</td></tr><tr><td>Static IP:</td><td colspan=\"2\">";
  html += settings.staticIP == true ? "True" : "False";
  html += "</td></tr><tr><td>Tally Light IP:</td><td colspan=\"2\">";
  html += WiFi.localIP().toString();
  html += "</td></tr><tr><td>Subnet mask: </td><td colspan=\"2\">";
  html += WiFi.subnetMask().toString();
  html += "</td></tr><tr><td>Gateway: </td><td colspan=\"2\">";
  html += WiFi.gatewayIP().toString();
  html += "</td></tr><tr><td><br></td></tr><tr><td>ATEM switcher status:</td><td colspan=\"2\">";
  // if (atemSwitcher.hasInitialized())
  //     html += "Connected - Initialized";
  // else
  if (atemSwitcher.isRejected())
    html += "Connection rejected - No empty spot";
  else if (atemSwitcher.isConnected())
    html += "Connected";  // - Wating for initialization";
  else if (WiFi.status() == WL_CONNECTED)
    html += "Disconnected - No response from switcher";
  else
    html += "Disconnected - Waiting for WiFi";
  html += "</td></tr><tr><td>ATEM switcher IP:</td><td colspan=\"2\">";
  html += (String)settings.switcherIP[0] + '.' + settings.switcherIP[1] + '.' + settings.switcherIP[2] + '.' + settings.switcherIP[3];
  html += "</td></tr><tr><td><br></td></tr><tr bgcolor=\"#777777\"style=\"color:#ffffff;font-size:.8em;\"><td colspan=\"3\"><h2>&nbsp;Settings:</h2></td></tr><tr><td><br></td></tr><form action=\"/save\"method=\"post\"><tr><td>Tally Light name: </td><td><input type=\"text\"size=\"30\"maxlength=\"30\"name=\"tName\"value=\"";
#if ESP32
  html += WiFi.getHostname();
#else
  html += WiFi.hostname();
#endif
  html += "\"required/></td></tr><tr><td><br></td></tr><tr><td>Tally Light number: </td><td><input type=\"number\"size=\"5\"min=\"1\"max=\"41\"name=\"tNo\"value=\"";
  html += (settings.tallyNo + 1);
  html += "\"required/></td></tr><tr><td>Tally Light mode (LED 1):&nbsp;</td><td><select name=\"tModeLED1\"><option value=\"";
  html += (String)MODE_NORMAL + "\"";
  if (settings.tallyModeLED1 == MODE_NORMAL)
    html += "selected";
  html += ">Normal</option><option value=\"";
  html += (String)MODE_PREVIEW_STAY_ON + "\"";
  if (settings.tallyModeLED1 == MODE_PREVIEW_STAY_ON)
    html += "selected";
  html += ">Preview stay on</option><option value=\"";
  html += (String)MODE_PROGRAM_ONLY + "\"";
  if (settings.tallyModeLED1 == MODE_PROGRAM_ONLY)
    html += "selected";
  html += ">Program only</option><option value=\"";
  html += (String)MODE_ON_AIR + "\"";
  if (settings.tallyModeLED1 == MODE_ON_AIR)
    html += "selected";
  html += ">On Air</option></select></td></tr><tr><td>Tally Light mode (LED 2):</td><td><select name=\"tModeLED2\"><option value=\"";
  html += (String)MODE_NORMAL + "\"";
  if (settings.tallyModeLED2 == MODE_NORMAL)
    html += "selected";
  html += ">Normal</option><option value=\"";
  html += (String)MODE_PREVIEW_STAY_ON + "\"";
  if (settings.tallyModeLED2 == MODE_PREVIEW_STAY_ON)
    html += "selected";
  html += ">Preview stay on</option><option value=\"";
  html += (String)MODE_PROGRAM_ONLY + "\"";
  if (settings.tallyModeLED2 == MODE_PROGRAM_ONLY)
    html += "selected";
  html += ">Program only</option><option value=\"";
  html += (String)MODE_ON_AIR + "\"";
  if (settings.tallyModeLED2 == MODE_ON_AIR)
    html += "selected";
  html += ">On Air</option></select></td></tr><tr><td> Led brightness: </td><td><input type=\"number\"size=\"5\"min=\"0\"max=\"255\"name=\"ledBright\"value=\"";
  html += settings.ledBrightness;
  html += "\"required/></td></tr><tr><td><br></td></tr><tr><td>Network name(SSID): </td><td><input type =\"text\"size=\"30\"maxlength=\"30\"name=\"ssid\"value=\"";
  html += getSSID();
  html += "\"required/></td></tr><tr><td>Network password: </td><td><input type=\"password\"size=\"30\"maxlength=\"30\"name=\"pwd\"pattern=\"^$|.{8,32}\"value=\"";
  if (WiFi.isConnected())  //As a minimum security meassure, to only send the wifi password if it's currently connected to the given network.
    html += WiFi.psk();
  html += "\"/></td></tr><tr><td><br></td></tr><tr><td>Use static IP: </td><td><input type=\"hidden\"id=\"staticIPHidden\"name=\"staticIP\"value=\"false\"/><input id=\"staticIP\"type=\"checkbox\"name=\"staticIP\"value=\"true\"onchange=\"toggleStaticIPFields()\"";
  if (settings.staticIP)
    html += "checked";
  html += "/></td></tr><tr><td>Tally Light IP: </td><td><input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"tIP1\"pattern=\"\\d{0,3}\"value=\"";
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
  html += "\"required/></td></tr><tr><td><br></td></tr><tr><td>ATEM switcher IP: </td><td><input class=\"IP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"aIP1\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.switcherIP[0];
  html += "\"required/>. <input class=\"IP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"aIP2\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.switcherIP[1];
  html += "\"required/>. <input class=\"IP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"aIP3\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.switcherIP[2];
  html += "\"required/>. <input class=\"IP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"aIP4\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.switcherIP[3];
  html += "\"required/></tr><tr><td><br></td></tr><tr><td/><td style=\"float: right;\"><input type=\"submit\"value=\"Save Changes\"/></td></tr></form><tr bgcolor=\"#cccccc\"style=\"font-size: .8em;\"><td colspan=\"3\"><p>&nbsp;&copy; 2023-2024 Francesco Cucchiaro; ATEM software based on the work by <a href=\"https://aronhetlam.github.io/\">Aron N. Het Lam</a></p><p>&nbsp; and <a href=\"https://www.skaarhoj.com/\">SKAARHOJ</a></p></td></tr></table></body></html>";
  server.send(200, "text/html", html);
}

//Save new settings from client in EEPROM and restart the ESP8266 module
void handleSave() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/html", "<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1.0\"><title>Tally Light setup</title></head><body style=\"font-family:Verdana;\"><table bgcolor=\"#777777\"border=\"0\"width=\"100%\"cellpadding=\"1\"style=\"color:#ffffff;font-size:.8em;\"><tr><td><h1>&nbsp;Tally Light setup</h1></td></tr></table><br>Request without posting settings not allowed</body></html>");
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
      } else if (var == "ledBright") {
        settings.ledBrightness = val.toInt();
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

      server.send(200, "text/html", (String) "<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1.0\"><title>Tally Light setup</title></head><body><table bgcolor=\"#777777\"border=\"0\"width=\"100%\"cellpadding=\"1\"style=\"font-family:Verdana;color:#ffffff;font-size:.8em;\"><tr><td><h1>&nbsp;Tally Light setup</h1></td></tr></table><br>Settings saved successfully.</body></html>");

      // Delay to let data be saved, and the response to be sent properly to the client
      server.close();  // Close server to flush and ensure the response gets to the client
      delay(100);

      // Change into STA mode to disable softAP
      WiFi.mode(WIFI_STA);
      delay(100);  // Give it time to switch over to STA mode (this is important on the ESP32 at least)

      if (ssid && pwd) {
        WiFi.persistent(true);  // Needed by ESP8266
        // Pass in 'false' as 5th (connect) argument so we don't waste time trying to connect, just save the new SSID/PSK
        // 3rd argument is channel - '0' is default. 4th argument is BSSID - 'NULL' is default.
        WiFi.begin(ssid.c_str(), pwd.c_str(), 0, NULL, false);
      }

      //Delay to apply settings before restart
      delay(100);
      ESP.restart();
    }
  }
}

//Send 404 to client in case of invalid webpage being requested.
void handleNotFound() {
  server.send(404, "text/html", "<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1.0\"><title>Tally Light setup</title></head><body style=\"font-family:Verdana;\"><table bgcolor=\"#777777\"border=\"0\"width=\"100%\"cellpadding=\"1\"style=\"color:#ffffff;font-size:.8em;\"><tr><td><h1>&nbsp Tally Light setup</h1></td></tr></table><br>404 - Page not found</body></html>");
}

String getSSID() {
  return WiFi.SSID();
}
