/*
 * KTOx Remote Control - M5Stack Cardputer
 * Simple, Clean, Working
 *
 * Boots → Connect WiFi → Connect to KTOX_Pi → Stream Video + Control
 */

#include <M5Cardputer.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>
#include <SPIFFS.h>

// Colors
#define RED    0xC02D2B
#define GREEN  0x1E8449
#define YELLOW 0xD4AC0D
#define BLACK  TFT_BLACK
#define WHITE  0xF5EDE8

WebSocketsClient webSocket;
bool ws_connected = false;
bool frame_received = false;
unsigned long last_frame = 0;

struct Settings {
    char wifi_ssid[64];
    char wifi_password[64];
    char ktox_host[64];
    uint16_t ktox_port;
} settings;

struct Stats {
    int frames = 0;
    int errors = 0;
} stats;

// ==================== PROTOTYPES ====================
void load_settings();
void save_settings();
void show_startup_wizard();
void setup_wifi();
void setup_websocket();
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void handle_frame_data(const char* base64_data);
void read_input();
bool jpeg_callback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
void draw_overlay();
uint32_t base64_decode_expected_len(uint32_t encoded_len);
int base64_decode(unsigned char *in, unsigned int in_len, unsigned char *out);

// ==================== SETUP ====================
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== KTOx Remote ===");

    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(YELLOW);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("KTOx Remote");
    M5Cardputer.Display.println("Loading...");

    if (!SPIFFS.begin(true)) {
        M5Cardputer.Display.setTextColor(RED);
        M5Cardputer.Display.println("SPIFFS FAIL");
        while(1) delay(100);
    }

    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(jpeg_callback);

    load_settings();
    delay(500);
}

// ==================== MAIN LOOP ====================
void loop() {
    M5Cardputer.update();

    if (!ws_connected) {
        // Not connected - try to connect
        static unsigned long last_attempt = 0;
        if (millis() - last_attempt > 5000) {
            last_attempt = millis();

            if (WiFi.status() != WL_CONNECTED) {
                setup_wifi();
            } else {
                setup_websocket();
            }
        }

        // Show connection status
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextColor(YELLOW);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(0, 0);
        M5Cardputer.Display.println("KTOx Remote");
        M5Cardputer.Display.println("");

        if (WiFi.status() == WL_CONNECTED) {
            M5Cardputer.Display.setTextColor(GREEN);
            M5Cardputer.Display.println("WiFi: OK");
            M5Cardputer.Display.setTextColor(YELLOW);
            M5Cardputer.Display.println("Connecting to KTOx...");
        } else {
            M5Cardputer.Display.setTextColor(RED);
            M5Cardputer.Display.println("WiFi: Connecting...");
        }

    } else {
        // Connected - process WebSocket and input
        webSocket.loop();
        read_input();

        // Show overlay with stats
        draw_overlay();
    }

    delay(10);
}

// ==================== SETTINGS ====================
void load_settings() {
    if (SPIFFS.exists("/settings.json")) {
        File file = SPIFFS.open("/settings.json", "r");
        DynamicJsonDocument doc(512);
        deserializeJson(doc, file);

        strcpy(settings.wifi_ssid, doc["ssid"] | "");
        strcpy(settings.wifi_password, doc["pass"] | "");
        strcpy(settings.ktox_host, doc["host"] | "192.168.0.50");
        settings.ktox_port = doc["port"] | 8765;

        file.close();

        if (strlen(settings.wifi_ssid) == 0) {
            show_startup_wizard();
        }
    } else {
        show_startup_wizard();
    }
}

void save_settings() {
    DynamicJsonDocument doc(512);
    doc["ssid"] = settings.wifi_ssid;
    doc["pass"] = settings.wifi_password;
    doc["host"] = settings.ktox_host;
    doc["port"] = settings.ktox_port;

    File file = SPIFFS.open("/settings.json", "w");
    serializeJson(doc, file);
    file.close();
}

void show_startup_wizard() {
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(YELLOW);
    M5Cardputer.Display.println("Setup WiFi");
    M5Cardputer.Display.println("");

    // SSID
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.println("SSID:");
    M5Cardputer.Display.setTextColor(GREEN);
    String ssid = get_input(63);
    strcpy(settings.wifi_ssid, ssid.c_str());

    // Password
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.println("Password:");
    M5Cardputer.Display.setTextColor(GREEN);
    String pass = get_input(63);
    strcpy(settings.wifi_password, pass.c_str());

    // KTOx IP
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.println("KTOx IP (192.168.0.50):");
    M5Cardputer.Display.setTextColor(GREEN);
    String ip = get_input(15);
    if (ip.length() > 0) strcpy(settings.ktox_host, ip.c_str());

    save_settings();

    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.println("Saved!");
    delay(1000);
}

