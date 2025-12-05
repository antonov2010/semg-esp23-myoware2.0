from fastapi import APIRouter, Depends, status
from sqlalchemy.orm import Session
from app.models import EmgRecord
from app.db import SessionLocal
from app.crud import create_emg_records
from typing import List
emg_router = APIRouter(prefix="/emg", tags=["emg"])

def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

# New endpoint for batch upload of EmgRecord
@emg_router.post("/records", status_code=status.HTTP_201_CREATED)
def receive_emg_records(records: List[EmgRecord], db: Session = Depends(get_db)):
    create_emg_records(db, records)
    return {"status": "Data Saved", "count": len(records)}

# New endpoint for saving a single EmgRecord
@emg_router.post("/record", status_code=status.HTTP_201_CREATED)
def receive_emg_record(record: EmgRecord, db: Session = Depends(get_db)):
    create_emg_records(db, [record])
    return {"status": "Data Saved", "count": 1}
