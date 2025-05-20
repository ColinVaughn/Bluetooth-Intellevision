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

// Enhanced power management
const uint8_t POWER_SAVE_LEVELS = 3;  // Number of power save levels
uint8_t currentPowerLevel = 0;  // Current power save level (0 = normal, 1 = low, 2 = ultra-low)
const unsigned long POWER_SAVE_TIMEOUTS[] = {300000, 180000, 60000};  // Timeouts for each level (5min, 3min, 1min)
const uint8_t POWER_SAVE_BATTERY_THRESHOLDS[] = {20, 40, 60};  // Battery thresholds for each level
const uint8_t LED_BRIGHTNESS_LEVELS[] = {255, 128, 64};  // LED brightness for each level
const uint8_t POLLING_INTERVALS[] = {10, 20, 50};  // Polling intervals in ms for each level

// Power management state
bool isCharging = false;
unsigned long lastPowerCheck = 0;
const unsigned long POWER_CHECK_INTERVAL = 5000;  // Check power state every 5 seconds

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
  {{29, 31, 0}, 3},  // Side buttons 1+3 = Save current profile
  {{17, 18, 19}, 4}  // Keypad 1+2+3 = Start calibration
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

// Macro support
struct MacroStep {
  uint8_t button;  // Button to press
  uint16_t duration;  // Duration in milliseconds
};

struct Macro {
  uint8_t triggerButtons[3];  // Up to 3 buttons to trigger the macro
  uint8_t stepCount;  // Number of steps in the macro
  MacroStep steps[16];  // Maximum 16 steps per macro
};

const uint8_t MAX_MACROS = 4;  // Maximum number of macros per profile
Macro macros[MAX_MACROS];
bool isExecutingMacro = false;
unsigned long macroStartTime = 0;
uint8_t currentMacroStep = 0;
uint8_t currentMacro = 0;

// Status LED patterns
const uint8_t LED_PATTERN_CONNECTING = 0;    // Slow blink (1s on, 1s off)
const uint8_t LED_PATTERN_CONNECTED = 1;     // Solid on
const uint8_t LED_PATTERN_SLEEP = 2;         // Off
const uint8_t LED_PATTERN_ERROR = 3;         // Fast blink (100ms on, 100ms off)
const uint8_t LED_PATTERN_LOW_BATTERY = 4;   // Double blink (200ms on, 200ms off, 200ms on, 1400ms off)
const uint8_t LED_PATTERN_CHARGING = 5;      // Breathing effect
const uint8_t LED_PATTERN_MACRO = 6;         // Quick triple blink every 2s
const uint8_t LED_PATTERN_PROFILE = 7;       // Number of blinks = profile number + 1

// LED state
uint8_t currentLEDPattern = LED_PATTERN_CONNECTING;
unsigned long lastLEDUpdate = 0;
uint8_t ledBrightness = 255;
bool ledState = false;

// Calibration support
struct CalibrationData {
  uint16_t minValues[8];  // Minimum values for each pin
  uint16_t maxValues[8];  // Maximum values for each pin
  uint16_t centerValues[8];  // Center/rest values for each pin
  bool isCalibrated;
};

CalibrationData calibrationData;
bool isCalibrating = false;
uint8_t calibrationStep = 0;
unsigned long calibrationStartTime = 0;

// Input sensitivity settings
struct SensitivitySettings {
  uint8_t deadzone;        // Deadzone size (0-255)
  uint8_t sensitivity;     // Input sensitivity (0-255)
  uint8_t responseCurve;   // Response curve type (0=linear, 1=exponential, 2=logarithmic)
  bool autoCenter;         // Auto-center the disc
};

SensitivitySettings sensitivitySettings;

// Input processing settings
struct InputProcessingSettings {
  uint8_t filterStrength;     // Signal filter strength (0-255)
  uint8_t smoothingFactor;    // Input smoothing factor (0-255)
  uint16_t debounceTime;      // Debounce time in milliseconds
  bool enableFiltering;       // Enable/disable signal filtering
  bool enableSmoothing;       // Enable/disable input smoothing
  bool enableDebouncing;      // Enable/disable debouncing
};

InputProcessingSettings inputSettings;

// Circular buffer for input smoothing
const uint8_t SMOOTHING_BUFFER_SIZE = 8;
struct InputBuffer {
  uint16_t values[SMOOTHING_BUFFER_SIZE];
  uint8_t index;
  uint8_t count;
};

InputBuffer inputBuffers[8];  // One buffer per pin

// Button debounce state
struct DebounceState {
  bool lastState;
  unsigned long lastChangeTime;
  bool stableState;
};

DebounceState debounceStates[31];  // One state per button

// Battery configuration
const uint16_t DEFAULT_BATTERY_CAPACITY = 1000;  // Default 1000mAh
uint16_t batteryCapacity = DEFAULT_BATTERY_CAPACITY;  // Current battery capacity in mAh

