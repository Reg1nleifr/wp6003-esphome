#include "esphome.h"
#include "esp_log.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <time.h>
#include <map>



#define SERVICE_UUID       "0000FFF0-0000-1000-8000-00805F9B34FB"
#define COMMAND_UUID       "0000FFF1-0000-1000-8000-00805F9B34FB"
#define SENSOR_UUID        "0000FFF4-0000-1000-8000-00805F9B34FB"

static BLEUUID  serviceUUID(SERVICE_UUID);
static BLEUUID  commandUUID(COMMAND_UUID);
static BLEUUID  sensorUUID(SENSOR_UUID);



enum ConnectionStatus { pending, connected, disconnected, error };



    
/***
   Internal Class for BLE Advertisement Callback
  ***/
class WP6003BLEAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  private:
    WP6003BLEDevice* wp6003bledevice;

  public:
    WP6003BLEAdvertisedDeviceCallbacks(WP6003BLEDevice* wp6003bledevice) : BLEAdvertisedDeviceCallbacks() {
      ESP_LOGD("wp6003ble_class", "Creating callback function");
      this->wp6003bledevice = wp6003bledevice;
    }

    /***
       Callback function on Advertisement Result
      ***/
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      ESP_LOGD("wp6003_ble_class", "%s", wp6003bledevice->deviceName.c_str());
      ESP_LOGD("wp6003_ble_class", "BLE Advertised Device found: %s, %s", advertisedDevice.toString().c_str(), advertisedDevice.getName().c_str());

      if (advertisedDevice.getName() == wp6003bledevice->deviceName) {
        ESP_LOGD("wp6003_ble_class", "Correct Device Found: %s", advertisedDevice.getAddress().toString().c_str());
        ESP_LOGD("wp6003_ble_class", "%s");
        advertisedDevice.getScan()->stop();
        wp6003bledevice->pServerAddress = new BLEAddress(advertisedDevice.getAddress());
        wp6003bledevice->connectionStatus = pending;
      }
      delay(10);
    }
};


class WP6003BLEDevice {

  private:
    int notifyRequest;
    int notificationInterval;
    int timeoutInterval = 1200; // ~2 min
    std::string deviceName;
    
    ConnectionStatus connectionStatus;
    time_t notify_time;
    BLEScan *pBLEScan;
    BLEAddress *pServerAddress;
    BLERemoteCharacteristic* pRemoteCommand;
    BLERemoteCharacteristic* pRemoteSensor;

    // current sensor values
    double temp;
    double tvoc;
    double hcho;
    double co2;
    
    static std::map<BLERemoteCharacteristic*, WP6003BLEDevice*> remoteSensorToDevice;
    
    /***
       Member function called by static method notifyCallback
     ***/
    void onNotification(
      BLERemoteCharacteristic* pBLERemoteCharacteristic,
      uint8_t* pData,
      size_t length,
      bool isNotify) {

      ESP_LOGD("wp6003_ble_class", "%s", "Notify callback for characteristic");
      ESP_LOGD("wp6003_ble_class", "%s", pBLERemoteCharacteristic->getUUID().toString().c_str());
      ESP_LOGD("wp6003_ble_class", " data length:%d", length);
      // ESP_LOGD("wp6003_ble_class", "%s" pData);
      ESP_LOGD("wp6003_ble_class", "Starting getting back data");
      if (pData[0] != 0x0a) {
        return;
      }

      this->temp  = (pData[6] * 256 + pData[7]) / 10.0;
      this->tvoc  = (pData[10] * 256 + pData[11]) / 1000.0;
      this->hcho  = (pData[12] * 256 + pData[13]) / 1000.0;
      this->co2   = (pData[16] * 256 + pData[17]);

    //  ESP_LOGD("wp6003_ble_class", "now: "); ESP_LOGD("wp6003_ble_class", "%s", time(NULL));
     ESP_LOGD("wp6003_ble_class", "Time: 20%02d/%02d/%02d %02d:%02d\n", pData[1], pData[2], pData[3], pData[4], pData[5]);
     ESP_LOGD("wp6003_ble_class", "Inside Class: Current Temp: %f\n", temp);
     ESP_LOGD("wp6003_ble_class", "Inside Class: Current TVOC: %f\n", tvoc);
     ESP_LOGD("wp6003_ble_class", "Inside Class: Current HCHO: %f\n", hcho);
     ESP_LOGD("wp6003_ble_class", "Inside Class: Current CO2: %f\n", co2);
      
      notify_time = time(NULL);
    }

