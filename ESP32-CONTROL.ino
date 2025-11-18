#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <string>
#include <LittleFS.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <freertos/semphr.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define LEARNING_TIMEOUT_MS 15000

const uint16_t kRecvPin = 36;
const uint16_t kIrLed = 33;
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 50;
const uint16_t kMinUnknownSize = 12;

IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
IRsend irsend(kIrLed);

enum State { IDLE, LEARNING };
volatile State currentState = IDLE;
volatile unsigned long learningStartTime = 0;
String targetButton = "";

SemaphoreHandle_t shared_data_mutex;
volatile bool send_ir_flag = false;
String file_to_send = "";

BLEAdvertising *pAdvertising = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
decode_results results;
TaskHandle_t ir_send_task_handle = NULL;

String buttonToFilename(const String& button) {
    return "/" + button + "_code.txt";
}

void writeCodeToFile(const String& filename, const char* code) {
    File file = LittleFS.open(filename, "w");
    if (!file) {
        Serial.println("Failed to open file for writing: " + filename);
        return;
    }
    if (file.print(code)) {
        Serial.println("IR code saved to " + filename);
    } else {
        Serial.println("Write to file failed.");
    }
    file.close();
}

String readCodeFromFile(const String& filename) {
    File file = LittleFS.open(filename, "r");
    if (!file) {
        Serial.println("Failed to open file for reading: " + filename);
        return "";
    }
    String code = file.readString();
    file.close();
    return code;
}

String decode_type_to_string(decode_type_t decode_type) {
  switch (decode_type) {
    case NEC: return "NEC";
    case SONY: return "SONY";
    case RC5: return "RC5";
    case RC6: return "RC6";
    case DISH: return "DISH";
    case SHARP: return "SHARP";
    case JVC: return "JVC";
    case SANYO: return "SANYO";
    case MITSUBISHI: return "MITSUBISHI";
    case SAMSUNG: return "SAMSUNG";
    case LG: return "LG";
    case WHYNTER: return "WHYNTER";
    case AIWA_RC_T501: return "AIWA_RC_T501";
    case PANASONIC: return "PANASONIC";
    case DENON: return "DENON";
    default: return "UNKNOWN";
  }
}