// Power consumption constants (in mA)
const uint8_t POWER_ACTIVE = 100;    // Active use power consumption
const uint8_t POWER_IDLE = 50;       // Connected but idle
const uint8_t POWER_LOW = 30;        // Low power mode
const uint8_t POWER_ULTRA = 20;      // Ultra-low power mode
const uint8_t POWER_SLEEP = 1;       // Sleep mode

// Enhanced battery detection settings
struct BatteryDetection {
  uint32_t lastVoltage;          // Last measured voltage
  uint32_t lastCapacity;         // Last detected capacity
  uint32_t dischargeStartTime;   // When discharge started
  uint32_t dischargeStartLevel;  // Battery level when discharge started
  uint32_t samples[10];          // Voltage samples for averaging
  uint8_t sampleIndex;           // Current sample index
  bool isDischarging;            // Whether battery is discharging
  bool isCharging;               // Whether battery is charging
  uint32_t lastDetectionTime;    // Last capacity detection time
  
  // New fields for enhanced detection
  uint32_t voltageHistory[50];     // Extended voltage history
  uint8_t voltageIndex;            // Current voltage history index
  uint32_t dischargeHistory[10];   // History of discharge measurements
  uint8_t dischargeIndex;          // Current discharge history index
  uint32_t capacityHistory[5];     // History of capacity measurements
  uint8_t capacityIndex;           // Current capacity history index
  float confidence;                // Detection confidence (0-1)
  uint32_t lastVerificationTime;   // Last capacity verification time
  bool isVerified;                 // Whether capacity is verified
};

BatteryDetection batteryDetection;

// Function to initialize battery detection
void initBatteryDetection() {
  batteryDetection.lastVoltage = 0;
  batteryDetection.lastCapacity = 0;
  batteryDetection.dischargeStartTime = 0;
  batteryDetection.dischargeStartLevel = 0;
  batteryDetection.sampleIndex = 0;
  batteryDetection.isDischarging = false;
  batteryDetection.isCharging = false;
  batteryDetection.lastDetectionTime = 0;
  
  // Initialize samples array
  for (uint8_t i = 0; i < 10; i++) {
    batteryDetection.samples[i] = 0;
  }
}

// Function to get average voltage
uint32_t getAverageVoltage() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 10; i++) {
    sum += batteryDetection.samples[i];
  }
  return sum / 10;
}

