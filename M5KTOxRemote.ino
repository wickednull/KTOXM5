/*
 * M5 CardComputer - KTOx Remote Control App (v2 with Built-in Config)
 * ================================================================
 * 
 * Complete app with configuration menu built-in!
 * No need to edit config.h - configure everything on the M5 display
 * 
 * First boot: Shows setup wizard
 * Press KEY2 anytime: Opens config menu
 * 
 * Platform: M5Stack M5CardComputer with ESP32-S3
 * Framework: Arduino
 */

#include <M5Cardputer.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>
#include <SPIFFS.h>
#include <SPI.h>

// ==================== GLOBALS ====================
WebSocketsClient webSocket;
bool ws_connected = false;
int frame_count = 0;

// Settings structure
struct Settings {
    char wifi_ssid[64];
    char wifi_password[64];
    char ktox_host[64];
    uint16_t ktox_port;
} settings;

// Frame statistics
struct {
    uint32_t received = 0;
    uint32_t decoded = 0;
    uint32_t errors = 0;
} frame_stats;

// App states
enum AppState {
    STATE_SETUP,
    STATE_WIFI_CONNECTING,
    STATE_WS_CONNECTING,
    STATE_RUNNING,
    STATE_CONFIG_MENU
};

AppState current_state = STATE_SETUP;

// ==================== PROTOTYPES ====================
void load_settings();
void save_settings();
void show_setup_wizard();
void show_config_menu();
void show_wifi_input();
void show_password_input();
void show_ktox_ip_input();
void setup_wifi();
void setup_websocket();
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void handle_frame_data(const char* base64_data);
void read_keyboard_input();
void send_button_press(const char* button);
bool jpeg_decode_callback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
void draw_status_bar();

// ==================== SETUP ====================
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);

    Serial.begin(115200);
    delay(2000);

    Serial.println("\n\n================================");
    Serial.println("M5 CardComputer - KTOx Remote");
    Serial.println("v2 (Built-in Config)");
    Serial.println("================================");

    // Initialize display with KTOX theme
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(0xC02D2B, TFT_BLACK);  // KTOX Red (192,45,43)
    M5Cardputer.Display.println("KTOx Remote");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(0xF5EDE8);  // Off-white
    M5Cardputer.Display.println("Initializing...");

    // Initialize SPIFFS for settings storage
    if (!SPIFFS.begin(true)) {
        M5Cardputer.Display.setTextColor(TFT_RED);
        M5Cardputer.Display.println("SPIFFS init failed!");
        while(1) delay(100);
    }

    // Setup TJpg Decoder
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(jpeg_decode_callback);

    // Load settings from storage
    load_settings();

    delay(1000);
}

// ==================== MAIN LOOP ====================
void loop() {
    M5Cardputer.update();

    switch(current_state) {
        case STATE_SETUP:
            show_setup_wizard();
            break;

        case STATE_WIFI_CONNECTING:
            setup_wifi();
            break;

        case STATE_WS_CONNECTING:
            setup_websocket();
            current_state = STATE_RUNNING;
            break;

        case STATE_RUNNING:
            webSocket.loop();

            // Check keyboard for config menu (KEY2)
            read_keyboard_input();

            // Monitor WebSocket connection
            static unsigned long last_ws_check = 0;
            if (millis() - last_ws_check > 5000) {
                last_ws_check = millis();
                if (!ws_connected) {
                    M5Cardputer.Display.setTextColor(0xC02D2B);  // KTOX Red
                    M5Cardputer.Display.fillRect(0, 120, 240, 15, TFT_BLACK);
                    M5Cardputer.Display.setCursor(0, 120);
                    M5Cardputer.Display.print("WS: Reconnecting...");
                    setup_websocket();
                }
            }

            draw_status_bar();
            break;

        case STATE_CONFIG_MENU:
            show_config_menu();
            break;
    }

    delay(10);
}

