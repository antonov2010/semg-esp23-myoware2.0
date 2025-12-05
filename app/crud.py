from sqlalchemy.orm import Session
from app.models_db import EmgRecordDB
from app.models import EmgRecord
from typing import List

def create_emg_records(db: Session, records: List[EmgRecord]):
    db_records = [EmgRecordDB(timestamp=r.timestamp, rawValue=r.rawValue) for r in records]
    db.add_all(db_records)
    db.commit()
    # No need to refresh or return records
    return