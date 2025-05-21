# Intellivision Bluetooth Gamepad Controller

This project is a firmware for an ESP32 board that makes it possible to use any original Intellivision controller as a Bluetooth gamepad with any device that supports Bluetooth HID gamepads (PC, Android, iOS, etc.). It is primarily intended for use with Intellivision emulators.

*Note: I'm currently working on a console dongle project that will allow you to use this Bluetooth controller on the actual console and not just an emulator. It will support all INTV consoles, but the INTV2 works best as it has controller ports. For INTV1 and INTV3, you will need to either cut out controllers or use 8BitWidgets IntelliPort.*

## Features

- Bluetooth HID gamepad support
- Battery level monitoring and reporting
- Power saving with automatic sleep mode
- Status LED indicators
- Support for all original controller functions
- Low battery protection
- Analog stick support
- Multiple controller profiles
- Button combinations for special functions

## The hardware

The hardware is very simple. You just need:
1. An ESP32 development board (like ESP32 DevKit, NodeMCU-32S, etc.)
2. One original Intellivision controller
3. Some jumper wires for connections
4. (Optional) A LiPo battery and charging circuit for portable use

Connect the original controller connector to the ESP32 as follows:
- Brown wire (Ground) -> ESP32 GND
- Pin 1 -> ESP32 GPIO 5
- Pin 2 -> ESP32 GPIO 4
- Pin 3 -> ESP32 GPIO 3
- Pin 4 -> ESP32 GPIO 2
- Pin 6 -> ESP32 GPIO 9
- Pin 7 -> ESP32 GPIO 8
- Pin 8 -> ESP32 GPIO 7
- Pin 9 -> ESP32 GPIO 6

For battery monitoring (optional):
- Battery voltage -> ESP32 GPIO 34 (ADC)

## Required Libraries

Before uploading the code, you need to install these libraries in the Arduino IDE:
1. "ESP32 BLE Gamepad" by lemmingDev
2. "Arduino" (should be installed by default)
3. "Preferences" (included with ESP32 board support)

## Flashing the firmware

1. Install the ESP32 board support in Arduino IDE:
   - Go to File -> Preferences
   - Add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to Additional Board Manager URLs
   - Go to Tools -> Board -> Boards Manager
   - Search for "esp32" and install "ESP32 by Espressif Systems"

2. Select your ESP32 board:
   - Go to Tools -> Board -> ESP32 Arduino
   - Select your specific ESP32 board model

3. Upload the code:
   - Connect your ESP32 to your computer
   - Select the correct port in Tools -> Port
   - Click the Upload button

## Using the controller

1. After uploading, the ESP32 will start advertising as a Bluetooth gamepad named "Intellivision Controller"
2. On your device (PC, phone, etc.), go to Bluetooth settings and pair with the controller
3. The controller will appear as a standard gamepad with:
   - 16 direction buttons (mapped to the disc)
   - 12 keypad buttons (0-9, Clear, Enter)
   - 3 side buttons
   - Analog stick support (when enabled)

## Advanced Features

### Input Calibration

The controller supports automatic calibration of all inputs to ensure accurate button detection:

1. **Calibration Process**:
   - Press Keypad buttons 1+2+3 simultaneously to start calibration
   - LED will blink rapidly during calibration
   - Move all buttons and the disc through their full range
   - Calibration completes after 5 seconds
   - LED returns to normal operation when done

2. **Calibration Features**:
   - Automatically detects min/max values for each input
   - Calculates center/rest positions
   - Saves calibration data with profiles
   - Persists across power cycles
   - Improves input accuracy and reliability

3. **When to Calibrate**:
   - First time setup
   - After changing hardware
   - If inputs feel unresponsive
   - When switching between different controllers
   - If buttons trigger incorrectly

### Enhanced Usability Features

1. **Quick Access Functions**:
   - Keypad 1+2+3: Start calibration
   - Side Buttons 1+2: Switch profiles
   - Side Buttons 2+3: Toggle analog mode
   - Side Buttons 1+3: Save current profile

2. **Profile Management**:
   - Up to 4 profiles with different settings
   - Each profile can have:
     - Custom button mappings
     - Analog/digital mode preference
     - Macro configurations
     - Calibration data
   - Profiles persist across power cycles
   - Quick profile switching

3. **Input Customization**:
   - Adjustable button sensitivity
   - Customizable analog stick response
   - Configurable dead zones
   - Button remapping
   - Macro support

