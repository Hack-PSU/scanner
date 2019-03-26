#include "network.h"

namespace hackPSU {

  //___________________________________________________________________ Request
  Network::Request::Request(API::Method method, String host, String route) :
      header(bf_header.createObject()),
      payload(bf_payload.createObject()),
      method(method)
  {
    #ifdef DEBUG
      Serial.println(host + route);
    #endif
    // Set the base URL for the request
    #ifdef HTTPS_FINGERPRINT
      url = "https://" + host + route;
    #else
      url = "http://" + host + route;
    #endif
    #ifdef DEBUG
      Serial.print("THE URL is: ");
      Serial.println(url);
    #endif
    addHeader("Content-Type", "application/json");
    addHeader("macaddr", WiFi.macAddress());
    addPayload("version", API_VERSION);

    response = nullptr;
  }

  Network::Request::~Request(){
    if(response != nullptr) delete response;
  }

  bool Network::Request::addPayload(String key, String value){
    return payload.set(key, value);
  }
  bool Network::Request::addHeader(String key, String value){
    return header.set(key, value);
  }

  bool Network::Request::addParameter(String key, String value){
    parameter += (parameter.length() == 0 ? '?' : '&') + key + "=" + value;
    return true;
  }

  Response* Network::Request::commit(bool reboot) {
    // if(WiFi.status() == WL_DISCONNECTED){
    //   WiFi.begin(NETWORK_SSID, NETWORK_PASSWORD);
    // }
    // Begin HTTP request
    #ifdef HTTPS_FINGERPRINT
      //http.begin(HOST, PORT, url, true, FP);
      http.begin(url + parameter, FP);
    #else
      http.begin(url + parameter);
    #endif
    
    // Set headers, if any, for request
    for(JsonPair& p: header){
      http.addHeader(p.key, p.value.as<char*>());
    }

    int tmpcode;
    if(method == API::GET) {
      tmpcode = http.GET();
    } else if(method == API::POST) {
      String pld = "";
      payload.printTo(pld);
      tmpcode = http.POST(pld);
    }
    if(tmpcode == 401){
      #ifdef DEBUG
      Serial.println("Authentication error");
      #endif
      char apibuff[36] = {0};
      EEPROM.put(0, apibuff);
      EEPROM.commit();
      if(reboot){
        #ifdef DEBUG
        Serial.println("Rebooting...");
        #endif
        delay(500);
        // Power cycle required after serial flash for this to work!!!
        ESP.restart();
      }
    }
    response = new Response(http.getString(), tmpcode);
    Serial.println(response->payload);
    // Terminate HTTP request
    http.end();
    return response;
  }

  bool Network::addHeader(String key, String value){
    return req->addHeader(key, value);
  }
  bool Network::addPayload(String key, String value){
    return req->addPayload(key, value);
  }
  bool Network::addParameter(String key, String value){
    return req->addParameter(key, value);
  }

  //___________________________________________________________________ Network

  Network::Network(String host) {
    this->host = host;
    OTA_enabled = false;
    req = nullptr;
    Serial.println("Connecting to : " + String(NETWORK_SSID));
    Serial.println("Password: " + String(NETWORK_PASSWORD));
    WiFi.begin(NETWORK_SSID, NETWORK_PASSWORD);
    String h = "hackpsu_scanner";
    h.toCharArray(hostname, 16);

    // Get API key from memory
    char apibuff[37];
    EEPROM.begin(37);
    EEPROM.get(0, apibuff);
    Serial.print("API buff: ");
    Serial.println(apibuff);
    apiKey = String(apibuff);
  }


  void Network::createRequest(API::Method method, String route){
    if(req != nullptr) delete req;
    req = nullptr;
    req = new Request(method, host, route);
  }

  bool Network::connected(){
    return WiFi.status() == WL_CONNECTED;
  }

  bool Network::checkApiKey(){
    return apiKey[0];
  }

