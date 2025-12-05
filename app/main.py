from fastapi import FastAPI
from app.api import endpoints
from app.api.emg import emg_router
from app.db import Base, engine
from contextlib import asynccontextmanager

@asynccontextmanager
async def lifespan(app):
    # Create tables if they do not exist
    Base.metadata.create_all(bind=engine)
    yield

app = FastAPI(lifespan=lifespan)

# Include API routes from endpoints.py
app.include_router(endpoints.router)
app.include_router(emg_router)
