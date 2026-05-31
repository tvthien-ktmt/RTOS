#include <HardwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <TinyGPSPlus.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <math.h>

// =====================================================
// WIFI
// =====================================================
const char *ssid = "XR";
const char *password = "hhhhhhhh";

// =====================================================
// GPS
// =====================================================
HardwareSerial GPS(1);
TinyGPSPlus gps;

// =====================================================
// LORA
// =====================================================
HardwareSerial LoRa(0);

// =====================================================
// LCD
// =====================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =====================================================
// WEB SERVER
// =====================================================
WebServer server(80);

// =====================================================
// REMOTE GPS
// =====================================================
bool remoteValid = false;
unsigned long lastLoRaReceived = 0;
const unsigned long LORA_TIMEOUT = 5000; // mất 5s không nhận → invalid

// LoRa receive buffer (non-blocking)
String loraBuffer = "";
const int LORA_BUF_MAX = 128; // giới hạn buffer tránh tràn RAM

// =====================================================
// LCD SCREEN
// =====================================================
int lcdScreen = 0;
unsigned long lastLCDChange = 0;
const unsigned long LCD_INTERVAL = 3000; // giảm từ 5000 → 3000ms

// =====================================================
// EMA FILTER + STATIC AVERAGING (thay thế Median filter)
// =====================================================
#define EMA_ALPHA 0.5       // 0.0 = rất smooth, 1.0 = không filter
#define DEADZONE_METERS 3.0 // Ngưỡng chuyển từ static → moving mode

double localLatFiltered = 0.0;
double localLonFiltered = 0.0;
double remoteLatFiltered = 0.0;
double remoteLonFiltered = 0.0;

bool localFilterInit = false;
bool remoteFilterInit = false;

// Static averaging: khi đứng yên, trung bình nhiều mẫu để giảm sai số GPS
// Sai số giảm theo công thức: error/√N (ví dụ: 50 mẫu → sai số giảm 7 lần)
double localLatSum = 0.0, localLonSum = 0.0;
unsigned long localAvgCount = 0;

double remoteLatSum = 0.0, remoteLonSum = 0.0;
unsigned long remoteAvgCount = 0;

// =====================================================
// DISTANCE cache - tính sẵn mỗi khi có dữ liệu mới
// =====================================================
double cachedDistance = 0.0;

// =====================================================
// GPS 5Hz rate command (không reset để giữ Hot/Warm Start)
// =====================================================
const uint8_t setRate5Hz[] = {0xB5, 0x62, 0x06, 0x08, 0x06, 0x00,
                              0xC8, 0x00, // 200ms
                              0x01, 0x00, 0x01, 0x00, 0xDE, 0x6A};


// =====================================================
// HAVERSINE DISTANCE
// =====================================================
double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double phi1 = lat1 * M_PI / 180.0;
  double phi2 = lat2 * M_PI / 180.0;
  double dPhi = (lat2 - lat1) * M_PI / 180.0;
  double dLambda = (lon2 - lon1) * M_PI / 180.0;

  double a = sin(dPhi / 2.0) * sin(dPhi / 2.0) +
             cos(phi1) * cos(phi2) * sin(dLambda / 2.0) * sin(dLambda / 2.0);

  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  return R * c;
}

// =====================================================
// HTML - web poll giảm từ 1000 → 300ms
// =====================================================
String getHTML() {
  String html = "";
  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<title>GPS TRACKER</title>";
  html += "<link rel='stylesheet' "
          "href='https://unpkg.com/leaflet/dist/leaflet.css'/>";
  html += "<script src='https://unpkg.com/leaflet/dist/leaflet.js'></script>";
  html += "<style>";
  html += "html,body{margin:0;padding:0;}";
  html += "#map{width:100%;height:100vh;}";
  html += ".info{position:absolute;top:10px;left:10px;background:white;padding:"
          "10px;z-index:999;font-family:Arial;font-size:14px;}";
  html += "</style></head><body>";
  html += "<div class='info'>";
  html += "<div id='local'></div>";
  html += "<div id='remote'></div>";
  html += "<div id='distance'></div>";
  html += "</div>";
  html += "<div id='map'></div>";
  html += "<script>";
  html += "var map=L.map('map').setView([0,0],18);";
  html += "L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/"
          "{y}.png',{attribution:'OpenStreetMap'}).addTo(map);";
  html += "var localMarker=L.marker([0,0]).addTo(map).bindPopup('LOCAL GPS');";
  html +=
      "var remoteMarker=L.marker([0,0]).addTo(map).bindPopup('REMOTE GPS');";
  html += "var firstView=true;";

  // Chỉ setView lần đầu, không nhảy map mỗi 300ms
  html += "function updateGPS(){";
  html += "fetch('/gps').then(r=>r.json()).then(data=>{";
  html += "if(data.localValid==1){";
  html += "localMarker.setLatLng([data.localLat,data.localLon]);";
  html += "document.getElementById('local').innerHTML='LOCAL: "
          "'+data.localLat.toFixed(6)+' , '+data.localLon.toFixed(6);";
  html += "if(firstView){map.setView([data.localLat,data.localLon]);firstView="
          "false;}";
  html +=
      "}else{document.getElementById('local').innerHTML='LOCAL: Waiting GPS';}";
  html += "if(data.remoteValid==1){";
  html += "remoteMarker.setLatLng([data.remoteLat,data.remoteLon]);";
  html += "document.getElementById('remote').innerHTML='REMOTE: "
          "'+data.remoteLat.toFixed(6)+' , '+data.remoteLon.toFixed(6);";
  html += "document.getElementById('distance').innerHTML='DISTANCE: "
          "'+data.distance.toFixed(2)+' m';";
  html += "}else{";
  html +=
      "document.getElementById('remote').innerHTML='REMOTE: No GPS Sender';";
  html += "document.getElementById('distance').innerHTML='';}";
  html += "}).catch(()=>{});"; // bắt lỗi fetch tránh crash
  html += "}";
  html += "setInterval(updateGPS, 300);"; // giảm từ 1000 → 300ms
  html += "updateGPS();";                 // gọi ngay lập tức khi load
  html += "</script></body></html>";
  return html;
}

