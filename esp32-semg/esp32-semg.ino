/*
 * ESP32 sEMG Data Logger
 *
 * This Arduino sketch collects surface electromyography (sEMG) signals using a MyoWare
 * Muscle Sensor 2.0 connected to an ESP32 DevKit V1. Originally designed to POST data
 * to a FastAPI backend, this version has been modified to log data directly to an SD card
 * for improved reliability and offline operation.
 *
 * Hardware Components:
 * - ESP32 DevKit V1 (microcontroller)
 * - MyoWare Muscle Sensor 2.0 (sEMG sensor on GPIO 34)
 * - 16x2 I2C LCD Display (0x27 address)
 * - SD Card Module (SPI interface)
 * - Start/Stop buttons (GPIO 25 and 26)
 *
 * Key Features:
 * - Real-time sEMG signal acquisition (variable sampling rate, no fixed timing)
 * - Local SD card logging with timestamped CSV files
 * - WiFi connectivity with automatic reconnection
 * - NTP time synchronization for accurate timestamps
 * - LCD status display
 * - Start/Stop control via hardware buttons
 * 
 * Note on Sampling Rate:
 * The current implementation does NOT enforce a fixed sampling rate. The loop()
 * runs as fast as possible, resulting in variable sampling rates depending on:
 * - WiFi connection checks (every 10 seconds)
 * - NTP time sync (every hour)
 * - SD card write/flush operations (every 1500 samples)
 * - LCD updates and Serial output
 * Expected actual rate: ~100-500 Hz (variable and uncontrolled)
 * 
 * ⚠️  CRITICAL LIMITATION - UNRELIABLE EMG DATA:
 * For trustworthy sEMG analysis, the Nyquist Theorem requires:
 * - Surface EMG signals contain frequencies up to ~400-500 Hz
 * - Sampling rate must be ≥2x the highest frequency (Nyquist rate)
 * - Minimum: 800-1000 Hz | Recommended: 1000-2000 Hz
 * - Anti-aliasing filter needed BEFORE digitization (cuts frequencies > Nyquist limit)
 * 
 * Current Issues:
 * 1. NO fixed sampling rate → variable intervals cause aliasing
 * 2. NO anti-aliasing hardware filter → high frequencies fold back into signal
 * 3. Sample rate too low and inconsistent for reliable frequency analysis
 * 
 * REQUIRED IMPROVEMENTS for clinical/research use:
 * - Implement hardware timer interrupt for precise 1-2 kHz sampling
 * - Add anti-aliasing filter circuit (low-pass filter with cutoff at Nyquist frequency)
 * - Use ISR (Interrupt Service Routine) for time-critical ADC reads
 * - Current implementation suitable only for basic amplitude monitoring, NOT
 *   for spectral analysis, frequency decomposition, or fatigue detection
 *
 * Timestamp Convention:
 * All timestamps are stored as milliseconds since custom epoch: 2025-06-22T00:00:00 UTC
 * (Unix timestamp: 1750550400)
 *
 * Work in Progress Notes:
 * - Transitioned from HTTP POST batching to SD card logging
 * - API posting code preserved but currently disabled (see postBatch function)
 * - SD card flush interval set to 1500 samples for data safety
 * - NO FIXED SAMPLING RATE: Loop runs as fast as possible, resulting in variable
 *   and unpredictable sampling intervals. For precise timing, add delay control
 *   or use hardware timers/interrupts.
 * 
 * TODO - Critical Improvements Needed:
 * [ ] Implement hardware timer interrupt for 1000-2000 Hz fixed sampling rate
 * [ ] Add ISR (Interrupt Service Routine) for time-critical ADC acquisition
 * [ ] Design and add anti-aliasing filter circuit (analog low-pass filter)
 * [ ] Set filter cutoff to Nyquist frequency (500-1000 Hz depending on sample rate)
 * [ ] Validate with known test signals to verify no aliasing occurs
 * [ ] Add ring buffer for ISR-to-main-loop data transfer
 * [ ] Document hardware filter specifications in schematic
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>              // For NTP time synchronization
#include <LiquidCrystal_I2C.h> // LCD display library
#include <SdFat.h>             // High-speed SD card library (faster than SD.h)

// SD card SPI pin definitions for ESP32
#define SD_CS_PIN 5    // Chip Select
#define SD_MOSI_PIN 23 // Master Out Slave In
#define SD_MISO_PIN 19 // Master In Slave Out
#define SD_SCK_PIN 18  // Serial Clock

SdFat sd;
SdFile logFile;
bool sdReady = false;
String sessionFilename = "";

// Helper to generate unique log filename with timestamp
String getLogFilename(uint64_t sessionStartTs)
{
  return "emg_log_" + String(sessionStartTs) + ".txt";
}

// Configuration Constants
#define BATCH_SIZE 1500                        // Max EMG records in buffer (legacy, for API posting)
#define BATCH_FLUSH_SAMPLE 1500                // Flush SD card every 1500 samples (~1.5 sec at 1kHz)
#define WIFI_RECONNECT_INTERVAL_MS 10000UL     // Check WiFi connection every 10 seconds
#define NTP_RESYNC_INTERVAL_MS 3600000UL       // Re-sync NTP time every 1 hour
#define LCD_COL_0 0                            // LCD column 0 (leftmost)
#define LCD_COL_11 11                          // LCD column 11 (for right-aligned values)
#define LCD_ROW_0 0                            // LCD top row
#define LCD_ROW_1 1                            // LCD bottom row
#define DELAY_LCD_SHORT 1000                   // Short LCD message delay (1 second)
#define DELAY_LCD_MED 2500                     // Medium LCD message delay (2.5 seconds)
#define DELAY_LCD_LONG 10000                   // Long LCD message delay (10 seconds)
#define DEBUG_SERIAL_PLOTTER 1                 // Set to 0 to disable serial plotter output

// Custom epoch: 2025-06-22T00:00:00 UTC
// All timestamps are stored as milliseconds since this epoch to save space
const time_t CUSTOM_EPOCH = 1750550400; // Unix timestamp for 2025-06-22T00:00:00 UTC

// Hardware Pin Definitions
#define EMG_PIN 34              // ADC1 pin (GPIO 34) - MyoWare sensor analog output
#define STOP_BUTTON_PIN 26      // Button to stop data acquisition (active LOW with pull-up)
#define START_BUTTON_PIN 25     // Button to start data acquisition (active LOW with pull-up)

// LCD Display (16 chars x 2 lines, I2C address 0x27)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// WiFi Configuration (replace with your network credentials)
const char* ssid = "SIGN24G";
const char* password = "wifi_password_here";

// API endpoint (LEGACY - currently disabled in favor of SD logging)
const char* serverUrl = "http://fastapi_server:8000/emg/records";

// State Management Flags
bool readingActive = false;          // Controls whether EMG data acquisition is active
bool stoppedMessagePrinted = false;  // Prevents LCD message spam when stopped

/**
 * EMG Record Data Structure
 * Stores a single EMG sample with timestamp and raw ADC value
 */
