/*
 集電箱1、集電箱2用
 http://esp32A.local 更新網頁
 
 程式功能:
 1. 喚醒後立即讀取 Modbus 設備資料並上傳到 Google Cloud。
 2. 保持清醒 5 分鐘，以允許使用者進行 OTA (Over-the-Air) 韌體更新。
 3. 在這 5 分鐘內，也允許透過網頁指令手動重置 RS485 設備。
 4. 5 分鐘後，再次讀取並上傳資料。
 5. 最後，進入 5 分鐘的深度睡眠以節省電力，然後循環以上步驟。
 6.在rs485()增加3次驗證資料，以確保資料正確性
 7.在send_data()增加離線驗證，驗證電表是否關機
*/

#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <esp_sleep.h>

#define esp32device 1

//--------------------- 網路與睡眠設定 ---------------------
// OTA 等待時間：5 分鐘
const long OTA_TIMEOUT_MS = 5 * 60 * 1000;
// 深度睡眠時間：5 分鐘
const long DEEP_SLEEP_SEC = 5 * 60;

// Cloud Run API 詳細資訊
const char* host = "jlm-co2e-db-connect-221081318298.asia-east1.run.app";
const char* api_path = "/api/upload_data";
const int httpsPort = 443;

// WiFi 憑證
const char* ssid ="hinet-15_Plus";
const char* password = "034967658";

//--------------------- Modbus 暫存器定義 (已完整) ---------------------
#define Vln_a 0x1000
#define Vln_b 0x1002
#define Vln_c 0x1004
#define Vln_avg 0x1006
#define Vll_ab 0x1008
#define Vll_bc 0x100A
#define Vll_ca 0x100C
#define Vll_avg 0x100E
#define I_a 0x1010
#define I_b 0x1012
#define I_c 0x1014
#define I_avg 0x1016
#define Frequency 0x1018

#define kW_a 0x101A
#define kW_b 0x101C
#define kW_c 0x101E
#define kW_tot 0x1020
#define kvar_a 0x1022
#define kvar_b 0x1024
#define kvar_c 0x1026
#define kvar_tot 0x1028
#define kVA_a 0x102A
#define kVA_b 0x102C
#define kVA_c 0x102E
#define kVA_tot 0x1030
#define PF 0x1032

#define kWh 0x1034
#define kvarh 0x1036
#define kVAh 0x1038

#define RX 16
#define TX 17
#define Modbus_ID1 15 
#define Modbus_ID2 16

//--------------------- 全域變數與物件 ---------------------
volatile bool my_flag = 0;
volatile uint8_t status = 0;
WebServer server(80);
ModbusMaster node;

//--------------------- 網頁介面 HTML ---------------------
const char* loginIndex =
"<form name='loginForm'>"
"<table width='20%' bgcolor='A09F9F' align='center'>"
"<tr><td colspan=2><center><font size=4><b>ESP32 Login Page</b></font></center><br></td></tr>"
"<tr><td>Username:</td><td><input type='text' size=25 name='userid'><br></td></tr>"
"<tr><td>Password:</td><td><input type='Password' size=25 name='pwd'><br></td></tr>"
"<tr><td><input type='submit' onclick='check(this.form)' value='Login'></td></tr>"
"</table>"
"</form>"
"<script>"
"function check(form){"
"if(form.userid.value=='admin' && form.pwd.value=='admin'){"
"window.open('/serverIndex')"
"}else{"
" alert('Error Password or Username')"
"}"
"}"
"</script>";

const char* serverIndex =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
"<input type='file' name='update'>"
"<input type='submit' value='Update'>"
"</form>"
"<div id='prg'>progress: 0%</div>"
"<script>"
"$('#upload_form').submit(function(e){"
"e.preventDefault();"
"var form = $('#upload_form')[0];"
"var data = new FormData(form);"
"$.ajax({"
"url: '/update',"
"type: 'POST',"
"data: data,"
"contentType: false,"
"processData:false,"
"xhr: function() {"
"var xhr = new window.XMLHttpRequest();"
"xhr.upload.addEventListener('progress', function(evt) {"
"if (evt.lengthComputable) {"
"var per = evt.loaded / evt.total;"
"$('#prg').html('progress: ' + Math.round(per*100) + '%');"
"}"
"}, false);"
"return xhr;"
"},"
"success:function(d, s) {"
"console.log('success!')"
"},"
"error: function (a, b, c) {"
"}"
"});"
"});"
"</script>";

//--------------------- 輔助函式 ---------------------
float RS485_data(uint16_t Reg) {
  uint8_t result;
  int retries = 3; // 設定重試次數為 3 次
  union {
    uint16_t raw_data[2];
    float floatValue;
  } Data;
  
  float final_Data = 9999.99;

  while (retries > 0) {
    result = node.readInputRegisters(Reg, 2);
    if (result == node.ku8MBSuccess) {
      Data.raw_data[0] = node.getResponseBuffer(0);
      Data.raw_data[1] = node.getResponseBuffer(1);
      final_Data = Data.floatValue;
      return final_Data; // 成功讀取，直接回傳正確的值
    }
    retries--; // 失敗則重試次數減 1
    delay(100); // 每次重試之間稍作延遲
  }

  // 如果重試 3 次都失敗，才印出錯誤訊息
  Serial.print("Failed to read data after 3 retries. Error: ");
  Serial.println(result);
  return final_Data; // 回傳失敗的預設值
}

