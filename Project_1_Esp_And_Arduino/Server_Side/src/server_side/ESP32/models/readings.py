from src.server_side.ESP32.database  import Base
from sqlalchemy import Column, DateTime, Float, String, UUID
import uuid

class Readings(Base):
    __tablename__ = "readings"

    sensor_id = Column(UUID, primary_key=True, default=uuid.uuid4, index=True)
    temperature = Column(Float, nullable=True)
    humidity = Column(Float, nullable=True)
    light = Column(Float, nullable=True)
    time = Column(DateTime, nullable=False)
    date = Column(DateTime, nullable=False)
    error_message = Column(String, nullable=True)  
    