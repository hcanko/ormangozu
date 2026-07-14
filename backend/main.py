import asyncio
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
import uvicorn

from models.database import engine, Base, SessionLocal
from models import orm
from api.routers import router, manager, generate_dashboard_data

Base.metadata.create_all(bind=engine)

app = FastAPI(title="OrmanGozu API", version="1.0.0", description="Termal Sensör Füzyonu ve Nirengi Sistemi")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"], 
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(router, prefix="/api")

# ==========================================
# YENİ: ARKA PLAN YAYIN MOTORU
# ==========================================
async def broadcast_task():
    while True:
        db = SessionLocal()
        try:
            # 1. Verileri topla ve analiz et
            data = await generate_dashboard_data(db)
            # 2. Tünele bağlı tüm kullanıcılara fırlat!
            await manager.broadcast(data)
        except Exception as e:
            print(f"Yayın Hatası: {e}")
        finally:
            db.close()
            
        # Sistem her 2 saniyede bir kendi kendini yenileyecek (hızlandırılabilir)
        await asyncio.sleep(2) 

@app.on_event("startup")
async def startup_event():
    # Sunucu başlarken yayın motorunu arka planda ateşle
    asyncio.create_task(broadcast_task())

if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)