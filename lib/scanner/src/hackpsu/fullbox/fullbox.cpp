#include "fullbox.h"

namespace hackPSU{


Box::Box(String redis_addr, const char* ssid, const char* password, Mode_e mode, const byte* band_key) {
  // Create class objects
  scanner = new Scanner("scanner", RFID_SS, RFID_RST);
  display = new Display(mode);
  http    = new Network(REDIS);
  keypad  = new Keypad(KPD_SRC, KPD_CLK, KPD_SIG, display);

  location_list = new List<Event>();
  item_list = new List<Item>();
  menu_list = new List<MenuItem>();
  MenuItem* menuItem = new MenuItem();
  //add each menu item to menu_item list
  menuItem->heading = "Set & Scan";
  menuItem->loop = &location;
  menu_list->addItem(menuItem); //memcpy in list

  menuItem->heading = "Check-In";
  menuItem->loop = &checkin;
  menu_list->addItem(menuItem);

  menuItem->heading = "Item checkout";
  menuItem->loop = &item_checkout;
  menu_list->addItem(menuItem);

  menuItem->heading = "Item return";
  menuItem->loop = &item_return;
  menu_list->addItem(menuItem);

  menuItem->heading = "Show Name";
  menuItem->loop = &getuid;
  menu_list->addItem(menuItem);

  menuItem->heading = "WiFi info";
  menuItem->loop = &wifi;
  menu_list->addItem(menuItem);

  menuItem->heading = "Clone Master";
  menuItem->loop = &duplicate;
  menu_list->addItem(menuItem);

  menuItem->heading = "Zeroize";
  menuItem->loop = &zeroize;
  menu_list->addItem(menuItem);

  menuItem->heading = "Lock";
  menuItem->loop = &lock;
  menu_list->addItem(menuItem);

  menuItem->heading = "OTA update";
  menuItem->loop = &update;
  menu_list->addItem(menuItem);

  // Set default values
  menu_state = 0;
  strength = UNDEFINED;
  state = LOCK;
  last_scan = 0;

  OTA_enabled = false;

  display->print("WiFi connecting", 0);
  while(WiFi.status() != WL_CONNECTED)
    yield();

  display->print("Connected...", 0);
  display->print("Fetching API key", 1);


  if(!http->checkApiKey()){
    display->clear();
    display->print("Enter API pin: ", 0);
    while(true) {
      display->clear(1);
      String pin = keypad->getPin(10, '*', '#', 10000);
      if(pin != "timeout" && http->getApiKey(pin.toInt())){
        break;
      }
      yield();
    }
  } else {
    Serial.println("Successfully got API key");
  }
  display->clear();
}

Box::~Box() {
  delete scanner;
  delete keypad;
  delete http;
  delete display;
}

void Box::cycle(void) {
  //switch on state; default to init
  menu_list->getCurrent()->loop();
}

void Box::lock(){

    #ifdef SECURE_BOX
      display->print("Scan to unlock", 0);

      byte buffer[READ_BUFFER] = {0};
      if(GOOD_RF != scanner->getData(buffer, READ_BUFFER, KEY_BLOCK, SCAN_TIMEOUT))
        return;

      if(String(MASTER_KEY) == String((char*)buffer)){
        state = MENU;
        display->clear();
      }
    #else
      display->print("INSECURE BOOT!", 0);
      delay(1000);
      state = MENU;
    #endif

}

void Box::menu() {
  display->print('A', UP_C, 'B', DOWN_C, '#', CHECK_C, 'D', LOCK_C);
  display->print(menu_list->getCurrent()->heading, 1);

  switch (keypad->getUniqueKey(500)) {
    case 'A':
      menu_list->previous();
      break;
    case 'B':
      menu_list->next();
      break;
    case 'D':
      menu_cleanup();
      state = LOCK;
      break;
    case '#':
      MenuItem *curr = menu_list->getCurrent();
      curr->loop();
      break;
      menu_cleanup();
    }
}

void Box::menu_cleanup(){
  display->clear();
  menu_list->reset();

}

void Box::location(){
  display->print('#', CHECK_C, 'B', DOWN_C, 'C', SCROLL_C, 'D', BACK_C);

  // Do not select location based on a number
  switch (keypad->getUniqueKey(500)) {
    case 'A':
      location_list->next();
      return;
    case 'B':
      location_list->previous();
      return;
    case 'C':
       display->scroll();
       return;
    case 'D':
      location_list = new Locations();
      location_cleanup();
      state = MENU;
      return;
    case '#':
      lid = location_list->getCurrent()->id;
      location_name = location_list->getCurrent()->name;
      location_cleanup();
      state = SCAN_EVENT;
      return;
    default:
      if (location_list->numLocations()  == 0) {
        display->print("Updating list", 1);

        delete location_list;
        location_list = http->getEvents();
      }

      if(location_list->numLocations() > 0){
        display->print(location_list->getCurrent()->name, 1);
      } else {
        display->print("No locations found", 1);
        delay(2000);
        state = MENU;
        location_cleanup();
      }
  }
}

void Box::location_cleanup(){
  display->clear();
  delete location_list;
  location_list = new Locations();
}

void Box::scan_event() {
  display->print('*',NONE_C, '#', NONE_C, '\0', NONE_C, 'D', LOCK_C);

  uint32_t uid = 0;
  char lid_buffer[10] = {0};
  char uid_buffer[10] = {0};
  display->print("Scan wristband", 1);

  char input = keypad->getUniqueKey(750);
  switch (input) {
    case 'C':
      last_scan = 0; // reset last_scan
      break;
    case 'D':
      last_scan = 0;
      state = LOCK;
      display->clear();
      return;
    //Normal behavior = scan band
    default:
      uid = scanner->getUID(SCAN_TIMEOUT);
      if (uid && uid != last_scan) {
        display->toggleDisplay();
        User data = http->sendScan(String(uid), lid);
        switch( int(data.code) ) {
          case 200: display->print("Allow", 1);         break;
          case 404: display->print("Unknown user", 1);  break;
          case 401: display->print("Restarting...", 1); break;
          default:
            if(int(data.code) < 0 ){
              display->print("Network error", 1);
            } else {
              display->print("Deny - " + int(data.code) , 1);
              display->print(data.code.toString());
            }
        }
        //#error Handle entryScan
        delay(750);
        last_scan = uid;
      }
  }
}

/**
 * First half is getting registrant's pin
 * Second half is associating registrant to wristband
 */
void Box::checkin() {
  display->print('*',CLEAR_C, '#', CHECK_C, 'C', SCROLL_C, 'D', LOCK_C);

  String pin;
  char keypress;
  uint32_t uid;

  display->print("Enter pin: ", 1);
  pin = keypad->getPin(6, '*', '#', 10000);

  //Character press
  switch(pin[0]){
    case 'A':
    case 'B':
    case 'C':
      display->print("Invalid command", 1);
      delay(1000);
      return;
    case '\0':
      display->print("Invalid pin", 1);
      delay(1000);
      return;
    case 'D':
      state = LOCK;
      display->clear();
      return;
    case 't': // timeout
      return;
  }

  User data = http->getDataFromPin(pin.toInt());
  if (!data.allow){
    display->print("Invalid pin", 1);
    delay(2000);
    return;
  }

  display->print("Validate name:", 0);
  display->print(data.name, 1);

  // Enter next half of checkin: associating registrant with wristband
  keypress = keypad->getUniqueKey(5000);
  bool validated = false;
  do{
    switch(keypress){
      case '*': // Wrong person
        if( !validated){
          return;
        }
        break;
      case 'D':
        state = LOCK;
        display->clear();
        return;
      case 'C':
        display->scroll();
        break;
      case '#': // Name validated
        validated = true;
        break;
    }
    if(validated){
      display->print("Scan wristband", 0);
      display->clear(1);
      uid = scanner->getUID(SCAN_TIMEOUT);
      if(uid){
        MAKE_BUFFER(1, 0) bf_assign;

        JsonObject& assign = bf_assign.createObject();
        HTTPCode code = http->assignUserWID(String(uid), pin.toInt());

        if(!code){
          display->print("Try again - " + String(int(code)), 0);
          display->print(code.toString(), 2);
          delay(2500);
          return;
        }
      }
    }
    keypress = keypad->getUniqueKey(1200);
  } while (!uid);

  display->print("Shirt Size: ", 0);
  display->print(data.shirtSize);

  display->print("Photo consent?",1);

  // Require keypress to continue
  while(keypad->getUniqueKey(5000) == 't');

  display->clear();
}

void Box::wifi() {

  display->print('B', BACK_C, '\0', NONE_C, '\0', NONE_C, 'D', LOCK_C);
  switch(keypad->getUniqueKey(500)){
    case 'B':
      state = MENU;
      strength = UNDEFINED;
      display->clear();
      break;
    case 'D':
      state = LOCK;
      strength = UNDEFINED;
      display->clear();
      break;
    default:
      int32_t rssi;
      if (WiFi.status() != WL_CONNECTED) {
        display->print("WiFi Disconnect", 1);
        return;
      }

      rssi = WiFi.RSSI();

      if(rssi > -50) {
        display->print("Excellent signal", 1);
        strength = EXCELLENT;
      }  else if(rssi > -60) {
        display->print("Good signal", 1);
        strength = GOOD;
      } else if( rssi > -70) {
        display->print("Fair signal", 1);
        strength = FAIR;
      } else {
        display->print("Weak signal", 1);
        strength = WEAK;
      }
  }
}

void Box::duplicate() {
  RfidState lastState = GOOD_RF;
  display->print('\0', NONE_C, '\0', NONE_C, '\0', NONE_C, 'D', LOCK_C);
  display->print("Scan Target", 1);

  byte write_buffer[WRITE_BUFFER] = MASTER_KEY;
  byte read_buffer[READ_BUFFER] = {0};

  switch(keypad->getUniqueKey(2000)){
    case 'D':
      state = LOCK;
      display->clear();
      break;
    default:
      switch(scanner->setData(write_buffer, WRITE_BUFFER, KEY_BLOCK, SCAN_TIMEOUT)){
        case GOOD_RF:
          display->print("Target written", 0);
          display->print("Rescan target", 1);
          while((lastState = scanner->getData(read_buffer, READ_BUFFER, KEY_BLOCK, SCAN_TIMEOUT)) == TIMEOUT){
            yield();
          }
          switch(lastState){
              case READ_FAIL:
                display->print("Read Failure!", 1);
                break;
              case CRYPTO_FAIL:
                display->print("Crypo Failure!", 1);
               break;
              case GOOD_RF:
                if (String((char*)read_buffer) == String(MASTER_KEY)) {
                  display->print("Write success!", 1);
                } else {
                  display->print("Write failure!", 1);
                }
                delay(1000);
                break;
            }
          break;
        case WRITE_FAIL:
          display->print("Write Failure!", 1);
          break;
        case CRYPTO_FAIL:
          display->print("Crypto Failure!", 1);
      }
  }
}

void Box::zeroize() {
  RfidState lastState = GOOD_RF;
  display->print('\0', NONE_C, '\0', NONE_C, '\0', NONE_C, 'D', LOCK_C);
  display->print("Scan Target", 1);

  byte write_buffer[WRITE_BUFFER] = {0};
  byte read_buffer[READ_BUFFER] = {0};

  switch(keypad->getUniqueKey(2000)){
    case 'D':
      state = LOCK;
      display->clear();
      break;
    default:
      switch(scanner->setData(write_buffer, WRITE_BUFFER, KEY_BLOCK, SCAN_TIMEOUT)){
        case GOOD_RF:
          display->print("Target zeroized", 0);
          display->print("Rescan target", 1);
          while((lastState = scanner->getData(read_buffer, READ_BUFFER, KEY_BLOCK, SCAN_TIMEOUT)) == TIMEOUT){
            yield();
          }
          switch(lastState){
              case READ_FAIL:
                display->print("Read Failure!", 1);
                break;
              case CRYPTO_FAIL:
                display->print("Crypo Failure!", 1);
               break;
              case GOOD_RF:
                if (String((char*)read_buffer) == String((char*)read_buffer)) {
                  display->print("Write success!", 1);
                } else {
                  display->print("Write failure!", 1);
                }
                delay(1000);
                break;
            }
          break;
        case WRITE_FAIL:
          display->print("Write Failure!", 1);
          break;
        case CRYPTO_FAIL:
          display->print("Crypto Failure!", 1);
      }
  }
}

void Box::getuid(){
  display->print('#', CHECK_C, '\0', NONE_C, '\0', NONE_C, 'D', LOCK_C);
  display->print("Scan for UID", 1);

  byte read_buffer[READ_BUFFER] = {0};

  switch(keypad->getUniqueKey(2000)){
    case 'D':
      state = LOCK;
      display->clear();
      return;
    default:
     uint32_t uid = scanner->getUID(SCAN_TIMEOUT);
     Serial.println("UID: " + String(uid));
      if(uid){
        User usr = http->userInfoFromWID(String(uid));
        display->print(usr.name, 1);
        while(keypad->getUniqueKey(5000) == 't');
      }
  }
}

void Box::update(){
  display->print("OTA Enabled. IP:", 0);
  display->print(http->localIP(), 1);
  if(!OTA_enabled){
    http->enableOTA();
  }

  switch(keypad->getUniqueKey(750)){
  case 'D':
    state = LOCK;
    display->clear();
    return;
  default:
    http->handleOTA();
    break;
  }
}

void Box::item_checkout(){
  display->print('#', CHECK_C, 'B', DOWN_C, 'C', SCROLL_C, 'D', BACK_C);

  // Do not select item based on a number
  switch (keypad->getUniqueKey(500)) {
    case 'A':
      item_list->next();
      return;
    case 'B':
      item_list->previous();
      return;
    case 'C':
      display->scroll();
      return;
    case 'D':
      item_cleanup();
      state = MENU;
      return;
    case '#':
      iid = item_list->getCurrent()->id;
      item_name = item_list->getCurrent()->name;
      state = SCAN_ITEM;
      checkout = true;
      location_cleanup();
      break;
    default:
      if (item_list->numItems()  == 0) {
        display->print("Updating list", 1);

        delete item_list;
        item_list = http->getItems();
      }

      if(item_list->numItems() > 0){
        display->print(item_list->getCurrent()->name, 1);
      } else {
        display->print("No items found", 1);
        delay(2000);
        state = MENU;
        item_cleanup();
      }
  }
}

void Box::item_return(){
  display->print('#', CHECK_C, 'B', DOWN_C, 'C', SCROLL_C, 'D', BACK_C);

  switch (keypad->getUniqueKey(500)) {
    case 'A':
      item_list->next();
      return;
    case 'B':
      item_list->previous();
      return;
    case 'C':
      display->scroll();
      return;
    case 'D':
      item_cleanup();
      state = MENU;
      return;
    case '#':
      iid = item_list->getCurrent()->id;
      item_name = item_list->getCurrent()->name;
      state = SCAN_ITEM;
      checkout = false;
      location_cleanup();
      break;
    default:
      if (item_list->numItems()  == 0) {
        display->print("Updating list", 1);

        delete item_list;
        item_list = http->getItems();
      }

      if(item_list->numItems() > 0){
        display->print(item_list->getCurrent()->name, 1);
      } else {
        display->print("No items found", 1);
        delay(2000);
        state = MENU;
        item_cleanup();
      }
  }
}

void Box::scan_item() {

  display->print('*',NONE_C, '#', NONE_C, '\0', NONE_C, 'D', LOCK_C);

  uint32_t uid = 0;
  display->print("Scan wristband", 1);

  char input = keypad->getUniqueKey(1500);
  switch (input) {
    case 'C':
      last_scan = 0; // reset last_scan
      break;
    case 'D':
      last_scan = 0;
      state = LOCK;
      display->clear();
      return;
    //Normal behavior = scan band
    default:
      uid = scanner->getUID(SCAN_TIMEOUT);
      if (uid && uid != last_scan) {
        //User data = http->(String(uid), lid);
        HTTPCode code = checkout ? http->itemCheckout(String(uid), iid) : http->itemReturn(String(uid), iid);
        if (code) {
          display->print("Allow", 1);
          display->toggleDisplay();
        } else {
          display->print("Deny - " + int(code) , 1);
          display->print(code.toString(), 2);
          delay(2500);
          return;
        }
        switch( int(code) ) {
          case 200: display->print("Allow", 1);         break;
          case 404: display->print("Unknown user", 1);  break;
          case 401: display->print("Restarting...", 1); break;
          default:
            if(int(code) < 0 ){
              display->print("Network error", 1);
            } else {
              display->print("Deny - " + int(code) , 1);
              display->print(code.toString());
            }
        }
        //#error Handle entryScan
        delay(750);
        last_scan = uid;
      }
  }
}

void Box::item_cleanup(){
  delete item_list;
  item_list = new Items();
}
}