// Function to calculate voltage trend
float calculateVoltageTrend() {
  float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
  uint8_t n = 50;
  
  for (uint8_t i = 0; i < n; i++) {
    float x = i;
    float y = batteryDetection.voltageHistory[i];
    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumX2 += x * x;
  }
  
  return (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
}

// Function to detect battery chemistry
uint8_t detectBatteryChemistry() {
  float voltageTrend = calculateVoltageTrend();
  uint32_t avgVoltage = getAverageVoltage();
  
  // LiPo: 3.7V nominal, steep discharge curve
  if (avgVoltage > 3700 && voltageTrend < -0.5) return 1;
  
  // LiFePO4: 3.2V nominal, flat discharge curve
  if (avgVoltage > 3200 && abs(voltageTrend) < 0.1) return 2;
  
  // NiMH: 1.2V nominal, gradual discharge curve
  if (avgVoltage > 1200 && voltageTrend > -0.2) return 3;
  
  return 0; // Unknown
}

// Function to verify capacity measurement
bool verifyCapacity(uint32_t capacity) {
  // Check if we have enough history
  if (batteryDetection.capacityIndex < 5) return false;
  
  // Calculate average and standard deviation
  uint32_t sum = 0;
  uint32_t sumSq = 0;
  for (uint8_t i = 0; i < 5; i++) {
    sum += batteryDetection.capacityHistory[i];
    sumSq += batteryDetection.capacityHistory[i] * batteryDetection.capacityHistory[i];
  }
  
  float avg = sum / 5.0;
  float variance = (sumSq / 5.0) - (avg * avg);
  float stdDev = sqrt(variance);
  
  // Calculate confidence based on standard deviation
  float confidence = 1.0 - (stdDev / avg);
  batteryDetection.confidence = confidence;
  
  // Consider verified if confidence is high enough
  return confidence > 0.9;
}

// Function to detect battery capacity with enhanced algorithms
void detectBatteryCapacity() {
  uint32_t currentTime = millis();
  uint8_t currentLevel = readBatteryLevel();
  uint32_t currentVoltage = analogRead(BATTERY_PIN);
  
  // Update voltage history
  batteryDetection.voltageHistory[batteryDetection.voltageIndex] = currentVoltage;
  batteryDetection.voltageIndex = (batteryDetection.voltageIndex + 1) % 50;
  
  // Detect charging state with enhanced algorithm
  bool wasCharging = batteryDetection.isCharging;
  float voltageTrend = calculateVoltageTrend();
  batteryDetection.isCharging = (voltageTrend > 0.1 || currentVoltage > batteryDetection.lastVoltage + 50);
  
  // If charging started, reset discharge tracking
  if (batteryDetection.isCharging && !wasCharging) {
    batteryDetection.isDischarging = false;
    batteryDetection.dischargeStartTime = 0;
    batteryDetection.dischargeStartLevel = 0;
  }
  
  // Enhanced discharge detection
  if (!batteryDetection.isCharging && currentLevel < batteryDetection.dischargeStartLevel) {
    if (!batteryDetection.isDischarging) {
      batteryDetection.isDischarging = true;
      batteryDetection.dischargeStartTime = currentTime;
      batteryDetection.dischargeStartLevel = currentLevel;
    } else {
      // Calculate capacity based on discharge rate with enhanced algorithm
      uint32_t timeDiff = currentTime - batteryDetection.dischargeStartTime;
      uint8_t levelDiff = batteryDetection.dischargeStartLevel - currentLevel;
      
      if (levelDiff >= 5 && timeDiff >= 300000) { // At least 5% drop over 5 minutes
        // Calculate current power consumption with battery chemistry consideration
        uint8_t currentPower;
        if (isSleeping) {
          currentPower = POWER_SLEEP;
        } else if (currentPowerLevel == 2) {
          currentPower = POWER_ULTRA;
        } else if (currentPowerLevel == 1) {
          currentPower = POWER_LOW;
        } else if (bleGamepad.isConnected()) {
          currentPower = POWER_IDLE;
        } else {
          currentPower = POWER_ACTIVE;
        }
        
        // Adjust power consumption based on battery chemistry
        uint8_t chemistry = detectBatteryChemistry();
        float efficiencyFactor = 1.0;
        switch (chemistry) {
          case 1: // LiPo
            efficiencyFactor = 0.95;
            break;
          case 2: // LiFePO4
            efficiencyFactor = 0.98;
            break;
          case 3: // NiMH
            efficiencyFactor = 0.90;
            break;
        }
        
        // Calculate capacity with efficiency factor
        uint32_t detectedCapacity = (currentPower * timeDiff * 100 * efficiencyFactor) / (levelDiff * 3600000);
        
        // Store in history
        batteryDetection.capacityHistory[batteryDetection.capacityIndex] = detectedCapacity;
        batteryDetection.capacityIndex = (batteryDetection.capacityIndex + 1) % 5;
        
        // Verify capacity if we have enough history
        bool isVerified = verifyCapacity(detectedCapacity);
        
        // Only update if verified or significantly different
        if (isVerified || abs((int32_t)detectedCapacity - (int32_t)batteryDetection.lastCapacity) > 100) {
          batteryDetection.lastCapacity = detectedCapacity;
          batteryDetection.isVerified = isVerified;
          setBatteryCapacity(detectedCapacity);
          
          // Log the detection with enhanced information
          char buffer[200];
          snprintf(buffer, sizeof(buffer), 
            "Detected battery capacity: %dmAh (Voltage: %dmV, Level: %d%%, Chemistry: %d, Confidence: %.2f, Verified: %s)",
            detectedCapacity,
            (currentVoltage * 3300) / 4095,
            currentLevel,
            chemistry,
            batteryDetection.confidence,
            isVerified ? "Yes" : "No"
          );
          Serial.println(buffer);
        }
        
        // Reset discharge tracking
        batteryDetection.isDischarging = false;
        batteryDetection.dischargeStartTime = 0;
        batteryDetection.dischargeStartLevel = 0;
      }
    }
  }
  
  batteryDetection.lastVoltage = currentVoltage;
  batteryDetection.lastDetectionTime = currentTime;
  
  // Periodic capacity verification
  if (currentTime - batteryDetection.lastVerificationTime >= 3600000) { // Every hour
    if (verifyCapacity(batteryDetection.lastCapacity)) {
      batteryDetection.isVerified = true;
      batteryDetection.lastVerificationTime = currentTime;
    }
  }
}

// Function to adjust input sensitivity
void adjustSensitivity(uint8_t deadzone, uint8_t sensitivity, uint8_t responseCurve) {
  sensitivitySettings.deadzone = deadzone;
  sensitivitySettings.sensitivity = sensitivity;
  sensitivitySettings.responseCurve = responseCurve;
  
  // Save settings
  preferences.begin("intvctrl", false);
  preferences.putUChar("deadzone", deadzone);
  preferences.putUChar("sensitivity", sensitivity);
  preferences.putUChar("responseCurve", responseCurve);
  preferences.end();
}

// Function to apply sensitivity settings to input
uint16_t applySensitivity(uint16_t rawValue, uint8_t pin) {
  if (!calibrationData.isCalibrated) return rawValue;
  
  // Apply deadzone
  uint16_t center = calibrationData.centerValues[pin];
  uint16_t range = calibrationData.maxValues[pin] - calibrationData.minValues[pin];
  uint16_t deadzoneSize = (range * sensitivitySettings.deadzone) / 255;
  
  if (abs(rawValue - center) < deadzoneSize) {
    return center;
  }
  
  // Apply sensitivity curve
  uint16_t normalizedValue;
  switch (sensitivitySettings.responseCurve) {
    case 1: // Exponential
      normalizedValue = map(pow(map(rawValue, calibrationData.minValues[pin], calibrationData.maxValues[pin], 0, 255) / 255.0, 2), 0, 1, 0, 4095);
      break;
    case 2: // Logarithmic
      normalizedValue = map(log10(map(rawValue, calibrationData.minValues[pin], calibrationData.maxValues[pin], 1, 255)) / 2.4, 0, 1, 0, 4095);
      break;
    default: // Linear
      normalizedValue = map(rawValue, calibrationData.minValues[pin], calibrationData.maxValues[pin], 0, 4095);
  }
  
  // Apply sensitivity multiplier
  return map(normalizedValue, 0, 4095, 0, (4095 * sensitivitySettings.sensitivity) / 255);
}

// Function to start calibration
void startCalibration() {
  isCalibrating = true;
  calibrationStep = 0;
  calibrationStartTime = millis();
  setLEDPattern(LED_PATTERN_ERROR);  // Use error pattern to indicate calibration mode
  
  // Initialize calibration data
  for (uint8_t i = 0; i < pincount; i++) {
    calibrationData.minValues[i] = 4095;  // Start with maximum value
    calibrationData.maxValues[i] = 0;     // Start with minimum value
    calibrationData.centerValues[i] = 0;
  }
  calibrationData.isCalibrated = false;
}

// Function to update calibration
void updateCalibration() {
  if (!isCalibrating) return;
  
  // Read all pins
  for (uint8_t i = 0; i < pincount; i++) {
    uint16_t value = analogRead(pins[i]);
    
    // Update min/max values
    if (value < calibrationData.minValues[i]) {
      calibrationData.minValues[i] = value;
    }
    if (value > calibrationData.maxValues[i]) {
      calibrationData.maxValues[i] = value;
    }
  }
  
  // Check if calibration time is up
  if (millis() - calibrationStartTime >= 5000) {  // 5 seconds calibration
    isCalibrating = false;
    calibrationData.isCalibrated = true;
    
    // Calculate center values
    for (uint8_t i = 0; i < pincount; i++) {
      calibrationData.centerValues[i] = (calibrationData.minValues[i] + calibrationData.maxValues[i]) / 2;
    }
    
    // Save calibration data
    saveCalibration();
    
    // Return to normal operation
    setLEDPattern(LED_PATTERN_CONNECTED);
  }
}

// Function to save calibration data
void saveCalibration() {
  preferences.begin("intvctrl", false);
  preferences.putBytes("calib", &calibrationData, sizeof(calibrationData));
  preferences.end();
}

// Function to load calibration data
void loadCalibration() {
  preferences.begin("intvctrl", true);
  if (preferences.getBytes("calib", &calibrationData, sizeof(calibrationData)) == sizeof(calibrationData)) {
    // Calibration data loaded successfully
  } else {
    // No calibration data found, use defaults
    for (uint8_t i = 0; i < pincount; i++) {
      calibrationData.minValues[i] = 0;
      calibrationData.maxValues[i] = 4095;
      calibrationData.centerValues[i] = 2048;
    }
    calibrationData.isCalibrated = false;
  }
  preferences.end();
}

// Function to update LED brightness based on power level
void updateLEDBrightness() {
  ledBrightness = LED_BRIGHTNESS_LEVELS[currentPowerLevel];
  if (currentLEDPattern == LED_PATTERN_CONNECTED) {
    analogWrite(STATUS_LED, ledBrightness);
  }
}

// Function to create breathing effect
uint8_t breathingEffect() {
  static uint8_t direction = 0;  // 0 = dimming, 1 = brightening
  static uint8_t brightness = 0;
  
  if (direction == 0) {
    brightness -= 5;
    if (brightness <= 50) {
      direction = 1;
    }
  } else {
    brightness += 5;
    if (brightness >= ledBrightness) {
      direction = 0;
    }
  }
  
  return brightness;
}

// Function to update status LED with enhanced patterns
void updateStatusLED() {
  unsigned long currentTime = millis();
  unsigned long patternTime = currentTime - lastLEDUpdate;
  
  switch (currentLEDPattern) {
    case LED_PATTERN_CONNECTING:
      // Slow blink (1s on, 1s off)
      if (patternTime >= 1000) {
        ledState = !ledState;
        lastLEDUpdate = currentTime;
        digitalWrite(STATUS_LED, ledState ? ledBrightness : 0);
      }
      break;
      
    case LED_PATTERN_CONNECTED:
      // Solid on with current brightness
      analogWrite(STATUS_LED, ledBrightness);
      break;
      
    case LED_PATTERN_SLEEP:
      // Off
      digitalWrite(STATUS_LED, LOW);
      break;
      
    case LED_PATTERN_ERROR:
      // Fast blink (100ms on, 100ms off)
      if (patternTime >= 100) {
        ledState = !ledState;
        lastLEDUpdate = currentTime;
        digitalWrite(STATUS_LED, ledState ? ledBrightness : 0);
      }
      break;
      
    case LED_PATTERN_LOW_BATTERY:
      // Double blink (200ms on, 200ms off, 200ms on, 1400ms off)
      if (patternTime >= 2000) {
        lastLEDUpdate = currentTime;
      } else if (patternTime < 200) {
        digitalWrite(STATUS_LED, ledBrightness);
      } else if (patternTime < 400) {
        digitalWrite(STATUS_LED, 0);
      } else if (patternTime < 600) {
        digitalWrite(STATUS_LED, ledBrightness);
      } else {
        digitalWrite(STATUS_LED, 0);
      }
      break;
      
    case LED_PATTERN_CHARGING:
      // Breathing effect
      analogWrite(STATUS_LED, breathingEffect());
      break;
      
    case LED_PATTERN_MACRO:
      // Quick triple blink every 2s
      if (patternTime >= 2000) {
        lastLEDUpdate = currentTime;
      } else if (patternTime < 100) {
        digitalWrite(STATUS_LED, ledBrightness);
      } else if (patternTime < 200) {
        digitalWrite(STATUS_LED, 0);
      } else if (patternTime < 300) {
        digitalWrite(STATUS_LED, ledBrightness);
      } else if (patternTime < 400) {
        digitalWrite(STATUS_LED, 0);
      } else if (patternTime < 500) {
        digitalWrite(STATUS_LED, ledBrightness);
      } else {
        digitalWrite(STATUS_LED, 0);
      }
      break;
      
    case LED_PATTERN_PROFILE:
      // Number of blinks = profile number + 1
      if (patternTime >= 3000) {
        lastLEDUpdate = currentTime;
      } else {
        uint8_t blinkCount = currentProfile + 1;
        uint16_t blinkTime = patternTime % 600;
        if (blinkTime < 300) {
          digitalWrite(STATUS_LED, (blinkTime / 300) < blinkCount ? ledBrightness : 0);
        } else {
          digitalWrite(STATUS_LED, 0);
        }
      }
      break;
  }
}

// Function to set LED pattern
void setLEDPattern(uint8_t pattern) {
  if (currentLEDPattern != pattern) {
    currentLEDPattern = pattern;
    lastLEDUpdate = millis();
    ledState = false;
  }
}

// Function to check for errors and update LED pattern
void checkErrors() {
  if (!bleGamepad.isConnected()) {
    setLEDPattern(LED_PATTERN_CONNECTING);
  } else if (readBatteryLevel() < 10) {
    setLEDPattern(LED_PATTERN_LOW_BATTERY);
  } else if (isCharging) {
    setLEDPattern(LED_PATTERN_CHARGING);
  } else if (isExecutingMacro) {
    setLEDPattern(LED_PATTERN_MACRO);
  } else {
    setLEDPattern(LED_PATTERN_CONNECTED);
  }
}

// Function to check if device is charging
bool checkCharging() {
  // If using a charging circuit, check the charging status pin
  // For now, we'll assume it's not charging
  return false;
}

// Function to update power save level
void updatePowerLevel() {
  uint8_t batteryLevel = readBatteryLevel();
  bool wasCharging = isCharging;
  isCharging = checkCharging();
  
  // If charging, use normal power level
  if (isCharging) {
    currentPowerLevel = 0;
  } else {
    // Find appropriate power level based on battery
    for (uint8_t i = 0; i < POWER_SAVE_LEVELS; i++) {
      if (batteryLevel <= POWER_SAVE_BATTERY_THRESHOLDS[i]) {
        currentPowerLevel = i;
        break;
      }
    }
  }
  
  // If power level changed or charging state changed
  if (wasCharging != isCharging) {
    // Update LED brightness
    updateLEDBrightness();
    
    // If charging started, wake up if sleeping
    if (isCharging && isSleeping) {
      wakeUp();
    }
  }
}

// Function to enter sleep mode with enhanced features
void enterSleepMode() {
  if (!isSleeping) {
    isSleeping = true;
    
    // Save current state
    preferences.begin("intvctrl", false);
    preferences.putUChar("lastProfile", currentProfile);
    preferences.putBool("analogMode", analogMode);
    preferences.end();
    
    // Turn off LED
    digitalWrite(STATUS_LED, LOW);
    
    // End BLE connection
    bleGamepad.end();
    
    // Configure wake sources
    esp_sleep_enable_ext0_wakeup((gpio_num_t)pins[0], 0);  // Wake on any button press
    if (BATTERY_PIN != 0) {
      esp_sleep_enable_adc_wakeup();  // Wake on battery level change
    }
    
    // Enter deep sleep
    esp_bt_controller_disable();
    esp_deep_sleep_start();
  }
}

// Function to wake up with enhanced features
void wakeUp() {
  if (isSleeping) {
    isSleeping = false;
    
    // Restore state
    preferences.begin("intvctrl", true);
    currentProfile = preferences.getUChar("lastProfile", 0);
    analogMode = preferences.getBool("analogMode", false);
    preferences.end();
    
    // Reinitialize BLE
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    bleGamepad.begin();
    
    // Update LED
    updateLEDBrightness();
    
    // Reset timers
    lastActivityTime = millis();
    lastPowerCheck = millis();
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
        case 4: // Start calibration
          startCalibration();
          break;
      }
    }
  }
}

