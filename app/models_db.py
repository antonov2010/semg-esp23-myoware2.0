from sqlalchemy import Column, Integer, BigInteger, Numeric, DateTime, TIMESTAMP, func
from app.db import Base

class LectureDB(Base):
    __tablename__ = "lectures"
    id = Column(Integer, primary_key=True, index=True)
    time = Column(TIMESTAMP(timezone=True), nullable=False)
    rawValue = Column(Numeric, nullable=False)
    created_at = Column(DateTime(timezone=True), server_default=func.timezone('utc', func.now()))

class LectureSecMillis(Base):
    __tablename__ = "lectures_sec_millis"
    id = Column(Integer, primary_key=True, index=True)
    seconds = Column(Integer)
    millis = Column(Integer)
    rawValue = Column(Numeric)
    created_at = Column(DateTime(timezone=True), server_default=func.timezone('utc', func.now()))

class EmgRecordDB(Base):
    __tablename__ = "emg_records"
    id = Column(Integer, primary_key=True, index=True)
    timestamp = Column(BigInteger, nullable=False)  # ms since custom epoch
    rawValue = Column(Numeric, nullable=False)
    created_at = Column(DateTime(timezone=True), server_default=func.timezone('utc', func.now()))