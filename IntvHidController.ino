/*********************************************************************
 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 Copyright (c) 2019 Ha Thach for Adafruit Industries
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/

#include <BleGamepad.h>
#include <Arduino.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp32-hal-bt.h>
#include <Preferences.h>

/* This sketch demonstrates USB HID keyboard.
 * - PIN A0-A5 is used to send digit '0' to '5' respectively
 *   (On the RP2040, pins D0-D5 used)
 * - LED and/or Neopixels will be used as Capslock indicator
 */

// Create a new gamepad instance with battery support and analog sticks
BleGamepad bleGamepad("Intellivision Controller", "Intellivision", 100);

// Input pins for the controller
const uint8_t pins[] = { 5, 4, 3, 2, 9, 8, 7, 6 };  // ESP32 GPIO pins
const uint8_t pincount = sizeof(pins)/sizeof(pins[0]);

// Status LED pin
const uint8_t STATUS_LED = 2;  // Built-in LED on most ESP32 boards

// Battery monitoring
const uint8_t BATTERY_PIN = 34;  // ADC pin for battery voltage
const float BATTERY_MAX_VOLTAGE = 4.2;  // Maximum battery voltage
const float BATTERY_MIN_VOLTAGE = 3.3;  // Minimum battery voltage
const uint8_t BATTERY_CHECK_INTERVAL = 60;  // Check battery every 60 seconds

// Power saving
const unsigned long IDLE_TIMEOUT = 300000;  // 5 minutes in milliseconds
unsigned long lastActivityTime = 0;
bool isSleeping = false;

// Controller profiles
const uint8_t MAX_PROFILES = 4;
uint8_t currentProfile = 0;
Preferences preferences;

// Button mapping for the gamepad
// Direction pad buttons (16 directions)
const uint8_t DIRECTION_BUTTONS = 16;
// Keypad buttons (12 buttons)
const uint8_t KEYPAD_BUTTONS = 12;
// Side buttons (3 buttons)
const uint8_t SIDE_BUTTONS = 3;

// Total number of buttons
const uint8_t TOTAL_BUTTONS = DIRECTION_BUTTONS + KEYPAD_BUTTONS + SIDE_BUTTONS;

// Button states
bool buttonStates[TOTAL_BUTTONS] = {false};
bool lastButtonStates[TOTAL_BUTTONS] = {false};

// Button mapping array (same as original keymap but for gamepad buttons)
uint8_t buttonMap[] = {
  0x40, 0x41, 0x61, 0x60, 0x20, 0x21, 0x31, 0x30, 
  0x10, 0x11, 0x91, 0x90, 0x80, 0x81, 0xc1, 0xc0,
  
  0x18, 0x14, 0x12, 0x28, 0x24, 
  0x22, 0x48, 0x44, 0x42, 
  0x88, 0x84, 0x82,
  
  0x0a, 0x06, 0x0c, 0x00
};

// Analog stick mapping for disc positions
struct AnalogMapping {
  int16_t x;
  int16_t y;
};

// Analog stick mappings for each disc position (clockwise from North)
const AnalogMapping analogMappings[] = {
  {0, -32767},    // N
  {16383, -28377}, // NNE
  {32767, -32767}, // NE
  {28377, 16383},  // ENE
  {32767, 0},      // E
  {28377, 16383},  // ESE
  {32767, 32767},  // SE
  {16383, 28377},  // SSE
  {0, 32767},      // S
  {-16383, 28377}, // SSW
  {-32767, 32767}, // SW
  {-28377, 16383}, // WSW
  {-32767, 0},     // W
  {-28377, -16383},// WNW
  {-32767, -32767},// NW
  {-16383, -28377} // NNW
};

// Button combinations
struct ButtonCombination {
  uint8_t buttons[3];  // Up to 3 buttons for combination
  uint8_t action;      // Action to perform
};

const ButtonCombination buttonCombos[] = {
  {{29, 30, 0}, 1},  // Side buttons 1+2 = Profile switch
  {{30, 31, 0}, 2},  // Side buttons 2+3 = Toggle analog mode
  {{29, 31, 0}, 3}   // Side buttons 1+3 = Save current profile
};