// Function to execute a macro
void executeMacro(uint8_t macroIndex) {
  if (macroIndex >= MAX_MACROS || macros[macroIndex].stepCount == 0) return;
  
  isExecutingMacro = true;
  macroStartTime = millis();
  currentMacroStep = 0;
  currentMacro = macroIndex;
}

// Function to update macro execution
void updateMacro() {
  if (!isExecutingMacro) return;
  
  Macro& macro = macros[currentMacro];
  if (currentMacroStep >= macro.stepCount) {
    isExecutingMacro = false;
    return;
  }
  
  MacroStep& step = macro.steps[currentMacroStep];
  unsigned long currentTime = millis();
  
  if (currentTime - macroStartTime >= step.duration) {
    // Move to next step
    currentMacroStep++;
    macroStartTime = currentTime;
    
    if (currentMacroStep >= macro.stepCount) {
      isExecutingMacro = false;
    }
  } else {
    // Keep current button pressed
    bleGamepad.press(step.button, true);
  }
}

// Function to save macros for a profile
void saveMacros(uint8_t profile) {
  preferences.begin("intvctrl", false);
  char key[20];
  
  for (uint8_t i = 0; i < MAX_MACROS; i++) {
    sprintf(key, "macro%d_trig", i);
    preferences.putBytes(key, macros[i].triggerButtons, sizeof(macros[i].triggerButtons));
    
    sprintf(key, "macro%d_steps", i);
    preferences.putUChar(key, macros[i].stepCount);
    
    for (uint8_t j = 0; j < macros[i].stepCount; j++) {
      sprintf(key, "macro%d_step%d", i, j);
      preferences.putBytes(key, &macros[i].steps[j], sizeof(MacroStep));
    }
  }
  
  preferences.end();
}