    /***
        Static Callback on BLE notification, calls mapped onNotification member function
     ***/
    static void notifyCallback(
      BLERemoteCharacteristic* pBLERemoteCharacteristic,
      uint8_t* pData,
      size_t length,
      bool isNotify) {
      remoteSensorToDevice[pBLERemoteCharacteristic]->onNotification(pBLERemoteCharacteristic, pData, length, isNotify);
    }


    /***
        Initialize the ble connection from ESP32 client to WP6003 server for getting data.
     ***/
    bool connectToServer(BLEAddress pAddress) {
      ESP_LOGD("wp6003_ble_class", "Forming a connection to %s", pAddress.toString().c_str());

      BLEClient*  pClient = BLEDevice::createClient();
      pClient->connect(pAddress);
      BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
      ESP_LOGD("wp6003_ble_class", "Success Connection to %s", pAddress.toString().c_str());


      if (pRemoteService == nullptr) {
        ESP_LOGD("wp6003_ble_class", "Failed to find our service UUID: %s", serviceUUID.toString().c_str());
        return false;
      }

      pRemoteCommand = pRemoteService->getCharacteristic(commandUUID);
      if (pRemoteCommand == nullptr) {
        ESP_LOGD("wp6003_ble_class", "Failed to find our command UUID: %s", serviceUUID.toString().c_str());
        return false;
      }
      
      pRemoteSensor = pRemoteService->getCharacteristic(sensorUUID);
      // Map Sensor to Device
      remoteSensorToDevice[pRemoteSensor] = this;
      
      if (pRemoteSensor == nullptr) {
        ESP_LOGD("wp6003_ble_class", "Failed to find our sensor UUID: %s", serviceUUID.toString().c_str());
        return false;
      }
      ESP_LOGD("wp6003_ble_class", " - Found our Sensor, starting connection");

#if 0
      delay(1000);
      ESP_LOGD("wp6003_ble_class", "send initialize command 'ee'");
      uint8_t command_ee[] = { 0xee };
      pRemoteCommand->writeValue(command_ee, sizeof(command_ee));
      delay(2000);
#endif

      notify_time = time(NULL);
      ESP_LOGD("wp6003_ble_class", "send register notify");
      pRemoteSensor->registerForNotify(notifyCallback);
      // setLEDsColor(25, 0x0000f0);
      delay(500);

      time_t t = time(NULL);
      struct tm *tm;
      tm = localtime(&t);
      uint8_t command_aa[] = { 0xaa, (uint8_t)(tm->tm_year % 100), (uint8_t)(tm->tm_mon + 1), (uint8_t)(tm->tm_mday),
                            (uint8_t)(tm->tm_hour), (uint8_t)(tm->tm_min), (uint8_t)(tm->tm_sec)
                          };
      pRemoteCommand->writeValue(command_aa, sizeof(command_aa));
      delay(500);

      ESP_LOGD("wp6003_ble_class", "send notify interval 'ae'"); //0xae = 1 minute?
      uint8_t command_ae[] = { 0xae, 0x01, 0x01 }; // notify every 1min // { 0xae, 0x02, 0x02 } => every 30s
      pRemoteCommand->writeValue(command_ae, sizeof(command_ae));
      delay(500);

      ESP_LOGD("wp6003_ble_class", "send notify request");
      static uint8_t command_ab[] = { 0xab };
      pRemoteCommand->writeValue(command_ab, sizeof(command_ab));
      delay(500);

      return true;
    }
    

  public: 
    WP6003BLEDevice(std::string deviceName) {
      this->deviceName = deviceName;
      this->notifyRequest = 0;
      this->notificationInterval = 600; // 1min
      this->timeoutInterval = 1200; // ~2 min
      this->connectionStatus = disconnected;
    }

    // current sensor values
    double getTemp() { return this->temp; }
    double getTvoc() { return this->tvoc; }
    double getHcho() { return this->hcho; }
    double getCo2() { return this->co2; }
    int getError() { return this->connectionStatus; }


