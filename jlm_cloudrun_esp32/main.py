import os
import mysql.connector
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from mysql.connector import pooling
from pydantic import BaseModel

# --- 1. 保留 FastAPI 應用程式實例 ---
app = FastAPI()

# --- 2. 保留合法的資料表名稱列表 (用於驗證) ---
VALID_TABLES = [
    '120型',
    'sensor_data1',
    'sensor_data2',
    '冰水機',
    '空壓機',
    '破碎機(220V)',
    '雕刻機',
    '攪拌機A'
]

# --- 3. 保留跨網域設定 ---
# 雖然只供 ESP32 使用，但保留 CORS 以便未來可能的 web-based 測試
origins = ["*"]  # 為了方便，允許所有來源

app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# --- 4. 保留資料庫連線池 ---
try:
    db_user = os.environ.get("DB_USER")
    db_password = os.environ.get("DB_PASSWORD")
    db_name = os.environ.get("DB_NAME")
    db_connection_name = os.environ.get("DB_CONNECTION_NAME")
    unix_socket_path = f"/cloudsql/{db_connection_name}"

    if not all([db_user, db_password, db_name, db_connection_name]):
        raise ValueError("Missing one or more database environment variables.")

    db_connection_pool = pooling.MySQLConnectionPool(
        pool_name="db_pool",
        pool_size=10,
        user=db_user,
        password=db_password,
        database=db_name,
        unix_socket=unix_socket_path
    )
    print("資料庫連線池已成功建立。")
except Exception as e:
    print(f"資料庫連線池建立失敗: {e}")
    db_connection_pool = None

def get_db_connection_from_pool():
    """從連線池中取得一個連線."""
    if db_connection_pool is None:
        raise HTTPException(status_code=500, detail="資料庫連線池未初始化。")
    try:
        return db_connection_pool.get_connection()
    except Exception as e:
        print(f"從連線池取得連線失敗: {e}")
        raise HTTPException(status_code=500, detail="無法從資料庫連線池取得連線。")

# --- 5. 保留 ESP32 上傳資料的數據模型 ---
class SensorData(BaseModel):
    table_name: str
    voltage: float
    current: float
    frequency: float
    pf: float
    watt: float
    total_watt_hours: float

# --- 6. 保留唯一需要的 API 路由：ESP32 上傳資料 ---
@app.post("/api/upload_data")
def upload_sensor_data(data: SensorData):
    """
    接收 ESP32 上傳的感測器資料，並將其寫入指定的資料表。
    """
    if data.table_name not in VALID_TABLES:
        raise HTTPException(status_code=400, detail=f"Invalid table name: {data.table_name}")
    
    conn = None
    cursor = None
    try:
        conn = get_db_connection_from_pool()
        cursor = conn.cursor()
        
        query = f"""
            INSERT INTO `{data.table_name}` 
            (voltage, current, frequency, pf, watt, total_watt_hours, timestamp)
            VALUES (%s, %s, %s, %s, %s, %s, CONVERT_TZ(NOW(),'UTC','Asia/Taipei'))
        """
        
        values = (
            data.voltage, 
            data.current, 
            data.frequency, 
            data.pf, 
            data.watt, 
            data.total_watt_hours
        )
        
        cursor.execute(query, values)
        conn.commit()
        
        return {"message": f"Data successfully inserted into {data.table_name}"}
        
    except Exception as e:
        print(f"寫入資料庫時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="寫入資料庫失敗")
    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()

# --- 已移除的功能 ---
# - get_tables
# - get_total_daily_kwh
# - get_total_latest_kwh
# - get_custom_watt_hours
# - get_chart_data
# - get_shift_watt_hours
# - 以及它們的輔助函式 get_total_watt_hours_difference