// Function to load macros for a profile
void loadMacros(uint8_t profile) {
  preferences.begin("intvctrl", true);
  char key[20];
  
  for (uint8_t i = 0; i < MAX_MACROS; i++) {
    sprintf(key, "macro%d_trig", i);
    preferences.getBytes(key, macros[i].triggerButtons, sizeof(macros[i].triggerButtons));
    
    sprintf(key, "macro%d_steps", i);
    macros[i].stepCount = preferences.getUChar(key, 0);
    
    for (uint8_t j = 0; j < macros[i].stepCount; j++) {
      sprintf(key, "macro%d_step%d", i, j);
      preferences.getBytes(key, &macros[i].steps[j], sizeof(MacroStep));
    }
  }
  
  preferences.end();
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
  saveMacros(profile);  // Save macros for this profile
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
  loadMacros(profile);  // Load macros for this profile
  preferences.end();
}

// Function to initialize input processing
void initInputProcessing() {
  // Set default values
  inputSettings.filterStrength = 200;
  inputSettings.smoothingFactor = 180;
  inputSettings.debounceTime = 20;
  inputSettings.enableFiltering = true;
  inputSettings.enableSmoothing = true;
  inputSettings.enableDebouncing = true;
  
  // Initialize buffers
  for (uint8_t i = 0; i < 8; i++) {
    inputBuffers[i].index = 0;
    inputBuffers[i].count = 0;
    for (uint8_t j = 0; j < SMOOTHING_BUFFER_SIZE; j++) {
      inputBuffers[i].values[j] = 0;
    }
  }
  
  // Initialize debounce states
  for (uint8_t i = 0; i < 31; i++) {
    debounceStates[i].lastState = false;
    debounceStates[i].lastChangeTime = 0;
    debounceStates[i].stableState = false;
  }
}

