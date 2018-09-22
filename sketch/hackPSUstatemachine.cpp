#include "hackPSUstatemachine.h"

namespace hackPSU {

Box::Box(String redis_addr, const char* ssid, const char* password, Mode_e mode, const byte* band_key) {
  scanner = new Scanner(RFID_SS, RFID_RST);
  http =    new HTTPImpl(redis_addr);
  display = new Display(mode);
  keypad =  new Keypad(KPD_SRC, KPD_CLK, KPD_SIG, display);
  rssi_old = 0xFFFFFFFF;

  //Hit up that wifi boi
  while (!WiFi.begin(ssid, password))
    yield();
}

Box::~Box() {
  delete scanner;
  delete keypad;
  delete http;
  delete display;
  Serial.println("Deleted all Box members");
}

void Box::cycle(void) {
  //switch on state; default to init
  switch (state) {
    case INIT:
      init();
      break;
    case MENU:
      menu();
      break;
    case DUPLICATE:
      duplicate();
      break;
    case WIFI:
      wifi();
      break;
    case LOCATION:
      location();
      break;
    case CHECKIN:
      checkin();
      break;
    case SCAN:
      scan();
      break;
    default:
      state = INIT;
      break;
  }
}

void Box::menu() {
  /*
    |-> 1:Set & Scan
    |-> Location list
    |-> 2:WiFi info
    |-> 3:Clone WB*/

  switch (keypad->getUniqueKey(500)) {

  }
}

void Box::wifi(void) {

  char buff[16] = {0};
  int32_t rssi;

  if (WiFi.status() != WL_CONNECTED) {
    display->print("WiFi Disconnect", 0);
    return;
  }
  byte available_networks = WiFi.scanNetworks();
  for (int network = 0; network < available_networks; network++) {
    if (WiFi.SSID(network), String(SSID)) {
      rssi = WiFi.RSSI(network);
      break;
    }
  }

  //Only print on significant change
  if ( abs(rssi - rssi_old ) <= 5) {
    sprintf("RSSI %d dBm", buff, rssi);
    display->print(SSID, 0);
    display->print(buff, 1);
    rssi_old = rssi;
  }

  if (keypad->getUniqueKey(500) == 'B')
    state = MENU;

}

void Box::scan(void) {

  char input = keypad->readKeypad();

  switch (input) {
    //B -> escape up
    case 'B':
      state = MENU;
      return;
    //C -> escape out
    case 'C':
      state = INIT;
      return;
    //Normal behavior = scan band
    default:
      Serial.println("Please scan wristband");
      uint32_t uid = scanner->getUID();
      char lid_buffer[10] = {0};
      char uid_buffer[10] = {0};
      Serial.println(uid);
      if (uid != last_scan) {
        if (http->entryScan(itoa(lid, lid_buffer, 10), itoa(uid, uid_buffer, 10))) {
          //TODO LCD->print Allow
          Serial.println("Allow");
        } else {
          //TODO LCD->print Deny
          Serial.println("Deny");
        }
        last_scan = uid;
      }
      //No state transition
  }

}

void Box::duplicate(void) {
  scanner->setUID(GOLDEN_KEY);
  state = MENU;
}



}
