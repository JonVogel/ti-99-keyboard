/*
 * BleHidHost — BLE HID keyboard host for ESP32-S3 (Bluedroid stack).
 *
 * Scans for HID keyboards, connects, subscribes to input reports, and
 * forwards raw HID reports to a caller-supplied callback. The caller
 * decides what to do with them (TI matrix, ASCII, scancodes, etc.).
 *
 * Usage:
 *
 *   #include <BleHidHost.h>
 *
 *   void onHidReport(const uint8_t* data, size_t len) { ... }
 *
 *   void setup() {
 *     BleHidHost::setReportCallback(onHidReport);
 *     BleHidHost::begin("MyDeviceName");
 *   }
 *
 *   void loop() {
 *     BleHidHost::task();
 *   }
 *
 * Pairing mode (forgets the bonded peer and accepts any HID keyboard for
 * 30 seconds) can be triggered with BleHidHost::enterPairingMode().
 *
 * State queries:
 *   BleHidHost::isConnected()    — true once GATT is connected
 *   BleHidHost::isReady()        — true once HID notifications are live
 *   BleHidHost::inPairingMode()  — true during the pairing window
 */

#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <Preferences.h>

class BleHidHost
{
public:
  typedef void (*ReportCallback)(const uint8_t* data, size_t len);

  static void setReportCallback(ReportCallback cb) { _cb = cb; }
  static void begin(const char* deviceName, const char* nvsNamespace = "blehidhost");
  static void task();
  static void enterPairingMode(uint32_t windowMs = 30000UL);

  // Safe to call from any context (including BLE notify callbacks).
  // The actual pairing-mode transition runs from task() on the main loop.
  static void requestPairingMode() { _pairingRequested = true; }

  static bool isConnected()    { return _connected; }
  static bool isReady()        { return _ready; }
  static bool inPairingMode()  { return _pairingMode; }

private:
  // HID service / report characteristic UUIDs
  static BLEUUID _hidServiceUUID;
  static BLEUUID _reportCharUUID;

  // State
  static ReportCallback _cb;
  static BLEClient* _client;
  static BLEAdvertisedDevice* _target;
  static volatile bool _connected;
  static volatile bool _ready;
  static volatile bool _doConnect;
  static volatile bool _doScan;
  static volatile bool _pairingMode;
  static volatile bool _pairingRequested;
  static unsigned long _pairingDeadline;
  static uint32_t _pairingWindowMs;

  static Preferences _prefs;
  static String _savedAddress;
  static const char* _nvsNamespace;
  static const char* _nvsKeyAddr;

  static const int _bootButtonPin = 0;

  // Internal
  static void _notifyCb(BLERemoteCharacteristic* pChar,
                        uint8_t* data, size_t len, bool isNotify);
  static bool _connectToServer();

  // Callback classes (inner)
  class ClientCallbacks;
  class ScanCallbacks;
};

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------
inline BLEUUID BleHidHost::_hidServiceUUID((uint16_t)0x1812);
inline BLEUUID BleHidHost::_reportCharUUID((uint16_t)0x2A4D);

inline BleHidHost::ReportCallback BleHidHost::_cb = nullptr;
inline BLEClient* BleHidHost::_client = nullptr;
inline BLEAdvertisedDevice* BleHidHost::_target = nullptr;
inline volatile bool BleHidHost::_connected = false;
inline volatile bool BleHidHost::_ready = false;
inline volatile bool BleHidHost::_doConnect = false;
inline volatile bool BleHidHost::_doScan = false;
inline volatile bool BleHidHost::_pairingMode = false;
inline volatile bool BleHidHost::_pairingRequested = false;
inline unsigned long BleHidHost::_pairingDeadline = 0;
inline uint32_t BleHidHost::_pairingWindowMs = 30000UL;

inline Preferences BleHidHost::_prefs;
inline String BleHidHost::_savedAddress = "";
inline const char* BleHidHost::_nvsNamespace = "blehidhost";
inline const char* BleHidHost::_nvsKeyAddr = "peer_addr";

// ---------------------------------------------------------------------------
// Inner callback classes
// ---------------------------------------------------------------------------
class BleHidHost::ClientCallbacks : public BLEClientCallbacks
{
  void onConnect(BLEClient* client) override
  {
    Serial.println("BleHidHost: Connected.");
    BleHidHost::_connected = true;
  }
  void onDisconnect(BLEClient* client) override
  {
    Serial.println("BleHidHost: Disconnected.");
    BleHidHost::_connected = false;
    BleHidHost::_ready = false;
    BleHidHost::_doScan = true;
  }
};

class BleHidHost::ScanCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice) override
  {
    String addr = advertisedDevice.getAddress().toString();
    bool isHid = advertisedDevice.haveServiceUUID() &&
                 advertisedDevice.isAdvertisingService(_hidServiceUUID);
    bool isKnownPeer = (_savedAddress.length() > 0) &&
                       addr.equalsIgnoreCase(_savedAddress);

    if (isHid || isKnownPeer)
    {
      Serial.printf("BleHidHost: Found %s (%s)\n",
                    advertisedDevice.getName().c_str(), addr.c_str());
      BLEDevice::getScan()->stop();
      if (_target != nullptr) delete _target;
      _target = new BLEAdvertisedDevice(advertisedDevice);
      _doConnect = true;
      _doScan = true;
    }
  }
};

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------
inline void BleHidHost::_notifyCb(BLERemoteCharacteristic* pChar,
                                   uint8_t* data, size_t len, bool isNotify)
{
  if (_cb != nullptr)
  {
    _cb(data, len);
  }
}

