#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PZEM004Tv30.h>
#include <AceButton.h>
#include <digitalWriteFast.h>
#include <ace_button/fast/ButtonConfigFast2.h>

using namespace ace_button;

#define RESET_BUTTON_PIN 12
#define WIFI_BUTTON_PIN 14

const char * host = "ESP32 EnergyMonitor";

WebServer server(80);
LiquidCrystal_I2C lcd(0x27, 20, 4);
PZEM004Tv30 pzem(Serial2, 16, 17);

const String userid = "******"; // Username
const String password = "********"; // Password

// HTML content for login and server index pages

String loginIndex PROGMEM = "<form name='loginForm'>"
"<table width='20%' bgcolor='A09F9F' align='center'>"
"<tr><td colspan=2><center><font size=4><b>ESP32 Energy Meter Login Page</b></font></center><br></td></tr>"
"<tr><td>Username:</td><td><input type='text' size=25 name='userid'><br></td></tr>"
"<tr><td>Password:</td><td><input type='Password' size=25 name='pwd'><br></td></tr>"
"<tr><td><input type='submit' onclick='check(this.form)' value='Login'></td></tr></table></form>"
"<script>" "function check(form) {"
"if(form.userid.value=='" + userid + "' && form.pwd.value=='" + password + "') {"
"window.open('/serverIndex')"
"} else {"
"alert('Error Password or Username')"
"}}" "</script>";

const char * serverIndex PROGMEM = "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
"<input type='file' name='update'> <input type='submit' value='Update'> </form>"
"<div id='prg'>progress: 0%</div>"
"<script>$('form').submit(function(e){e.preventDefault();var form = $('#upload_form')[0];"
"var data = new FormData(form);$.ajax({url: '/update',type: 'POST',data: data,contentType: false,"
"processData:false,xhr: function() {var xhr = new window.XMLHttpRequest();"
"xhr.upload.addEventListener('progress', function(evt) {if (evt.lengthComputable) {"
"var per = evt.loaded / evt.total;$('#prg').html('progress: ' + Math.round(per*100) + '%');}});return xhr;},"
"success:function(d, s) {console.log('success!')},error: function (a, b, c) {}});});</script>";

float voltage, current, power, frequency, pf, energy;
float energyConsumed = 0.0;
float costPerUnit = 7; // Adjust based on your tariff
float cost = 0.0;

unsigned long lastUpdateTime = 0; // Variable to store the last update time
const unsigned long updateInterval = 1000; // Interval between data updates in milliseconds
bool otaMode = false;

byte rupeeSymbol[8] = {
    B11111,
    B00010,
    B11111,
    B00010,
    B11100,
    B01100,
    B00010,
    B00001
};

// AceButton configuration
ButtonConfigFast2 < WIFI_BUTTON_PIN, RESET_BUTTON_PIN > buttonConfig;
AceButton wifiButton( & buttonConfig, 0); // Virtual pin 0 for Wi-Fi button
AceButton resetButton( & buttonConfig, 1); // Virtual pin 1 for Reset button

// Forward declaration of event handler
void handleEvent(AceButton * , uint8_t, uint8_t);

void setup() {
    // Initialize buttons and LCD
    lcdInitialize();
    initializeButtons();
    Serial.begin(115200);
}
void loop() {
    // Check button states
    wifiButton.check();
    resetButton.check();

    if (otaMode) {
        // Only handle OTA when in OTA mode
        server.handleClient();
        delay(1);
        //Serial.println("In OTA Mode");
        return;
    }
    if (millis() - lastUpdateTime >= updateInterval) {
        lastUpdateTime = millis(); // Update the last update time
        getData(); // Fetch new data
        //serialPrintData(); // Print data to serial monitor
        lcdPrintData(); // Display data on the LCD
    }
    //buttonListener();
}

void printCentered(const char * text, int row) {
    int len = strlen(text);
    int startCol = (20 - len) / 2; //20 characters per row
    lcd.setCursor(startCol, row);
    lcd.print(text);
}

void lcdInitialize() {
    lcd.init();
    lcd.backlight();
    lcd.createChar(0, rupeeSymbol);
    printCentered("ENERGY", 1);
    printCentered("MONITOR", 2);
    delay(2000); // 2-second delay
    lcd.clear();
}