struct EmgRecord
{
  uint64_t timestamp; // Milliseconds since custom epoch (2025-06-22T00:00:00 UTC)
  int16_t rawValue;   // Raw 12-bit ADC value (0-4095) from MyoWare sensor
};

// EMG data buffer (legacy for API posting, but still used for temporary storage)
EmgRecord emgBuffer[BATCH_SIZE];
uint16_t emgIndex = 0;  // Current position in buffer

// ============================================================================
// LCD DISPLAY FUNCTIONS
// ============================================================================
// Note: LCD is 16 characters wide x 2 lines tall

/**
 * Initialize LCD display with backlight
 */
void setupLCD()
{
  lcd.init();
  lcd.clear();
  lcd.backlight(); // Ensure backlight is on for visibility
}

/**
 * Print a message to LCD at specified position
 * Clears the line before printing to avoid artifacts
 * 
 * @param message Text to display (max 16 chars)
 * @param x Column position (0-15)
 * @param y Row position (0-1)
 */
void printLCD(String message, int x, int y)
{
  clearLCDLine(x, y);
  lcd.setCursor(x, y);
  lcd.print(message);
}

/**
 * Display a two-line status message on LCD with delay
 * Useful for status updates and user notifications
 * 
 * @param line1 First line text (top row)
 * @param line2 Second line text (bottom row)
 * @param delayMs Duration to display message (milliseconds)
 */
