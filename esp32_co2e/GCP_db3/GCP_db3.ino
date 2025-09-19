/*
 破碎機(220V), 冰水機, 雕刻機, 空壓機用
 http://esp32C.local 更新網頁
 
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

String device[4] = {"破碎機(220V)", "冰水機", "雕刻機", "空壓機"};

// KWS-AC301 Input 暫存器定義
#define Vll_avg 0x0E
#define I_avg 0x0F      //0x0F, 0x10
#define PF 0x1D
#define Frequency 0x1E
#define kVA_tot 0x11    //0x11, 0x12
#define kVAh 0x17       //0x17, 0x18

// Connected Pin
#define RX 16
#define TX 17
#define Modbus_ID 2
#define s0 25
#define s1 26
#define s2 27

//--------------------- 全域變數與物件 ---------------------
volatile bool my_flag = 0;
volatile uint8_t status = 0;
WebServer server(80);
ModbusMaster node;



//--------------------- 網頁介面 HTML ---------------------
const char* loginIndex =
 "<form name='loginForm'>"
    "<table width='20%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>ESP32 Login Page</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
             "<td>Username:</td>"
             "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{"
    "window.open('/serverIndex')"
    "}"
    "else"
    "{"
    " alert('Error Password or Username')/*displays error message*/"
    "}"
    "}"
"</script>";

/*
 * Server Index Page
 */

const char* serverIndex =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
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


// Function to generate random float values between min and max
float generateRandomData(float min, float max) {
  return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (max - min)));
}

void readMux(int channel){
  int controlPin[] = {s0, s1, s2};

  int muxChannel[8][3]={
    {0,0,0}, //channel 1
    {0,0,1}, //channel 2
    {0,1,0}, //channel 3
    {0,1,1}, //channel 4
    {1,0,0}, //channel 5
    {1,0,1}, //channel 6
    {1,1,0}, //channel 7
    {1,1,1}  //channel 8
  };
  //loop through the 3 sig
  for(int i=0; i<3; i++){
    digitalWrite(controlPin[2-i], muxChannel[channel][2-i]);
  }
}


/////////////////////////////// PA330 Data //////////////////////////////////////////////
float RS485_data(uint16_t Reg, int size, float ratio)
{
  uint8_t result;
  // Combine the two 16-bit registers into a 32-bit int register
  int retries = 3;
  union {
    uint16_t raw_data[2] = {0x0000, 0x0000};
    uint32_t value;
  } Data;

  float final_Data = 9999.99;

  // Read Voltage L-N for Phase A (Register 0x1000 - 0x1001)
  while (retries > 0){
    result = node.readHoldingRegisters(Reg, size);

    if (result == node.ku8MBSuccess) {
      for(int i=0; i<size; i++)
      {
      Data.raw_data[i] = node.getResponseBuffer(i);
      }
      final_Data = Data.value / ratio;
      return final_Data;
    } 
    retries--;
    delay(100);
  }
  // else {
  //   Serial.print("Failed to read data. Error: ");
  //   Serial.println(result);
  // }
  Serial.print("Failed to read data after 3 retries. Error: ");
  Serial.println(result);
  return final_Data;
}
/////////////////////////////////////////////////////////////////////////////////////////

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

void wifi_signal(){
  String message;
  message += "WiFi RSSI: ";
  message += WiFi.RSSI(); //讀取WiFi強度
  server.send(200, "text/plain", message); // Send response
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

  bool shouldReset = false;
  // portENTER_CRITICAL(&flagMutex);
  if(my_flag == 1) {
    shouldReset = true;
  }
  // portEXIT_CRITICAL(&flagMutex);

  if(shouldReset)
  {
    // Start Modbus RTU transaction
    uint16_t startAddress = 0x154;
    uint8_t numberOfRegisters = 5; // From 0x154 to 0x158

    uint16_t values[numberOfRegisters] = {0, 0, 0, 0, 0}; // Array of zeros

    for(int i=0; i<4; i++)
    {
      readMux(i);
      // Write 0 to registers from 0x154 to 0x158
      for (uint8_t j = 0; j < numberOfRegisters; j++) {
        node.setTransmitBuffer(j, 0);  // Set register value at index i
      }

      // Write multiple registers
      uint8_t result = node.writeMultipleRegisters(startAddress, numberOfRegisters);
    }

    // // Clear flag in critical section
    // portENTER_CRITICAL(&flagMutex);
    my_flag = 0;
    // portEXIT_CRITICAL(&flagMutex);
  }
}

// Function for the task running on Core 0
void TaskCore0(void *pvParameters) {
  for (;;) {
    // Your code for Core 0
    server.handleClient();
    vTaskDelay(10 / portTICK_PERIOD_MS); // Delay for 10 milliseconds
  }
}

void send_data() {
  node.begin(Modbus_ID, Serial2);
  for(int i=0; i<4; i++)
  {
    readMux(i);
    float voltage = RS485_data(Vll_avg, 1, 10.0);
    float current = RS485_data(I_avg, 2, 1000.0);
    //若voltage為9999.99代表電表關機或故障
    if (voltage == 9999.99) {
        Serial.printf("設備 %s (通道 %d) 讀取失敗，跳過本次上傳。\n", device[i].c_str(), i);
        continue; // <-- 請務必將 return 修改為 continue
    }

    //若current為1代表機台關機電表未關機
    if (current <= 1){
      Serial.printf("設備%s關機(通道 %d)，跳過本次上傳節省資料傳輸費用。\n",device[i].c_str(),i);
      continue;
    }
    
    float freq = RS485_data(Frequency, 1, 10.0);
    float pf = RS485_data(PF, 1, 100.0);
    float watt = RS485_data(kVA_tot, 2, 10000.0);
    float total_w = RS485_data(kVAh, 2, 1000.0);
    String tableName = device[i];
    
    sendToGCP(voltage, current, freq, pf, watt, total_w, tableName);
  }
}

void setup() {

  pinMode(s0, OUTPUT); 
  pinMode(s1, OUTPUT); 
  pinMode(s2, OUTPUT); 

  digitalWrite(s0, LOW);
  digitalWrite(s1, LOW);
  digitalWrite(s2, LOW);

  // Start serial communication
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17); // ESP32 RX=16, TX=17, Baud Rate=9600
  node.begin(Modbus_ID, Serial2);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("正在連線到 WiFi...");
  }
  Serial.println("已連線到 WiFi");
  Serial.println(WiFi.localIP());

  while (!MDNS.begin("esp32C")) { 
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
  xTaskCreatePinnedToCore(TaskCore0, "TaskCore0", 10000, NULL, 1, NULL, 0);
  
  Serial.println("ESP32 喚醒！");

  send_data();
  Serial.println("資料已發送 (第一次)。");

  unsigned long startMillis = millis();
  Serial.println("等待 OTA 連線中... (5分鐘)");
  while (millis() - startMillis < OTA_TIMEOUT_MS) {
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
