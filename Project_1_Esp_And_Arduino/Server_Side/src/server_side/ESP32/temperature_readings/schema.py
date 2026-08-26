from pydantic import BaseModel

class TemperatureReadings(BaseModel):
    Temperature_Level : int
    Sensor_Id : str