void initializeButtons() {
  buttonConfig.setDebounceDelay(20); // Set debounce to 20ms

    buttonConfig.setEventHandler(handleEvent);
    buttonConfig.setFeature(ButtonConfig::kFeatureClick);
    buttonConfig.setFeature(ButtonConfig::kFeatureDoubleClick);
    buttonConfig.setFeature(ButtonConfig::kFeatureLongPress);
    pinModeFast(WIFI_BUTTON_PIN, INPUT_PULLUP);
    pinModeFast(RESET_BUTTON_PIN, INPUT_PULLUP);
}

void getData() {
    voltage = isnan(pzem.voltage()) ? 0.0 : pzem.voltage();
    current = isnan(pzem.current()) ? 0.0 : pzem.current();
    power = isnan(pzem.power()) ? 0.0 : pzem.power();
    energy = isnan(pzem.energy()) ? 0.0 : pzem.energy();
    frequency = isnan(pzem.frequency()) ? 0.0 : pzem.frequency();
    pf = isnan(pzem.pf()) ? 0.0 : pzem.pf();
    energyConsumed = energy;
    cost = energyConsumed * costPerUnit;
}

void serialPrintData() {
    Serial.print("Voltage: ");   Serial.print(voltage);      Serial.println("V");
    Serial.print("Current: ");   Serial.print(current);      Serial.println("A");
    Serial.print("Power: ");     Serial.print(power);        Serial.println("W");
    Serial.print("Energy: ");    Serial.print(energy, 3);    Serial.println("kWh");
    Serial.print("Frequency: "); Serial.print(frequency, 1); Serial.println("Hz");
    Serial.print("PF: ");        Serial.println(pf);
    Serial.print("Cost: Rs.");   Serial.println(cost);
}

void printData(String justification, int row, int col, String name, float value, int width, int format, String unit = "") {
    char buffer[16]; // Increased buffer size to accommodate larger numbers
    int startCol = col;

    dtostrf(value, width, format, buffer); // Convert the float value to a string with specified width and decimal places
    if (justification == "left" || "center" || "right") {
        int i = 0;
        while (buffer[i] == ' ') i++; // Find the first non-space character
        memmove(buffer, buffer + i, strlen(buffer) - i + 1); // Shift the string to remove leading spaces
    }
    // Calculate the start column based on justification
    if (justification == "center") {
        startCol = (20 - (name.length() + String(buffer).length() + unit.length() + 1)) / 2; // +1 for ":"
    } else if (justification == "right") {
        startCol = 20 - (name.length() + String(buffer).length() + unit.length() + 1); // +1 for ":"
    }

    // Clear the previous value by overwriting with spaces
    lcd.setCursor(startCol, row);
    int lengthToClear = name.length() + String(buffer).length() + unit.length() + 1;
    for (int i = 0; i < lengthToClear; i++) {
        lcd.print(" ");
    }

    // Set the cursor again and print the new value
    lcd.setCursor(startCol, row);
    lcd.print(name + ":");
    if (name == "Cost") {
        lcd.write(byte(0)); // Print the rupee symbol if it's the cost
    }
    lcd.print(buffer);
    if (unit != "") {
        lcd.print(unit);
    }
}

void lcdPrintData() {
  
        printData("left", 0, 0, "V", voltage, 5, 2, "V");
        printData("right", 0, 0, "I", current, 4, 2, "A");
        printData("left", 1, 0, "P", power, 6, 2, "W"); 
        printData("right", 1, 0, "E", energy, 5, 3, "kWh");
        printData("left", 2, 0, "F", frequency, 3, 1, "Hz");
        printData("right", 2, 0, "PF", pf, 3, 2, "");
        printData("center", 3, 0, "Cost", cost, 6, 2, "");
}

// Event handler for both buttons
void handleEvent(AceButton * button, uint8_t eventType, uint8_t buttonState) {
    if (button -> getPin() == 0) { // Wi-Fi button
        if (eventType == AceButton::kEventClicked) {
            wifiButtonShortPress();
        }
        
        else if (eventType == AceButton::kEventLongPressed) {
            if (otaMode) {
                exitOtaMode(); // Exit OTA mode on long press
            } else {
                wifiButtonLongPress();
            }
        }
    } else if (button -> getPin() == 1) { // Reset button
        if (eventType == AceButton::kEventClicked) {
            resetButtonShortPress();}
            else if (eventType == AceButton::kEventDoubleClicked) {
          wifiButtondoubleClick();
        }
         else if (eventType == AceButton::kEventLongPressed) {
            resetEnergy();
        }
    }
}