void ir_send_f(void *pvParameters) {
    irsend.begin();
    Serial.println("IR Send Task started on Core 1.");

    while (1) {
        if (send_ir_flag) {
            String local_file_to_send;
            
            if (xSemaphoreTake(shared_data_mutex, portMAX_DELAY) == pdTRUE) {
                local_file_to_send = file_to_send;
                send_ir_flag = false;
                xSemaphoreGive(shared_data_mutex);
            }
            
            if (!local_file_to_send.isEmpty()) {
                String data = readCodeFromFile(local_file_to_send);
                Serial.println("Read from file: [" + data + "]");

                if (data.length() > 0) {
                    String protocol, code_str, bits_str;
                    int first_comma = data.indexOf(',');
                    int second_comma = data.indexOf(',', first_comma + 1);

                    if (first_comma > 0 && second_comma > 0) {
                        protocol = data.substring(0, first_comma);
                        code_str = data.substring(first_comma + 1, second_comma);
                        bits_str = data.substring(second_comma + 1);

                        uint64_t code = strtoull(code_str.c_str(), NULL, 16);
                        uint16_t bits = bits_str.toInt();
                        
                        Serial.println("Protocol: " + protocol + ", Code: 0x" + String((uint32_t)code, HEX) + ", Bits: " + String(bits));
                        
                        if (protocol == "SAMSUNG") irsend.sendSAMSUNG(code, bits);
                        else if (protocol == "NEC") irsend.sendNEC(code, bits);
                        else if (protocol == "SONY") irsend.sendSony(code, bits);
                        else if (protocol == "LG") irsend.sendLG(code, bits);
                        else if (protocol == "JVC") irsend.sendJVC(code, bits);
                        else if (protocol == "PANASONIC") irsend.sendPanasonic(code, bits);
                        else { Serial.println("Unsupported protocol in file."); }
                        
                        Serial.println("Code sent.");
                    } else {
                        Serial.println("Invalid data format in file. Expected PROTOCOL,CODE,BITS");
                    }
                } else {
                    Serial.println("IR Task (Core 1): File is empty or could not be read.");
                    String button_id = local_file_to_send;
                    button_id.replace("/", "");
                    button_id.replace("_code.txt", "");
                    String notification_msg = button_id + "_SEND_FAIL_NOCODE";
                    pCharacteristic->setValue(notification_msg.c_str());
                    pCharacteristic->notify();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string value_str_full = pCharacteristic->getValue();
        if (value_str_full.length() == 0) return;

        std::string command_part = value_str_full;
        std::string payload_part = "";

        size_t colon_pos = value_str_full.find(":");
        if (colon_pos != std::string::npos) {
            command_part = value_str_full.substr(0, colon_pos);
            payload_part = value_str_full.substr(colon_pos + 1);
        }

        String command(command_part.c_str());
        String button_id = "";
        String action = "";

        int separator_pos = command.indexOf('_');
        if (separator_pos > 0) {
            button_id = command.substring(0, separator_pos);
            action = command.substring(separator_pos + 1);
        } else { return; }

        if (action == "REC") {
            if (currentState == LEARNING) {
                 pCharacteristic->setValue((button_id + "_REC_FAIL_BUSY").c_str());
                 pCharacteristic->notify();
                 return;
            }
            if (xSemaphoreTake(shared_data_mutex, portMAX_DELAY) == pdTRUE) {
                targetButton = button_id;
                xSemaphoreGive(shared_data_mutex);
            }
            currentState = LEARNING;
            learningStartTime = millis();
            irrecv.enableIRIn();
            Serial.println("Entering LEARNING mode for " + button_id);
            pCharacteristic->setValue((button_id + "_LEARNING").c_str());
            pCharacteristic->notify();
        } else if (action == "TOGGLE") {
            if (xSemaphoreTake(shared_data_mutex, portMAX_DELAY) == pdTRUE) {
                file_to_send = buttonToFilename(button_id);
                send_ir_flag = true;
                xSemaphoreGive(shared_data_mutex);
                Serial.println("Signaling IR Task for " + button_id);
            } else { Serial.println("ERROR: Could not take mutex."); }
            pCharacteristic->setValue((button_id + "_TOGGLE_DONE").c_str());
            pCharacteristic->notify();
        } else if (action == "CONFIG") {
            Serial.println("Executing Command: " + button_id + "_CONFIG");
            if (!payload_part.empty()) {
                String filename = buttonToFilename(button_id);
                writeCodeToFile(filename, payload_part.c_str());
                pCharacteristic->setValue((button_id + "_CONFIG_DONE").c_str());
                pCharacteristic->notify();
            } else {
                pCharacteristic->setValue((button_id + "_CONFIG_FAIL").c_str());
                pCharacteristic->notify();
            }
        }
    }
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { Serial.println("Client Connected."); }
    void onDisconnect(BLEServer* pServer) {
      Serial.println("Client Disconnected. Restarting Advertising...");
      pAdvertising->start();
    }
};


void setup() {
    Serial.begin(115200);
    Serial.println("Starting Universal BLE IR Learner/Controller...");
    
    shared_data_mutex = xSemaphoreCreateMutex();
    if (!LittleFS.begin(true)) {
        Serial.println("FATAL: LittleFS mount failed!");
        return;
    }

    xTaskCreatePinnedToCore(ir_send_f, "IRSendTask", 4096, NULL, 2, &ir_send_task_handle, 1);
    
    BLEDevice::init("ESP32-Universal-IR");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharacteristic->setCallbacks(new MyCallbacks());
    pCharacteristic->setValue("READY");
    pService->start();

    pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
    Serial.println("BLE Advertising started. Ready for commands.");
}

void loop() {
    if (currentState == LEARNING) {
        if (irrecv.decode(&results)) {
            if (results.decode_type != UNKNOWN) {
                String protocol_name = decode_type_to_string(results.decode_type);
                uint64_t code = results.value;
                uint16_t bits = results.bits;

                String code_hex_str = String((uint32_t)code, HEX);
                code_hex_str.toUpperCase(); 
                String data_to_save = protocol_name + "," + code_hex_str + "," + String(bits);
                
                Serial.println("Learned Code: " + data_to_save);

                String local_target_button;
                if (xSemaphoreTake(shared_data_mutex, portMAX_DELAY) == pdTRUE) {
                    local_target_button = targetButton;
                    targetButton = "";
                    xSemaphoreGive(shared_data_mutex);
                }

                String filename = buttonToFilename(local_target_button);
                writeCodeToFile(filename, data_to_save.c_str());
                pCharacteristic->setValue((local_target_button + "_REC_DONE").c_str());
                pCharacteristic->notify();
            } else {
                Serial.println("Could not decode IR protocol.");
                pCharacteristic->setValue((targetButton + "_REC_FAIL_UNKNOWN").c_str());
                pCharacteristic->notify();
            }
            irrecv.disableIRIn();
            currentState = IDLE;
            irrecv.resume();
        }

        if (millis() - learningStartTime > LEARNING_TIMEOUT_MS) {
            String local_target_button;
            if (xSemaphoreTake(shared_data_mutex, portMAX_DELAY) == pdTRUE) {
                local_target_button = targetButton;
                targetButton = "";
                xSemaphoreGive(shared_data_mutex);
            }
            Serial.println("LEARNING mode timed out for " + local_target_button);
            pCharacteristic->setValue((local_target_button + "_REC_FAIL_TIMEOUT").c_str());
            pCharacteristic->notify();
            irrecv.disableIRIn();
            currentState = IDLE;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
}