#include <Adafruit_BNO055.h>
#include <Adafruit_Sensor.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <math.h>
#include <utility/imumaths.h>

const char *ssid = "HUAWEI-ufNU";
const char *password = "3vCEbK2X";

WebServer server(80);
Adafruit_BNO055 bno(55, 0x29, &Wire);

//====================================================
// GPS GIẢ LẬP THUYỀN
//====================================================

double boatLat = 16.075599;
double boatLon = 108.155089;

//====================================================
// GPS GIẢ LẬP NẠN NHÂN
//====================================================

double targetLat = 16.075618;
double targetLon = 108.155146;

//====================================================

float headingDeg = 0.0;
float bearingDeg = 0.0;
float errorDeg = 0.0;
float distanceM = 0.0;

String action = "GO STRAIGHT";

//====================================================

double deg2rad(double deg) { return deg * PI / 180.0; }

double rad2deg(double rad) { return rad * 180.0 / PI; }

//====================================================
// DISTANCE
//====================================================

double calcDistance(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;

  double dLat = deg2rad(lat2 - lat1);
  double dLon = deg2rad(lon2 - lon1);

  lat1 = deg2rad(lat1);
  lat2 = deg2rad(lat2);

  double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
             cos(lat1) * cos(lat2) * sin(dLon / 2.0) * sin(dLon / 2.0);

  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

  return R * c;
}

//====================================================
// BEARING
//====================================================

double calcBearing(double lat1, double lon1, double lat2, double lon2) {
  lat1 = deg2rad(lat1);
  lat2 = deg2rad(lat2);

  double dLon = deg2rad(lon2 - lon1);

  double y = sin(dLon) * cos(lat2);

  double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);

  double bearing = rad2deg(atan2(y, x));

  bearing = fmod(bearing + 360.0, 360.0);

  return bearing;
}

//====================================================
// WEB PAGE
//====================================================