void showStatusLCD(const char* line1, const char* line2, int delayMs)
{
  printLCD(String(line1), LCD_COL_0, LCD_ROW_0);
  printLCD(String(line2), LCD_COL_0, LCD_ROW_1);
  delay(delayMs);
}

/**
 * Overloaded version accepting String objects
 */
void showStatusLCD(String line1, String line2, int delayMs)
{
  printLCD(line1, LCD_COL_0, LCD_ROW_0);
  printLCD(line2, LCD_COL_0, LCD_ROW_1);
  delay(delayMs);
}

/**
 * Clear a specific line on the LCD by writing spaces
 * 
 * @param x Starting column (typically 0)
 * @param y Row to clear (0 or 1)
 */
void clearLCDLine(int x, int y)
{
  lcd.setCursor(x, y);
  // Write 16 spaces to clear entire line
  lcd.print("                ");
}

// ============================================================================
// BUTTON INPUT FUNCTIONS
// ============================================================================

/**
 * Initialize start and stop buttons with internal pull-up resistors
 * Buttons are active LOW (pressed = LOW, released = HIGH)
 */
void setupStopStartButtons()
{
  pinMode(STOP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
}

/**
 * Debounce button input to prevent false triggers from mechanical bounce
 * Uses 20ms debounce interval for reliable operation
 * Maintains separate state for each button (stop and start)
 * 
 * @param pin Button pin to check (STOP_BUTTON_PIN or START_BUTTON_PIN)
 * @return true if button was pressed (debounced), false otherwise
 */
bool debounceButton(int pin)
{
  // Static variables maintain state between function calls
  static unsigned long lastDebounceTime[2] = {0, 0};   // Last time button changed
  static bool lastButtonReading[2] = {HIGH, HIGH};     // Previous raw reading
  static bool buttonState[2] = {HIGH, HIGH};           // Stable debounced state
  int idx = (pin == STOP_BUTTON_PIN) ? 0 : 1;          // Map pin to array index

  bool reading = digitalRead(pin);  // Read current button state

  // If button state changed, reset debounce timer
  if (reading != lastButtonReading[idx])
  {
    lastDebounceTime[idx] = millis();
    lastButtonReading[idx] = reading;
  }

  // Check if button state has been stable for 20ms
  if ((millis() - lastDebounceTime[idx]) > 20)
  {
    if (buttonState[idx] != reading)
    {
      buttonState[idx] = reading;
      if (buttonState[idx] == LOW)
      {
        return true; // Button press confirmed (active LOW)
      }
    }
  }
  return false;  // No press detected
}

// ============================================================================
// WIFI AND NETWORK FUNCTIONS
// ============================================================================

/**
 * Connect to WiFi network and display status on LCD
 * Blocks until connection is established
 */
void setupWiFi()
{
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  printLCD("CONNECTING-WIFI", 0, 0);
  printLCD("...", 0, 1);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  printLCD("CONNECTED!", 0, 0);
  printLCD(WiFi.localIP().toString(), 0, 1);
}

/**
 * Synchronize system time with NTP server
 * Uses pool.ntp.org for UTC time
 * Waits up to 30 seconds for time sync
 * Accurate timestamps are critical for EMG data correlation
 */
void setupNTP()
{
  // Configure NTP client (0 offset = UTC, no DST)
  configTime(0, 0, "pool.ntp.org");
  Serial.println("Waiting for NTP time sync...");
  time_t now = time(nullptr);
  int ntpWaits = 0;
  while (now < 8 * 3600 * 2 && ntpWaits < 60)
  { // Wait up to ~30s
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    ntpWaits++;
  }
  Serial.println("");
  if (now >= 8 * 3600 * 2)
  {
    Serial.println("Time synchronized!");
    printLCD("TIME-SYNCED!", 0, 0);
  }
  else
  {
    Serial.println("NTP sync failed or timed out.");
    printLCD("NTP-SYNC-FAILED!", 0, 0);
  }
}

// ============================================================================
// API POSTING FUNCTIONS (LEGACY - CURRENTLY DISABLED)
// ============================================================================
// Note: This code was used when the system posted batches to a FastAPI backend.
// It has been preserved for reference but is not actively used.
// The system now logs directly to SD card instead.

/**
 * Post a batch of EMG records to the API endpoint
 * LEGACY FUNCTION - Currently disabled in favor of SD card logging
 * 
 * @return true if POST succeeded, false otherwise
 */
bool postBatch()
{
  // Build JSON payload from buffer
  // Memory allocation: 40KB supports ~1000 records with overhead
  StaticJsonDocument<40960> doc;
  JsonArray array = doc.to<JsonArray>();

  // Serialize all buffered EMG records to JSON
  for (uint16_t i = 0; i < emgIndex; i++)
  {
    JsonObject rec = array.createNestedObject();
    rec["timestamp"] = emgBuffer[i].timestamp;  // ms since custom epoch
    rec["rawValue"] = emgBuffer[i].rawValue;    // 12-bit ADC value
  }
  String payload;
  serializeJson(doc, payload);

  // Attempt to POST if WiFi is connected
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.setTimeout(60000);  // 60 second timeout for large batches
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");
    printLCD("SENDING-DATA", 0, 0);
    printLCD("...", 0, 1);
    int httpResponseCode = http.POST(payload);
    if (httpResponseCode == 201)  // 201 Created = success
    {
      printLCD("DATA SAVED", 0, 1);
      http.end();
      printLCD("READING-SENSOR", 0, 0);
      printLCD("...", 0, 1);
      return true;
    }
    else
    {
      // Handle various error conditions
      if (httpResponseCode == -1)
      {
        printLCD("API-UNREACHABLE", 0, 1);  // Connection failed
      }
      else
      {
        printLCD("API-ERROR-" + String(httpResponseCode), 0, 1);  // HTTP error code
      }
      delay(2000);
    }
    http.end();
    printLCD("READING-SENSOR", 0, 0);
    printLCD("...", 0, 1);
  }
  else
  {
    printLCD("NO WIFI", 0, 0);
    printLCD(wifiStatusToString(WiFi.status()), 0, 1);
  }
  return false;
}

