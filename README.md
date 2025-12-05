# sEMG Data API

This project is a FastAPI backend for collecting and storing surface electromyography (sEMG) readings. It is designed to interface with an sEMG system that uses an ESP32 DevKit V1 board and a MyoWare Muscle Sensor 2.0. The ESP32 posts readings to this API when a threshold is reached, indicating that the sensor is properly installed on a person.

## Timestamp Convention

**All EMG data timestamps are stored as the number of milliseconds since a custom epoch:**

**Custom Epoch:** 2025-06-22T00:00:00 UTC (Unix timestamp: 1750579200)

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

## Features
- **FastAPI**-based REST API with automatic documentation (Swagger UI at `/docs`, ReDoc at `/redoc`)
- **Endpoints**:
  - `GET /hello` – Simple test endpoint
  - `POST /emg/lectures` – Receive and store sEMG readings
- **Input validation** with Pydantic models
- **PostgreSQL database** integration using SQLAlchemy ORM
- **Automatic table creation** at startup
- **Environment-based configuration** via `.env`

## Project Structure
```
app/
  api/
    endpoints.py      # General (non-EMG) endpoints
    emg.py            # EMG controller endpoints
  crud.py             # Database CRUD operations
  db.py               # Database connection and session
  main.py             # FastAPI app entry point
  models.py           # Pydantic models
  models_db.py        # SQLAlchemy models
```

## Usage
1. **Install dependencies:**
   ```bash
   pip install -r requirements.txt
   ```
2. **Configure environment variables:**
   - Copy `.env.example` to `.env` and set your PostgreSQL credentials and connection info.
3. **Run the API server:**
   ```bash
   uvicorn app.main:app --reload
   ```
4. **Send sEMG readings:**
   - POST to `/emg/lectures` with JSON data:
     ```json
     {
       "time": "2025-06-03T00:00:00Z",
       "rawValue": 1234
     }
     ```

## Hardware Context
- **ESP32 DevKit V1**: Microcontroller board running firmware that reads sEMG data from the sensor.
- **MyoWare Muscle Sensor 2.0**: sEMG sensor attached to the user.
- The ESP32 firmware is programmed to POST readings to this API once a threshold is reached, signaling the sensor is in place and active.

## Setting Up a Python Virtual Environment

It is recommended to use a virtual environment to manage dependencies for this project. Here are the basic steps:

1. **Create a virtual environment:**
   ```bash
   python3 -m venv venv
   ```

2. **Activate the virtual environment:**
   - On Linux/macOS:
     ```bash
     source venv/bin/activate
     ```
   - On Windows:
     ```cmd
     venv\Scripts\activate
     ```

3. **Deactivate the virtual environment:**
   ```bash
   deactivate
   ```

## Dependencies
See `requirements.txt` for all required Python packages:
- fastapi
- uvicorn
- sqlalchemy
- python-dotenv
- psycopg2-binary

---

## Database Migrations with Alembic

This project uses **Alembic** to manage database schema migrations for PostgreSQL. All EMG and lecture tables are created and updated via Alembic migrations.

### Alembic Setup & Usage
- Alembic config and migrations are in the `alembic/` directory (at project root, sibling to `app/`).
- Alembic is configured to use the same database URL and SQLAlchemy `Base` as the FastAPI backend.
- Environment variables for DB connection are loaded from `.env`.

#### Common Alembic Commands
```bash
# Create a new migration after changing models
alembic revision --autogenerate -m "Describe your change"

# Apply migrations to the database
alembic upgrade head
```

#### Troubleshooting Alembic
- **Empty migrations?**
  - Ensure `alembic/env.py` imports models using absolute paths (e.g., `from app.models_db import ...`).
  - Make sure `sys.path` is set to the project root, not `app`.
  - Confirm all models inherit from the same `Base` as imported in `env.py`.
- **No tables created?**
  - Check your DB connection string and current schema.
  - Use a debug script to print `Base.metadata.tables.keys()` and ensure all models are registered.

#### Example `alembic/env.py` Import Block
```python
import sys, os
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from app.db import Base, DATABASE_URL
from app.models_db import EmgRecordDB, LectureDB, LectureSecMillis
```

#### Best Practices
- Never edit the database schema manually—always use Alembic migrations.
- Keep Alembic and app directories at the same level for import clarity.
- Document any changes to model import paths in the README for future maintainers.

---

## Local Network Debugging

If you are debugging or developing on a local network and want devices like the ESP32 to make POST requests to your FastAPI development server, make sure to open the relevant port (e.g., 8000) in your firewall. For example, on openSUSE with firewalld, you can run:

```bash
sudo firewall-cmd --permanent --zone=public --add-port=8000/tcp
sudo firewall-cmd --reload
```

This allows network devices to access your FastAPI server at `http://<your_local_ip>:8000`.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

---

For questions or contributions, please open an issue or pull request.