4. **Status Feedback**:
   - LED indicators for all states
   - Battery level monitoring
   - Connection status
   - Profile selection
   - Error conditions
   - Operation modes

5. **Power Management**:
   - Multiple power save levels
   - Automatic sleep mode
   - Battery protection
   - Charging detection
   - State preservation

6. **Troubleshooting**:
   - Visual error indicators
   - Calibration support
   - Profile backup/restore
   - Connection diagnostics
   - Battery monitoring

### Input Customization

1. **Sensitivity Settings**:
   - Adjustable deadzone (0-255)
   - Customizable sensitivity (0-255)
   - Multiple response curves:
     - Linear (default)
     - Exponential (more precise near center)
     - Logarithmic (more precise near edges)
   - Auto-center option for disc
   - Settings persist with profiles

2. **Response Curves**:
   - Linear: Standard 1:1 response
   - Exponential: More precise control near center
   - Logarithmic: More precise control near edges
   - Each curve optimized for different game types

3. **Deadzone Adjustment**:
   - Prevent accidental inputs
   - Compensate for controller wear
   - Adjust for different game requirements
   - Fine-tune for personal preference

4. **Sensitivity Control**:
   - Adjust overall input sensitivity
   - Fine-tune for different games
   - Compensate for hardware variations
   - Match personal play style

### Advanced Features

1. **Input Processing**:
   - Advanced signal filtering
   - Noise reduction
   - Debouncing
   - Input smoothing

2. **Performance Optimization**:
   - Adaptive polling rates
   - Power-aware processing
   - Efficient memory usage
   - Optimized response times

3. **Diagnostic Tools**:
   - Input value monitoring
   - Response time measurement
   - Error detection
   - Performance metrics

4. **User Experience**:
   - Intuitive button combinations
   - Clear status feedback
   - Easy configuration
   - Quick profile switching

### Using the Controller

1. **First Time Setup**:
   - Connect the controller
   - Run calibration (Keypad 1+2+3)
   - Configure your preferred profile
   - Save settings (Side Buttons 1+3)

2. **Daily Use**:
   - Power on the controller
   - Wait for connection (solid LED)
   - Select profile if needed
   - Use as normal
   - Controller will auto-sleep when idle

3. **Maintenance**:
   - Recalibrate if inputs feel off
   - Check battery level regularly
   - Update profiles as needed
   - Clean controller periodically

4. **Troubleshooting**:
   - Check LED patterns for status
   - Recalibrate if needed
   - Try different profiles
   - Check battery level
   - Reset if problems persist

## Power Management

The controller includes several power-saving features and supports different battery capacities:

1. **Battery Configuration**:
   - Default capacity: 1000mAh
   - Configurable for different batteries
   - Supports up to 5000mAh
   - Accurate power consumption tracking
   - Automatic capacity detection

2. **Power Consumption Levels**:
   - Active use: ~100mA
   - Connected but idle: ~50mA
   - Low power mode: ~30mA
   - Ultra-low power mode: ~20mA
   - Sleep mode: < 1mA

3. **Battery Life Estimates** (for 2500mAh battery):
   - Active use: ~25 hours
   - Connected idle: ~50 hours
   - Low power mode: ~83 hours
   - Ultra-low power mode: ~125 hours
   - Sleep mode: ~2500 hours

4. **Power Management Features**:
   - Automatic power level adjustment
   - Battery capacity configuration
   - Remaining time calculation
   - Power consumption monitoring
   - Low battery protection

5. **Configuration Options**:
   - Set battery capacity
   - Adjust power thresholds
   - Configure sleep timeouts
   - Enable/disable features
   - Save power settings

6. **Status Information**:
   - Current power mode
   - Battery percentage
   - Remaining time
   - Power consumption
   - Charging status

7. **Power Saving Tips**:
   - Use appropriate power mode
   - Enable sleep when idle
   - Monitor battery level
   - Configure timeouts
   - Use power-efficient settings

## Button Mapping

The controller maps to standard gamepad buttons as follows:

- Direction disc: Buttons 1-16 (clockwise from North)
- Keypad: Buttons 17-28 (1-9, Clear, 0, Enter)
- Side buttons: Buttons 29-31

In analog mode, the disc also controls the left analog stick with 16 positions.

## Troubleshooting

If the controller doesn't connect:
1. Make sure Bluetooth is enabled on your device
2. Try resetting the ESP32 (press the reset button)
3. Check that all wires are properly connected
4. Verify the ESP32 is powered properly
5. Check the status LED for connection state

