import asyncio
from contextlib import asynccontextmanager
from fastapi import FastAPI
from src.server_side.ESP32.database import Base, engine
from src.server_side.ESP32.models import readings  # noqa: F401 (registers model into Base.metadata)
from src.server_side.ESP32.readings.routers import router as readings_router
from fastapi.middleware.cors import CORSMiddleware

@asynccontextmanager
async def lifespan(app: FastAPI):
    # Create tables on startup
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    yield

app = FastAPI(lifespan=lifespan)
app.include_router(readings_router)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"], # Khi đã có tên miền thực tế, hãy thay "*" bằng tên miền của bạn (vd: "https://tenmiencuaban.com")
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/")
def root():
    return {"message": "Hello, ESP32!"}