#include <BleGamepad.h>
#include <Arduino.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp32-hal-bt.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// Create a BLE gamepad instance to receive controller input
BleGamepad bleGamepad("Intellivision Dongle", "Intellivision", 100);

// Controller port pins (matching original Intellivision controller pinout)
const uint8_t CONTROLLER_PINS[] = {
    5,  // Pin 1 (GND)
    4,  // Pin 2
    3,  // Pin 3
    2,  // Pin 4
    9,  // Pin 6
    8,  // Pin 7
    7,  // Pin 8
    6   // Pin 9
};

// Status LED pin
const uint8_t STATUS_LED = 2;  // Built-in LED on most ESP32 boards

// Power management
const uint8_t POWER_MODE_NORMAL = 0;
const uint8_t POWER_MODE_LOW = 1;
const uint8_t POWER_MODE_SLEEP = 2;
uint8_t currentPowerMode = POWER_MODE_NORMAL;
unsigned long lastActivityTime = 0;
const unsigned long SLEEP_TIMEOUT = 300000;  // 5 minutes
const unsigned long LOW_POWER_TIMEOUT = 60000;  // 1 minute

// Connection management
const uint8_t MAX_RECONNECT_ATTEMPTS = 5;
uint8_t reconnectAttempts = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 5000;  // 5 seconds
String lastConnectedDevice = "";

// Input processing
const uint8_t DEBOUNCE_TIME = 20;  // milliseconds
const uint8_t FILTER_STRENGTH = 3;  // number of samples to average
struct InputState {
    bool currentState;
    bool lastState;
    unsigned long lastChangeTime;
    uint8_t filterBuffer[FILTER_STRENGTH];
    uint8_t filterIndex;
} inputStates[31];

// Error handling
const uint8_t MAX_ERROR_COUNT = 5;
uint8_t errorCount = 0;
unsigned long lastErrorTime = 0;
const unsigned long ERROR_RESET_INTERVAL = 60000;  // 1 minute

// Button mapping for the original controller (active-low logic)
const uint8_t BUTTON_MAP[] = {
    0x40, 0x41, 0x61, 0x60, 0x20, 0x21, 0x31, 0x30,  // Direction pad
    0x10, 0x11, 0x91, 0x90, 0x80, 0x81, 0xc1, 0xc0,  // Direction pad continued
    
    0x18, 0x14, 0x12, 0x28, 0x24,  // Keypad 1-5
    0x22, 0x48, 0x44, 0x42,        // Keypad 6-9
    0x88, 0x84, 0x82,              // Keypad Clear, 0, Enter
    
    0x0a, 0x06, 0x0c, 0x00         // Side buttons
};

// Button states
bool buttonStates[31] = {false};  // 16 direction + 12 keypad + 3 side buttons

// Connection status
bool isConnected = false;
unsigned long lastConnectionCheck = 0;
const unsigned long CONNECTION_CHECK_INTERVAL = 1000;  // Check every second

// LED patterns
const uint8_t LED_PATTERN_DISCONNECTED = 0;  // Slow blink
const uint8_t LED_PATTERN_CONNECTED = 1;     // Solid on
const uint8_t LED_PATTERN_ERROR = 2;         // Fast blink
const uint8_t LED_PATTERN_PAIRING = 3;       // Double blink
const uint8_t LED_PATTERN_LOW_POWER = 4;     // Triple blink
const uint8_t LED_PATTERN_SLEEP = 5;         // Very slow blink

// Current LED pattern
uint8_t currentLEDPattern = LED_PATTERN_DISCONNECTED;
unsigned long lastLEDUpdate = 0;
bool ledState = false;

// Function to initialize input processing
void initInputProcessing() {
    for (uint8_t i = 0; i < 31; i++) {
        inputStates[i].currentState = false;
        inputStates[i].lastState = false;
        inputStates[i].lastChangeTime = 0;
        inputStates[i].filterIndex = 0;
        for (uint8_t j = 0; j < FILTER_STRENGTH; j++) {
            inputStates[i].filterBuffer[j] = 0;
        }
    }
}

