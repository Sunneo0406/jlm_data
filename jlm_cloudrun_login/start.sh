#!/bin/bash
# 在背景啟動 Cloud SQL Auth Proxy
/usr/local/bin/cloud-sql-proxy --unix-socket=/cloudsql/${DB_CONNECTION_NAME} &

# 等待幾秒鐘讓 Proxy 建立連線
sleep 5

# 啟動 FastAPI 應用程式
uvicorn main:app --host 0.0.0.0 --port 8080