inline bool BleHidHost::_connectToServer()
{
  Serial.printf("BleHidHost: Connecting to %s (%s)...\n",
                _target->getName().c_str(),
                _target->getAddress().toString().c_str());

  _client = BLEDevice::createClient();
  _client->setClientCallbacks(new ClientCallbacks());
  _client->connect(_target);
  _client->setMTU(185);

  BLERemoteService* pHidService = _client->getService(_hidServiceUUID);
  if (pHidService == nullptr)
  {
    Serial.println("BleHidHost: HID service not found.");
    _client->disconnect();
    return false;
  }

  int subscribed = 0;
  std::map<std::string, BLERemoteCharacteristic*>* pCharMap =
    pHidService->getCharacteristics();

  for (auto const& entry : *pCharMap)
  {
    BLERemoteCharacteristic* pChar = entry.second;
    if (pChar->getUUID().equals(_reportCharUUID) && pChar->canNotify())
    {
      BLERemoteDescriptor* pReportRef =
        pChar->getDescriptor(BLEUUID((uint16_t)0x2908));
      if (pReportRef != nullptr)
      {
        String refValue = pReportRef->readValue();
        if (refValue.length() >= 2 && refValue[1] == 1)   // input report
        {
          pChar->registerForNotify(_notifyCb);
          subscribed++;
        }
      }
      else
      {
        pChar->registerForNotify(_notifyCb);
        subscribed++;
      }
    }
  }

  if (subscribed == 0)
  {
    Serial.println("BleHidHost: No input reports found.");
    _client->disconnect();
    return false;
  }

  _ready = true;
  _pairingMode = false;
  Serial.printf("BleHidHost: Ready. %d input report(s).\n", subscribed);

  // Persist peer address for fast reconnect
  String connectedAddr = _target->getAddress().toString();
  if (connectedAddr != _savedAddress)
  {
    _savedAddress = connectedAddr;
    _prefs.begin(_nvsNamespace, false);
    _prefs.putString(_nvsKeyAddr, _savedAddress);
    _prefs.end();
    Serial.printf("BleHidHost: Saved peer address %s\n", _savedAddress.c_str());
  }
  return true;
}

inline void BleHidHost::begin(const char* deviceName, const char* nvsNamespace)
{
  _nvsNamespace = nvsNamespace;

  _prefs.begin(_nvsNamespace, true);
  _savedAddress = _prefs.getString(_nvsKeyAddr, "");
  _prefs.end();
  if (_savedAddress.length() > 0)
  {
    Serial.printf("BleHidHost: Known peer %s\n", _savedAddress.c_str());
  }

  BLEDevice::init(deviceName);
  BLESecurity* pSecurity = new BLESecurity();
  pSecurity->setCapability(ESP_IO_CAP_NONE);
  pSecurity->setAuthenticationMode(true, false, true);
  // Register default security callbacks — required on some boards for
  // bonding to complete before service discovery. Without this, HID
  // keyboards like the L75 expose only generic services on first connect.
  BLEDevice::setSecurityCallbacks(new BLESecurityCallbacks());

  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
  pScan->setInterval(1349);
  pScan->setWindow(449);
  pScan->setActiveScan(true);
  pScan->start(5, false);
  _doScan = true;

  pinMode(_bootButtonPin, INPUT_PULLUP);
  Serial.println("BleHidHost: Scanning...");
}

inline void BleHidHost::enterPairingMode(uint32_t windowMs)
{
  Serial.printf("BleHidHost: Entering pairing mode (%lus).\n", (unsigned long)(windowMs / 1000));
  _pairingMode = true;
  _pairingWindowMs = windowMs;
  _pairingDeadline = millis() + windowMs;

  _savedAddress = "";
  _prefs.begin(_nvsNamespace, false);
  _prefs.remove(_nvsKeyAddr);
  _prefs.end();

  if (_client != nullptr && _client->isConnected())
  {
    _client->disconnect();
    delay(200);
  }

  BLEScan* pScan = BLEDevice::getScan();
  pScan->stop();
  pScan->clearResults();
  _doScan = true;
}

inline void BleHidHost::task()
{
  if (_pairingRequested)
  {
    _pairingRequested = false;
    enterPairingMode(_pairingWindowMs);
  }

  if (_doConnect)
  {
    _doConnect = false;
    _connectToServer();
  }

  if (_pairingMode && millis() > _pairingDeadline)
  {
    Serial.println("BleHidHost: Pairing window expired.");
    _pairingMode = false;
  }

  if (!_connected && _doScan)
  {
    _doScan = false;
    BLEScan* pScan = BLEDevice::getScan();
    pScan->clearResults();
    pScan->start(5, false);
    _doScan = true;
  }

  // BOOT button → pairing mode
  if (digitalRead(_bootButtonPin) == LOW)
  {
    delay(50);
    if (digitalRead(_bootButtonPin) == LOW)
    {
      enterPairingMode(_pairingWindowMs);
      while (digitalRead(_bootButtonPin) == LOW) { delay(50); }
    }
  }
}
