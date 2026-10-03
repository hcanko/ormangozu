import asyncio
import os
from contextlib import asynccontextmanager, suppress

import uvicorn
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from api.routers import generate_dashboard_data, manager, router
from api.mesh_events import router as mesh_router
from api.control_api import router as control_router
from models import orm
from models.database import Base, SessionLocal, engine, migrate_sqlite_schema
from models.orm import SystemSettingsDB
from services.sensor_fusion import apply_calibration


async def broadcast_task() -> None:
    while True:
        db = SessionLocal()
        try:
            data = await generate_dashboard_data(db)
            await manager.broadcast(data)
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            print(f"Yayın Hatası: {exc}")
        finally:
            db.close()
        await asyncio.sleep(2)


def load_persisted_calibration() -> None:
    db = SessionLocal()
    try:
        row = db.query(SystemSettingsDB).filter(SystemSettingsDB.id == 1).first()
        if row is not None:
            apply_calibration({
                "weight_delta_t": row.weight_delta_t,
                "weight_temp": row.weight_temp,
                "weight_gas": row.weight_gas,
                "override_temp": row.override_temp,
            })
    finally:
        db.close()


@asynccontextmanager
async def lifespan(app: FastAPI):
    del app
    Base.metadata.create_all(bind=engine)
    migrate_sqlite_schema()
    load_persisted_calibration()
    task = asyncio.create_task(broadcast_task(), name="dashboard-broadcast")
    try:
        yield
    finally:
        task.cancel()
        with suppress(asyncio.CancelledError):
            await task


app = FastAPI(
    title="OrmanGozu API",
    version="0.7.0-pilot",
    description="Offline saha kayıtları, sensör füzyonu ve canlı dashboard API'si",
    lifespan=lifespan,
    docs_url="/docs" if os.getenv("OG_ENABLE_API_DOCS", "0") == "1" else None,
    redoc_url=None,
    openapi_url="/openapi.json" if os.getenv("OG_ENABLE_API_DOCS", "0") == "1" else None,
)

cors_env = os.getenv(
    "CORS_ORIGINS",
    "http://localhost:5173,http://127.0.0.1:5173,http://localhost:3000",
)
origins = [origin.strip() for origin in cors_env.split(",") if origin.strip()]
app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=False,
    allow_methods=["GET", "POST", "DELETE"],
    allow_headers=["Content-Type", "X-Client-Token"],
)

@app.middleware("http")
async def private_api_headers(request, call_next):
    response = await call_next(request)
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["Referrer-Policy"] = "no-referrer"
    if request.url.path.startswith("/api"):
        response.headers["Cache-Control"] = "no-store"
    return response


app.include_router(router, prefix="/api")
app.include_router(mesh_router, prefix="/api")
app.include_router(control_router, prefix="/api")


if __name__ == "__main__":
    uvicorn.run("main:app", host="127.0.0.1", port=8000, reload=True)