  HTTPCode Network::getApiKey(int pin) {

    // TODO: add api key check here

    createRequest(API::POST, "/auth/scanner/register");
    addPayload("pin",String(pin));
    addPayload("version", API_VERSION);

    Response* registerScanner = commit(false);

    if(bool(*registerScanner)){
      MAKE_BUFFER(25, 25) bf_data;
      JsonObject& response = bf_data.parseObject(registerScanner->payload);

      JsonObject& data = response.get<JsonObject>("data");
      apiKey = data.get<String>("apikey");
      Serial.println(apiKey);
      char apibuff[37];
      apiKey.toCharArray(apibuff, 37);
      EEPROM.put(0, apibuff);
      EEPROM.commit();
      Serial.println(apibuff);
      #ifdef DEBUG
        EEPROM.get(0, apibuff);
        Serial.print("APIBUFF (getpin): ");
        Serial.println(apibuff);
      #endif

    } else {
      Serial.println("Failed updating API key");
      Serial.println(registerScanner->code.toString());
    }
    Serial.println("Using apikey: " + apiKey);
    return registerScanner->code;
  }

  User Network::getDataFromPin(int pin) {
    createRequest(API::POST, "/rfid/getpin");
    addPayload("pin", String(pin));
    addPayload("version", API_VERSION);
    addPayload("apikey", apiKey);
    Response* registerScanner = commit();
    User user = {.name = "NULL", .shirtSize = "NULL", .diet = "NULL", .allow = false, .code = registerScanner->code};
    if(bool(*registerScanner)){
      MAKE_BUFFER(25, 25) bf_data;
      JsonObject& response = bf_data.parseObject(registerScanner->payload);
      JsonObject& data = response.get<JsonObject>("data");
      user.name = data.get<String>("name");
      user.shirtSize = data.get<String>("shirtSize");
      user.diet = data.get<String>("diet");
      user.allow = true;

    }
    #ifdef DEBUG
      Serial.print("Response Code: ");
      Serial.println(registerScanner->code.toString());
    #endif
    return user;
  }

  HTTPCode Network::assignUserWID(String wid, int pin) {
    createRequest(API::POST, "/rfid/assign");
    addPayload("pin", String(pin));
    addPayload("wid", wid);
    addPayload("apikey", apiKey);
    Response* registerScanner = commit();
    #ifdef DEBUG
      Serial.print("Response Code: ");
      Serial.println(registerScanner->code.toString());
    #endif
    return registerScanner->code;
  }

