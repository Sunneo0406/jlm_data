# 使用 Python 3.10 作為基礎映像檔
FROM python:3.10-slim

# 設定工作目錄
WORKDIR /app

# 將 requirements.txt 複製到容器中並安裝依賴套件
COPY requirements.txt .
RUN pip install -r requirements.txt --no-cache-dir

# 將所有專案檔案複製到容器中
COPY . .

# 定義服務監聽的通訊埠，增加可讀性
EXPOSE 8080

# 啟動 Uvicorn 服務
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8080"]