void wifiButtonShortPress() {
    //Serial.println("Wi-Fi Button Short Press: Initiating Wi-Fi Manager");
    lcd.clear();
    printCentered("Wi-Fi", 0);
    printCentered("CONNECTION", 1);
    printCentered("BEING", 2);
    printCentered("ATTEMPTED", 3);
    delay(1000);
    lcd.clear();

    WiFiManager wm;
    //wm.resetSettings(); // Optional: Use only if you want to clear saved credentials each time
    delay(1000);
    // Force disconnect from any existing Wi-Fi connections
    WiFi.disconnect(true);  // Disconnect from any station network and erase Wi-Fi credentials

    // Set Wi-Fi mode to AP+STA
    WiFi.mode(WIFI_AP_STA);  // Ensure both AP and STA modes are enabledreset
    wm.setAPCallback([](WiFiManager *myWiFiManager) {
        //Serial.println("WiFiManager Captive Portal started.");
        lcd.clear();
        printCentered("CAPTIVE", 1);
        printCentered("PORTAL STARTED", 2);
    });
    if (!wm.autoConnect("EnergyMonitorAP")) {
        //Serial.println("Failed to connect and hit timeout");
        lcd.clear();
        printCentered("Wi-Fi", 1);
        printCentered("FAILED", 2);
        delay(2000);
        ESP.restart();
    }

    //Serial.println("Wi-Fi connected.");
    lcd.clear();
    printCentered("Wi-Fi", 0);
    printCentered("CONNECTED", 1);
    String ipAddress = WiFi.localIP().toString();
    printCentered(ipAddress.c_str(), 2);
    delay(2000);
    lcd.clear();
}
void wifiButtondoubleClick(){
  WiFiManager wm;
  wm.resetSettings(); // Optional: Use only if you want to clear saved credentials each time
  printCentered("Wi-Fi", 1);
        printCentered("Reset", 2);
}
void wifiButtonLongPress() {
    //Serial.println("Wi-Fi Button Long Press: Entering OTA Mode");
    // Check if Wi-Fi is already connected
    if (WiFi.status() != WL_CONNECTED) {
        WiFiManager wm;
        lcd.clear();
    printCentered("Wi-Fi", 0);
    printCentered("CONNECTION", 1);
    printCentered("BEING", 2);
    printCentered("ATTEMPTED", 3);
    delay(1000);
    lcd.clear();
        if (!wm.autoConnect("EnergyMonitorAP")) {
            //Serial.println("Failed to connect and hit timeout");
            ESP.restart();
        }
    }

    // Initialize mDNS for OTA access
    if (!MDNS.begin(host)) {
        //Serial.println("Error setting up MDNS responder!");
        while (1) {
            delay(1000);
        }
    }
    //Serial.println("mDNS responder started");

    // Start the web server for OTA
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", loginIndex);
    });

    server.on("/serverIndex", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", serverIndex);
    });

    // Handling firmware update
    server.on("/update", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        ESP.restart();
    }, []() {
        HTTPUpload & upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            //Serial.printf("Update: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { // Start with max available size
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) { // true to set the size to the current progress
                //Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
            } else {
                Update.printError(Serial);
            }
        }
    });

    // Start web server and indicate OTA mode on LCD
    server.begin();
    otaMode = true;
    lcd.clear();
    printCentered("OTA MODE", 1);
    printCentered("Waiting for", 2);
    printCentered("update...", 3);
    delay(1000);
}

void exitOtaMode() {
    //Serial.println("Exiting OTA Mode");
    otaMode = false;
    lcd.clear();
    printCentered("Exiting", 1);
    printCentered("OTA Mode", 2);
    delay(1000);
    lcd.clear();
}
void resetButtonShortPress() {
    //Serial.println("Reset Button Short Press: Clearing LCD");
    lcd.clear();
    printCentered("LCD Refresh", 1);
    delay(500);
    lcd.clear();
}

void resetEnergy() {
    //Serial.println("Reset Button Long Press: Resetting Energy");
    pzem.resetEnergy();
    energyConsumed = 0.0;
    lcd.clear();
    printCentered("ENERGY", 1);
    printCentered("RESET", 2);
    delay(1000);
    lcd.clear();
}
