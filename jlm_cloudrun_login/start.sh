#!/bin/bash
# 建議的 start.sh 內容

# 1. 在背景啟動 Cloud SQL Auth Proxy
/usr/local/bin/cloud-sql-proxy --unix-socket=/cloudsql/${DB_CONNECTION_NAME} &

# 給 Proxy 一點時間啟動並建立連線
sleep 5

# 2. 啟動 Gunicorn 伺服器來管理 Uvicorn workers
#    -w 4: 指定 4 個 worker process 來處理請求 (可根據您的機器規格調整)
#    -k uvicorn.workers.UvicornWorker: 告訴 Gunicorn 使用 Uvicorn 的 worker
#    -b 0.0.0.0:8080: 綁定到容器的 8080 端口
gunicorn -w 4 -k uvicorn.workers.UvicornWorker -b 0.0.0.0:8080 main:app