#pragma once

#define GOLDEN_KEY "C0DEBABE"

namespace hackPSU{

  typedef enum State {BOOT, MENU, MAKE, WIFI, LOCATE, REGISTER, SCAN} State_e;

  class hackPSUstatemachine{
    private:
      State_e state;
      byte[4] key;
      int keylen;
      
      State_e boot_state(void);
      State_e menu_state(void);
      State_e make_master_state(void);
      State_e wifi_state(void);
      State_e locate_state(void);
      State_e register_state(void);
      State_e scan_state(void);
      State_e gotoRoot(void);
      
      void restart_machine(void);
      
    public:
      hackPSUstatemachine(
        byte[4] key, 
        int keylen, 
        hackPSUrfid* scanner, 
        hackPSUlcd* lcd, 
        hackPSUkeypad* keypad
      ): key(key), kenlen(keylen){ state = BOOT };

      // Call cycle from loop.
      void cycle(); // runs one state at a time
  };
}