// Function to process input with debouncing and filtering
bool processInput(uint8_t buttonIndex, bool rawState) {
    InputState& state = inputStates[buttonIndex];
    unsigned long currentTime = millis();
    
    // Update filter buffer
    state.filterBuffer[state.filterIndex] = rawState ? 1 : 0;
    state.filterIndex = (state.filterIndex + 1) % FILTER_STRENGTH;
    
    // Calculate filtered state
    uint8_t sum = 0;
    for (uint8_t i = 0; i < FILTER_STRENGTH; i++) {
        sum += state.filterBuffer[i];
    }
    bool filteredState = (sum > FILTER_STRENGTH / 2);
    
    // Apply debouncing
    if (filteredState != state.lastState) {
        if (currentTime - state.lastChangeTime >= DEBOUNCE_TIME) {
            state.currentState = filteredState;
            state.lastState = filteredState;
            state.lastChangeTime = currentTime;
        }
    }
    
    return state.currentState;
}

// Function to update status LED
void updateStatusLED() {
    unsigned long currentTime = millis();
    unsigned long patternTime = currentTime - lastLEDUpdate;
    
    switch (currentLEDPattern) {
        case LED_PATTERN_DISCONNECTED:
            // Slow blink (1s on, 1s off)
            if (patternTime >= 1000) {
                ledState = !ledState;
                lastLEDUpdate = currentTime;
                digitalWrite(STATUS_LED, ledState);
            }
            break;
            
        case LED_PATTERN_CONNECTED:
            // Solid on
            digitalWrite(STATUS_LED, HIGH);
            break;
            
        case LED_PATTERN_ERROR:
            // Fast blink (100ms on, 100ms off)
            if (patternTime >= 100) {
                ledState = !ledState;
                lastLEDUpdate = currentTime;
                digitalWrite(STATUS_LED, ledState);
            }
            break;
            
        case LED_PATTERN_PAIRING:
            // Double blink (200ms on, 200ms off, 200ms on, 1400ms off)
            if (patternTime >= 2000) {
                lastLEDUpdate = currentTime;
            } else if (patternTime < 200) {
                digitalWrite(STATUS_LED, HIGH);
            } else if (patternTime < 400) {
                digitalWrite(STATUS_LED, LOW);
            } else if (patternTime < 600) {
                digitalWrite(STATUS_LED, HIGH);
            } else {
                digitalWrite(STATUS_LED, LOW);
            }
            break;
            
        case LED_PATTERN_LOW_POWER:
            // Triple blink (100ms on, 100ms off, 100ms on, 100ms off, 100ms on, 1500ms off)
            if (patternTime >= 2000) {
                lastLEDUpdate = currentTime;
            } else if (patternTime < 100) {
                digitalWrite(STATUS_LED, HIGH);
            } else if (patternTime < 200) {
                digitalWrite(STATUS_LED, LOW);
            } else if (patternTime < 300) {
                digitalWrite(STATUS_LED, HIGH);
            } else if (patternTime < 400) {
                digitalWrite(STATUS_LED, LOW);
            } else if (patternTime < 500) {
                digitalWrite(STATUS_LED, HIGH);
            } else {
                digitalWrite(STATUS_LED, LOW);
            }
            break;
            
        case LED_PATTERN_SLEEP:
            // Very slow blink (2s on, 2s off)
            if (patternTime >= 2000) {
                ledState = !ledState;
                lastLEDUpdate = currentTime;
                digitalWrite(STATUS_LED, ledState);
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

// Function to update power mode
void updatePowerMode() {
    unsigned long currentTime = millis();
    unsigned long idleTime = currentTime - lastActivityTime;
    
    if (isConnected) {
        if (idleTime >= SLEEP_TIMEOUT) {
            currentPowerMode = POWER_MODE_SLEEP;
            setLEDPattern(LED_PATTERN_SLEEP);
        } else if (idleTime >= LOW_POWER_TIMEOUT) {
            currentPowerMode = POWER_MODE_LOW;
            setLEDPattern(LED_PATTERN_LOW_POWER);
        } else {
            currentPowerMode = POWER_MODE_NORMAL;
            setLEDPattern(LED_PATTERN_CONNECTED);
        }
    }
}

// Function to handle errors
void handleError(const char* errorMessage) {
    errorCount++;
    lastErrorTime = millis();
    
    Serial.print("Error: ");
    Serial.println(errorMessage);
    
    if (errorCount >= MAX_ERROR_COUNT) {
        // Reset the device after too many errors
        ESP.restart();
    }
    
    setLEDPattern(LED_PATTERN_ERROR);
}

// Function to update controller output
void updateControllerOutput() {
    if (!isConnected) return;
    
    // Read button states from BLE gamepad
    for (uint8_t i = 0; i < 31; i++) {
        bool rawState = bleGamepad.isPressed(i + 1);
        bool newState = processInput(i, rawState);
        
        if (newState != buttonStates[i]) {
            buttonStates[i] = newState;
            
            // Map button state to controller output (active-low logic)
            uint8_t keyvalue = 0;
            if (newState) {
                keyvalue = BUTTON_MAP[i];
            }
            
            // Update controller pins based on keyvalue
            // Note: We invert the logic here since the original controller is active-low
            for (uint8_t pin = 0; pin < 8; pin++) {
                if (pin == 0) {  // Pin 5 (GND)
                    digitalWrite(CONTROLLER_PINS[pin], LOW);  // Always keep GND LOW
                } else {
                    // For other pins, HIGH when inactive, LOW when active
                    digitalWrite(CONTROLLER_PINS[pin], !(keyvalue & (1 << (7-pin))));
                }
            }
            
            // Update activity time
            lastActivityTime = millis();
        }
    }
}

// Function to check connection status
void checkConnection() {
    bool wasConnected = isConnected;
    isConnected = bleGamepad.isConnected();
    
    if (isConnected != wasConnected) {
        if (isConnected) {
            setLEDPattern(LED_PATTERN_CONNECTED);
            Serial.println("Controller connected!");
            reconnectAttempts = 0;
            lastActivityTime = millis();
        } else {
            setLEDPattern(LED_PATTERN_DISCONNECTED);
            Serial.println("Controller disconnected!");
            
            // Reset all controller pins when disconnected
            // Keep GND LOW, all others HIGH
            for (uint8_t pin = 0; pin < 8; pin++) {
                if (pin == 0) {  // Pin 5 (GND)
                    digitalWrite(CONTROLLER_PINS[pin], LOW);
                } else {
                    digitalWrite(CONTROLLER_PINS[pin], HIGH);
                }
            }
            
            // Start reconnection attempts
            reconnectAttempts = 0;
            lastReconnectAttempt = millis();
        }
    }
    
    // Handle reconnection attempts
    if (!isConnected && reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        if (millis() - lastReconnectAttempt >= RECONNECT_INTERVAL) {
            Serial.println("Attempting to reconnect...");
            bleGamepad.begin();
            reconnectAttempts++;
            lastReconnectAttempt = millis();
        }
    }
}

void setup() {
    // Initialize watchdog timer
    esp_task_wdt_init(5, true);  // 5 second timeout
    esp_task_wdt_add(NULL);
    
    // Initialize serial communication
    Serial.begin(115200);
    Serial.println("Starting Intellivision Console Dongle...");
    
    // Initialize status LED
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    
    // Initialize controller pins
    for (uint8_t pin = 0; pin < 8; pin++) {
        pinMode(CONTROLLER_PINS[pin], OUTPUT);
        if (pin == 0) {  // Pin 5 (GND)
            digitalWrite(CONTROLLER_PINS[pin], LOW);  // Set GND LOW
        } else {
            digitalWrite(CONTROLLER_PINS[pin], HIGH);  // Set other pins HIGH (inactive)
        }
    }
    
    // Initialize input processing
    initInputProcessing();
    
    // Initialize BLE
    bleGamepad.begin();
    bleGamepad.setButtonCount(31);  // 16 direction + 12 keypad + 3 side buttons
    
    // Set initial LED pattern
    setLEDPattern(LED_PATTERN_DISCONNECTED);
    
    // Initialize timers
    lastActivityTime = millis();
    lastConnectionCheck = millis();
    lastErrorTime = millis();
    
    Serial.println("Dongle initialized and ready for pairing!");
}

void loop() {
    // Feed watchdog timer
    esp_task_wdt_reset();
    
    // Update status LED
    updateStatusLED();
    
    // Check connection status periodically
    if (millis() - lastConnectionCheck >= CONNECTION_CHECK_INTERVAL) {
        checkConnection();
        lastConnectionCheck = millis();
    }
    
    // Update power mode
    updatePowerMode();
    
    // Update controller output if connected
    if (isConnected) {
        updateControllerOutput();
    }
    
    // Reset error count if no errors for a while
    if (errorCount > 0 && millis() - lastErrorTime >= ERROR_RESET_INTERVAL) {
        errorCount = 0;
    }
    
    // Adjust delay based on power mode
    switch (currentPowerMode) {
        case POWER_MODE_NORMAL:
            delay(10);
            break;
        case POWER_MODE_LOW:
            delay(50);
            break;
        case POWER_MODE_SLEEP:
            delay(100);
            break;
    }
} 