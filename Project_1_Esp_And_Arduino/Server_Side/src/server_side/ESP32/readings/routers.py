import asyncio
import json
import os
from datetime import date as dt_date, datetime as dt_datetime, time as dt_time
from typing import Any
from uuid import UUID, uuid4

from fastapi import (
    APIRouter,
    Depends,
    HTTPException,
    Query,
    WebSocket,
    WebSocketDisconnect,
    status,
)
from pydantic import BaseModel, ConfigDict, field_validator
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from src.server_side.ESP32.database import AsyncSessionLocal, get_db
from src.server_side.ESP32.models.readings import Readings

# ==============================================================================
# CONFIGURABLE WEBSOCKET SETTINGS
# Change this variable to adjust the waiting period before saving to database.
# Can also be configured via .env with WS_BUFFER_WAIT_SECONDS=10.0
# ==============================================================================
BUFFER_WAIT_SECONDS: float = float(os.getenv("WS_BUFFER_WAIT_SECONDS", 10.0))

router = APIRouter(prefix="/readings", tags=["Readings"])


# ==============================================================================
# WEBSOCKET CONNECTION MANAGER
# ==============================================================================
class ConnectionManager:
    """Manages active WebSocket connections and handles broadcasting."""

    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def send_personal_message(self, message: dict, websocket: WebSocket):
        try:
            await websocket.send_json(message)
        except Exception:
            self.disconnect(websocket)

    async def broadcast(self, message: dict):
        for connection in list(self.active_connections):
            try:
                await connection.send_json(message)
            except Exception:
                self.disconnect(connection)


manager = ConnectionManager()


# ==============================================================================
# DATE & TIME PARSERS
# ==============================================================================
def parse_date(val: str | dt_datetime | dt_date | None) -> dt_datetime:
    """Parse string (e.g. 10/09/2016 or 2016-09-10) or date object into a datetime."""
    if val is None:
        return dt_datetime.now()
    if isinstance(val, dt_datetime):
        return val
    if isinstance(val, dt_date):
        return dt_datetime.combine(val, dt_time.min)
    if isinstance(val, str):
        val = val.strip()
        for fmt in ("%d/%m/%Y", "%d-%m-%Y", "%Y-%m-%d", "%Y-%m-%d %H:%M:%S", "%Y-%m-%dT%H:%M:%S"):
            try:
                return dt_datetime.strptime(val, fmt)
            except ValueError:
                pass
        try:
            return dt_datetime.fromisoformat(val)
        except ValueError:
            pass
    return dt_datetime.now()


def parse_time(val: str | dt_datetime | dt_time | None, base_date: dt_date | None = None) -> dt_datetime:
    """Parse string (e.g. 14:30:00 or 14:30) or time object into a datetime."""
    d = base_date or dt_date.today()
    if val is None:
        return dt_datetime.now()
    if isinstance(val, dt_datetime):
        return val
    if isinstance(val, dt_time):
        return dt_datetime.combine(d, val)
    if isinstance(val, str):
        val = val.strip()
        for fmt in ("%H:%M:%S", "%H:%M"):
            try:
                t = dt_datetime.strptime(val, fmt).time()
                return dt_datetime.combine(d, t)
            except ValueError:
                pass
        for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%dT%H:%M:%S", "%d/%m/%Y %H:%M:%S"):
            try:
                return dt_datetime.strptime(val, fmt)
            except ValueError:
                pass
        try:
            return dt_datetime.fromisoformat(val)
        except ValueError:
            pass
    return dt_datetime.now()


# ==============================================================================
# PYDANTIC SCHEMAS
# ==============================================================================
class ReadingRequest(BaseModel):
    sensor_id: UUID | None = None
    temperature: float | None = None
    humidity: float | None = None
    light: float | None = None
    time: str | dt_time | dt_datetime | None = None
    date: str | dt_date | dt_datetime | None = None
    error_message: str | None = None


class ReadingUpdateRequest(BaseModel):
    temperature: float | None = None
    humidity: float | None = None
    light: float | None = None
    time: str | dt_time | dt_datetime | None = None
    date: str | dt_date | dt_datetime | None = None
    error_message: str | None = None


