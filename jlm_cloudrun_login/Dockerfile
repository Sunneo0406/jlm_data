# ---- Stage 1: Build the application ----
FROM python:3.10-slim as builder

WORKDIR /app

# 安裝必要的系統工具
RUN apt-get update && apt-get install -y curl && rm -rf /var/lib/apt/lists/*

# 下載並設定 Cloud SQL Auth Proxy
RUN curl -o /usr/local/bin/cloud-sql-proxy https://storage.googleapis.com/cloud-sql-proxy/v2.8.2/cloud-sql-proxy.linux.amd64 && \
    chmod +x /usr/local/bin/cloud-sql-proxy

# 安裝 Python 依賴套件
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# ---- Stage 2: Create the final production image ----
FROM python:3.10-slim

WORKDIR /app

# 從 builder stage 複製所有安裝的套件和執行檔
COPY --from=builder /usr/local /usr/local

# 複製應用程式程式碼
COPY . .

# 定義服務監聽的通訊埠
EXPOSE 8080

# 複製外部的 start.sh 腳本到容器中並賦予權限
COPY start.sh /app/start.sh
RUN chmod +x /app/start.sh

# 執行啟動腳本
CMD ["/app/start.sh"]