  #if defined(OTA_PASSWORD) && defined(OTA_PASSWORD_HASH)
  void Network::enableOTA(){
    if(OTA_enabled) { return; }

    ArduinoOTA.setPort(8266);
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword((char*)OTA_PASSWORD);
    ArduinoOTA.setPasswordHash((char*)OTA_PASSWORD_HASH); // md5(OTA_PASSWORD)
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
          type = "sketch";
        } else { // U_SPIFFS
          type = "filesystem";
        }

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
          Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
          Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
          Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
          Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
          Serial.println("End Failed");
        }
    });

    ArduinoOTA.begin();
    OTA_enabled = true;

  }
  void Network::handleOTA(){
    ArduinoOTA.handle();
  }
  #endif
  User Network::userInfoFromWID(String wid) {
    createRequest(API::GET, "/rfid/user-info");
    Serial.println("WID: " + wid);
    addParameter("wid", wid);
    addParameter("apikey", apiKey);
    Response* registerScanner = commit();
    User user = {.name = "NULL", .shirtSize = "NULL", .diet = "NULL", .allow = false, .code = registerScanner->code };
    if(*registerScanner){
      MAKE_BUFFER(25, 25) bf_data;
      JsonObject& response = bf_data.parseObject(registerScanner->payload);
      JsonObject& data = response.get<JsonObject>("data");
      user.name = data.get<String>("name");
      user.shirtSize = data.get<String>("shirtSize");
      user.diet = data.get<String>("diet");
      user.allow = true;

    }
    #ifdef DEBUG
      Serial.print("Response Code: ");
      Serial.println(registerScanner->code.toString());
    #endif
    return user;
  }


  Response* Network::commit(bool reboot){
    return req->commit(reboot);
  }

  Locations* Network::getEvents() {
    createRequest(API::GET, "/rfid/events");
    Response* registerScanner = commit();
    Locations* list = new Locations();
    if(*registerScanner){
      Serial.println("Received list");
      MAKE_BUFFER(25, 25) bf_data;
      JsonObject& response = bf_data.parseObject(registerScanner->payload);
      int length = response.get<int>("length");
      Serial.println("Found "+String(length)+" events");
      JsonArray& jsonLoc = response.get<JsonArray>("locations");

      for(int i = 0; i < length; i++){
        //Serial.println("Adding evetn: " + String(jsonLoc[i]["event_title"].asString()));
        Location tmp(jsonLoc[i]["event_title"], jsonLoc[i]["uid"]);
        list->addLocation(tmp);
      }
    } else {
      Serial.println("Failed");
    }
    #ifdef DEBUG
      Serial.print("Response Code: ");
      Serial.println(registerScanner->code.toString());
    #endif
    return list;
  }

  User Network::sendScan(String wid, String loc) {
    createRequest(API::POST, "/rfid/scan");
    addPayload("wid", wid);
    addPayload("location", String(loc));
    addPayload("apikey", apiKey);
    Response* registerScanner = commit();
    User user = {.name = "NULL", .shirtSize = "NULL", .diet = "NULL", .allow = false, .code = registerScanner->code };
    if(*registerScanner){
      MAKE_BUFFER(25, 25) bf_data;
      JsonObject& response = bf_data.parseObject(registerScanner->payload);
      JsonObject& data = response.get<JsonObject>("data");

      user.name = data.get<String>("name");
      user.shirtSize = data.get<String>("shirtSize");
      user.diet = data.get<String>("diet");
      user.allow = !(data.get<bool>("isRepeat"));

    }

    #ifdef DEBUG
      Serial.print("Response Code: ");
      Serial.println(registerScanner->code.toString());
    #endif
    return user;
  }

  Items* Network::getItems(){
    createRequest(API::GET, "/rfid/items");
    addParameter("apikey", apiKey);
    Response* registerScanner = commit();
    Items* list = new Items();
    if(*registerScanner){
      Serial.println("Received list");
      MAKE_BUFFER(25, 25) bf_data;
      JsonObject& response = bf_data.parseObject(registerScanner->payload);
      int length = response.get<int>("length");
      //Serial.println("Found "+String(length)+" events");
      JsonArray& jsonLoc = response.get<JsonArray>("items");

      for(int i = 0; i < length; i++){
        //Serial.println("Adding evetn: " + String(jsonLoc[i]["event_title"].asString()));
        Item tmp(jsonLoc[i]["name"], jsonLoc[i]["uid"]);
        list->addItem(tmp);
      }
    } else {
      Serial.println("Failed");
    }
    #ifdef DEBUG
      Serial.print("Response Code: ");
      Serial.println(registerScanner->code.toString());
    #endif
    return list;
  }

  HTTPCode Network::itemCheckout(String wid, int iid){
    createRequest(API::POST, "/rfid/checkout");
    addPayload("apikey", apiKey);
    addPayload("wid", wid);
    addPayload("itemId", String(iid));

    // Response* checkout = commit();
    //HTTPCode res(checkout->code);
    HTTPCode res(501);  // Unimplemented
    return res;
  }

  HTTPCode Network::itemReturn(String wid, int iid){
    createRequest(API::POST, "/rfid/return");
    addPayload("apikey", apiKey);
    addPayload("wid", wid);
    addPayload("itemId", String(iid));

    Response* checkout = commit();
    HTTPCode res(checkout->code);
    return res;
    
  }
  String Network::localIP(){
    return WiFi.localIP().toString();
  }
}