class ReadingResponse(BaseModel):
    sensor_id: UUID
    temperature: float | None
    humidity: float | None
    light: float | None
    time: str
    date: str
    error_message: str | None

    model_config = ConfigDict(from_attributes=True)

    @field_validator("time", mode="before")
    @classmethod
    def format_time(cls, v: Any) -> str:
        if isinstance(v, (dt_datetime, dt_time)):
            return v.strftime("%H:%M:%S")
        if isinstance(v, str):
            try:
                dt = dt_datetime.fromisoformat(v)
                return dt.strftime("%H:%M:%S")
            except ValueError:
                return v
        return str(v) if v is not None else ""

    @field_validator("date", mode="before")
    @classmethod
    def format_date(cls, v: Any) -> str:
        if isinstance(v, (dt_datetime, dt_date)):
            return v.strftime("%d/%m/%Y")
        if isinstance(v, str):
            try:
                dt = dt_datetime.fromisoformat(v)
                return dt.strftime("%d/%m/%Y")
            except ValueError:
                return v
        return str(v) if v is not None else ""


# ==============================================================================
# DATABASE HELPER FOR BUFFERED WRITES
# ==============================================================================
async def save_buffered_reading(buffer_data: dict[str, Any]) -> dict[str, Any]:
    """Save accumulated buffer data to PostgreSQL and return formatted result."""
    parsed_date = parse_date(buffer_data.get("date"))
    parsed_time = parse_time(buffer_data.get("time"), base_date=parsed_date.date())

    raw_id = buffer_data.get("sensor_id")
    sensor_id = UUID(raw_id) if isinstance(raw_id, str) else (raw_id or uuid4())

    db_reading = Readings(
        sensor_id=sensor_id,
        temperature=buffer_data.get("temperature"),
        humidity=buffer_data.get("humidity"),
        light=buffer_data.get("light"),
        time=parsed_time,
        date=parsed_date,
        error_message=buffer_data.get("error_message"),
    )

    async with AsyncSessionLocal() as db:
        db.add(db_reading)
        await db.commit()
        await db.refresh(db_reading)

    return ReadingResponse.model_validate(db_reading).model_dump(mode="json")


# ==============================================================================
# WEBSOCKET ENDPOINT
# ==============================================================================
@router.websocket("/ws")
async def websocket_readings_endpoint(
    websocket: WebSocket,
    wait_seconds: float | None = Query(default=None),
):
    """
    WebSocket endpoint for real-time sensor readings ingestion and updates.

    - Receives sensor updates (temperature, humidity, light, time, date).
    - Buffers incoming values for `wait_seconds` (default BUFFER_WAIT_SECONDS = 10s)
      to allow slower sensors to arrive before saving to PostgreSQL.
    - Automatically flushes and saves the consolidated record to the database.
    - Broadcasts saved readings to all connected clients.
    """
    await manager.connect(websocket)

    delay = wait_seconds if wait_seconds is not None else BUFFER_WAIT_SECONDS
    buffer: dict[str, Any] = {}
    buffer_lock = asyncio.Lock()
    flush_task: asyncio.Task | None = None

    async def flush_after_delay():
        try:
            await asyncio.sleep(delay)
            async with buffer_lock:
                if not buffer:
                    return
                data_to_save = dict(buffer)
                buffer.clear()

            saved_record = await save_buffered_reading(data_to_save)

            # Notify sender
            await manager.send_personal_message(
                {
                    "event": "saved",
                    "status": "success",
                    "message": f"Reading successfully saved to database after {delay}s buffer window.",
                    "data": saved_record,
                },
                websocket,
            )

            # Broadcast new reading to all active clients (e.g. web dashboards)
            await manager.broadcast(
                {
                    "event": "new_reading",
                    "data": saved_record,
                }
            )
        except asyncio.CancelledError:
            pass
        except Exception as e:
            await manager.send_personal_message(
                {"event": "error", "status": "failed", "message": str(e)},
                websocket,
            )

    try:
        while True:
            raw_data = await websocket.receive_text()
            try:
                payload = json.loads(raw_data)
            except json.JSONDecodeError:
                await manager.send_personal_message(
                    {"event": "error", "message": "Invalid JSON format received."},
                    websocket,
                )
                continue

            # Support direct payload or nested {"data": {...}}
            reading_data = payload.get("data", payload) if isinstance(payload, dict) else {}

            async with buffer_lock:
                # Merge incoming sensor fields
                for field in ("sensor_id", "temperature", "humidity", "light", "time", "date", "error_message"):
                    if field in reading_data and reading_data[field] is not None:
                        buffer[field] = reading_data[field]

                # Start flush timer if not already running
                if flush_task is None or flush_task.done():
                    flush_task = asyncio.create_task(flush_after_delay())

            # Immediate acknowledgment that data is buffered and waiting
            clean_buffer = {
                k: (str(v) if isinstance(v, UUID) else v)
                for k, v in buffer.items()
            }
            await manager.send_personal_message(
                {
                    "event": "buffered",
                    "status": "waiting",
                    "message": f"Reading received and buffered. Waiting {delay}s before saving to database.",
                    "current_buffer": clean_buffer,
                },
                websocket,
            )

    except WebSocketDisconnect:
        # Flush any remaining buffered data immediately when client disconnects
        if flush_task and not flush_task.done():
            flush_task.cancel()
        async with buffer_lock:
            if buffer:
                try:
                    await save_buffered_reading(buffer)
                except Exception:
                    pass
        manager.disconnect(websocket)