void handleRoot() {
  String html = "";

  html += "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";

  html += "<title>RESCUE BOAT NAV</title>";

  html += "<link rel='stylesheet' "
          "href='https://unpkg.com/leaflet/dist/leaflet.css'/>";
  html += "<script src='https://unpkg.com/leaflet/dist/leaflet.js'></script>";

  // ===== CSS =====
  html += "<style>";
  html += "*{box-sizing:border-box;margin:0;padding:0}";
  html += "body{background:#f5f5f5;color:#333;font-family:'Segoe "
          "UI',Arial,sans-serif}";
  html += "#map{height:65vh;width:100%;border-bottom:3px solid #4285f4}";

  // Dashboard panel
  html += "#dash{display:grid;grid-template-columns:repeat(auto-fit,minmax("
          "150px,1fr));";
  html += "gap:10px;padding:12px 16px;background:#fff}";

  // Card style
  html += ".card{background:#f8f9fa;border:1px solid #e0e0e0;";
  html += "border-radius:12px;padding:12px 14px;text-align:center;";
  html += "transition:border-color .3s,box-shadow .3s}";
  html += ".card:hover{border-color:#4285f4;box-shadow:0 2px 12px "
          "rgba(66,133,244,0.2)}";
  html += ".card "
          ".lbl{font-size:11px;text-transform:uppercase;letter-spacing:1px;"
          "color:#888;margin-bottom:4px}";
  html += ".card .val{font-size:22px;font-weight:700;color:#222}";

  // Action banner
  html += "#action-bar{text-align:center;padding:14px;font-size:26px;font-"
          "weight:800;";
  html += "letter-spacing:2px;background:#fff;border-top:1px solid #e0e0e0}";
  html += ".go{color:#0f9d58}.left{color:#ea4335}.right{color:#ea4335}";

  // Compass widget
  html += "#compass{position:absolute;bottom:20px;right:20px;z-index:1000;";
  html += "width:90px;height:90px;border-radius:50%;";
  html += "background:rgba(255,255,255,0.92);border:2px solid #4285f4;";
  html += "box-shadow:0 2px 8px "
          "rgba(0,0,0,0.15);display:flex;align-items:center;justify-content:"
          "center}";

  // Leaflet custom icon overrides
  html += ".boat-arrow{transition:transform .3s ease}";
  html +=
      ".leaflet-popup-content-wrapper{background:#fff;color:#333;border-radius:"
      "10px;border:1px solid #4285f4;box-shadow:0 2px 8px rgba(0,0,0,0.12)}";
  html += ".leaflet-popup-tip{background:#fff}";

  html += "</style>";

  html += "</head>";
  html += "<body>";

  html += "<div id='map'></div>";
  html += "<div id='dash'>";
  html += "<div class='card'><div class='lbl'>Heading</div><div class='val' "
          "id='v-hdg'>--</div></div>";
  html += "<div class='card'><div class='lbl'>Bearing</div><div class='val' "
          "id='v-brg'>--</div></div>";
  html += "<div class='card'><div class='lbl'>Error</div><div class='val' "
          "id='v-err'>--</div></div>";
  html += "<div class='card'><div class='lbl'>Distance</div><div class='val' "
          "id='v-dst'>--</div></div>";
  html += "</div>";
  html += "<div id='action-bar'>LOADING...</div>";

  html += "<script>";

  // ===== Standard map tile =====
  html += "var "
          "map=L.map('map',{zoomControl:false}).setView([16.075599,108.155089],"
          "19);";
  html += "L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{";
  html += "maxZoom:20,attribution:'OSM'}).addTo(map);";
  html += "L.control.zoom({position:'topleft'}).addTo(map);";

  // ===== Boat arrow icon (Google Maps style blue cone) =====
  html += "function makeBoatIcon(deg){";
  html += "var svg=\"<svg xmlns='http://www.w3.org/2000/svg' width='48' "
          "height='48' viewBox='0 0 48 48'>\";";
  html += "svg+=\"<circle cx='24' cy='24' r='22' fill='rgba(66,133,244,0.15)' "
          "stroke='rgba(66,133,244,0.4)' stroke-width='1'/>\";";
  html += "svg+=\"<path d='M24 4 L32 28 L24 22 L16 28 Z' fill='#4285f4' "
          "stroke='#1a56db' stroke-width='1' "
          "transform='rotate(\"+deg+\",24,24)'/>\";";
  html += "svg+=\"<circle cx='24' cy='24' r='5' fill='#4285f4' stroke='#fff' "
          "stroke-width='2'/>\";";
  html += "svg+=\"</svg>\";";
  html += "return "
          "L.divIcon({html:svg,iconSize:[48,48],iconAnchor:[24,24],className:'"
          "boat-arrow'});";
  html += "}";

  // ===== Victim pulsing icon =====
  html += "var victimSvg=\"<svg xmlns='http://www.w3.org/2000/svg' width='36' "
          "height='36' viewBox='0 0 36 36'>\";";
  html +=
      "victimSvg+=\"<circle cx='18' cy='18' r='16' fill='rgba(255,82,82,0.18)' "
      "stroke='rgba(255,82,82,0.5)' stroke-width='1'>\";";
  html += "victimSvg+=\"<animate attributeName='r' values='10;16;10' dur='2s' "
          "repeatCount='indefinite'/>\";";
  html += "victimSvg+=\"<animate attributeName='opacity' values='1;0.4;1' "
          "dur='2s' repeatCount='indefinite'/>\";";
  html += "victimSvg+=\"</circle>\";";
  html += "victimSvg+=\"<circle cx='18' cy='18' r='6' fill='#ff5252' "
          "stroke='#fff' stroke-width='2'/>\";";
  html += "victimSvg+=\"</svg>\";";
  html += "var "
          "victimIcon=L.divIcon({html:victimSvg,iconSize:[36,36],iconAnchor:["
          "18,18],className:''});";

  // ===== Create markers =====
  html += "var "
          "boat=L.marker([16.075599,108.155089],{icon:makeBoatIcon(0)}).addTo("
          "map).bindPopup('<b>RESCUE BOAT</b>');";
  html += "var "
          "target=L.marker([16.075618,108.155146],{icon:victimIcon}).addTo(map)"
          ".bindPopup('<b>VICTIM (SOS)</b>');";

  // ===== Bearing dashed line =====
  html +=
      "var line=L.polyline([[16.075599,108.155089],[16.075618,108.155146]],{";
  html += "color:'#ea4335',weight:3,dashArray:'8,8',opacity:0.8}).addTo(map);";

  // ===== Heading direction line (shows where boat is facing) =====
  html += "var "
          "headLine=L.polyline([[0,0],[0,0]],{color:'#4285f4',weight:3,opacity:"
          "0.6}).addTo(map);";

  // ===== Compass overlay =====
  html +=
      "var compassDiv=document.createElement('div');compassDiv.id='compass';";
  html += "document.getElementById('map').appendChild(compassDiv);";

  // ===== Bearing arrow polyline decorator (arrowheads along the line) =====
  html += "var arrowMarkers=[];";
  html += "function drawArrows(lat1,lon1,lat2,lon2,brg){";
  html +=
      "arrowMarkers.forEach(function(m){map.removeLayer(m)});arrowMarkers=[];";
  html += "var steps=3;";
  html += "for(var i=1;i<=steps;i++){";
  html += "var f=i/(steps+1);";
  html += "var aLat=lat1+(lat2-lat1)*f;";
  html += "var aLon=lon1+(lon2-lon1)*f;";
  html += "var aSvg=\"<svg xmlns='http://www.w3.org/2000/svg' width='20' "
          "height='20' viewBox='0 0 20 20'>\";";
  html += "aSvg+=\"<path d='M10 2 L16 14 L10 10 L4 14 Z' fill='#ea4335' "
          "opacity='0.9' transform='rotate(\"+brg+\",10,10)'/>\";";
  html += "aSvg+=\"</svg>\";";
  html += "var "
          "aIcon=L.divIcon({html:aSvg,iconSize:[20,20],iconAnchor:[10,10],"
          "className:''});";
  html +=
      "var m=L.marker([aLat,aLon],{icon:aIcon,interactive:false}).addTo(map);";
  html += "arrowMarkers.push(m);";
  html += "}}";

  // ===== Compass draw function =====
  html += "function drawCompass(heading,bearing){";
  html += "var c=document.getElementById('compass');if(!c)return;";
  html += "var svg=\"<svg width='86' height='86' viewBox='0 0 86 86'>\";";
  // Cardinal labels: N (darker), E/S/W (lighter)
  html += "svg+=\"<text x='43' y='11' text-anchor='middle' fill='#444' font-size='9' font-weight='700'>N</text>\";";
  html += "svg+=\"<text x='81' y='47' text-anchor='middle' fill='#888' font-size='8' font-weight='600'>E</text>\";";
  html += "svg+=\"<text x='43' y='83' text-anchor='middle' fill='#888' font-size='8' font-weight='600'>S</text>\";";
  html += "svg+=\"<text x='5' y='47' text-anchor='middle' fill='#888' font-size='8' font-weight='600'>W</text>\";";
  // Compass circle
  html += "svg+=\"<circle cx='43' cy='43' r='36' fill='none' stroke='#ccc' stroke-width='1'/>\";";
  // Tick marks at N/E/S/W
  html += "svg+=\"<line x1='43' y1='7' x2='43' y2='14' stroke='#aaa' stroke-width='1.5'/>\";";
  html += "svg+=\"<line x1='79' y1='43' x2='72' y2='43' stroke='#ccc' stroke-width='1'/>\";";
  html += "svg+=\"<line x1='43' y1='79' x2='43' y2='72' stroke='#ccc' stroke-width='1'/>\";";
  html += "svg+=\"<line x1='7' y1='43' x2='14' y2='43' stroke='#ccc' stroke-width='1'/>\";";
  // Heading arrow (blue)
  html += "svg+=\"<path d='M43 18 L48 34 L43 30 L38 34 Z' fill='#4285f4' transform='rotate(\"+heading+\",43,43)'/>\";";
  // Bearing arrow (red)
  html += "svg+=\"<path d='M43 20 L46 32 L43 29 L40 32 Z' fill='#ea4335' opacity='0.7' transform='rotate(\"+bearing+\",43,43)'/>\";";
  // Center dot
  html += "svg+=\"<circle cx='43' cy='43' r='3' fill='#fff'/>\";";
  html += "svg+=\"</svg>\";";
  html += "c.innerHTML=svg;";
  html += "}";

  // ===== Heading line endpoint calculator =====
  html += "function headEndpoint(lat,lon,hdg,dist){";
  html += "var R=6371000;var d=dist/R;";
  html += "var brg=hdg*Math.PI/180;";
  html += "var la1=lat*Math.PI/180;var lo1=lon*Math.PI/180;";
  html += "var "
          "la2=Math.asin(Math.sin(la1)*Math.cos(d)+Math.cos(la1)*Math.sin(d)*"
          "Math.cos(brg));";
  html += "var "
          "lo2=lo1+Math.atan2(Math.sin(brg)*Math.sin(d)*Math.cos(la1),Math.cos("
          "d)-Math.sin(la1)*Math.sin(la2));";
  html += "return[la2*180/Math.PI,lo2*180/Math.PI];";
  html += "}";

  // ===== Update loop =====
  html += "async function updateData(){";
  html += "try{";
  html += "let r=await fetch('/data');";
  html += "let d=await r.json();";

  // Update boat icon rotation
  html += "boat.setIcon(makeBoatIcon(d.heading));";
  html += "boat.setLatLng([d.boatLat,d.boatLon]);";
  html += "target.setLatLng([d.targetLat,d.targetLon]);";

  // Update bearing line
  html += "line.setLatLngs([[d.boatLat,d.boatLon],[d.targetLat,d.targetLon]]);";

  // Update heading direction line
  html +=
      "var hEnd=headEndpoint(d.boatLat,d.boatLon,d.heading,d.distance*1.5+5);";
  html += "headLine.setLatLngs([[d.boatLat,d.boatLon],hEnd]);";

  // Draw arrow decorators along bearing line
  html += "drawArrows(d.boatLat,d.boatLon,d.targetLat,d.targetLon,d.bearing);";

  // Update compass
  html += "drawCompass(d.heading,d.bearing);";

  // Update dashboard cards
  html +=
      "document.getElementById('v-hdg').innerText=d.heading.toFixed(1)+'°';";
  html +=
      "document.getElementById('v-brg').innerText=d.bearing.toFixed(1)+'°';";

  // Color-code error
  html += "var errEl=document.getElementById('v-err');";
  html += "errEl.innerText=d.error.toFixed(1)+'°';";
  html += "errEl.style.color=Math.abs(d.error)<10?'#0f9d58':'#ea4335';";

  html +=
      "document.getElementById('v-dst').innerText=d.distance.toFixed(2)+'m';";

  // Action bar
  html += "var ab=document.getElementById('action-bar');";
  html += "ab.innerText=d.action;";
  html += "if(d.action.indexOf('STRAIGHT')>=0){ab.className='go'}";
  html += "else{ab.className=d.error>0?'right':'left'}";

  html += "}catch(e){console.log(e)}";
  html += "}";

  html += "setInterval(updateData,500);";
  html += "updateData();";

  html += "</script>";

  html += "</body>";
  html += "</html>";

  server.send(200, "text/html", html);
}