// Function to apply signal filtering
uint16_t applyFilter(uint16_t value, uint8_t pin) {
  if (!inputSettings.enableFiltering) return value;
  
  // Simple moving average filter
  static uint16_t lastValues[8] = {0};
  static uint8_t filterIndex[8] = {0};
  
  // Update filter buffer
  lastValues[pin] = value;
  filterIndex[pin] = (filterIndex[pin] + 1) % 4;
  
  // Calculate filtered value
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) {
    sum += lastValues[pin];
  }
  
  // Apply filter strength
  uint16_t filteredValue = sum / 4;
  return map(inputSettings.filterStrength, 0, 255, value, filteredValue);
}

// Function to apply input smoothing
uint16_t applySmoothing(uint16_t value, uint8_t pin) {
  if (!inputSettings.enableSmoothing) return value;
  
  InputBuffer& buffer = inputBuffers[pin];
  
  // Add new value to buffer
  buffer.values[buffer.index] = value;
  buffer.index = (buffer.index + 1) % SMOOTHING_BUFFER_SIZE;
  if (buffer.count < SMOOTHING_BUFFER_SIZE) buffer.count++;
  
  // Calculate smoothed value
  uint32_t sum = 0;
  for (uint8_t i = 0; i < buffer.count; i++) {
    sum += buffer.values[i];
  }
  
  uint16_t smoothedValue = sum / buffer.count;
  
  // Apply smoothing factor
  return map(inputSettings.smoothingFactor, 0, 255, value, smoothedValue);
}

