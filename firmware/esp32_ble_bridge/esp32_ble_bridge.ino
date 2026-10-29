/*
 * AlphaX — ESP32 BLE Bridge
 * Team AlphaX, BMED 2310
 *
 * ROLE
 *   The ESP32 is only a radio. It advertises a BLE service, hands any command
 *   it receives straight to the STM32 over UART, and relays the STM32's status
 *   lines back out as BLE notifications. No session logic lives here, so the
 *   Nucleo stays the single source of truth for what the hardware is doing.
 *
 * WIRING (ESP32 DevKit  <->  NUCLEO-F303RE)
 *   ESP32 GPIO16 (RX2)  <--  Nucleo PA9  (D8, TX)
 *   ESP32 GPIO17 (TX2)  -->  Nucleo PA10 (D2, RX)
 *   ESP32 GND           <--> Nucleo GND        <-- REQUIRED, common ground
 *
 *   Both boards are 3.3 V logic, so the UART lines connect directly. No level
 *   shifter needed.
 *
 * POWER
 *   Power the ESP32 from its own USB cable, or from the Nucleo's 5V pin into
 *   the ESP32 VIN. Do NOT power it from the Nucleo's 3V3 pin — the ESP32 pulls
 *   ~250 mA spikes when the radio transmits and will brown out.
 *
 *   Do NOT connect the 9 V battery to the ESP32 anywhere. The 9 V rail belongs
 *   to the LT1637 output stage only. The two share a ground and nothing else.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define DEVICE_NAME "AlphaX-01"

// UART to the Nucleo
#define UART_RX_PIN 16
#define UART_TX_PIN 17
#define UART_BAUD   115200

// BLE UUIDs — must match the controller app
#define SERVICE_UUID     "6e5a0001-b5a3-f393-e0a9-e50e24dcca9e"
#define CMD_CHAR_UUID    "6e5a0002-b5a3-f393-e0a9-e50e24dcca9e"  // app -> device
#define STATUS_CHAR_UUID "6e5a0003-b5a3-f393-e0a9-e50e24dcca9e"  // device -> app

BLECharacteristic *statusChar = nullptr;
BLEServer         *bleServer  = nullptr;
bool deviceConnected = false;

String uartLine = "";

// ------------------------------------------------------------- callbacks ---

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    deviceConnected = true;
    Serial.println("[ble] app connected");
    Serial2.println("HELLO");          // let the STM32 know someone is watching
  }

  void onDisconnect(BLEServer *s) override {
    deviceConnected = false;
    Serial.println("[ble] app disconnected -> sending STOP");
    // Fail safe: if the phone walks away, the session ends.
    Serial2.println("STOP");
    s->startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    // ESP32 core 3.x returns an Arduino String here; core 2.x returned
    // std::string. Both are handled so the sketch builds either way.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    String cmd = c->getValue();
#else
    std::string v = c->getValue();
    if (v.empty()) return;
    String cmd = String(v.c_str());
#endif
    cmd.trim();
    if (cmd.length() == 0) return;

    Serial.printf("[ble->uart] %s\n", cmd.c_str());
    Serial2.println(cmd);              // straight through to the Nucleo
  }
};

// ------------------------------------------------------------------ main ---

void setup() {
  Serial.begin(115200);                                  // USB debug
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  delay(300);
  Serial.println("\nAlphaX BLE bridge starting");

  // setMTU must come before init. Without it, BLE notifications are capped
  // at 20 bytes and the status JSON arrives truncated and unparseable.
  BLEDevice::setMTU(247);
  BLEDevice::init(DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());

  BLEService *service = bleServer->createService(SERVICE_UUID);

  BLECharacteristic *cmdChar = service->createCharacteristic(
      CMD_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  cmdChar->setCallbacks(new CommandCallbacks());

  statusChar = service->createCharacteristic(
      STATUS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusChar->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.printf("Advertising as %s\n", DEVICE_NAME);
}

void loop() {
  // Read status lines from the Nucleo, one line at a time, and notify the app.
  while (Serial2.available()) {
    char ch = (char)Serial2.read();

    if (ch == '\n' || ch == '\r') {
      if (uartLine.length() > 0) {
        Serial.printf("[uart->ble] %s\n", uartLine.c_str());

        if (deviceConnected && statusChar) {
          statusChar->setValue((uint8_t *)uartLine.c_str(), uartLine.length());
          statusChar->notify();
        }
        uartLine = "";
      }
    } else {
      uartLine += ch;
      if (uartLine.length() > 250) uartLine = "";   // runaway guard
    }
  }
}
