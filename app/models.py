from pydantic import BaseModel
from datetime import datetime
from decimal import Decimal

class HelloResponse(BaseModel):
    message: str

class EmgRecord(BaseModel):
    timestamp: int  # milliseconds since custom epoch (2025-06-22T00:00:00 UTC)
    rawValue: Decimal