// Function to apply debouncing
bool applyDebouncing(bool state, uint8_t button) {
  if (!inputSettings.enableDebouncing) return state;
  
  DebounceState& debounce = debounceStates[button];
  unsigned long currentTime = millis();
  
  if (state != debounce.lastState) {
    debounce.lastChangeTime = currentTime;
  }
  
  if (currentTime - debounce.lastChangeTime >= inputSettings.debounceTime) {
    debounce.stableState = state;
  }
  
  debounce.lastState = state;
  return debounce.stableState;
}

// Function to process input with all enhancements
uint16_t processInput(uint8_t pin) {
  uint16_t value = analogRead(pins[pin]);
  
  // Apply calibration
  if (calibrationData.isCalibrated) {
    value = map(value, calibrationData.minValues[pin], calibrationData.maxValues[pin], 0, 4095);
  }
  
  // Apply signal filtering
  value = applyFilter(value, pin);
  
  // Apply input smoothing
  value = applySmoothing(value, pin);
  
  // Apply sensitivity settings
  value = applySensitivity(value, pin);
  
  return value;
}

// Function to save input processing settings
void saveInputSettings() {
  preferences.begin("intvctrl", false);
  preferences.putUChar("filterStrength", inputSettings.filterStrength);
  preferences.putUChar("smoothingFactor", inputSettings.smoothingFactor);
  preferences.putUShort("debounceTime", inputSettings.debounceTime);
  preferences.putBool("enableFiltering", inputSettings.enableFiltering);
  preferences.putBool("enableSmoothing", inputSettings.enableSmoothing);
  preferences.putBool("enableDebouncing", inputSettings.enableDebouncing);
  preferences.end();
}

// Function to load input processing settings
void loadInputSettings() {
  preferences.begin("intvctrl", true);
  inputSettings.filterStrength = preferences.getUChar("filterStrength", 200);
  inputSettings.smoothingFactor = preferences.getUChar("smoothingFactor", 180);
  inputSettings.debounceTime = preferences.getUShort("debounceTime", 20);
  inputSettings.enableFiltering = preferences.getBool("enableFiltering", true);
  inputSettings.enableSmoothing = preferences.getBool("enableSmoothing", true);
  inputSettings.enableDebouncing = preferences.getBool("enableDebouncing", true);
  preferences.end();
}

// Function to set battery capacity
void setBatteryCapacity(uint16_t capacity) {
  batteryCapacity = capacity;
  
  // Save capacity to preferences
  preferences.begin("intvctrl", false);
  preferences.putUShort("battCap", capacity);
  preferences.end();
}

// Function to load battery capacity
void loadBatteryCapacity() {
  preferences.begin("intvctrl", true);
  batteryCapacity = preferences.getUShort("battCap", DEFAULT_BATTERY_CAPACITY);
  preferences.end();
}

// Function to calculate remaining battery time
uint32_t calculateBatteryTime() {
  uint8_t currentPower;
  
  // Determine current power consumption
  if (isSleeping) {
    currentPower = POWER_SLEEP;
  } else if (currentPowerLevel == 2) {
    currentPower = POWER_ULTRA;
  } else if (currentPowerLevel == 1) {
    currentPower = POWER_LOW;
  } else if (bleGamepad.isConnected()) {
    currentPower = POWER_IDLE;
  } else {
    currentPower = POWER_ACTIVE;
  }
  
  // Calculate remaining time in hours
  // Convert mAh to hours by dividing by current power consumption
  return (batteryCapacity * readBatteryLevel() / 100) / currentPower;
}

// Function to get power consumption estimate
void getPowerConsumptionEstimate(char* buffer, size_t size) {
  uint32_t activeTime = (batteryCapacity * 100) / POWER_ACTIVE;
  uint32_t idleTime = (batteryCapacity * 100) / POWER_IDLE;
  uint32_t lowTime = (batteryCapacity * 100) / POWER_LOW;
  uint32_t ultraTime = (batteryCapacity * 100) / POWER_ULTRA;
  uint32_t sleepTime = (batteryCapacity * 100) / POWER_SLEEP;
  
  snprintf(buffer, size, 
    "Battery: %dmAh\n"
    "Active: %d hours\n"
    "Idle: %d hours\n"
    "Low Power: %d hours\n"
    "Ultra-Low: %d hours\n"
    "Sleep: %d hours",
    batteryCapacity,
    activeTime,
    idleTime,
    lowTime,
    ultraTime,
    sleepTime
  );
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

  // Load calibration data
  loadCalibration();

  // Load sensitivity settings
  sensitivitySettings.deadzone = preferences.getUChar("deadzone", 20);
  sensitivitySettings.sensitivity = preferences.getUChar("sensitivity", 200);
  sensitivitySettings.responseCurve = preferences.getUChar("responseCurve", 0);
  preferences.end();

  // Initialize input processing
  initInputProcessing();
  loadInputSettings();

  // Initialize the BLE gamepad
  bleGamepad.begin();
  
  // Set the number of buttons
  bleGamepad.setButtonCount(TOTAL_BUTTONS);
  
  // Enable battery reporting
  bleGamepad.setBatteryLevel(100);
  
  Serial.println("BLE Gamepad started!");

  // Load battery capacity
  loadBatteryCapacity();

  // Initialize battery detection
  initBatteryDetection();
}