// ==================== SETTINGS MANAGEMENT ====================
void load_settings() {
    if (SPIFFS.exists("/settings.json")) {
        File file = SPIFFS.open("/settings.json", "r");
        if (file) {
            DynamicJsonDocument doc(512);
            deserializeJson(doc, file);

            strcpy(settings.wifi_ssid, doc["wifi_ssid"] | "");
            strcpy(settings.wifi_password, doc["wifi_password"] | "");
            strcpy(settings.ktox_host, doc["ktox_host"] | "192.168.1.100");
            settings.ktox_port = doc["ktox_port"] | 8765;

            file.close();

            // Check if settings are empty
            if (strlen(settings.wifi_ssid) == 0) {
                current_state = STATE_SETUP;
            } else {
                current_state = STATE_WIFI_CONNECTING;
            }
        }
    } else {
        // Default settings
        strcpy(settings.wifi_ssid, "");
        strcpy(settings.wifi_password, "");
        strcpy(settings.ktox_host, "192.168.1.100");
        settings.ktox_port = 8765;
        current_state = STATE_SETUP;
    }

    Serial.printf("Loaded settings: SSID=%s, Host=%s:%d\n",
                  settings.wifi_ssid, settings.ktox_host, settings.ktox_port);
}

void save_settings() {
    DynamicJsonDocument doc(512);
    doc["wifi_ssid"] = settings.wifi_ssid;
    doc["wifi_password"] = settings.wifi_password;
    doc["ktox_host"] = settings.ktox_host;
    doc["ktox_port"] = settings.ktox_port;

    File file = SPIFFS.open("/settings.json", "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("Settings saved!");
    }
}

// ==================== INPUT HELPERS ====================
String get_text_input(const char* prompt, int max_len, bool is_password = false) {
    String input = "";
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(0xC02D2B);  // KTOX Red
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println(prompt);
    M5Cardputer.Display.setTextColor(0xD4AC0D);  // KTOX Yellow

    unsigned long timeout = millis() + 60000; // 60 second timeout

    while (millis() < timeout) {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isChange()) {
            auto status = M5Cardputer.Keyboard.keysState();

            if (status.enter) {  // Enter key
                return input;
            }
            if (status.del) {  // Backspace/Delete
                if (input.length() > 0) {
                    input.remove(input.length() - 1);
                }
            }
            if (!status.word.empty() && input.length() < max_len) {
                for (char c : status.word) {
                    if (input.length() < max_len) {
                        input += c;
                    }
                }
            }

            // Display input with KTOX theme
            M5Cardputer.Display.fillRect(0, 30, 240, 80, TFT_BLACK);
            M5Cardputer.Display.setCursor(0, 30);
            M5Cardputer.Display.setTextColor(0x1E8449);  // KTOX Green
            if (is_password) {
                for (int i = 0; i < input.length(); i++) {
                    M5Cardputer.Display.print("*");
                }
            } else {
                M5Cardputer.Display.print(input);
            }

            M5Cardputer.Display.setTextColor(0x7B241C);  // KTOX Rust
            M5Cardputer.Display.setCursor(0, 110);
            M5Cardputer.Display.println("ENTER to confirm");
            M5Cardputer.Display.println("Timeout: 60s");
        }

        delay(10);
    }

    return input;
}