//====================================================

void handleData() {
  String json = "{";

  json += "\"boatLat\":";
  json += String(boatLat, 6);

  json += ",\"boatLon\":";
  json += String(boatLon, 6);

  json += ",\"targetLat\":";
  json += String(targetLat, 6);

  json += ",\"targetLon\":";
  json += String(targetLon, 6);

  json += ",\"heading\":";
  json += String(headingDeg, 1);

  json += ",\"bearing\":";
  json += String(bearingDeg, 1);

  json += ",\"error\":";
  json += String(errorDeg, 1);

  json += ",\"distance\":";
  json += String(distanceM, 2);

  json += ",\"action\":\"";
  json += action;
  json += "\"";

  json += "}";

  server.send(200, "application/json", json);
}

//====================================================

void setup() {
  Serial.begin(115200);

  Serial.println();
  Serial.println("BOOT");

  Wire.begin(6, 7);

  if (!bno.begin()) {
    Serial.println("BNO055 FAIL");

    while (1)
      ;
  }

  Serial.println("BNO055 OK");

  delay(1000);

  WiFi.begin(ssid, password);

  Serial.print("Connecting");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("WIFI OK");

  Serial.print("IP = ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);

  server.on("/data", handleData);

  server.begin();

  Serial.println("WEB SERVER START");
}

//====================================================

void loop() {
  imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);

  headingDeg = euler.x();

  distanceM = calcDistance(boatLat, boatLon, targetLat, targetLon);

  bearingDeg = calcBearing(boatLat, boatLon, targetLat, targetLon);

  errorDeg = bearingDeg - headingDeg;

  while (errorDeg > 180)
    errorDeg -= 360;

  while (errorDeg < -180)
    errorDeg += 360;

  if (fabs(errorDeg) < 10) {
    action = "GO STRAIGHT";
  } else if (errorDeg > 0) {
    action = "TURN RIGHT " + String(errorDeg, 1) + " DEG";
  } else {
    action = "TURN LEFT " + String(fabs(errorDeg), 1) + " DEG";
  }

  Serial.print("Heading=");
  Serial.print(headingDeg);

  Serial.print(" Bearing=");
  Serial.print(bearingDeg);

  Serial.print(" Error=");
  Serial.print(errorDeg);

  Serial.print(" Action=");
  Serial.println(action);

  server.handleClient();

  delay(100);
}