void loop()
{
  static unsigned long lastBatteryCheck = 0;
  static unsigned long lastLEDUpdate = 0;
  static unsigned long lastErrorCheck = 0;
  
  // Update status LED every 10ms for smooth patterns
  if (millis() - lastLEDUpdate >= 10) {
    updateStatusLED();
    lastLEDUpdate = millis();
  }

  // Check for errors and update LED pattern every 100ms
  if (millis() - lastErrorCheck >= 100) {
    checkErrors();
    lastErrorCheck = millis();
  }

  // Check power state periodically
  if (millis() - lastPowerCheck >= POWER_CHECK_INTERVAL) {
    updatePowerLevel();
    updateLEDBrightness();  // Update LED brightness when power level changes
    lastPowerCheck = millis();
  }

  // Check battery level periodically
  if (millis() - lastBatteryCheck >= (BATTERY_CHECK_INTERVAL * 1000)) {
    uint8_t batteryLevel = readBatteryLevel();
    bleGamepad.setBatteryLevel(batteryLevel);
    
    // Run battery capacity detection
    detectBatteryCapacity();
    
    // Calculate and store remaining time
    uint32_t remainingTime = calculateBatteryTime();
    
    // Enter sleep mode if battery is critically low
    if (batteryLevel < 10) {
      enterSleepMode();
    }
    
    lastBatteryCheck = millis();
  }

  // Update calibration if active
  if (isCalibrating) {
    updateCalibration();
  }

  if (bleGamepad.isConnected()) {
    // Reset idle timer on any activity
    lastActivityTime = millis();
    
    // Read the controller state with processing
    uint8_t keyvalue = 0;
    
    // Read all pins with processing
    for (uint8_t i = 0; i < pincount; i++) {
      uint16_t value = processInput(i);
      if (value < 2048) {  // Use processed threshold
        keyvalue |= (1 << (7-i));
      }
    }

    // Map the keyvalue to button states with debouncing
    bool anyButtonPressed = false;
    for (uint8_t i = 0; i < TOTAL_BUTTONS; i++) {
      bool newState = (buttonMap[i] == keyvalue);
      newState = applyDebouncing(newState, i);
      
      if (newState != buttonStates[i]) {
        buttonStates[i] = newState;
        bleGamepad.press(i + 1, newState);
        anyButtonPressed = true;
      }
    }

    // Check for button combinations
    checkButtonCombinations();

    // Check for macro triggers if not executing a macro
    if (!isExecutingMacro) {
      for (uint8_t i = 0; i < MAX_MACROS; i++) {
        bool macroTriggered = true;
        for (uint8_t j = 0; j < 3 && macros[i].triggerButtons[j] != 0; j++) {
          if (!buttonStates[macros[i].triggerButtons[j] - 1]) {
            macroTriggered = false;
            break;
          }
        }
        if (macroTriggered) {
          executeMacro(i);
          break;
        }
      }
    }

    // Update macro execution
    updateMacro();

    // Handle analog stick if in analog mode
    if (analogMode) {
      for (uint8_t i = 0; i < DIRECTION_BUTTONS; i++) {
        if (buttonStates[i]) {
          bleGamepad.setLeftThumb(analogMappings[i].x, analogMappings[i].y);
          break;
        }
      }
    }

    // If no buttons are pressed and no macro is executing, release all
    if (!anyButtonPressed && !isExecutingMacro) {
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
    // Check for idle timeout based on current power level
    if (millis() - lastActivityTime >= POWER_SAVE_TIMEOUTS[currentPowerLevel]) {
      enterSleepMode();
    }
  }

  // Adjust polling interval based on power level
  delay(POLLING_INTERVALS[currentPowerLevel]);
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

// Function to read battery level
uint8_t readBatteryLevel() {
  int rawValue = analogRead(BATTERY_PIN);
  float voltage = (rawValue * 3.3) / 4095.0;  // Convert to voltage
  uint8_t percentage = map(voltage * 100, BATTERY_MIN_VOLTAGE * 100, BATTERY_MAX_VOLTAGE * 100, 0, 100);
  return constrain(percentage, 0, 100);
}
