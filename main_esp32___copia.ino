#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "ac-test.h" 

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define PROTOCOL_CHAR_UUID  "a3d5f8e2-4b7c-11ef-9a1d-0242ac120002"

BLECharacteristic *pCharacteristic;
BLECharacteristic *pProtocolCharacteristic;

class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string valor_rec_ble = pCharacteristic->getValue();
        
        if (valor_rec_ble.length() == 0) return;

        String command = String(valor_rec_ble.c_str());
        command.trim();

        Serial.print("BLE Received: '");
        Serial.print(command);
        Serial.print("' (length: ");
        Serial.print(command.length());
        Serial.println(")");

        if (command.equalsIgnoreCase("ACTEST")) {
           Serial.println("Command: Start Scan");
           EmpezarEscaneo();
        } 
        else if (command.equalsIgnoreCase("YES")) {
           Serial.println("Command: User said YES");
           RespuestaUsuario(true);
           pProtocolCharacteristic->setValue("FOUND_DONE");
           pProtocolCharacteristic->notify();
        } 
        else if (command.equalsIgnoreCase("NO")) {
           Serial.println("Command: User said NO");
           RespuestaUsuario(false);
        }
    }
};

void setup() {
  Serial.begin(115200);
  
  opEscaneo(); 

  Serial.println("Starting BLE server setup!");
  BLEDevice::init("PEA - BLE Server Test");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE |
                                         BLECharacteristic::PROPERTY_NOTIFY 
                                       );

  pCharacteristic->setValue("Ready");
  
  pCharacteristic->setCallbacks(new MyCallbacks());

  pProtocolCharacteristic = pService->createCharacteristic(
                                         PROTOCOL_CHAR_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_NOTIFY 
                                       );

  pProtocolCharacteristic->setValue("Waiting");

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Characteristic defined and advertising!");
}

void loop() {
  bucleEscaneo(pProtocolCharacteristic);
  delay(10);
}