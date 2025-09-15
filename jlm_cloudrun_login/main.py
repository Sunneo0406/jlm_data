import os
import mysql.connector
from fastapi import FastAPI, HTTPException, Query, Depends, status, Request
from datetime import datetime, timedelta, timezone
from fastapi.middleware.cors import CORSMiddleware
from typing import List
from mysql.connector import pooling
from dateutil.parser import isoparse
import pytz
from pydantic import BaseModel
from typing import Optional
from passlib.context import CryptContext
import secrets
import string
import hashlib
from fastapi.security import OAuth2PasswordBearer, OAuth2PasswordRequestForm
from jose import JWTError, jwt
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse, RedirectResponse

# 建立 FastAPI 應用程式實例
app = FastAPI()

# 掛載靜態檔案目錄，處理前端網頁、CSS、JS等靜態資源
# 當使用者訪問 /static/ 時，會從 'static' 資料夾中尋找對應的檔案
app.mount("/static", StaticFiles(directory="static"), name="static")

# 集中管理所有合法的資料表名稱
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

# 密碼雜湊設定，使用 bcrypt 演算法
pwd_context = CryptContext(schemes=["bcrypt"], deprecated="auto")

# JWT 認證設定
SECRET_KEY = os.environ.get("JWT_SECRET_KEY")
if not SECRET_KEY:
    raise ValueError("JWT_SECRET_KEY environment variable not set. Please set a strong, random key.")
ALGORITHM = "HS256"
ACCESS_TOKEN_EXPIRE_MINUTES = 30

# OAuth2 設定，定義了 JWT Token 的獲取路徑
oauth2_scheme = OAuth2PasswordBearer(tokenUrl="/api/login")

# 跨網域設定
# 這裡允許所有來源進行 CORS 請求，在實際部署時應限制為特定網域
origins = [
    "http://localhost",
    "http://localhost:8000",
    "http://127.0.0.1",
    "http://127.0.0.1:5500",
    "file://",
    "*"
]

app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 建立資料庫連線池 (省略，與您原有的程式碼相同)
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

# 資料庫連線函式
def get_db_connection_from_pool():
    if db_connection_pool is None:
        raise HTTPException(status_code=500, detail="資料庫連線池未初始化。")
    try:
        return db_connection_pool.get_connection()
    except Exception as e:
        print(f"從連線池取得連線失敗: {e}")
        raise HTTPException(status_code=500, detail="無法從資料庫連線池取得連線。")

# Helper Functions
def get_password_hash(password):
    return pwd_context.hash(password)

def verify_password(plain_password, hashed_password):
    return pwd_context.verify(plain_password, hashed_password)

def create_access_token(data: dict):
    # 建立一個包含過期時間的 Token
    to_encode = data.copy()
    expire = datetime.utcnow() + timedelta(minutes=ACCESS_TOKEN_EXPIRE_MINUTES)
    to_encode.update({"exp": expire})
    encoded_jwt = jwt.encode(to_encode, SECRET_KEY, algorithm=ALGORITHM)
    return encoded_jwt

def generate_random_token():
    return ''.join(secrets.choice(string.ascii_letters + string.digits) for _ in range(64))

# JWT 核心驗證函式
def get_current_user(token: str = Depends(oauth2_scheme)):
    """
    此函式由 FastAPI 自動調用，從請求的 Authorization Header 中解析並驗證 JWT Token。
    如果 Token 無效或過期，它會自動拋出 HTTPException，並回傳 401 Unauthorized 狀態碼。
    """
    credentials_exception = HTTPException(
        status_code=status.HTTP_401_UNAUTHORIZED,
        detail="Could not validate credentials",
        headers={"WWW-Authenticate": "Bearer"},
    )
    try:
        # 解碼 Token，如果解碼失敗 (例如簽名無效)，會拋出 JWTError
        payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
        user_id: str = payload.get("sub")
        if user_id is None:
            raise credentials_exception
    except JWTError:
        raise credentials_exception
    return user_id