# ==============================================================================
# HTTP REST ENDPOINTS (KEPT FOR TESTING / REST QUERIES)
# ==============================================================================
@router.post("/createReading", response_model=ReadingResponse, status_code=status.HTTP_201_CREATED)
async def createReading(reading: ReadingRequest, db: AsyncSession = Depends(get_db)):
    parsed_date = parse_date(reading.date)
    parsed_time = parse_time(reading.time, base_date=parsed_date.date())

    db_reading = Readings(
        sensor_id=reading.sensor_id or uuid4(),
        temperature=reading.temperature,
        humidity=reading.humidity,
        light=reading.light,
        time=parsed_time,
        date=parsed_date,
        error_message=reading.error_message,
    )
    db.add(db_reading)
    await db.commit()
    await db.refresh(db_reading)
    return db_reading


@router.get("/getAllReadings", response_model=list[ReadingResponse])
async def getAllReadings(db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(Readings).order_by(Readings.date.desc(), Readings.time.desc()))
    return result.scalars().all()


@router.get("/getReading/{id}", response_model=ReadingResponse)
async def getReading(id: UUID, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(Readings).where(Readings.sensor_id == id))
    db_reading = result.scalar_one_or_none()
    if not db_reading:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=f"Reading with id '{id}' not found")
    return db_reading


@router.put("/updateReading/{id}", response_model=ReadingResponse)
async def updateReading(id: UUID, reading: ReadingUpdateRequest, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(Readings).where(Readings.sensor_id == id))
    db_reading = result.scalar_one_or_none()
    if not db_reading:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=f"Reading with id '{id}' not found")

    update_data = reading.model_dump(exclude_unset=True)
    ref_date = db_reading.date.date() if db_reading.date else dt_date.today()

    if "date" in update_data and update_data["date"] is not None:
        db_reading.date = parse_date(update_data["date"])
        ref_date = db_reading.date.date()

    if "time" in update_data and update_data["time"] is not None:
        db_reading.time = parse_time(update_data["time"], base_date=ref_date)

    for field, value in update_data.items():
        if field not in ("time", "date"):
            setattr(db_reading, field, value)

    await db.commit()
    await db.refresh(db_reading)
    return db_reading


@router.delete("/deleteReading/{id}", status_code=status.HTTP_200_OK)
async def deleteReading(id: UUID, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(Readings).where(Readings.sensor_id == id))
    db_reading = result.scalar_one_or_none()
    if not db_reading:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=f"Reading with id '{id}' not found")

    await db.delete(db_reading)
    await db.commit()
    return {"message": "Reading deleted successfully", "sensor_id": str(id)}