// ==================== UI SCREENS ====================
void show_setup_wizard() {
    static int setup_step = 0;

    if (setup_step == 0) {
        // Welcome - KTOX Theme
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setTextColor(0xC02D2B);  // KTOX Red
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setCursor(0, 10);
        M5Cardputer.Display.println("KTOx Remote");
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(0x1E8449);  // KTOX Green
        M5Cardputer.Display.setCursor(0, 50);
        M5Cardputer.Display.println("First Time Setup");
        M5Cardputer.Display.println("");
        M5Cardputer.Display.setTextColor(0xF5EDE8);  // Off-white
        M5Cardputer.Display.println("Press ANY KEY to start");
        M5Cardputer.Display.println("");
        M5Cardputer.Display.println("1. WiFi SSID");
        M5Cardputer.Display.println("2. WiFi Password");
        M5Cardputer.Display.println("3. KTOx IP Address");

        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange()) {
            setup_step = 1;
            delay(500);
        }

    } else if (setup_step == 1) {
        // WiFi SSID
        String ssid = get_text_input("Enter WiFi SSID:", 63);
        if (ssid.length() > 0) {
            strcpy(settings.wifi_ssid, ssid.c_str());
            setup_step = 2;
        }

    } else if (setup_step == 2) {
        // WiFi Password
        String pwd = get_text_input("Enter WiFi Password:", 63, true);
        if (pwd.length() > 0) {
            strcpy(settings.wifi_password, pwd.c_str());
            setup_step = 3;
        }

    } else if (setup_step == 3) {
        // KTOx IP
        String ip = get_text_input("KTOx IP (192.168.1.100):", 15);
        if (ip.length() > 0) {
            strcpy(settings.ktox_host, ip.c_str());
        }
        save_settings();

        // Confirm - KTOX Theme
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setTextColor(0x1E8449);  // KTOX Green
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(0, 10);
        M5Cardputer.Display.println("Settings Saved!");
        M5Cardputer.Display.println("");
        M5Cardputer.Display.setTextColor(0xF5EDE8);  // Off-white
        M5Cardputer.Display.println("SSID: " + String(settings.wifi_ssid));
        M5Cardputer.Display.println("IP: " + String(settings.ktox_host));
        M5Cardputer.Display.println("Port: " + String(settings.ktox_port));
        M5Cardputer.Display.println("");
        M5Cardputer.Display.setTextColor(0xD47F1E);  // KTOX Orange
        M5Cardputer.Display.println("Connecting to WiFi...");

        delay(2000);
        current_state = STATE_WIFI_CONNECTING;
    }
}

void show_config_menu() {
    static int menu_index = 0;
    const char* menu_items[] = {
        "WiFi SSID",
        "WiFi Password",
        "KTOx IP Address",
        "Back to Remote"
    };
    const int menu_size = 4;

    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(0xC02D2B);  // KTOX Red
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("CONFIG");

    M5Cardputer.Display.setTextSize(1);
    for (int i = 0; i < menu_size; i++) {
        if (i == menu_index) {
            M5Cardputer.Display.setTextColor(TFT_BLACK, 0xC02D2B);  // Selected: black on KTOX Red
            M5Cardputer.Display.printf("> %s\n", menu_items[i]);
            M5Cardputer.Display.setTextColor(0xF5EDE8);  // Off-white
        } else {
            M5Cardputer.Display.printf("  %s\n", menu_items[i]);
        }
    }

    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange()) {
        auto status = M5Cardputer.Keyboard.keysState();

        if (!status.word.empty()) {
            char key = status.word[0];

            if (key == 'w' || key == 'i') {  // UP
                menu_index = (menu_index - 1 + menu_size) % menu_size;
                delay(200);
            } else if (key == 's' || key == 'k') {  // DOWN
                menu_index = (menu_index + 1) % menu_size;
                delay(200);
            } else if (key == ' ') {  // SELECT
                if (menu_index == 0) {
                    String ssid = get_text_input("Enter WiFi SSID:", 63);
                    if (ssid.length() > 0) {
                        strcpy(settings.wifi_ssid, ssid.c_str());
                        save_settings();
                    }
                } else if (menu_index == 1) {
                    String pwd = get_text_input("Enter WiFi Password:", 63, true);
                    if (pwd.length() > 0) {
                        strcpy(settings.wifi_password, pwd.c_str());
                        save_settings();
                    }
                } else if (menu_index == 2) {
                    String ip = get_text_input("Enter KTOx IP:", 15);
                    if (ip.length() > 0) {
                        strcpy(settings.ktox_host, ip.c_str());
                        save_settings();
                    }
                } else if (menu_index == 3) {
                    current_state = STATE_RUNNING;
                    menu_index = 0;
                    return;
                }
                delay(500);
            }
        }

        if (status.enter) {  // ENTER key selects menu item
            if (menu_index == 0) {
                String ssid = get_text_input("Enter WiFi SSID:", 63);
                if (ssid.length() > 0) {
                    strcpy(settings.wifi_ssid, ssid.c_str());
                    save_settings();
                }
            } else if (menu_index == 1) {
                String pwd = get_text_input("Enter WiFi Password:", 63, true);
                if (pwd.length() > 0) {
                    strcpy(settings.wifi_password, pwd.c_str());
                    save_settings();
                }
            } else if (menu_index == 2) {
                String ip = get_text_input("Enter KTOx IP:", 15);
                if (ip.length() > 0) {
                    strcpy(settings.ktox_host, ip.c_str());
                    save_settings();
                }
            } else if (menu_index == 3) {
                current_state = STATE_RUNNING;
                menu_index = 0;
                return;
            }
            delay(500);
        }
    }
}