String get_input(int max_len) {
    String input = "";
    unsigned long timeout = millis() + 60000;

    while (millis() < timeout) {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isChange()) {
            auto status = M5Cardputer.Keyboard.keysState();

            if (status.enter) break;

            if (status.del && input.length() > 0) {
                input.remove(input.length() - 1);
            }

            if (!status.word.empty()) {
                for (char c : status.word) {
                    if (input.length() < max_len) input += c;
                }
            }

            // Display input
            M5Cardputer.Display.fillRect(0, 100, 240, 35, BLACK);
            M5Cardputer.Display.setCursor(0, 100);
            M5Cardputer.Display.print(input);
            M5Cardputer.Display.setTextColor(RED);
            M5Cardputer.Display.print("_");
        }

        delay(10);
    }

    return input;
}

// ==================== WiFi ====================
void setup_wifi() {
    static bool shown = false;

    if (!shown) {
        Serial.printf("Connecting WiFi: %s\n", settings.wifi_ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(settings.wifi_ssid, settings.wifi_password);
        shown = true;
    }

    if (WiFi.status() == WL_CONNECTED) {
        shown = false;
        Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
    }
}

// ==================== WebSocket ====================
void setup_websocket() {
    Serial.printf("Connecting: %s:%d\n", settings.ktox_host, settings.ktox_port);

    webSocket.begin(settings.ktox_host, settings.ktox_port, "/");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(3000);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_CONNECTED:
            Serial.println("[WS] Connected!");
            ws_connected = true;
            break;

        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected");
            ws_connected = false;
            break;

        case WStype_TEXT: {
            DynamicJsonDocument doc(50000);
            if (deserializeJson(doc, payload) == DeserializationError::Ok) {
                const char* msg_type = doc["type"];

                if (msg_type && strcmp(msg_type, "frame") == 0) {
                    const char* data = doc["data"];
                    if (data) {
                        handle_frame_data(data);
                        frame_received = true;
                        last_frame = millis();
                    }
                }
            }
            break;
        }

        case WStype_BIN:
        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
        case WStype_PING:
        case WStype_PONG:
            break;
    }
}

// ==================== FRAME HANDLING ====================
void handle_frame_data(const char* base64_data) {
    uint32_t jpeg_size = base64_decode_expected_len(strlen(base64_data));
    uint8_t jpeg_buffer[jpeg_size];

    int decoded_size = base64_decode((unsigned char*)base64_data, strlen(base64_data), jpeg_buffer);

    if (decoded_size > 0) {
        TJpgDec.drawJpg(0, 0, jpeg_buffer, decoded_size);
        stats.frames++;
    } else {
        stats.errors++;
    }
}

bool jpeg_callback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    M5Cardputer.Display.pushImage(x, y, w, h, bitmap);
    return true;
}

// ==================== INPUT ====================
void read_input() {
    if (!M5Cardputer.Keyboard.isChange()) return;

    auto status = M5Cardputer.Keyboard.keysState();

    if (!status.word.empty()) {
        char key = status.word[0];

        const char* button = nullptr;
        switch(key) {
            case 'w': case 'i': button = "UP"; break;
            case 's': case 'k': button = "DOWN"; break;
            case 'a': case 'j': button = "LEFT"; break;
            case 'd': case 'l': button = "RIGHT"; break;
            case ' ': button = "OK"; break;
            case 'q': button = "KEY3"; break;
            case '\x1b': button = "KEY1"; break;
        }

        if (button) send_button(button);
    }

    if (status.enter) send_button("OK");
}

void send_button(const char* button) {
    if (!ws_connected) return;

    DynamicJsonDocument doc(256);
    doc["type"] = "input";
    doc["button"] = button;
    doc["state"] = "press";

    String json;
    serializeJson(doc, json);
    webSocket.sendTXT(json);
}

// ==================== DISPLAY ====================
void draw_overlay() {
    static unsigned long last_draw = 0;
    if (millis() - last_draw < 500) return;
    last_draw = millis();

    // Status bar at bottom
    M5Cardputer.Display.fillRect(0, 120, 240, 15, BLACK);
    M5Cardputer.Display.drawRect(0, 120, 240, 15, GREEN);

    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.setTextSize(0);
    M5Cardputer.Display.setCursor(2, 123);

    char status[60];
    snprintf(status, sizeof(status), "[●] Frames:%d FPS:%.1f",
             stats.frames,
             stats.frames * 1000.0f / (millis() + 1));

    M5Cardputer.Display.print(status);

    // Show if no frames
    if (millis() - last_frame > 2000) {
        M5Cardputer.Display.setTextColor(RED);
        M5Cardputer.Display.setCursor(2, 110);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.print("No frames");
    }
}

// ==================== BASE64 ====================
uint32_t base64_decode_expected_len(uint32_t encoded_len) {
    return encoded_len / 4 * 3;
}

int base64_decode(unsigned char *in, unsigned int in_len, unsigned char *out) {
    unsigned int i = 0, j = 0, k = 0;
    unsigned char c[4];
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    while (i < in_len) {
        for (k = 0; k < 4 && i < in_len; k++, i++) {
            c[k] = strchr(base64_chars, in[i]) - base64_chars;
            if (in[i] == '=') {
                c[k] = 0;
                break;
            }
        }

        if (k > 1) out[j++] = (c[0] << 2) | (c[1] >> 4);
        if (k > 2) out[j++] = (c[1] << 4) | (c[2] >> 2);
        if (k > 3) out[j++] = (c[2] << 6) | c[3];
    }

    return j;
}
