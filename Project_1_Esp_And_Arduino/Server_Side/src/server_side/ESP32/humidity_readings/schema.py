from pydantic import BaseModel

class HumidityReading(BaseModel):
    Humidity_Level : int
    Sensor_Id : str