bool analogMode = false;

//------------- Neopixel -------------//
// #define PIN_NEOPIXEL  8
#ifdef PIN_NEOPIXEL

// How many NeoPixels are attached to the Arduino?
// use on-board defined NEOPIXEL_NUM if existed
#ifndef NEOPIXEL_NUM
  #define NEOPIXEL_NUM  10
#endif

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

#endif

// Function to read battery level
uint8_t readBatteryLevel() {
  int rawValue = analogRead(BATTERY_PIN);
  float voltage = (rawValue * 3.3) / 4095.0;  // Convert to voltage
  uint8_t percentage = map(voltage * 100, BATTERY_MIN_VOLTAGE * 100, BATTERY_MAX_VOLTAGE * 100, 0, 100);
  return constrain(percentage, 0, 100);
}

// Function to update status LED
void updateStatusLED() {
  if (!bleGamepad.isConnected()) {
    // Blink slowly when not connected
    digitalWrite(STATUS_LED, (millis() / 1000) % 2);
  } else {
    // Solid on when connected
    digitalWrite(STATUS_LED, HIGH);
  }
}

// Function to enter sleep mode
void enterSleepMode() {
  if (!isSleeping) {
    isSleeping = true;
    bleGamepad.end();
    esp_bt_controller_disable();
    esp_deep_sleep_start();
  }
}

// Function to wake up from sleep mode
void wakeUp() {
  if (isSleeping) {
    isSleeping = false;
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    bleGamepad.begin();
  }
}

// Function to check for button combinations
void checkButtonCombinations() {
  for (const auto& combo : buttonCombos) {
    bool comboPressed = true;
    for (uint8_t i = 0; i < 3 && combo.buttons[i] != 0; i++) {
      if (!buttonStates[combo.buttons[i] - 1]) {
        comboPressed = false;
        break;
      }
    }
    
    if (comboPressed) {
      switch (combo.action) {
        case 1: // Profile switch
          currentProfile = (currentProfile + 1) % MAX_PROFILES;
          loadProfile(currentProfile);
          break;
        case 2: // Toggle analog mode
          analogMode = !analogMode;
          break;
        case 3: // Save profile
          saveProfile(currentProfile);
          break;
      }
    }
  }
}

// Function to save current profile
void saveProfile(uint8_t profile) {
  preferences.begin("intvctrl", false);
  preferences.putUChar("profile", profile);
  preferences.putBool("analog", analogMode);
  // Save button mappings for this profile
  for (uint8_t i = 0; i < TOTAL_BUTTONS; i++) {
    char key[10];
    sprintf(key, "btn%d", i);
    preferences.putUChar(key, buttonMap[i]);
  }
  preferences.end();
}

// Function to load profile
void loadProfile(uint8_t profile) {
  preferences.begin("intvctrl", true);
  currentProfile = preferences.getUChar("profile", 0);
  analogMode = preferences.getBool("analog", false);
  // Load button mappings for this profile
  for (uint8_t i = 0; i < TOTAL_BUTTONS; i++) {
    char key[10];
    sprintf(key, "btn%d", i);
    buttonMap[i] = preferences.getUChar(key, buttonMap[i]);
  }
  preferences.end();
}

// the setup function runs once when you press reset or power the board
void setup()
{
#if defined(ARDUINO_ARCH_MBED) && defined(ARDUINO_ARCH_RP2040)
  // Manual begin() is required on core without built-in support for TinyUSB such as mbed rp2040
  TinyUSB_Device_Init(0);
#endif

  // Notes: following commented-out functions has no affect on ESP32
  // usb_hid.setBootProtocol(HID_ITF_PROTOCOL_KEYBOARD);
  // usb_hid.setPollInterval(2);
  // usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("Intellivision Disk Controller");

  // Set up output report (on control endpoint) for Capslock indicator
  usb_hid.setReportCallback(NULL, hid_report_callback);

  usb_hid.begin();

  // led pin
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // neopixel if existed
#ifdef PIN_NEOPIXEL
  pixels.begin();
  pixels.setBrightness(50);

  #ifdef NEOPIXEL_POWER
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  #endif
#endif

  // Initialize status LED
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  // Initialize battery monitoring
  pinMode(BATTERY_PIN, INPUT);

  // Set up pin as input
  for (uint8_t i=0; i<pincount; i++)
  {
    pinMode(pins[i], INPUT_PULLUP);
  }

  // wait until device mounted
  while( !TinyUSBDevice.mounted() ) delay(1);

  Serial.begin(115200);
  Serial.println("Starting BLE Gamepad...");

  // Load saved profile
  loadProfile(0);

  // Initialize the BLE gamepad
  bleGamepad.begin();
  
  // Set the number of buttons
  bleGamepad.setButtonCount(TOTAL_BUTTONS);
  
  // Enable battery reporting
  bleGamepad.setBatteryLevel(100);
  
  Serial.println("BLE Gamepad started!");
}