void sendToGCP(float voltage, float current, float frequency, float pf, float watt, float total_watt_hours, String tableName) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://" + String(host) + String(api_path);
  Serial.println("向目標發送資料......");
  StaticJsonDocument<512> doc;
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
      Serial.printf("發送碼: %d\n", httpResponseCode);
      Serial.println(response);
    } else {
      Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
  } else {
    Serial.println("[HTTP] Unable to connect");
  }
}

void wifi_signal() {
  String message;
  message += "WiFi RSSI: ";
  message += WiFi.RSSI();
  server.send(200, "text/plain", message);
}

void setFlag() {
  String message = "status = ";
  message += status;
  message += "\n";
  status = 0;
  my_flag = 1;
  message += "Reset flag set 1.";
  server.send(200, "text/plain", message);
}

void reset() {
  if (my_flag == 1) {
    uint16_t resetAddress = 0x1201;
    uint16_t resetValue = 0x5AA5;
    node.begin(Modbus_ID1, Serial2);
    node.setTransmitBuffer(0, resetValue);
    uint8_t result1 = node.writeMultipleRegisters(resetAddress, 1);
    delay(10);
    node.begin(Modbus_ID2, Serial2);
    node.setTransmitBuffer(0, resetValue);
    uint8_t result2 = node.writeMultipleRegisters(resetAddress, 1);
    if (result1 == node.ku8MBSuccess && result2 == node.ku8MBSuccess)
      status = 1;
    else if (result1 == node.ku8MBSuccess)
      status = 2;
    else if (result2 == node.ku8MBSuccess)
      status = 3;
    else
      status = result1;
    my_flag = 0;
  }
}

void send_data() {
  node.begin(Modbus_ID1, Serial2);
  float voltage1 = RS485_data(Vll_avg);
   float current1 = RS485_data(I_avg);
  // 如果 voltage1 是 9999.99，代表電表離線或通訊失敗
    if (voltage1 == 9999.99) {
        Serial.println("讀取電表失敗 (可能已關機)，中止本次資料上傳。");
        return; // 直接結束此函式，不執行後續的上傳動作
    }
    //如果current1是<1表示機台沒有在運作
    if (current1 <= 1){
      Serial.println("機台關機、電表未關機，中止資料上傳節省資料庫傳輸費用。");
      return;
    }
  float freq1 = RS485_data(Frequency);
  float pf1 = RS485_data(PF);
  float watt1 = RS485_data(kVA_tot);
  float total_w1 = RS485_data(kVAh);
  String tableName1 = "sensor_data1";
  sendToGCP(voltage1, current1, freq1, pf1, watt1, total_w1, tableName1);

#if (esp32device==1)
  node.begin(Modbus_ID2, Serial2);
  float voltage2 = RS485_data(Vll_avg);
  float current2 = RS485_data(I_avg);
  // 如果 voltage2 是 9999.99，代表電表離線或通訊失敗
    if (voltage2 == 9999.99) {
        Serial.println("讀取電表失敗 (可能已關機)，中止本次資料上傳。");
        return; // 直接結束此函式，不執行後續的上傳動作
    }
  //如果current2是<1代表機台關機
     if (current2 <= 1){
      Serial.println("機台關機、電表未關機，中止資料上傳節省資料庫傳輸費用。");
      return;
    }
  float freq2 = RS485_data(Frequency);
  float pf2 = RS485_data(PF);
  float watt2 = RS485_data(kVA_tot);
  float total_w2 = RS485_data(kVAh);
  String tableName2 = "sensor_data2";
  sendToGCP(voltage2, current2, freq2, pf2, watt2, total_w2, tableName2);
#endif
  Serial.println("資料上傳完畢!");
}

//--------------------- 主程式邏輯 ---------------------
void setup() {
  Serial.begin(115200);
  Serial2.begin(19200, SERIAL_8N1, 16, 17);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("正在連線到 WiFi...");
  }
  Serial.println("已連線到 WiFi");
  Serial.println(WiFi.localIP());

  while (!MDNS.begin("esp32A")) { 
    Serial.println("mDNS 啟動失敗!");
    delay(1000);
  }
  Serial.println("mDNS 已啟動");
  server.on("/", HTTP_GET, []() { server.sendHeader("Connection", "close"); server.send(200, "text/html", loginIndex); });
  server.on("/serverIndex", HTTP_GET, []() { server.sendHeader("Connection", "close"); server.send(200, "text/html", serverIndex); });
  server.on("/update", HTTP_POST, []() { server.sendHeader("Connection", "close"); server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK"); ESP.restart(); }, []() { HTTPUpload& upload = server.upload(); if (upload.status == UPLOAD_FILE_START) { Serial.printf("Update: %s\n", upload.filename.c_str()); if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); }} else if (upload.status == UPLOAD_FILE_WRITE) { if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { Update.printError(Serial); }} else if (upload.status == UPLOAD_FILE_END) { if (Update.end(true)) { Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize); } else { Update.printError(Serial); }} });
  server.on("/wifi", wifi_signal);
  server.on("/reset", setFlag);
  server.begin();
  
  Serial.println("ESP32 喚醒！");

  send_data();
  Serial.println("資料已發送 (第一次)。");

  unsigned long startMillis = millis();
  Serial.println("等待 OTA 連線中... (5分鐘)");
  while (millis() - startMillis < OTA_TIMEOUT_MS) {
    server.handleClient();
    reset(); 
    delay(10);
  }
  Serial.println("OTA 等待結束。");

  send_data();
  Serial.println("資料已發送 (第二次)。");

  Serial.println("即將進入深度睡眠...");
  esp_deep_sleep(DEEP_SLEEP_SEC * 1000000);
}

void loop() {
}