// ==================== WiFi SETUP ====================
void setup_wifi() {
    static bool shown_screen = false;

    if (!shown_screen) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setTextColor(0xD4AC0D);  // KTOX Yellow
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(0, 0);
        M5Cardputer.Display.println("WiFi Setup");
        M5Cardputer.Display.setTextColor(0xF5EDE8);  // Off-white
        M5Cardputer.Display.printf("SSID: %s\n", settings.wifi_ssid);
        M5Cardputer.Display.setTextColor(0xD47F1E);  // KTOX Orange
        M5Cardputer.Display.println("Connecting...");

        WiFi.mode(WIFI_STA);
        WiFi.begin(settings.wifi_ssid, settings.wifi_password);

        shown_screen = true;
    }

    if (WiFi.status() == WL_CONNECTED) {
        M5Cardputer.Display.setTextColor(0x1E8449);  // KTOX Green
        M5Cardputer.Display.println("Connected!");
        M5Cardputer.Display.setTextColor(0xF5EDE8);  // Off-white
        M5Cardputer.Display.printf("IP: %s\n", WiFi.localIP().toString().c_str());

        Serial.println("WiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());

        delay(2000);
        current_state = STATE_WS_CONNECTING;
        shown_screen = false;
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
        M5Cardputer.Display.setTextColor(0xC02D2B);  // KTOX Red
        M5Cardputer.Display.println("Failed!");
        M5Cardputer.Display.setTextColor(0xF5EDE8);  // Off-white
        M5Cardputer.Display.println("Press KEY2 to retry");

        delay(5000);
        current_state = STATE_SETUP;
        shown_screen = false;
    } else {
        M5Cardputer.Display.print(".");
    }
}

// ==================== WebSocket SETUP ====================
void setup_websocket() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(0xD4AC0D);  // KTOX Yellow
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("KTOx Connection");
    M5Cardputer.Display.setTextColor(0xF5EDE8);  // Off-white
    M5Cardputer.Display.printf("Host: %s:%d\n", settings.ktox_host, settings.ktox_port);
    M5Cardputer.Display.setTextColor(0xD47F1E);  // KTOX Orange
    M5Cardputer.Display.println("Connecting...");

    Serial.printf("Connecting to WebSocket: %s:%d\n", settings.ktox_host, settings.ktox_port);

    webSocket.begin(settings.ktox_host, settings.ktox_port, "/");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);

    delay(2000);
}

// ==================== WebSocket EVENT HANDLER ====================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("[WSc] Disconnected!");
            ws_connected = false;
            break;

        case WStype_CONNECTED:
            Serial.println("[WSc] Connected!");
            ws_connected = true;
            M5Cardputer.Display.fillScreen(TFT_BLACK);
            M5Cardputer.Display.setTextColor(0x1E8449);  // KTOX Green
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.setCursor(20, 50);
            M5Cardputer.Display.println("KTOx");
            M5Cardputer.Display.setTextColor(0xC02D2B);  // KTOX Red
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(0, 90);
            M5Cardputer.Display.println("Connected!");
            delay(1000);
            break;

        case WStype_TEXT: {
            DynamicJsonDocument doc(50000);
            DeserializationError error = deserializeJson(doc, payload);

            if (error) {
                Serial.print("JSON error: ");
                Serial.println(error.c_str());
                frame_stats.errors++;
                return;
            }

            const char* type = doc["type"];
            if (type == nullptr) return;

            if (strcmp(type, "frame") == 0) {
                const char* data = doc["data"];
                if (data) {
                    handle_frame_data(data);
                    frame_stats.received++;
                }
            }
            break;
        }

        case WStype_ERROR:
            Serial.println("[WSc] WebSocket error!");
            break;
    }
}