/**
 * Convert WiFi status enum to readable string for debugging
 * 
 * @param status WiFi status code
 * @return Human-readable status string
 */
String wifiStatusToString(wl_status_t status)
{
  switch (status)
  {
  case WL_IDLE_STATUS:
    return "IDLE";
  case WL_NO_SSID_AVAIL:
    return "NO_SSID";
  case WL_SCAN_COMPLETED:
    return "SCAN_DONE";
  case WL_CONNECTED:
    return "CONNECTED";
  case WL_CONNECT_FAILED:
    return "FAILED";
  case WL_CONNECTION_LOST:
    return "LOST";
  case WL_DISCONNECTED:
    return "DISCONNECTED";
  default:
    return "UNKNOWN";
  }
}

/**
 * Check WiFi connection and attempt reconnection if disconnected
 * Called periodically from main loop
 */
void checkAndReconnectWiFi()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    initSDCard();  // Re-init SD card after WiFi issues
    delay(1000);
    printLCD("RECONNECTING WIFI", 0, 0);
    printLCD("...", 0, 1);
    delay(1000);
    WiFi.disconnect();
    WiFi.reconnect();
    delay(1000);
  }
}

// ============================================================================
// SD CARD FUNCTIONS
// ============================================================================

/**
 * Initialize SD card module using SPI interface
 * Sets up SPI pins explicitly for ESP32 compatibility
 * Uses 10 MHz clock for maximum reliability
 */
