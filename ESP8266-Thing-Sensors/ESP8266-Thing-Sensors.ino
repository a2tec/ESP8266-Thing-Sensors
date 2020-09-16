#include "user_interface.h"
#include <Wire.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <ArduinoOTA.h>

#define DBG_OUTPUT_PORT Serial
#define DEBUG true

#include "sensors.h" // our sensor functions moved out for ease of reading/editing..

const char* ssid = "XXXX";
const char* password = "XXXX";
const char * hostName = "esp-async";
const char* http_username = "admin";
const char* http_password = "password";

IPAddress timeServer(203,100,61,10); // au.pool.ntp.org
const int timeZone = 10;     // AEDT

WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets
time_t prevDisplay = 0; // when the digital clock was displayed
int synced = false;
unsigned long wait000 = 1000UL;

ADC_MODE(ADC_VCC);

// AsyncWeb BEGIN
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

void setup(){
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);
  serialbanner();
  delay(100); //wait for serial
  DBG_OUTPUT_PORT.println("Starting WiFi AP_STA...");
  WiFi.mode(WIFI_STA);
  //WiFi.softAP(hostName);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    DBG_OUTPUT_PORT.printf("STA: Failed!\n");
    WiFi.disconnect(false);
    delay(1000);
    WiFi.begin(ssid, password);
  }
  DBG_OUTPUT_PORT.print("Connected to: ");
  DBG_OUTPUT_PORT.println(ssid);
  DBG_OUTPUT_PORT.print("Hostname: ");
  DBG_OUTPUT_PORT.println(hostName);
  DBG_OUTPUT_PORT.println("...WiFi Done!\n");

  DBG_OUTPUT_PORT.println("Starting UDP & NTP...");
  Udp.begin(localPort);
  DBG_OUTPUT_PORT.print("Local port: ");
  DBG_OUTPUT_PORT.println(Udp.localPort());
  DBG_OUTPUT_PORT.println("Waiting for sync...");
  while (!synced) {  // keep checking until it gets time..
        DBG_OUTPUT_PORT.println("Retrying...");
        setSyncProvider(getNtpTime);
  }
  digitalClockDisplay();
  setSyncInterval(86400); // check NTP every 24hrs (60*60*24)
  DBG_OUTPUT_PORT.println("...NTP Done!\n");
  
  DBG_OUTPUT_PORT.println("Setting up OTA...");
  //Send OTA events to the browser
  ArduinoOTA.onStart([]() { events.send("Update Start", "ota"); });
  ArduinoOTA.onEnd([]() { events.send("Update End", "ota"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char p[32];
    sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
    events.send(p, "ota");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if(error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
    else if(error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
    else if(error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
    else if(error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
    else if(error == OTA_END_ERROR) events.send("End Failed", "ota");
  });
  ArduinoOTA.setHostname(hostName);
  ArduinoOTA.begin();
  DBG_OUTPUT_PORT.println("...OTA Done!");

  DBG_OUTPUT_PORT.printf("\nStarting SPIFFS...\n");
  SPIFFS.begin();
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {    
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    DBG_OUTPUT_PORT.printf("\n");
  }
  SPIFFS.info(fs_info);
  DBG_OUTPUT_PORT.println("...SPIFFS Done!\n");

  DBG_OUTPUT_PORT.println("Setup i2c Sensors...");
  //setup sensors from sensor.h..
    setupBME280(); 
    setupMPU9250();
    setupHMC5883L();
    setupSSD1306();
  DBG_OUTPUT_PORT.println("...Sensors Done!\n");

  DBG_OUTPUT_PORT.println("Setting up WebServers..");
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  events.onConnect([](AsyncEventSourceClient *client){
    client->send("hello!",NULL,millis(),1000);
  });
  server.addHandler(&events);

  server.addHandler(new SPIFFSEditor(http_username,http_password, SPIFFS));

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });
  
  server.on("/all", HTTP_GET, [](AsyncWebServerRequest *request){
  doMPU9250();
  String json = "{";
    json += "\"uptime\":\""+String(millis2time())+"\"";
    //json += ", \"analog\":"+String(analogRead(A0));
    //json += ", \"gpio\":"+String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    json += ", \"hour\":\""+String(hourFormat12())+"\"";    
    if (minute() < 10) {
      json +=", \"minute\":\"0"+String(minute())+"\"";
    }
    else {
    json += ", \"minute\":\""+String(minute())+"\"";
    }
    if (second() < 10) {
      json +=", \"second\":\"0"+String(second())+"\"";
    }
    else {
    json += ", \"second\":\""+String(second())+"\"";
    }    
    if (weekday() == 1) {
      json += ", \"weekday\":\"Sunday\"";
    }
    if (weekday() == 2) {
      json += ", \"weekday\":\"Monday\"";
    }
    if (weekday() == 3) {
      json += ", \"weekday\":\"Tuesday\"";
    }
    if (weekday() == 4) {
      json += ", \"weekday\":\"Wednesday\"";
    }
    if (weekday() == 5) {
      json += ", \"weekday\":\"Thursday\"";
    }
    if (weekday() == 6) {
      json += ", \"weekday\":\"Friday\"";
    }
    if (weekday() == 7) {
      json += ", \"weekday\":\"Saturday\"";
    }
    json += ", \"day\":\""+String(day())+"\"";
    json += ", \"month\":\""+String(month())+"\"";
    json += ", \"year\":\""+String(year())+"\"";
    if (isAM() == 1) {
      json += ", \"isAM\": \"AM\"";
    }
    else {
      json += ", \"isAM\": \"PM\"";
    }
    float vccd = (ESP.getVcc());
    json += ", \"vcc\":\""+String((vccd/1000),2)+"\"";
    json += ", \"rssi\":\""+String(WiFi.RSSI())+"\"";
    json += ", \"cpufreq\":\""+String(ESP.getCpuFreqMHz())+"\"";
    json += ", \"heap\":\""+String(ESP.getFreeHeap())+"\"";
    json += ", \"corever\":\""+String(ESP.getCoreVersion())+"\"";
    String cid = String(ESP.getChipId(),HEX);
    cid.toUpperCase();
    json += ", \"chipid\":\""+cid+"\"";
    json += ", \"sdkver\":\""+String(ESP.getSdkVersion())+"\"";
    json += ", \"bootver\":\""+String(ESP.getBootVersion())+"\"";
    json += ", \"bootmode\":\""+String(ESP.getBootMode())+"\"";
    String fid = String(ESP.getFlashChipId(),HEX);
    fid.toUpperCase();
    json += ", \"flashid\":\""+fid+"\"";
    json += ", \"flashsize\":\""+String(flashChipSize,2)+"\"";
    json += ", \"flashfreq\":\""+String(flashFreq,2)+"\"";
    json += ", \"mode\":\""+String((ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"))+"\"";
    json += ", \"fstotal\":\""+String((fs_info.totalBytes/1024.0))+"\"";
    json += ", \"fsused\":\""+String((fs_info.usedBytes/1024.0))+"\"";
    json += ", \"blocksize\":\""+String(fs_info.blockSize)+"\"";
    json += ", \"pagesize\":\""+String(fs_info.pageSize)+"\"";
    json += ", \"maxopenfiles\":\""+String(fs_info.maxOpenFiles)+"\"";
    json += ", \"maxpathlen\":\""+String(fs_info.maxPathLength)+"\"";
/*
    json += ", \"PCFvalue0\":\""+String(value0)+"\"";
    json += ", \"PCFvalue1\":\""+String(value1)+"\"";
    json += ", \"PCFvalue2\":\""+String(value2)+"\"";
    json += ", \"PCFvalue3\":\""+String(value3)+"\"";
*/
    json += ", \"tempc\":\""+String(mySensor.readTempC())+"\"";
    json += ", \"tempf\":\""+String(mySensor.readTempF())+"\"";
    json += ", \"humidity\":\""+String(mySensor.readFloatHumidity(), 2)+"\"";
    json += ", \"pressure\":\""+String((mySensor.readFloatPressure()/100), 2)+"\"";

    //json += ", \"temp\":\""+String(float(myIMU.readTempData()/100))+"\"";
    json += ", \"temp\":\""+String(float(myIMU.temperature))+"\"";
    
    json += ", \"ax\":\""+String(100*myIMU.ax)+"\"";
    json += ", \"ay\":\""+String(100*myIMU.ay)+"\"";
    json += ", \"az\":\""+String(10*myIMU.az)+"\"";
    json += ", \"gx\":\""+String(myIMU.gx)+"\"";
    json += ", \"gy\":\""+String(myIMU.gy)+"\"";
    json += ", \"gz\":\""+String(myIMU.gz)+"\"";
    json += ", \"mx\":\""+String(myIMU.mx)+"\"";
    json += ", \"my\":\""+String(myIMU.my)+"\"";
    json += ", \"mz\":\""+String(myIMU.mz)+"\"";
    Vector raw = compass.readRaw();
    Vector norm = compass.readNormalize();
    json += ", \"hmcrx\":\""+String(raw.XAxis)+"\"";
    json += ", \"hmcry\":\""+String(raw.YAxis)+"\"";
    json += ", \"hmcrz\":\""+String(raw.ZAxis)+"\"";
    json += ", \"hmcnx\":\""+String(norm.XAxis)+"\"";
    json += ", \"hmcny\":\""+String(norm.YAxis)+"\"";
    json += ", \"hmcnz\":\""+String(norm.ZAxis)+"\"";

    
    //json += ", \"array\": [\""+String(myArray[0])+","+String(myArray[1])+","+String(myArray[2])+","+String(myArray[3])+","+String(myArray[4])+","+String(myArray[5])+","+String(myArray[6])+","+String(myArray[7])+","+String(myArray[8])+","+String(myArray[9])+"]\"";
    
    json += "}";
    request->send(200, "text/json", json);
    //server.send(200, "text/json", json);
    //DBG_OUTPUT_PORT.println(json);
    json = String();
  });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index1.htm");

  server.onNotFound([](AsyncWebServerRequest *request){
    DBG_OUTPUT_PORT.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      DBG_OUTPUT_PORT.printf("GET");
    else if(request->method() == HTTP_POST)
      DBG_OUTPUT_PORT.printf("POST");
    else if(request->method() == HTTP_DELETE)
      DBG_OUTPUT_PORT.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      DBG_OUTPUT_PORT.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      DBG_OUTPUT_PORT.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      DBG_OUTPUT_PORT.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      DBG_OUTPUT_PORT.printf("OPTIONS");
    else
      DBG_OUTPUT_PORT.printf("UNKNOWN");
    DBG_OUTPUT_PORT.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      DBG_OUTPUT_PORT.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      DBG_OUTPUT_PORT.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      DBG_OUTPUT_PORT.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        DBG_OUTPUT_PORT.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        DBG_OUTPUT_PORT.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        DBG_OUTPUT_PORT.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index)
      DBG_OUTPUT_PORT.printf("UploadStart: %s\n", filename.c_str());
    DBG_OUTPUT_PORT.printf("%s", (const char*)data);
    if(final)
      DBG_OUTPUT_PORT.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      DBG_OUTPUT_PORT.printf("BodyStart: %u\n", total);
    DBG_OUTPUT_PORT.printf("%s", (const char*)data);
    if(index + len == total)
      DBG_OUTPUT_PORT.printf("BodyEnd: %u\n", total);
  });
  DBG_OUTPUT_PORT.println("Starting Servers...");
  server.begin();
  DBG_OUTPUT_PORT.println("...WebServers Done!");
}


void loop(){
   ArduinoOTA.handle();

  int remainingTimeBudget = ui.update();
  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    
    delay(remainingTimeBudget);
  }

}

/*-------- NTP code ----------*/

void digitalClockDisplay()
{
  // digital clock display of the time
  DBG_OUTPUT_PORT.print(hour());
  printDigits(minute());
  printDigits(second());
  DBG_OUTPUT_PORT.print(" ");
  DBG_OUTPUT_PORT.print(day());
  DBG_OUTPUT_PORT.print(".");
  DBG_OUTPUT_PORT.print(month());
  DBG_OUTPUT_PORT.print(".");
  DBG_OUTPUT_PORT.print(year());
  DBG_OUTPUT_PORT.println();
}
void printDigits(int digits)
{
  // utility for digital clock display: prints preceding colon and leading 0
  DBG_OUTPUT_PORT.print(":");
  if (digits < 10)
    DBG_OUTPUT_PORT.print('0');
  DBG_OUTPUT_PORT.print(digits);
}
// send an NTP request to the time server at the given address
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:                 
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  DBG_OUTPUT_PORT.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      DBG_OUTPUT_PORT.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      synced = true;
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  DBG_OUTPUT_PORT.println("No NTP Response :-(   ***WARNING*** Time not set!");
  synced = false;
  return 0; // return 0 if unable to get the time
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    DBG_OUTPUT_PORT.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    DBG_OUTPUT_PORT.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    DBG_OUTPUT_PORT.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    DBG_OUTPUT_PORT.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      DBG_OUTPUT_PORT.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      DBG_OUTPUT_PORT.printf("%s\n",msg.c_str());

      if(info->opcode == WS_TEXT)
        client->text("I got your text message");
      else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          DBG_OUTPUT_PORT.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        DBG_OUTPUT_PORT.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      DBG_OUTPUT_PORT.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      DBG_OUTPUT_PORT.printf("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        DBG_OUTPUT_PORT.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          DBG_OUTPUT_PORT.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT)
            client->text("I got your text message");
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
}
