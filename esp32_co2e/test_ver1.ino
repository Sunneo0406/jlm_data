/* 
 總表 (95型 100型) 用 
 */ 

 #include <ModbusMaster.h> 
 #include <HardwareSerial.h> 
 #include <WiFi.h> 
 #include <WebServer.h> 
 #include <HTTPClient.h> // 新增 HTTPClient 函式庫 
 #include <ArduinoJson.h> // 新增 ArduinoJson 函式庫 
 #include <WiFiClientSecure.h> 

 #define esp32device 1 

 // Static IP for ESP32 
 IPAddress local_IP(192,168,1,135);  //135-->A 136-->B 137-->C  
 IPAddress gateway(192,168,1,1); 
 IPAddress subnet(255,255,255,0); 
 // Connection details (不再使用這些資料庫連線資訊) 
 /* 
 IPAddress server_ip(35, 234, 19, 186); // Cloud SQL public IP 
 char user[] = "Henk"; 
 char db_password[] = "1234"; 
 char database[] = "esp32_data"; 
 int db_port = 3306; 
 char db[] = "esp32_data"; 
 WiFiClient client;  
 MySQL_Connection conn((Client *)&client); 
 */ 

 // PA330 Input Registers 
 #define Vln_a 0x1000 
 #define Vln_b 0x1002 
 #define Vln_c 0x1004 
 #define Vll_ab 0x1006 
 #define Vll_bc 0x1008 
 #define Vll_ca 0x100A 
 #define I_a 0x100C 
 #define I_b 0x100E 
 #define I_c 0x1010 
 #define Frequency 0x101C 
 #define PF_a 0x101E 
 #define PF_b 0x1020 
 #define PF_c 0x1022 
 #define P_a 0x1024 
 #define P_b 0x1026 
 #define P_c 0x1028 
 #define P_total 0x102A 
 #define Q_total 0x102C 
 #define S_total 0x102E 
 #define kWh_total 0x1030 
 #define Vln_avg 0x104C 
 #define Vll_avg 0x104E 
 #define I_avg 0x1050 
 #define PF_total 0x1052 
 #define RS485_ID1 1 
 #define Modbus_ID2 2 

 ModbusMaster node; 

 // 新增 Cloud Run API 服務位址 
 const char* host = "jlm-co2e-db-connect-221081318298.asia-east1.run.app"; 
 const char* api_path = "/api/upload_data"; 
 const int httpsPort = 443; 

 // 新增函式來處理 HTTP POST 請求 
 void sendToGCP(float voltage, float current, float frequency, float pf, float watt, float total_watt_hours, String tableName) { 
     WiFiClientSecure client; 
     client.setInsecure(); // 允許不安全的 HTTPS 連線，僅用於測試 
     HTTPClient http; 

     String url = "https://" + String(host) + String(api_path); 
     Serial.println("發送到 API: " + url); 

     StaticJsonDocument<256> doc; // 調整大小以適應您的 JSON 資料 
     doc["table_name"] = tableName; 
     doc["voltage"] = voltage; 
     doc["current"] = current; 
     doc["frequency"] = frequency; 
     doc["pf"] = pf; 
     doc["watt"] = watt; 
     doc["total_watt_hours"] = total_watt_hours; 

     String jsonString; 
     serializeJson(doc, jsonString); 

     if (http.begin(client, url)) { 
         http.addHeader("Content-Type", "application/json"); 
         int httpResponseCode = http.POST(jsonString); 

         if (httpResponseCode > 0) { 
             String response = http.getString(); 
             Serial.printf("[HTTP] POST... code: %d\n", httpResponseCode); 
             Serial.println(response); 
         } else { 
             Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str()); 
         } 
         http.end(); 
     } else { 
         Serial.println("[HTTP] Unable to connect"); 
     } 
 } 

 // 模擬隨機數據 
 float generateRandomData(float min, float max) { 
   return min + (float)random(0, 100) / 100.0 * (max - min); 
 } 

 void setup() { 
   Serial.begin(115200); 
   WiFi.mode(WIFI_STA); 
   //WiFi.config(local_IP, gateway, subnet); 
   // 您需要在這裡手動修改 SSID 和密碼 
   WiFi.begin("hinet-15_Plus", "034967658");  
   Serial.println("Connecting to WiFi..."); 
   while (WiFi.status() != WL_CONNECTED) { 
     delay(500); 
     Serial.print("."); 
   } 
   Serial.println("\nWiFi connected."); 
   Serial.print("IP Address: "); 
   Serial.println(WiFi.localIP()); 

   // 初始化 ModbusMaster 實例 
   Serial2.begin(9600, SERIAL_8N1, 16, 17); // GPIO16=RX2, GPIO17=TX2 
   node.begin(RS485_ID1, Serial2); 
    
   // 註解掉舊的資料庫連線程式碼 
   /* 
   Serial.println("Connecting to MySQL..."); 
   while (!conn.connect(server_ip, 3306, user, db_password) && WiFi.status() == WL_CONNECTED) { 
     delay(500); 
     Serial.print("."); 
   } 
   if (conn.connected()) { 
     Serial.println("Connected to MySQL."); 
   } else { 
     Serial.println("Failed to connect to MySQL."); 
   } 
   */ 
 } 

 // 用於記錄上一次發送數據的時間戳 
 unsigned long lastSendTime = 0; 
 const long interval = 20000; // 20 秒 

 void loop() { 
   unsigned long currentMillis = millis(); 

   // 每 20 秒發送一次數據 
   if (currentMillis - lastSendTime >= interval) { 
     lastSendTime = currentMillis; 

     // 模擬數據 
     float voltage = generateRandomData(210.0, 230.0);  // 模擬電壓 
     float current = generateRandomData(0.1, 10.0);      // 模擬電流 
     float frequency = generateRandomData(49.5, 50.5);    // 模擬頻率 
     float pf = generateRandomData(0.8, 1.0);             // 模擬功率因數 
     float watt = generateRandomData(100.0, 5000.0);      // 模擬瓦特 
     float total_watt_hours = generateRandomData(1000.0, 50000.0); // 模擬總瓦特小時 

     // 設置資料表名稱為 '攪拌機A' 
     String tableName = "攪拌機A"; 

     // 發送數據到 Cloud Run API 
     sendToGCP(voltage, current, frequency, pf, watt, total_watt_hours, tableName); 
   } 
    
   // 舊的 RS485 讀取和資料庫寫入程式碼 
   /* 
   float voltage1 = RS485_data(Vll_avg); 
   float current1 = RS485_data(I_avg); 
   float freq1 = RS485_data(Frequency); 
   float pf1 = RS485_data(PF_total); 
   float watt1 = RS485_data(P_total); 
   float total_w1 = RS485_data(kWh_total); 
   String tableName1 = "sensor_data1"; 
    
   // float voltage1 = generateRandomData(210.0, 230.0);  // Random voltage between 210-230V 
   // float current1 = generateRandomData(0.1, 10.0);  // Random current between 0.1-10A 
   // float freq1 = generateRandomData(49.5, 50.5);  // Random frequency between 49.5-50.5Hz 
   // float pf1 = generateRandomData(0.8, 1.0);           // Random power factor between 0.8-1.0 
   // float watt1 = generateRandomData(100.0, 5000.0);    // Random watt between 100-5000W 
   // float total_w1 = generateRandomData(1000.0, 50000.0); // Random total watt hours 

   // sendToGCP(voltage1, current1, freq1, pf1, watt1, total_w1, tableName1); 

 #if (esp32device==1) 
   node.begin(Modbus_ID2, Serial2); 
   float voltage2 = RS485_data(Vll_avg); 
   float current2 = RS485_data(I_avg); 
   float freq2 = RS485_data(Frequency); 
   float pf2 = RS485_data(PF); 
   float watt2 = RS485_data(kVA_tot); 
   float total_w2 = RS485_data(kVAh); 
   String tableName2 = "sensor_data2"; 

   // float voltage2 = generateRandomData(210.0, 230.0);  // Random voltage between 210-230V 
   // float current2 = generateRandomData(0.1, 10.0);     // Random current between 0.1-10A 
   // float freq2 = generateRandomData(49.5, 50.5);       // Random frequency between 49.5-50.5Hz 
   // float pf2 = generateRandomData(0.8, 1.0);           // Random power factor between 0.8-1.0 
   // float watt2 = generateRandomData(100.0, 5000.0);    // Random watt between 100-5000W 
   // float total_w2 = generateRandomData(1000.0, 50000.0); // Random total watt hours 

   // sendToGCP(voltage2, current2, freq2, pf2, watt2, total_w2, tableName2); 
 #endif 
    
   if (WiFi.status() == WL_CONNECTED) { 
     if (!conn.connected()) { 
       Serial.println("Reconnecting to MySQL..."); 
       if (!conn.connect(server_ip, db_port, user, db_password)) { 
         Serial.println("Reconnection failed."); 
         delay(5000); 
         return; 
       } 
     } 
      
     MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn); 
     // 寫入資料表 sensor_data1 
     char insert_sql[200]; 
     sprintf(insert_sql, "INSERT INTO `sensor_data1` (voltage, current, frequency, pf, watt, total_watt_hours, timestamp) VALUES (%f, %f, %f, %f, %f, %f, CONVERT_TZ(NOW(),'UTC','Asia/Taipei'))",  
     voltage1, current1, freq1, pf1, watt1, total_w1); 
     cur_mem->execute(insert_sql); 
      
 #if (esp32device==1) 
     // 寫入資料表 sensor_data2 
     sprintf(insert_sql, "INSERT INTO `sensor_data2` (voltage, current, frequency, pf, watt, total_watt_hours, timestamp) VALUES (%f, %f, %f, %f, %f, %f, CONVERT_TZ(NOW(),'UTC','Asia/Taipei'))",  
     voltage2, current2, freq2, pf2, watt2, total_w2); 
     cur_mem->execute(insert_sql); 
 #endif 
      
     delete cur_mem; 
     Serial.println("Data inserted."); 
   } 
   delay(1000); 
   */ 
 }