For connection issues, you can monitor the Serial output at 115200 baud to see debug messages.

## Power Consumption

Typical power consumption:
- Active use: ~100mA
- Connected but idle: ~50mA
- Low power mode: ~30mA
- Ultra-low power mode: ~20mA
- Sleep mode: < 1mA

With a 1000mAh battery, you can expect:
- 10 hours of active use
- 20 hours of connected idle time
- 33 hours in low power mode
- 50 hours in ultra-low power mode
- Weeks in sleep mode

### Advanced Input Processing

1. **Signal Filtering**:
   - Moving average filter to reduce noise
   - Adjustable filter strength (0-255)
   - Per-pin filtering
   - Automatic noise reduction
   - Configurable filter settings

2. **Input Smoothing**:
   - Circular buffer for smooth transitions
   - Adjustable smoothing factor (0-255)
   - Per-pin smoothing
   - Prevents jitter and spikes
   - Maintains responsiveness

3. **Button Debouncing**:
   - Hardware-level debouncing
   - Configurable debounce time
   - Per-button state tracking
   - Prevents false triggers
   - Reliable button detection

4. **Processing Pipeline**:
   - Raw input reading
   - Signal filtering
   - Input smoothing
   - Calibration
   - Sensitivity adjustment
   - Debouncing
   - Final output

5. **Configuration Options**:
   - Enable/disable each processing step
   - Adjust processing parameters
   - Save settings with profiles
   - Load default settings
   - Quick reset to defaults

6. **Performance Features**:
   - Efficient memory usage
   - Optimized processing
   - Minimal latency
   - Power-aware operation
   - Automatic optimization

7. **Troubleshooting**:
   - Processing status indicators
   - Error detection
   - Performance monitoring
   - Debug information
   - Reset options

### Battery Management

1. **Automatic Capacity Detection**:
   - Automatically detects battery capacity
   - Monitors discharge rate and voltage
   - Calculates capacity based on usage patterns
   - Updates capacity estimates over time
   - No manual configuration needed

2. **Detection Process**:
   - Monitors voltage changes
   - Tracks discharge rates
   - Measures power consumption
   - Calculates capacity from usage
   - Verifies accuracy over time

3. **Detection Features**:
   - Voltage sampling and averaging
   - Discharge rate monitoring
   - Charging state detection
   - Power consumption tracking
   - Capacity verification

4. **Accuracy Improvements**:
   - Multiple measurement points
   - Voltage averaging
   - Discharge rate analysis
   - Power consumption tracking
   - Continuous refinement

5. **Detection Conditions**:
   - Minimum 5% battery drop
   - At least 5 minutes of discharge
   - Stable power consumption
   - No charging during measurement
   - Significant capacity change

6. **Status Information**:
   - Detected capacity
   - Current voltage
   - Battery level
   - Discharge rate
   - Power consumption

7. **Usage Notes**:
   - First detection may take time
   - Accuracy improves with use
   - Works with any battery size
   - Updates automatically
   - No user intervention needed

### Advanced Battery Detection

1. **Enhanced Detection Algorithms**:
   - Linear regression for voltage trends
   - Battery chemistry detection
   - Efficiency factor calculation
   - Extended voltage history
   - Discharge rate analysis

2. **Battery Chemistry Detection**:
   - LiPo (3.7V nominal)
   - LiFePO4 (3.2V nominal)
   - NiMH (1.2V nominal)
   - Chemistry-specific efficiency
   - Automatic adaptation

3. **Capacity Verification**:
   - Statistical analysis
   - Confidence calculation
   - Standard deviation tracking
   - Multiple measurement points
   - Periodic verification

4. **Detection Accuracy**:
   - Voltage trend analysis
   - Chemistry-specific factors
   - Efficiency compensation
   - Confidence scoring
   - Verification thresholds

5. **Verification Process**:
   - Minimum 5 measurements
   - Standard deviation analysis
   - Confidence threshold (90%)
   - Hourly verification
   - Continuous refinement

6. **Chemistry-Specific Features**:
   - LiPo: 95% efficiency
   - LiFePO4: 98% efficiency
   - NiMH: 90% efficiency
   - Custom discharge curves
   - Voltage characteristics

7. **Advanced Monitoring**:
   - 50-point voltage history
   - 10-point discharge history
   - 5-point capacity history
   - Trend analysis
   - Pattern recognition

8. **Status Information**:
   - Detected chemistry
   - Confidence level
   - Verification status
   - Efficiency factor
   - Historical data