// =====================================================
// HANDLERS
// =====================================================
void handleRoot() { server.send(200, "text/html", getHTML()); }

void handleGPS() {
  double displayDist = cachedDistance;
  if (displayDist < 0.0) displayDist = 0.0;

  String json = "{";
  json += "\"localLat\":" + String(localLatFiltered, 6) + ",";
  json += "\"localLon\":" + String(localLonFiltered, 6) + ",";
  json += "\"remoteLat\":" + String(remoteLatFiltered, 6) + ",";
  json += "\"remoteLon\":" + String(remoteLonFiltered, 6) + ",";
  json += "\"distance\":" + String(displayDist, 2) + ",";
  json += "\"localValid\":" + String(localFilterInit ? 1 : 0) + ",";
  json += "\"remoteValid\":" + String(remoteValid ? 1 : 0);
  json += "}";

  server.send(200, "application/json", json);
}

// =====================================================
// UPDATE LOCAL GPS FILTER (Static Averaging + EMA)
// =====================================================
void updateLocalGPSFilter() {
  if (!gps.location.isValid())
    return;
  if (!gps.location.isUpdated())
    return;

  double newLat = gps.location.lat();
  double newLon = gps.location.lng();

  if (!localFilterInit) {
    // Lần đầu: gán trực tiếp + khởi tạo averaging
    localLatFiltered = newLat;
    localLonFiltered = newLon;
    localLatSum = newLat;
    localLonSum = newLon;
    localAvgCount = 1;
    localFilterInit = true;
  } else {
    double moved =
        calculateDistance(localLatFiltered, localLonFiltered, newLat, newLon);

    if (moved < DEADZONE_METERS) {
      // Đứng yên: tích lũy và trung bình để giảm sai số GPS
      // Quy luật: sai số giảm theo 1/√N
      localLatSum += newLat;
      localLonSum += newLon;
      localAvgCount++;
      localLatFiltered = localLatSum / localAvgCount;
      localLonFiltered = localLonSum / localAvgCount;
    } else {
      // Di chuyển: dùng EMA để phản ứng nhanh, reset averaging
      localLatFiltered =
          EMA_ALPHA * newLat + (1.0 - EMA_ALPHA) * localLatFiltered;
      localLonFiltered =
          EMA_ALPHA * newLon + (1.0 - EMA_ALPHA) * localLonFiltered;
      localLatSum = localLatFiltered;
      localLonSum = localLonFiltered;
      localAvgCount = 1;
    }
  }

  // Tính lại distance ngay khi local GPS update
  if (remoteFilterInit) {
    cachedDistance = calculateDistance(localLatFiltered, localLonFiltered,
                                       remoteLatFiltered, remoteLonFiltered);
  }
}

