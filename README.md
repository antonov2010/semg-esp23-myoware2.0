# ESP32 sEMG Data Logger

A prototype surface electromyography (sEMG) data acquisition system using ESP32 DevKit V1 and MyoWare Muscle Sensor 2.0. This project logs muscle electrical activity to an SD card with timestamps and includes a FastAPI backend for data storage and analysis.

## ⚠️ Project Status: Work in Progress - Not Production Ready

**Current Limitations:**
- **No fixed sampling rate** - Variable timing makes data unsuitable for frequency analysis
- **No anti-aliasing filter** - High-frequency noise can corrupt measurements
- **Not compliant with Nyquist Theorem** - Required for trustworthy EMG analysis
- **Suitable only for basic amplitude monitoring** - NOT for clinical or research use

See [Critical Improvements Required](#critical-improvements-required) section below.

## System Overview

This project consists of two main components:

1. **ESP32 Firmware** (`esp32-semg/esp32-semg.ino`) - Embedded system for real-time EMG data acquisition
2. **FastAPI Backend** (`app/`) - REST API for data storage and management (PostgreSQL)

### Current Implementation

The ESP32 firmware was originally designed to POST batches of EMG data to the FastAPI backend. **It has been transitioned to SD card logging** for improved reliability and offline operation. The API posting code is preserved but disabled.

## Hardware Components

- **ESP32 DevKit V1** - 32-bit microcontroller with WiFi
- **MyoWare Muscle Sensor 2.0** - sEMG sensor connected to GPIO 34 (ADC1)
- **16x2 I2C LCD Display** (address 0x27) - Status display
- **SD Card Module** - SPI interface for data logging
  - CS: GPIO 5
  - MOSI: GPIO 23
  - MISO: GPIO 19
  - SCK: GPIO 18
- **Control Buttons**:
  - START: GPIO 25 (active LOW with pull-up)
  - STOP: GPIO 26 (active LOW with pull-up)

- **Control Buttons**:
  - START: GPIO 25 (active LOW with pull-up)
  - STOP: GPIO 26 (active LOW with pull-up)

## Features

### ESP32 Firmware Features
- Real-time sEMG signal acquisition (variable rate, uncontrolled timing)
- Local SD card logging with timestamped CSV files
- WiFi connectivity with automatic reconnection
- NTP time synchronization for accurate timestamps
- LCD status display with real-time EMG values
- Start/Stop control via hardware buttons
- Serial plotter output for debugging (optional)

### FastAPI Backend Features
- RESTful API with automatic documentation (Swagger UI at `/docs`)
- PostgreSQL database integration
- Input validation with Pydantic models
- Alembic database migrations
- Environment-based configuration

## Timestamp Convention

**All EMG data timestamps are stored as the number of milliseconds since a custom epoch:**

**Custom Epoch:** 2025-06-22T00:00:00 UTC (Unix timestamp: 1750550400)

- Each EMG record's `timestamp` field represents the elapsed milliseconds since this custom epoch.
- To convert a stored timestamp to an absolute UTC datetime, add the timestamp (in ms) to the custom epoch.
- Example conversion (Python):
  ```python
  import datetime
  custom_epoch = datetime.datetime(2025, 6, 22, 0, 0, 0, tzinfo=datetime.timezone.utc)
  # Suppose timestamp_ms is 12345
  dt = custom_epoch + datetime.timedelta(milliseconds=12345)
  print(dt.isoformat())
  ```
- This approach ensures all time calculations are relative to a known, project-specific reference point.

## CSV Data Format

EMG data is logged to SD card in CSV format. Each session creates a new file named `emg_log_<timestamp>.txt`:

```csv
timestamp,rawValue
123456789,2048
123456790,2051
123456791,2053
```

Where:
- `timestamp` = milliseconds since custom epoch (2025-06-22T00:00:00 UTC)
- `rawValue` = 12-bit ADC reading (0-4095, representing 0-3.3V)

## Critical Improvements Required

### ⚠️ Nyquist Theorem Compliance

For trustworthy sEMG analysis, the following requirements must be met:

**Background:**
- Surface EMG signals contain frequencies up to ~400-500 Hz
- Nyquist Theorem requires sampling rate ≥ 2× highest frequency
- Minimum sampling rate: 800-1000 Hz
- Recommended sampling rate: 1000-2000 Hz
- Anti-aliasing filter required BEFORE digitization

**Current Issues:**
1. ❌ No fixed sampling rate → variable intervals cause aliasing
2. ❌ No anti-aliasing hardware filter → high frequencies fold back into signal
3. ❌ Sample rate too low and inconsistent for frequency analysis
4. ❌ Cannot perform FFT, spectral analysis, or fatigue detection reliably

**Required Implementations:**

### 1. Hardware Timer Interrupt (Software)
```cpp
// ESP32 hardware timer for precise 1kHz sampling
hw_timer_t *timer = NULL;
volatile bool sampleReady = false;

void IRAM_ATTR onTimer() {
  sampleReady = true;  // Set flag for main loop
}

void setup() {
  timer = timerBegin(0, 80, true);  // 80 prescaler for 1MHz
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000, true);  // 1000µs = 1kHz
  timerAlarmEnable(timer);
}
```

### 2. Anti-Aliasing Filter (Hardware)
- Add analog low-pass filter circuit BEFORE ESP32 ADC input
- Filter type: Sallen-Key or Butterworth (2nd or 4th order)
- Cutoff frequency: 500 Hz (for 1kHz sampling) or 1000 Hz (for 2kHz sampling)
- Components needed: Op-amp (e.g., TL072), resistors, capacitors
- Prevents frequencies above Nyquist limit from corrupting measurements

### 3. Ring Buffer for ISR Data Transfer
```cpp
#define RING_BUFFER_SIZE 256
volatile uint16_t ringBuffer[RING_BUFFER_SIZE];
volatile uint16_t writeIndex = 0;
volatile uint16_t readIndex = 0;
```

### 4. Validation & Testing
- Test with known frequency signals using function generator
- Perform FFT analysis on captured data
- Verify no aliasing artifacts
- Document filter frequency response curve
- Validate sample rate consistency from timestamps

## Quick Start

### ESP32 Firmware Setup

1. **Install Arduino IDE** and ESP32 board support
2. **Install required libraries:**
   - WiFi (built-in)
   - HTTPClient (built-in)
   - ArduinoJson
   - LiquidCrystal_I2C
   - SdFat
3. **Configure WiFi credentials** in `esp32-semg.ino`:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```
4. **Upload firmware** to ESP32
5. **Insert FAT32-formatted SD card**
6. **Press START button** to begin logging

### FastAPI Backend Setup
### FastAPI Backend Setup

1. **Create a virtual environment:**
   ```bash
   python3 -m venv venv
   source venv/bin/activate  # Linux/macOS
   # or: venv\Scripts\activate  # Windows
   ```

2. **Install dependencies:**
   ```bash
   pip install -r requirements.txt
   ```

3. **Configure environment variables:**
   - Copy `.env.example` to `.env`
   - Set PostgreSQL credentials and connection info

4. **Run database migrations:**
   ```bash
   alembic upgrade head
   ```

5. **Start the API server:**
   ```bash
   uvicorn app.main:app --reload
   ```

6. **Access API documentation:**
   - Swagger UI: http://localhost:8000/docs
   - ReDoc: http://localhost:8000/redoc

### API Endpoints

- `GET /hello` - Simple test endpoint
- `POST /emg/records` - Store EMG readings (currently disabled in ESP32 firmware)

**Example POST request:**
```json
{
  "timestamp": 123456789,
  "rawValue": 2048
}
```

## Project Structure

```
esp32-semg/
├── esp32-semg/
│   └── esp32-semg.ino          # ESP32 firmware (Arduino sketch)
├── app/
│   ├── api/
│   │   ├── endpoints.py        # General API endpoints
│   │   └── emg.py              # EMG-specific endpoints
│   ├── crud.py                 # Database CRUD operations
│   ├── db.py                   # Database connection
│   ├── main.py                 # FastAPI entry point
│   ├── models.py               # Pydantic models
│   └── models_db.py            # SQLAlchemy ORM models
├── alembic/                    # Database migrations
├── ML/
│   └── ML_Project.ipynb        # Machine learning analysis notebook
├── requirements.txt            # Python dependencies
├── alembic.ini                 # Alembic configuration
└── README.md                   # This file
```

## Development Notes

### Current Workflow
1. ESP32 samples EMG signal continuously when active
2. Data is logged to SD card with high-precision timestamps
3. SD card is flushed every 1500 samples for data safety
4. WiFi maintains NTP time synchronization (hourly)
5. LCD displays current status and EMG values

### Known Limitations
- Sampling rate varies with system load (WiFi checks, SD writes, LCD updates)
- No guarantee of consistent sample intervals
- Timestamps are accurate but intervals between samples are not uniform
- Unsuitable for applications requiring precise frequency analysis

## Troubleshooting

### SD Card Issues
- Ensure SD card is formatted as FAT32
- Check SPI wiring connections
- Verify SD card is properly inserted
- Try different SD card if initialization fails

### WiFi Connection
- Verify SSID and password are correct
- Check 2.4GHz WiFi is available (ESP32 doesn't support 5GHz)
- For local network debugging, open port 8000 in firewall:
  ```bash
  sudo firewall-cmd --permanent --zone=public --add-port=8000/tcp
  sudo firewall-cmd --reload
  ```

### MyoWare Sensor
- Allow 5 seconds for sensor stabilization after power-on
- Ensure proper electrode placement on muscle
- Check sensor output voltage is within 0-3.3V range
- Verify GPIO 34 connection (ADC1 pin)

- Verify GPIO 34 connection (ADC1 pin)

### Database Issues
- Check PostgreSQL is running
- Verify connection string in `.env`
- Run migrations: `alembic upgrade head`
- Check `alembic/env.py` imports models correctly

## Database Migrations with Alembic

This project uses **Alembic** for PostgreSQL schema migrations.

### Common Commands
```bash
# Create a new migration after changing models
alembic revision --autogenerate -m "Describe your change"

# Apply migrations to the database
alembic upgrade head

# View migration history
alembic history

# Downgrade one revision
alembic downgrade -1
```

### Troubleshooting Alembic
- **Empty migrations?**
  - Ensure `alembic/env.py` imports models using absolute paths
  - Verify `sys.path` is set to project root
  - Confirm all models inherit from the same `Base`
  
- **No tables created?**
  - Check database connection string
  - Verify models are registered in `Base.metadata`

### Example `alembic/env.py` Import Block
```python
import sys, os
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from app.db import Base, DATABASE_URL
from app.models_db import EmgRecordDB, LectureDB, LectureSecMillis
```

## Python Dependencies

See `requirements.txt`:
- fastapi - Web framework
- uvicorn - ASGI server
- sqlalchemy - ORM
- python-dotenv - Environment variables
- psycopg2-binary - PostgreSQL adapter
- alembic - Database migrations
- pydantic - Data validation

## Future Improvements

### High Priority (Required for Production)
- [ ] Implement hardware timer interrupt for fixed 1-2 kHz sampling
- [ ] Design and add anti-aliasing filter circuit
- [ ] Add ISR with ring buffer for reliable data acquisition
- [ ] Validate sampling rate consistency
- [ ] Test with function generator signals

### Medium Priority
- [ ] Real-time FFT computation for frequency analysis
- [ ] Muscle fatigue detection (median frequency shift)
- [ ] Battery voltage monitoring for portable operation
- [ ] Configuration file on SD card for WiFi credentials
- [ ] Data compression for longer recording sessions

### Low Priority
- [ ] Web dashboard for live EMG visualization
- [ ] Bluetooth connectivity option
- [ ] Multiple sensor support
- [ ] Cloud data synchronization

## References & Resources

- [Nyquist-Shannon Sampling Theorem](https://en.wikipedia.org/wiki/Nyquist%E2%80%93Shannon_sampling_theorem)
- [Surface EMG for Non-Invasive Assessment](https://www.seniam.org/)
- [MyoWare Muscle Sensor Documentation](https://myoware.com/)
- [ESP32 Hardware Timers](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/timer.html)
- [Anti-Aliasing Filter Design](https://www.analog.com/en/design-center/design-tools-and-calculators/ltspice-simulator.html)

## Contributing

This is a work-in-progress prototype. Contributions are welcome, especially:
- Hardware timer implementation
- Anti-aliasing filter circuit design
- Signal processing algorithms
- Documentation improvements

Please open an issue before starting significant work.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

---

## Disclaimer

**This device is a prototype and NOT suitable for medical or clinical use.** It has not been validated, certified, or approved for any diagnostic or therapeutic purpose. Use at your own risk.

---

For questions or contributions, please open an issue or pull request.