// ==================== FRAME HANDLING ====================
void handle_frame_data(const char* base64_data) {
    uint32_t jpeg_size = base64_decode_expected_len(strlen(base64_data));
    uint8_t jpeg_buffer[jpeg_size];

    int decoded_size = base64_decode((unsigned char*)base64_data,
                                      strlen(base64_data),
                                      jpeg_buffer);

    if (decoded_size <= 0) {
        Serial.println("Base64 decode failed!");
        frame_stats.errors++;
        return;
    }

    TJpgDec.drawJpg(0, 0, jpeg_buffer, decoded_size);
    frame_stats.decoded++;
    frame_count++;

    static unsigned long last_stats = 0;
    if (millis() - last_stats > 5000) {
        last_stats = millis();
        Serial.printf("Frames: recv=%d, decoded=%d, errors=%d\n",
                      frame_stats.received, frame_stats.decoded, frame_stats.errors);
    }
}

// ==================== JPEG CALLBACK ====================
bool jpeg_decode_callback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    M5Cardputer.Display.pushImage(x, y, w, h, bitmap);
    return true;
}

// ==================== KEYBOARD INPUT ====================
void read_keyboard_input() {
    if (M5Cardputer.Keyboard.isChange()) {
        auto status = M5Cardputer.Keyboard.keysState();

        // KEY2 opens config menu
        if (!status.word.empty()) {
            char key = status.word[0];

            if (key == 'h') {  // KEY2 config menu
                current_state = STATE_CONFIG_MENU;
                delay(300);
                return;
            }

            // Handle navigation and action keys
            switch(key) {
                case 'w': case 'W': case 'i': send_button_press("UP"); break;
                case 's': case 'S': case 'k': send_button_press("DOWN"); break;
                case 'a': case 'A': case 'j': send_button_press("LEFT"); break;
                case 'd': case 'D': case 'l': send_button_press("RIGHT"); break;
                case ' ': send_button_press("OK"); break;
                case '\x1b': send_button_press("KEY1"); break;
                case 'q': case 'Q': send_button_press("KEY3"); break;
            }
        }

        if (status.enter) {
            send_button_press("OK");
        }
    }
}

// ==================== SEND INPUT ====================
void send_button_press(const char* button) {
    if (!ws_connected) return;

    DynamicJsonDocument doc(256);
    doc["type"] = "input";
    doc["button"] = button;
    doc["state"] = "press";

    String json_str;
    serializeJson(doc, json_str);
    webSocket.sendTXT(json_str);
}

// ==================== STATUS BAR ====================
void draw_status_bar() {
    static unsigned long last_update = 0;
    if (millis() - last_update < 500) return;
    last_update = millis();

    char status[256];
    snprintf(status, sizeof(status), "[%s] F:%d FPS:%.1f",
             ws_connected ? "●" : "○",
             frame_count,
             frame_count * 1000.0f / (millis() + 1));

    uint16_t status_color = ws_connected ? 0x1E8449 : 0xC02D2B;  // Green if connected, Red if not
    M5Cardputer.Display.setTextColor(status_color, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillRect(0, 120, 240, 15, TFT_BLACK);
    M5Cardputer.Display.drawRect(0, 120, 240, 15, status_color);  // Border
    M5Cardputer.Display.setCursor(2, 124);
    M5Cardputer.Display.print(status);
}

// ==================== BASE64 DECODE ====================
uint32_t base64_decode_expected_len(uint32_t encoded_len) {
    return encoded_len / 4 * 3;
}

int base64_decode(unsigned char *in, unsigned int in_len, unsigned char *out) {
    unsigned int i = 0, j = 0, k = 0;
    unsigned char c[4];

    const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    while (i < in_len) {
        for (k = 0; k < 4 && i < in_len; k++, i++) {
            c[k] = strchr(base64_chars, in[i]) - base64_chars;
            if (in[i] == '=') {
                c[k] = 0;
                break;
            }
        }

        if (k > 1) {
            out[j++] = (c[0] << 2) | (c[1] >> 4);
        }
        if (k > 2) {
            out[j++] = (c[1] << 4) | (c[2] >> 2);
        }
        if (k > 3) {
            out[j++] = (c[2] << 6) | c[3];
        }
    }

    return j;
}