void loop()
{
  static unsigned long lastBatteryCheck = 0;
  static unsigned long lastLEDUpdate = 0;
  
  // Update status LED every 100ms
  if (millis() - lastLEDUpdate >= 100) {
    updateStatusLED();
    lastLEDUpdate = millis();
  }

  // Check battery level periodically
  if (millis() - lastBatteryCheck >= (BATTERY_CHECK_INTERVAL * 1000)) {
    uint8_t batteryLevel = readBatteryLevel();
    bleGamepad.setBatteryLevel(batteryLevel);
    lastBatteryCheck = millis();
    
    // Enter sleep mode if battery is critically low
    if (batteryLevel < 10) {
      enterSleepMode();
    }
  }

  if (bleGamepad.isConnected()) {
    // Reset idle timer on any activity
    lastActivityTime = millis();
    
    // Read the controller state
    uint8_t keyvalue = 0;
    
    // Read all pins
    for (uint8_t i = 0; i < pincount; i++) {
      if (digitalRead(pins[i]) == LOW) {  // Active low
        keyvalue |= (1 << (7-i));
      }
    }

    // Map the keyvalue to button states
    bool anyButtonPressed = false;
    for (uint8_t i = 0; i < TOTAL_BUTTONS; i++) {
      bool newState = (buttonMap[i] == keyvalue);
      if (newState != buttonStates[i]) {
        buttonStates[i] = newState;
        bleGamepad.press(i + 1, newState);  // Buttons are 1-based
        anyButtonPressed = true;
      }
    }

    // Check for button combinations
    checkButtonCombinations();

    // Handle analog stick if in analog mode
    if (analogMode) {
      for (uint8_t i = 0; i < DIRECTION_BUTTONS; i++) {
        if (buttonStates[i]) {
          bleGamepad.setLeftThumb(analogMappings[i].x, analogMappings[i].y);
          break;
        }
      }
    }

    // If no buttons are pressed, release all
    if (!anyButtonPressed) {
      for (uint8_t i = 0; i < TOTAL_BUTTONS; i++) {
        if (buttonStates[i]) {
          buttonStates[i] = false;
          bleGamepad.press(i + 1, false);
        }
      }
      if (analogMode) {
        bleGamepad.setLeftThumb(0, 0);
      }
    }
  } else {
    // Check for idle timeout
    if (millis() - lastActivityTime >= IDLE_TIMEOUT) {
      enterSleepMode();
    }
  }

  // Small delay to prevent too frequent updates
  delay(10);
}

// Output report callback for LED indicator such as Caplocks
void hid_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) report_id;
  (void) bufsize;

  // LED indicator is output report with only 1 byte length
  if ( report_type != HID_REPORT_TYPE_OUTPUT ) return;

  // The LED bit map is as follows: (also defined by KEYBOARD_LED_* )
  // Kana (4) | Compose (3) | ScrollLock (2) | CapsLock (1) | Numlock (0)
  uint8_t ledIndicator = buffer[0];

  // turn on LED if capslock is set
  digitalWrite(LED_BUILTIN, ledIndicator & KEYBOARD_LED_CAPSLOCK);

#ifdef PIN_NEOPIXEL
  pixels.fill(ledIndicator & KEYBOARD_LED_CAPSLOCK ? 0xff0000 : 0x000000);
  pixels.show();
#endif
}