# Pydantic Models (省略，與您原有的程式碼相同)
# 請將此段程式碼新增到 main.py 的 Pydantic Models 區塊
class SensorData(BaseModel):
    table_name: str
    voltage: float
    current: float
    frequency: float
    pf: float
    watt: float
    total_watt_hours: float

class UserCreate(BaseModel):
    employee_name: str
    account: str
    password: str

class UserLogin(BaseModel):
    account: str
    password: str

class PasswordResetRequest(BaseModel):
    account: str

class PasswordReset(BaseModel):
    token: str
    new_password: str

# API Routes
class User(BaseModel):
    id: int
    employee_name: str
    account: str

# API Route to get current user info
@app.get("/api/users/me", response_model=User)
def read_users_me(user_id: int = Depends(get_current_user)):
    """
    根據 JWT Token 獲取當前登入者的資訊。
    """
    conn = None
    cursor = None
    try:
        conn = get_db_connection_from_pool()
        cursor = conn.cursor(dictionary=True)
        cursor.execute("SELECT id, employee_name, account FROM employees WHERE id = %s", (user_id,))
        user = cursor.fetchone()
        if user is None:
            raise HTTPException(status_code=404, detail="找不到使用者")
        return user
    except Exception as e:
        print(f"獲取使用者資訊時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="無法獲取使用者資訊")
    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()

@app.post("/api/register")
def register_user(user: UserCreate):
    """
    建立新會員帳號。
    """
    conn = None
    cursor = None
    try:
        conn = get_db_connection_from_pool()
        cursor = conn.cursor()
        cursor.execute("SELECT id FROM employees WHERE account = %s", (user.account,))
        if cursor.fetchone():
            raise HTTPException(status_code=400, detail="此帳號已存在")
        salt = secrets.token_hex(16)
        password_hash = get_password_hash(user.password + salt)
        query = "INSERT INTO employees (employee_name, account, password_hash, salt, created_at) VALUES (%s, %s, %s, %s, NOW())"
        values = (user.employee_name, user.account, password_hash, salt)
        cursor.execute(query, values)
        conn.commit()
        return {"message": "會員註冊成功"}
    except HTTPException as e:
        raise e
    except Exception as e:
        print(f"註冊時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="註冊失敗，請稍後再試")
    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()

@app.post("/api/login")
def login_for_access_token(user_data: UserLogin):
    """
    驗證使用者帳號和密碼。若成功則回傳 JWT token。
    此路由不再設定 session，因為我們改用 JWT 認證。
    """
    conn = None
    cursor = None
    try:
        conn = get_db_connection_from_pool()
        cursor = conn.cursor(dictionary=True)
        cursor.execute("SELECT id, password_hash, salt FROM employees WHERE account = %s", (user_data.account,))
        user = cursor.fetchone()
        if not user:
            raise HTTPException(status_code=400, detail="帳號或密碼不正確")
        password_with_salt = user_data.password + user['salt']
        if not verify_password(password_with_salt, user['password_hash']):
            raise HTTPException(status_code=400, detail="帳號或密碼不正確")
        
        # 建立 JWT Token，其中 'sub' (subject) 欄位通常用來存放使用者 ID
        access_token = create_access_token(data={"sub": str(user['id'])})
        
        # 直接回傳 Token，前端會將其儲存並在後續請求中帶上
        return {"access_token": access_token, "token_type": "bearer"}
    except HTTPException as e:
        raise e
    except Exception as e:
        print(f"登入時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="登入失敗，請稍後再試")
    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()

@app.post("/api/forgot_password")
def forgot_password(reset_request: PasswordResetRequest):
    """
    處理忘記密碼請求 (省略，與您原有的程式碼相同)
    """
    conn = None
    cursor = None
    try:
        conn = get_db_connection_from_pool()
        cursor = conn.cursor(dictionary=True)
        cursor.execute("SELECT id FROM employees WHERE account = %s", (reset_request.account,))
        user = cursor.fetchone()
        if not user:
            raise HTTPException(status_code=404, detail="找不到此帳號")
        employee_id = user['id']
        token = generate_random_token()
        expires_at = datetime.utcnow() + timedelta(hours=1)
        cursor.execute("DELETE FROM password_resets WHERE employee_id = %s", (employee_id,))
        query = "INSERT INTO password_resets (employee_id, token, expires_at) VALUES (%s, %s, %s)"
        cursor.execute(query, (employee_id, token, expires_at))
        conn.commit()
        print(f"請使用此 token 重設密碼: {token}")
        return {"message": "密碼重設連結已發送 (請查看控制台的 token)。", "token": token}
    except HTTPException as e:
        raise e
    except Exception as e:
        print(f"忘記密碼時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="處理請求時發生錯誤")
    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()

@app.post("/api/reset_password")
def reset_password(reset_data: PasswordReset):
    """
    使用 token 驗證並重設密碼 (省略，與您原有的程式碼相同)
    """
    conn = None
    cursor = None
    try:
        conn = get_db_connection_from_pool()
        cursor = conn.cursor(dictionary=True)
        cursor.execute("SELECT employee_id FROM password_resets WHERE token = %s AND expires_at > NOW()", (reset_data.token,))
        result = cursor.fetchone()
        if not result:
            raise HTTPException(status_code=400, detail="無效或過期的 token")
        employee_id = result['employee_id']
        salt = secrets.token_hex(16)
        password_hash = get_password_hash(reset_data.new_password + salt)
        update_query = "UPDATE employees SET password_hash = %s, salt = %s WHERE id = %s"
        cursor.execute(update_query, (password_hash, salt, employee_id))
        delete_query = "DELETE FROM password_resets WHERE employee_id = %s"
        cursor.execute(delete_query, (employee_id,))
        conn.commit()
        return {"message": "密碼重設成功"}
    except HTTPException as e:
        raise e
    except Exception as e:
        print(f"重設密碼時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="重設密碼失敗")
    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()

# 這是一個受保護的路由，需要 JWT Token 才能存取。
# Depends(get_current_user) 會自動從請求中驗證 Token，並將解析後的使用者 ID 傳入函式。
@app.get("/api/protected")
def protected_route(user_id: int = Depends(get_current_user)):
    """
    一個需要認證才能存取的保護路由。
    """
    return {"message": f"Hello, user {user_id}! 你已成功通過認證。"}

# 重導向到登入頁面
@app.get("/", include_in_schema=False)
def redirect_to_login():
    return RedirectResponse(url="/static/login.html")

# 以下為原本的程式碼，請依序複製貼上
# 修正後的 get_total_watt_hours_difference 函式
def get_total_watt_hours_difference(start_time, end_time, table_name):
    """
    計算指定時間區間內 'total_watt_hours' 的差值。
    此版本接收帶有時區的 datetime 物件，並將其格式化為字串進行查詢。
    """
    conn = None
    cursor = None
    try:
        conn = get_db_connection_from_pool()
        cursor = conn.cursor(dictionary=True)
        
        # --- 修正處：將傳入的 datetime 物件格式化為查詢字串 ---
        # 這樣可以確保傳遞給資料庫的格式永遠是正確的
        start_time_str = start_time.strftime('%Y-%m-%d %H:%M:%S')
        end_time_str = end_time.strftime('%Y-%m-%d %H:%M:%S')
        # --- 修正結束 ---

        # 取得開始時間區間的第一筆 'total_watt_hours'
        query_start = f"SELECT total_watt_hours FROM `{table_name}` WHERE timestamp >= %s ORDER BY timestamp ASC LIMIT 1"
        cursor.execute(query_start, (start_time_str,))
        start_watt_hours = cursor.fetchone()

        # 取得結束時間區間的最後一筆 'total_watt_hours'
        query_end = f"SELECT total_watt_hours FROM `{table_name}` WHERE timestamp <= %s ORDER BY timestamp DESC LIMIT 1"
        cursor.execute(query_end, (end_time_str,))
        end_watt_hours = cursor.fetchone()
        
        if start_watt_hours and end_watt_hours:
            difference = end_watt_hours['total_watt_hours'] - start_watt_hours['total_watt_hours']
            return difference
        return None
    except Exception as e:
        print(f"計算瓦特小時差值時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="計算瓦特小時時發生錯誤")
    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()

# 新增 API 路由
@app.post("/api/upload_data")
def upload_sensor_data(data: SensorData):
    """
    接收 ESP32 上傳的感測器資料，並將其寫入指定的資料表。
    """
    # 確保傳入的資料表名稱是合法的，以避免 SQL 注入
    if data.table_name not in VALID_TABLES:
        raise HTTPException(status_code=400, detail=f"Invalid table name: {data.table_name}")
    
    conn = None
    cursor = None
    try:
        conn = get_db_connection_from_pool()
        cursor = conn.cursor()
        
        # 使用參數化查詢，防止 SQL 注入攻擊
        # 資料庫中的 timestamp 欄位應為 datetime 類型
        # FastAPI 後端負責處理 NOW() 的部分，不需要 ESP32 傳入
        query = f"""
            INSERT INTO `{data.table_name}` 
            (voltage, current, frequency, pf, watt, total_watt_hours, timestamp)
            VALUES (%s, %s, %s, %s, %s, %s, CONVERT_TZ(NOW(),'UTC','Asia/Taipei'))
        """
        
        # 參數化查詢的資料
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
        # 在終端機印出詳細錯誤訊息，方便除錯
        print(f"寫入資料庫時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="寫入資料庫失敗")
    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()



# API 路由
@app.get("/api/get_tables", response_model=List[str])
def get_tables(user_id: int = Depends(get_current_user)):
    """從資料庫回傳所有合法的資料表名稱列表。"""
    conn = None
    cursor = None
    try:
        conn = get_db_connection_from_pool()
        cursor = conn.cursor()
        cursor.execute("SHOW TABLES")
        tables = [table[0] for table in cursor.fetchall()]
        return tables
    except HTTPException as e:
        raise e
    except Exception as e:
        print(f"從資料庫取得資料表時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="無法從資料庫取得資料表清單")
    finally:
        if 'conn' in locals() and conn.is_connected():
            cursor.close()
            conn.close()

@app.get("/api/get_total_daily_kwh")
def get_total_daily_kwh(user_id: int = Depends(get_current_user)):
    """
    計算從今天早上 08:00:00 到現在所有合法資料表總用電量的總和。
    此函數會查詢每個資料表在 08:00:00 後的第一筆數據，並與最新一筆數據做差值計算，
    最後將所有差值加總，以顯示當日的總耗電量。
    """
    conn = None
    cursor = None
    total_kwh_sum = 0
    
    try:
        # 修正時區問題，明確設定為台灣時間
        # 由於伺服器通常使用 UTC，我們先取得 UTC 時間，然後設定為台灣時區 (+8)
        taipei_tz = pytz.timezone('Asia/Taipei')
        now_taipei = datetime.now(taipei_tz)
        
        # 設定台灣時間的早上 8:00
        start_time_today_taipei = now_taipei.replace(hour=8, minute=0, second=0, microsecond=0)
        
        # 確保當前台灣時間在早上 8:00 之後才進行計算
        if now_taipei < start_time_today_taipei:
            return {"total_kwh": 0}
            
        # 將台灣時間的 8:00 轉換為 UTC，並移除時區資訊，以匹配資料庫的時間戳記
        start_time_str = start_time_today_taipei.strftime('%Y-%m-%d %H:%M:%S')
            
        conn = get_db_connection_from_pool()
        cursor = conn.cursor(dictionary=True)
        
        for table_name in VALID_TABLES:
            # 查詢 08:00:00 後的第一筆數據 (當日開工數據)
            query_start = f"SELECT total_watt_hours FROM `{table_name}` WHERE timestamp >= %s ORDER BY timestamp ASC LIMIT 1"
            cursor.execute(query_start, (start_time_str,))
            result_start = cursor.fetchone()
            
            # 查詢最新一筆數據 (當前數據)
            query_latest = f"SELECT total_watt_hours FROM `{table_name}` ORDER BY timestamp DESC LIMIT 1"
            cursor.execute(query_latest)
            result_latest = cursor.fetchone()
            
            # 如果兩筆數據都存在，才進行差值計算
            if result_start and result_latest:
                start_kwh = result_start['total_watt_hours']
                latest_kwh = result_latest['total_watt_hours']
                
                # 確保最新數據不小於起始數據，以避免電表重置等問題
                if latest_kwh >= start_kwh:
                    total_kwh_sum += (latest_kwh - start_kwh)
                else:
                    print(f"警告：資料表 '{table_name}' 的最新數據 ({latest_kwh}) 小於起始數據 ({start_kwh})，已忽略此差值。")
            else:
                # 如果缺少任何一筆數據，表示今日無足夠資料進行計算
                print(f"資訊：資料表 '{table_name}' 今日沒有足夠數據可供計算。")

        return {"total_kwh": total_kwh_sum}
    except HTTPException as e:
        raise e
    except Exception as e:
        print(f"取得所有表格每日總用電量時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="無法取得每日總用電量數據")
    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()


@app.get("/api/get_total_latest_kwh")
def get_total_latest_kwh(user_id: int = Depends(get_current_user)):
    """取得所有合法資料表最新一筆 'total_watt_hours' 的總和，不進行單位轉換。"""
    conn = None
    cursor = None
    total_kwh_sum = 0
    try:
        conn = get_db_connection_from_pool()
        cursor = conn.cursor(dictionary=True)
        for table_name in VALID_TABLES:
            query = f"SELECT total_watt_hours FROM `{table_name}` ORDER BY timestamp DESC LIMIT 1"
            cursor.execute(query)
            result = cursor.fetchone()
            if result and 'total_watt_hours' in result:
                total_kwh_sum += result['total_watt_hours']
        
        return {"total_kwh": total_kwh_sum} # 移除 / 1000
    except HTTPException as e:
        raise e
    except Exception as e:
        print(f"取得所有表格最新數據時發生錯誤: {e}")
        raise HTTPException(status_code=500, detail="無法取得總耗能數據")
    finally:
        if cursor:
            cursor.close()
        if conn and conn.is_connected():
            conn.close()

@app.get("/api/get_watt_hours/custom/{table_name}")
def get_custom_watt_hours(table_name: str, start_iso: str, end_iso: str, user_id: int = Depends(get_current_user)):
    if table_name not in VALID_TABLES:
        raise HTTPException(status_code=400, detail=f"Invalid table name: {table_name}")
        
    try:
        # --- 修正處：將前端傳來的 UTC 時間轉換為台灣時間 ---
        taiwan_tz = pytz.timezone('Asia/Taipei')
        start_time_tw = isoparse(start_iso).astimezone(taiwan_tz)
        end_time_tw = isoparse(end_iso).astimezone(taiwan_tz)
        # --- 修正結束 ---

    except ValueError:
        raise HTTPException(status_code=400, detail="時間格式不正確，請使用 ISO 8601 格式。")
        
    # 將轉換後的台灣時間傳遞給底層函式
    kwh = get_total_watt_hours_difference(start_time_tw, end_time_tw, table_name)
    
    if kwh is None:
        raise HTTPException(status_code=404, detail="找不到指定時間範圍內的資料")
        
    return {"kilo_watt_hours": kwh}

@app.get("/api/get_chart_data")
async def get_chart_data(table_name: str, start_iso: str, end_iso: str, user_id: int = Depends(get_current_user)):
    """
    根據時間範圍和資料表名稱，取得即時用電數據。
    (此版本包含偵錯用的 print 語句)
    """
    if table_name not in VALID_TABLES:
        raise HTTPException(status_code=400, detail=f"Invalid table name: {table_name}")

    # ==================== DEBUG START ====================
    print("\n--- [DEBUG] Entering get_chart_data ---")
    print(f"1. Raw ISO strings received from frontend:")
    print(f"   start_iso: {start_iso}")
    print(f"   end_iso:   {end_iso}")
    # ====================  DEBUG END  ====================

    db_connection = get_db_connection_from_pool()
    cursor = db_connection.cursor(dictionary=True)

    try:
        taiwan_tz = pytz.timezone('Asia/Taipei')

        start_time_utc = isoparse(start_iso)
        end_time_utc = isoparse(end_iso)

        start_time_tw = start_time_utc.astimezone(taiwan_tz)
        end_time_tw = end_time_utc.astimezone(taiwan_tz)

        # ==================== DEBUG START ====================
        print(f"2. Converted to Taiwan Time (datetime objects):")
        print(f"   start_time_tw: {start_time_tw}")
        print(f"   end_time_tw:   {end_time_tw}")
        # ====================  DEBUG END  ====================

        start_time_str = start_time_tw.strftime('%Y-%m-%d %H:%M:%S')
        end_time_str = end_time_tw.strftime('%Y-%m-%d %H:%M:%S')

        # ==================== DEBUG START ====================
        print(f"3. Final strings for SQL query:")
        print(f"   start_time_str: '{start_time_str}'")
        print(f"   end_time_str:   '{end_time_str}'")
        print("-------------------------------------------\n")
        # ====================  DEBUG END  ====================

        query = f"SELECT timestamp, watt, total_watt_hours, pf FROM `{table_name}` WHERE timestamp BETWEEN %s AND %s ORDER BY timestamp"
        cursor.execute(query, (start_time_str, end_time_str))
        rows = cursor.fetchall()
        
        # (以下程式碼保持不變)
        formatted_data = []
        initial_watt_hours = None

        if rows:
            initial_watt_hours = rows[0]['total_watt_hours'] if rows[0]['total_watt_hours'] is not None else 0.0

            for row in rows:
                cumulative_watt_hours = 0.0
                if initial_watt_hours is not None and row['total_watt_hours'] is not None:
                    cumulative_watt_hours = row['total_watt_hours'] - initial_watt_hours

                formatted_timestamp = row['timestamp'].strftime('%Y-%m-%d %H:%M:%S')

                formatted_data.append({
                    'timestamp': formatted_timestamp,
                    'watt': round(row['watt'], 2) if row['watt'] is not None else 0.0,
                    'total_watt_hours': round(cumulative_watt_hours, 2),
                    'pf': round(row['pf'], 2) if row['pf'] is not None else 0.0
                })
        
        return {"data": formatted_data}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Database query error: {e}")
    finally:
        cursor.close()
        db_connection.close()

@app.get("/api/get_watt_hours/{shift_type}/{table_name}")
def get_shift_watt_hours(shift_type: str, table_name: str,user_id: int = Depends(get_current_user)):
    if table_name not in VALID_TABLES:
        raise HTTPException(status_code=400, detail=f"Invalid table name: {table_name}")
    taiwan_tz = pytz.timezone('Asia/Taipei')    
    now = datetime.now(taiwan_tz)    #使用台灣時間
    if shift_type == "day_shift":
        temp1_time = now.replace(hour=8, minute=0, second=0, microsecond=0) - timedelta(days=1)
        start_time = temp1_time.strftime('%Y-%m-%d %H:%M:%S')
        temp2_time = now.replace(hour=17, minute=0, second=0, microsecond=0) - timedelta(days=1)
        end_time = temp2_time.strftime('%Y-%m-%d %H:%M:%S')
    elif shift_type == "night_shift":
        temp1_time = now.replace(hour=19, minute=0, second=0, microsecond=0) - timedelta(days=1)
        start_time = temp1_time.strftime('%Y-%m-%d %H:%M:%S')
        temp2_time = now.replace(hour=5, minute=0, second=0, microsecond=0)
        end_time = temp2_time.strftime('%Y-%m-%d %H:%M:%S')
    elif shift_type == "since_morning":
        temp1_time = now.replace(hour=8, minute=0, second=0, microsecond=0)
        start_time = temp1_time.strftime('%Y-%m-%d %H:%M:%S')
        temp2_time = now.replace(microsecond=0)#將毫秒去掉
        end_time = temp2_time.strftime('%Y-%m-%d %H:%M:%S') #將時區08:00去掉
    else:
        raise HTTPException(status_code=400, detail="Invalid shift type")
    
    kwh = get_total_watt_hours_difference(start_time, end_time, table_name)
    
    if kwh is None:
        raise HTTPException(status_code=404, detail="找不到指定時間範圍內的資料")
    
    return {"kilo_watt_hours": kwh}