from fastapi import APIRouter
from app.models import HelloResponse

router = APIRouter()

@router.get("/hello", response_model=HelloResponse)
def say_hello(name: str = "World"):
    """A simple hello endpoint."""
    return HelloResponse(message=f"Hello, {name}!")