// =====================================================
// RECEIVE LORA (Non-blocking + EMA + Dead-zone)
// =====================================================
void updateLoRa() {
  // Timeout: nếu lâu không nhận được → đánh dấu invalid
  if (remoteValid && millis() - lastLoRaReceived > LORA_TIMEOUT) {
    remoteValid = false;
  }

  // Đọc từng byte, KHÔNG blocking — tránh chặn loop()
  while (LoRa.available()) {
    char c = LoRa.read();

    if (c == '\n') {
      // Đã nhận đủ 1 dòng → xử lý
      loraBuffer.trim();

      if (loraBuffer.startsWith("GPS:")) {
        String data = loraBuffer.substring(4);
        int comma = data.indexOf(',');

        if (comma > 0) {
          double rawLat = data.substring(0, comma).toDouble();
          double rawLon = data.substring(comma + 1).toDouble();

          // Validate tọa độ Việt Nam
          if (rawLat >= 8.0 && rawLat <= 24.0 && rawLon >= 102.0 &&
              rawLon <= 110.0) {

            if (!remoteFilterInit) {
              remoteLatFiltered = rawLat;
              remoteLonFiltered = rawLon;
              remoteLatSum = rawLat;
              remoteLonSum = rawLon;
              remoteAvgCount = 1;
              remoteFilterInit = true;
            } else {
              double moved = calculateDistance(
                  remoteLatFiltered, remoteLonFiltered, rawLat, rawLon);

              if (moved < DEADZONE_METERS) {
                // Đứng yên: static averaging
                remoteLatSum += rawLat;
                remoteLonSum += rawLon;
                remoteAvgCount++;
                remoteLatFiltered = remoteLatSum / remoteAvgCount;
                remoteLonFiltered = remoteLonSum / remoteAvgCount;
              } else {
                // Di chuyển: EMA + reset averaging
                remoteLatFiltered =
                    EMA_ALPHA * rawLat + (1.0 - EMA_ALPHA) * remoteLatFiltered;
                remoteLonFiltered =
                    EMA_ALPHA * rawLon + (1.0 - EMA_ALPHA) * remoteLonFiltered;
                remoteLatSum = remoteLatFiltered;
                remoteLonSum = remoteLonFiltered;
                remoteAvgCount = 1;
              }
            }

            remoteValid = true;
            lastLoRaReceived = millis();

            if (localFilterInit) {
              cachedDistance =
                  calculateDistance(localLatFiltered, localLonFiltered,
                                    remoteLatFiltered, remoteLonFiltered);
            }
          }
        }
      }

      loraBuffer = ""; // Reset buffer cho dòng tiếp theo
    } else {
      // Tích lũy ký tự, giới hạn buffer tránh tràn RAM
      if (loraBuffer.length() < LORA_BUF_MAX) {
        loraBuffer += c;
      } else {
        loraBuffer = ""; // Buffer overflow → bỏ dòng lỗi
      }
    }
  }
}

// =====================================================
// LCD
// =====================================================
void updateLCD() {
  if (millis() - lastLCDChange < LCD_INTERVAL)
    return;
  lastLCDChange = millis();
  lcd.clear();

  if (lcdScreen == 0) {
    if (localFilterInit) {
      lcd.setCursor(0, 0);
      lcd.print("L:");
      lcd.print(localLatFiltered, 4);
      lcd.setCursor(0, 1);
      lcd.print("O:");
      lcd.print(localLonFiltered, 4);
    } else {
      lcd.setCursor(0, 0);
      lcd.print("LocalGPS:");
      lcd.setCursor(0, 1);
      lcd.print("Waiting");
    }
  }

  else if (lcdScreen == 1) {
    if (remoteValid) {
      lcd.setCursor(0, 0);
      lcd.print("R:");
      lcd.print(remoteLatFiltered, 4);
      lcd.setCursor(0, 1);
      lcd.print("O:");
      lcd.print(remoteLonFiltered, 4);
    } else {
      lcd.setCursor(0, 0);
      lcd.print("RemoteGPS:");
      lcd.setCursor(0, 1);
      lcd.print("No Sender");
    }
  }

  else if (lcdScreen == 2) {
    lcd.setCursor(0, 0);
    lcd.print("Distance:");
    lcd.setCursor(0, 1);
    if (localFilterInit && remoteValid) {
      double lcdDist = cachedDistance;
      if (lcdDist < 0.0) lcdDist = 0.0;
      lcd.print(lcdDist, 2);
      lcd.print(" m");
    } else {
      lcd.print("No Distance");
    }
  }

  lcdScreen++;
  if (lcdScreen > 2)
    lcdScreen = 0;
}


// =====================================================
// SETUP
// =====================================================
void setup() {
  Wire.begin(6, 7);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Starting...");

  // Khởi động GPS trực tiếp (giữ Hot/Warm Start) + cấu hình 5Hz
  GPS.begin(9600, SERIAL_8N1, 4, 5);
  delay(200);
  GPS.write(setRate5Hz, sizeof(setRate5Hz)); // set 5Hz
  delay(200);

  LoRa.begin(9600, SERIAL_8N1, 8, 9);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(3000);

  server.on("/", handleRoot);
  server.on("/gps", handleGPS);
  server.begin();
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  server.handleClient();

  while (GPS.available())
    gps.encode(GPS.read());

  updateLocalGPSFilter();
  updateLoRa();
  updateLCD();
}