void initSDCard()
{
  // Explicitly configure SPI pins for ESP32
  // This ensures correct pin mapping even if other libraries modify SPI
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  
  // Initialize SdFat filesystem with 10 MHz clock (conservative for stability)
  if (!sd.begin(SD_CS_PIN, SD_SCK_MHZ(10)))
  {
    sdReady = false;
    printLCD("SD FAIL", 0, 0);
    printLCD("CHECK SD CARD", 0, 1);
    Serial.println("SD Card initialization failed!");
    Serial.println("- Check SD card is inserted");
    Serial.println("- Verify wiring connections");
    Serial.println("- Try formatting SD card as FAT32");
    delay(3000);
  }
  else
  {
    sdReady = true;
    printLCD("SD CARD READY", 0, 0);
    Serial.println("SD Card initialized successfully!");
    delay(1000);
  }
}

// ============================================================================
// SETUP FUNCTION
// ============================================================================

/**
 * Arduino setup function - runs once at boot
 * Initializes all peripherals in sequence:
 * 1. Serial communication for debugging
 * 2. LCD display
 * 3. SD card for data logging
 * 4. WiFi network connection
 * 5. NTP time synchronization
 * 6. Start/stop buttons
 * 7. Sensor stabilization period
 */
void setup()
{
  Serial.begin(115200);
  Serial.println("\n=== ESP32 sEMG Data Logger Starting ===");
  
  setupLCD();
  delay(1000);
  
  initSDCard();  // Initialize SD card first (critical for data logging)
  delay(2000);
  
  setupWiFi();   // Connect to WiFi (needed for NTP time sync)
  delay(1000);
  
  setupNTP();    // Synchronize time via NTP
  delay(1000);
  
  setupStopStartButtons();  // Configure control buttons
  
  // Allow MyoWare sensor to stabilize (requires ~5 seconds after power-on)
  printLCD("STABILIZING", 0, 0);
  printLCD("MUSCLE-SENSOR...", 0, 1);
  Serial.println("Waiting for MyoWare sensor to stabilize (5s)...");
  delay(5000);
  
  Serial.println("Setup complete. Ready to log EMG data.");
  Serial.println("Press START button to begin acquisition.");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

/**
 * Arduino main loop - runs continuously
 * 
 * Main responsibilities:
 * 1. Monitor WiFi connection and reconnect if needed
 * 2. Periodically re-sync NTP time
 * 3. Handle start/stop button presses
 * 4. Read EMG sensor (variable rate, no timing control)
 * 5. Log data to SD card with timestamps
 * 6. Update LCD with current status
 * 7. Optional: Output to Serial Plotter for debugging
 * 
 * IMPORTANT: This loop does NOT implement precise timing control.
 * Sampling rate is variable and depends on execution speed of all operations.
 */
void loop()
{
  // Static variables preserve state between loop iterations
  static bool wasReadingActive = false;     // Previous loop's reading state
  static uint64_t sessionStartTs = 0;       // Session start time (for filename)
  static unsigned long lastWifiCheck = 0;   // Last WiFi check timestamp
  static unsigned long lastNtpSync = 0;     // Last NTP sync timestamp

  unsigned long now = millis();

  // Periodically verify WiFi connection (every 10 seconds)
  if (now - lastWifiCheck > WIFI_RECONNECT_INTERVAL_MS)
  {
    checkAndReconnectWiFi();
    lastWifiCheck = now;
  }

  // Periodically re-sync time with NTP server (every 1 hour)
  // Important for long-running sessions to prevent clock drift
  if (now - lastNtpSync > NTP_RESYNC_INTERVAL_MS)
  {
    setupNTP();
    lastNtpSync = now;
  }

  // Handle STOP button press (only if currently reading)
  if (debounceButton(STOP_BUTTON_PIN) && readingActive)
  {
    readingActive = false;
    emgIndex = 0;  // Reset buffer index
    stoppedMessagePrinted = false;  // Allow stopped message to display again
    showStatusLCD("STOPPING-DATA.", "TRANSMISSION", DELAY_LCD_SHORT);
    lcd.clear();
    Serial.println("Data acquisition stopped by user.");
  }
  
  // Handle START button press (only if currently stopped)
  if (debounceButton(START_BUTTON_PIN) && !readingActive)
  {
    readingActive = true;
    Serial.println("Data acquisition started by user.");
  }

  // Session start: Create new log file when transitioning from stopped to active
  if (readingActive && !wasReadingActive)
  {
    if (sdReady)
    {
      // Get current time as milliseconds since custom epoch
      struct timeval tv;
      gettimeofday(&tv, NULL);
      sessionStartTs = (static_cast<uint64_t>(tv.tv_sec) - CUSTOM_EPOCH) * 1000ULL + tv.tv_usec / 1000ULL;
      
      // Create unique filename with timestamp
      sessionFilename = getLogFilename(sessionStartTs);
      Serial.print("Creating new log file: ");
      Serial.println(sessionFilename);
      
      // Open file for writing (create if doesn't exist, truncate if exists)
      if (!logFile.open(sessionFilename.c_str(), O_WRONLY | O_CREAT | O_TRUNC))
      {
        printLCD("SD FILE ERR", 0, 0);
        printLCD("NO LOGGING", 0, 1);
        Serial.println("ERROR: Failed to create log file!");
      }
      else
      {
        Serial.println("Log file created successfully.");
      }
    }
  }

  // Session stop: Close log file when transitioning from active to stopped
  if (!readingActive && wasReadingActive)
  {
    if (logFile.isOpen())
    {
      logFile.sync();   // Flush any remaining data to SD card
      logFile.close();  // Close file handle
      Serial.print("Log file closed: ");
      Serial.println(sessionFilename);
    }
  }
  
  // Update state tracking for next loop iteration
  wasReadingActive = readingActive;

  // Read raw EMG value from MyoWare sensor
  // ESP32 ADC is 12-bit: range 0-4095 (0-3.3V)
  int emgValue = analogRead(EMG_PIN);
  
#if DEBUG_SERIAL_PLOTTER
  // Output for Arduino Serial Plotter
  // Format: "label value" pairs with upper/lower bounds for scaling
  Serial.print(" ");       // Spacing
  Serial.print(3800);      // Upper bound
  Serial.print(" ");
  Serial.print(0);         // Lower bound
  Serial.print("EMG ");
  Serial.println(emgValue); // Actual EMG value
#endif

  // Active data acquisition mode
  if (readingActive)
  {
    // Get high-resolution timestamp (microsecond precision)
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // Convert to milliseconds since custom epoch (2025-06-22T00:00:00 UTC)
    // Formula: ((seconds - epoch_seconds) * 1000) + (microseconds / 1000)
    emgBuffer[emgIndex].timestamp = (static_cast<uint64_t>(tv.tv_sec) - CUSTOM_EPOCH) * 1000ULL + tv.tv_usec / 1000ULL;
    emgBuffer[emgIndex].rawValue = emgValue;
    emgIndex++;
    
    // SD card logging: Write each sample immediately to file
    // CSV format: timestamp,rawValue
    // Example: 123456789,2048
    if (sdReady && logFile.isOpen())
    {
      logFile.print(String(emgBuffer[emgIndex - 1].timestamp));
      logFile.print(",");
      logFile.println(emgBuffer[emgIndex - 1].rawValue);
      
      // Periodically flush data to SD card (every 1500 samples = ~1.5 seconds)
      // This is a tradeoff between:
      // - Write speed (less frequent flushes = faster)
      // - Data safety (more frequent flushes = less data loss if power fails)
      if (emgIndex >= BATCH_FLUSH_SAMPLE)
      {
        logFile.sync();  // Force write buffered data to SD card
        emgIndex = 0;    // Reset buffer counter
      }
    }

    // LEGACY CODE - API posting (now disabled)
    // Originally, when buffer filled up, data was POSTed to API
    // This has been replaced by continuous SD card logging
    // Preserved for reference:
    // if (emgIndex >= BATCH_SIZE) {
    //   postBatch();
    //   emgIndex = 0;
    // }
  }

  // LCD display updates when stopped
  if (!stoppedMessagePrinted)
  {
    // Display stopped message once (prevents constant LCD rewrites)
    showStatusLCD("STOPPED-PRESS-ST", "EMG-VALUE: " + String(emgValue), DELAY_LCD_SHORT);
    stoppedMessagePrinted = true;
  }
  else if (!readingActive)
  {
    // Update EMG value on LCD while stopped
    // Only updates the number portion (right side of row 1)
    // This runs at ~5Hz effective update rate
    printLCD(String(emgValue), 11, 1);
  }
}

/*
 * END OF CODE
 * 
 * Project Status: Work in Progress
 * Last Major Change: Switched from API POST batching to SD card logging
 * 
 * Known Issues:
 * - NO FIXED SAMPLING RATE: Variable timing makes frequency analysis unreliable
 * - NO ANTI-ALIASING FILTER: High-frequency noise can alias into measured signal
 * - Sample rate too low and inconsistent for clinical-grade EMG analysis
 * - Current implementation suitable ONLY for basic amplitude monitoring
 * 
 * Critical Improvements Required (Nyquist Theorem Compliance):
 * 1. Hardware Timer Interrupt:
 *    - Implement ESP32 hw_timer_t for precise 1-2 kHz sampling
 *    - Use timer ISR to trigger ADC reads at exact intervals
 *    - Decouple sampling from main loop operations
 * 
 * 2. Anti-Aliasing Filter (HARDWARE):
 *    - Add analog low-pass filter BEFORE ESP32 ADC input
 *    - Cutoff frequency = Nyquist frequency (half of sampling rate)
 *    - Example: 1000 Hz sampling → 500 Hz cutoff filter
 *    - Prevents frequencies above Nyquist limit from corrupting signal
 *    - Required components: Op-amp based Sallen-Key or Butterworth filter
 * 
 * 3. Signal Processing Improvements:
 *    - Implement ring buffer for ISR-to-main data transfer
 *    - Add sample rate validation and monitoring
 *    - Calculate actual achieved sample rate from timestamps
 *    - Log sample rate statistics to verify consistency
 * 
 * 4. Validation & Testing:
 *    - Test with known frequency signals (function generator)
 *    - Verify no aliasing artifacts in captured data
 *    - FFT analysis to confirm frequency response
 *    - Document filter design and frequency response curve
 * 
 * Future Improvements:
 * - Add error recovery for SD card write failures
 * - Implement data compression for longer recording sessions
 * - Add battery voltage monitoring for portable operation
 * - Create configuration file on SD card for WiFi credentials
 * - Real-time FFT computation for frequency analysis
 * - Muscle fatigue detection algorithms (median frequency shift)
 * 
 * CSV File Format:
 * Each log file contains comma-separated values:
 * timestamp,rawValue
 * 123456789,2048
 * 123456790,2051
 * ...\n * 
 * Where:
 * - timestamp = milliseconds since 2025-06-22T00:00:00 UTC
 * - rawValue = 12-bit ADC reading (0-4095)
 */
