from dotenv import load_dotenv
from sqlalchemy.ext.asyncio import create_async_engine
import os

load_dotenv()

DATABASE_URL = os.getenv("DATABASE_URL")

engine = create_async_engine(DATABASE_URL)