    /***
        Setup BLE Connection and startup function.
     ***/
    void setupBLEConnection() {
      ESP_LOGD("wp6003_ble_class", "Creating Connection");
      BLEDevice::init("wp6003");
      pBLEScan = BLEDevice::getScan(); // hopefully works..
      pBLEScan->setAdvertisedDeviceCallbacks(new WP6003BLEAdvertisedDeviceCallbacks(this));
      pBLEScan->setActiveScan(true);
      ESP_LOGD("wp6003_ble_class", "Starting Scan BLE");
      pBLEScan->start(10, true); // do the actual scan here!
      ESP_LOGD("wp6003_ble_class", "Done Scanning BLE");
      // pBLEScan->setInterval(100); //?
      // pBLEScan->setWindow(99); //?
      ESP_LOGD("wp6003_ble_class", "End of BLE setup, waiting for callback..");
    }

    /***
        Connects to the sensor and requests data.
     ***/
    void updateSensorData() {
      ESP_LOGD("wp6003_ble_class", "Updating BLE Sensor data");
      if (connectionStatus == pending) {
        if (connectToServer(*pServerAddress)) {
          ESP_LOGD("wp6003_ble_class", "We are now connected to the BLE Server.");
          connectionStatus = connected;
        } else {
          ESP_LOGD("wp6003_ble_class", "We have failed to connect to the server; there is nothin more we will do.");
          connectionStatus = disconnected;
        }
      }
      ESP_LOGD("wp6003_ble_class", "After 1st condition in updateSensorData()..");
      if (connectionStatus == connected) {
        notifyRequest++;
        if (notifyRequest > notificationInterval) { // 60 sec
          ESP_LOGD("wp6003_ble_class", "send requesting notify 'ab'"); // request notify
          static uint8_t command_ab[] = { 0xab };
          pRemoteCommand->writeValue(command_ab, sizeof(command_ab));
          notifyRequest = 0;
          time_t now = time(NULL);
          if (now > notify_time + timeoutInterval) { // if no notify in default ~20 min.
            // ESP.restart();
            connectionStatus = error;
            ESP_LOGD("wp6003_ble_class", "connection error on wp6003"); // request notify
            // there's an error :( maybe I can set an error State here..
          }
        }
      }
      ESP_LOGD("wp6003_ble_class", "After 2nd condition in updateSensorData()..");
      pBLEScan->clearResults(); // delete scans / release memory.
      delay(100);
      ESP_LOGD("wp6003_ble_class", "Scan Results cleared!");
    }
};

// BLERemoteCharacteristic to WP6003BLEDevice
std::map<BLERemoteCharacteristic*, WP6003BLEDevice*> WP6003BLEDevice::remoteSensorToDevice;


class WP6003 : public PollingComponent {
 public:
  Sensor *temperature_sensor = new Sensor();
  Sensor *tvoc_sensor = new Sensor();
  Sensor *hcho_sensor = new Sensor();
  Sensor *co2_sensor = new Sensor();
  Sensor *error_sensor = new Sensor();
  std::string deviceName;
  WP6003BLEDevice* wp6003ble;

  // constructor
  // Pull all 60 Seconds
  WP6003(std::string deviceName, int pollingInterval) : PollingComponent(pollingInterval) {
    this->deviceName = deviceName;
  }

  void setup() override {
    // This will be called by App.setup()
    Serial.begin(115200);
    wp6003ble = new WP6003BLEDevice(deviceName); // initialize the new device
    ESP_LOGD("wp", "Setting up BLE Connection started.");
    wp6003ble->setupBLEConnection();
    ESP_LOGD("wp", "Setting up BLE Connection done.");
  }

  void update() override {
    // This will be called every "update_interval" milliseconds.
    wp6003ble->updateSensorData(); // lets not get updates anymore and see if that changes anything!
    ESP_LOGD("wp", "The value of temperature sensor is: %f", wp6003ble->getTemp());
    temperature_sensor->publish_state(wp6003ble->getTemp());
    tvoc_sensor->publish_state(wp6003ble->getTvoc());
    hcho_sensor->publish_state(wp6003ble->getHcho());
    co2_sensor->publish_state(wp6003ble->getCo2());
    error_sensor->publish_state(wp6003ble->getError());
  }
};
