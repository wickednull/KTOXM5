/*
 * KTOx Remote Control - M5Stack Cardputer
 * =========================================
 * Full-featured remote control for KTOX_Pi
 * Comprehensive menu system with all operations
 *
 * Platform: M5Stack M5Cardputer with ESP32-S3
 * Framework: Arduino
 */

#include <M5Cardputer.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>
#include <SPIFFS.h>
#include <SPI.h>

// ==================== COLOR PALETTE ====================
#define KTOX_RED      0xC02D2B
#define KTOX_DARK_RED 0x641410
#define KTOX_RUST     0x7B241C
#define KTOX_GREEN    0x1E8449
#define KTOX_YELLOW   0xD4AC0D
#define KTOX_ORANGE   0xD47F1E
#define KTOX_WHITE    0xF5EDE8
#define KTOX_BLUE     0x1F618D

// ==================== GLOBALS ====================
WebSocketsClient webSocket;
bool ws_connected = false;
int frame_count = 0;
unsigned long last_command = 0;

struct Settings {
    char wifi_ssid[64];
    char wifi_password[64];
    char ktox_host[64];
    uint16_t ktox_port;
    char auth_token[256];
} settings;

struct {
    uint32_t received = 0;
    uint32_t decoded = 0;
    uint32_t errors = 0;
} frame_stats;

enum AppState {
    STATE_SETUP,
    STATE_WIFI_CONNECTING,
    STATE_WS_CONNECTING,
    STATE_RUNNING,
    STATE_CONFIG_MENU,
    STATE_MAIN_MENU,
    STATE_SUBMENU,
    STATE_EXECUTION,
    STATE_RESULTS
};

AppState current_state = STATE_SETUP;

// Menu structure
int menu_index = 0;
int submenu_index = 0;
int current_menu = 0;  // 0=main, 1=recon, 2=offensive, 3=defensive, 4=wifi, 5=system
String last_result = "";
String execution_log = "";
String target_ip = "";

const char* main_menu[] = {
    "[>] Reconnaissance",
    "[>] Offensive Attacks",
    "[>] Defensive/MITM",
    "[>] WiFi Attacks",
    "[>] System Tools",
    "[>] Settings",
    "[<] Back to Stream"
};
const int main_menu_size = 7;

// Reconnaissance submenu
const char* recon_menu[] = {
    "ARP Scan",
    "Host Discovery",
    "Port Scan",
    "Device Fingerprint",
    "MAC Spoof Setup",
    "Network Baseline",
    "Back"
};
const int recon_menu_size = 7;

// Offensive submenu
const char* offensive_menu[] = {
    "Kick Single Host",
    "Kick Multiple",
    "ARP Poisoner",
    "ARP Flood",
    "Gateway DoS",
    "ARP Cage",
    "Back"
};
const int offensive_menu_size = 7;

// Defensive submenu
const char* defensive_menu[] = {
    "MITM Engine",
    "Advanced Engine",
    "Extended Engine",
    "ARP Hardening",
    "Rogue Detector",
    "ARP Watch",
    "Back"
};
const int defensive_menu_size = 7;

// WiFi submenu
const char* wifi_menu[] = {
    "Deauth Attack",
    "Handshake Capture",
    "PMKID Attack",
    "Evil Twin",
    "WiFi Scan",
    "Channel Hopper",
    "Back"
};
const int wifi_menu_size = 7;

// System submenu
const char* system_menu[] = {
    "View Status",
    "Check Targets",
    "View Loot",
    "Execute Command",
    "Check Logs",
    "Stealth Mode",
    "Back"
};
const int system_menu_size = 7;

// ==================== PROTOTYPES ====================
void load_settings();
void save_settings();
void show_setup_wizard();
void show_config_menu();
void setup_wifi();
void setup_websocket();
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
void handle_frame_data(const char* base64_data);
void read_keyboard_input();
void send_command(const char* command, const char* param = "");
void send_button_press(const char* button);
bool jpeg_decode_callback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
void draw_status_bar();
void draw_menu(const char** items, int size, int selected);
void show_main_menu();
void show_submenu(const char** items, int size);
void execute_operation(const char* operation);
void show_results();
void display_header(const char* title);
void show_help_screen();
String get_text_input(const char* prompt, int max_len, bool is_password = false);
uint32_t base64_decode_expected_len(uint32_t encoded_len);
int base64_decode(unsigned char *in, unsigned int in_len, unsigned char *out);

// ==================== SETUP ====================
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);

    Serial.begin(115200);
    delay(2000);

    Serial.println("\n\n================================");
    Serial.println("KTOx Remote Control");
    Serial.println("M5Cardputer Edition");
    Serial.println("================================");

    // Initialize display
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(KTOX_RED, TFT_BLACK);
    M5Cardputer.Display.println("KTOx Remote");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(KTOX_WHITE);
    M5Cardputer.Display.println("Initializing...");

    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        M5Cardputer.Display.setTextColor(TFT_RED);
        M5Cardputer.Display.println("SPIFFS init failed!");
        while(1) delay(100);
    }

    // Setup TJpg Decoder
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(jpeg_decode_callback);

    // Load settings
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
            read_keyboard_input();

            static unsigned long last_ws_check = 0;
            if (millis() - last_ws_check > 5000) {
                last_ws_check = millis();
                if (!ws_connected) {
                    M5Cardputer.Display.setTextColor(KTOX_RED);
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

        case STATE_MAIN_MENU:
            show_main_menu();
            break;

        case STATE_SUBMENU:
            show_submenu(NULL, 0);  // Will be set by caller
            break;

        case STATE_EXECUTION:
            show_results();
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
            strcpy(settings.ktox_host, doc["ktox_host"] | "192.168.0.50");
            settings.ktox_port = doc["ktox_port"] | 8765;
            strcpy(settings.auth_token, doc["auth_token"] | "");

            file.close();

            if (strlen(settings.wifi_ssid) == 0) {
                current_state = STATE_SETUP;
            } else {
                current_state = STATE_WIFI_CONNECTING;
            }
        }
    } else {
        strcpy(settings.wifi_ssid, "");
        strcpy(settings.wifi_password, "");
        strcpy(settings.ktox_host, "192.168.1.100");
        settings.ktox_port = 8765;
        current_state = STATE_SETUP;
    }

    Serial.printf("Loaded: SSID=%s, Host=%s:%d\n",
                  settings.wifi_ssid, settings.ktox_host, settings.ktox_port);
}

void save_settings() {
    DynamicJsonDocument doc(1024);
    doc["wifi_ssid"] = settings.wifi_ssid;
    doc["wifi_password"] = settings.wifi_password;
    doc["ktox_host"] = settings.ktox_host;
    doc["ktox_port"] = settings.ktox_port;
    doc["auth_token"] = settings.auth_token;

    File file = SPIFFS.open("/settings.json", "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("Settings saved!");
    }
}

// ==================== INPUT HELPERS ====================
String get_text_input(const char* prompt, int max_len, bool is_password) {
    String input = "";
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(KTOX_RED);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println(prompt);
    M5Cardputer.Display.setTextColor(KTOX_YELLOW);

    unsigned long timeout = millis() + 60000;

    while (millis() < timeout) {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isChange()) {
            auto status = M5Cardputer.Keyboard.keysState();

            if (status.enter) {
                return input;
            }
            if (status.del) {
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

            M5Cardputer.Display.fillRect(0, 30, 240, 80, TFT_BLACK);
            M5Cardputer.Display.setCursor(0, 30);
            if (is_password) {
                for (int i = 0; i < input.length(); i++) {
                    M5Cardputer.Display.print("*");
                }
            } else {
                M5Cardputer.Display.print(input);
            }

            M5Cardputer.Display.setTextColor(KTOX_RUST);
            M5Cardputer.Display.setCursor(0, 110);
            M5Cardputer.Display.println("ENTER to confirm");
        }

        delay(10);
    }

    return input;
}

// ==================== UI DISPLAY ====================
void display_header(const char* title) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(KTOX_RED);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(0, 2);
    M5Cardputer.Display.println(title);

    M5Cardputer.Display.drawLine(0, 20, 240, 20, KTOX_RED);
    M5Cardputer.Display.setTextSize(1);
}

void draw_menu(const char** items, int size, int selected) {
    for (int i = 0; i < size && i < 6; i++) {
        if (i == selected) {
            M5Cardputer.Display.setTextColor(TFT_BLACK, KTOX_RED);
            M5Cardputer.Display.printf("> %s\n", items[i]);
            M5Cardputer.Display.setTextColor(KTOX_WHITE);
        } else {
            M5Cardputer.Display.printf("  %s\n", items[i]);
        }
    }
}

void show_main_menu() {
    static bool first_run = true;

    if (first_run) {
        display_header("KTOx CONTROL");
        M5Cardputer.Display.setTextColor(KTOX_WHITE);
        M5Cardputer.Display.setCursor(0, 25);
        draw_menu(main_menu, main_menu_size, menu_index);
        M5Cardputer.Display.setTextColor(KTOX_RUST);
        M5Cardputer.Display.setCursor(0, 115);
        M5Cardputer.Display.println("UP/DOWN:Nav ENTER:Select");
        first_run = false;
    }

    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange()) {
        auto status = M5Cardputer.Keyboard.keysState();

        if (!status.word.empty()) {
            char key = status.word[0];

            if (key == 'w' || key == 'i') {
                menu_index = (menu_index - 1 + main_menu_size) % main_menu_size;
                first_run = true;
            } else if (key == 's' || key == 'k') {
                menu_index = (menu_index + 1) % main_menu_size;
                first_run = true;
            }
        }

        if (status.enter) {
            submenu_index = 0;

            if (menu_index == 0) {
                current_menu = 1;
                current_state = STATE_SUBMENU;
            } else if (menu_index == 1) {
                current_menu = 2;
                current_state = STATE_SUBMENU;
            } else if (menu_index == 2) {
                current_menu = 3;
                current_state = STATE_SUBMENU;
            } else if (menu_index == 3) {
                current_menu = 4;
                current_state = STATE_SUBMENU;
            } else if (menu_index == 4) {
                current_menu = 5;
                current_state = STATE_SUBMENU;
            } else if (menu_index == 5) {
                current_state = STATE_CONFIG_MENU;
            } else if (menu_index == 6) {
                current_state = STATE_RUNNING;
                menu_index = 0;
            }
            first_run = true;
            delay(200);
        }
    }
}

void show_submenu(const char** items, int size) {
    static bool first_run = true;
    const char** current_items = NULL;
    int current_size = 0;
    const char* title = "";

    // Select the correct menu based on current_menu
    switch(current_menu) {
        case 1:
            current_items = recon_menu;
            current_size = recon_menu_size;
            title = "RECONNAISSANCE";
            break;
        case 2:
            current_items = offensive_menu;
            current_size = offensive_menu_size;
            title = "OFFENSIVE ATTACKS";
            break;
        case 3:
            current_items = defensive_menu;
            current_size = defensive_menu_size;
            title = "DEFENSIVE/MITM";
            break;
        case 4:
            current_items = wifi_menu;
            current_size = wifi_menu_size;
            title = "WiFi ATTACKS";
            break;
        case 5:
            current_items = system_menu;
            current_size = system_menu_size;
            title = "SYSTEM TOOLS";
            break;
    }

    if (first_run) {
        display_header(title);
        M5Cardputer.Display.setTextColor(KTOX_WHITE);
        M5Cardputer.Display.setCursor(0, 25);
        draw_menu(current_items, current_size, submenu_index);
        M5Cardputer.Display.setTextColor(KTOX_RUST);
        M5Cardputer.Display.setCursor(0, 115);
        M5Cardputer.Display.println("UP/DOWN:Nav ENTER:Run");
        first_run = false;
    }

    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange()) {
        auto status = M5Cardputer.Keyboard.keysState();

        if (!status.word.empty()) {
            char key = status.word[0];

            if (key == 'w' || key == 'i') {
                submenu_index = (submenu_index - 1 + current_size) % current_size;
                first_run = true;
            } else if (key == 's' || key == 'k') {
                submenu_index = (submenu_index + 1) % current_size;
                first_run = true;
            }
        }

        if (status.enter) {
            // Check if "Back" was selected
            if (submenu_index == current_size - 1) {
                menu_index = current_menu - 1;
                current_state = STATE_MAIN_MENU;
            } else {
                // Execute the selected operation
                String operation = String(current_items[submenu_index]);

                // Show execution screen
                M5Cardputer.Display.fillScreen(TFT_BLACK);
                M5Cardputer.Display.setTextColor(KTOX_YELLOW);
                M5Cardputer.Display.setTextSize(1);
                M5Cardputer.Display.setCursor(0, 0);
                M5Cardputer.Display.println("Executing:");
                M5Cardputer.Display.setTextColor(KTOX_GREEN);
                M5Cardputer.Display.println(operation);
                M5Cardputer.Display.setTextColor(KTOX_WHITE);
                M5Cardputer.Display.println("");
                M5Cardputer.Display.println("Sending command...");

                // Send command to KTOX_Pi
                send_command(operation.c_str(), target_ip.c_str());

                delay(2000);
                current_state = STATE_RUNNING;
            }
            first_run = true;
            delay(200);
        }
    }
}

void show_results() {
    static bool first_run = true;

    if (first_run) {
        display_header("EXECUTION RESULT");
        M5Cardputer.Display.setTextColor(KTOX_GREEN);
        M5Cardputer.Display.setCursor(0, 25);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.println("[✓] Command Sent");

        M5Cardputer.Display.setTextColor(KTOX_WHITE);
        M5Cardputer.Display.setCursor(0, 45);
        M5Cardputer.Display.println("Status: Processing");
        M5Cardputer.Display.println("");
        M5Cardputer.Display.println("Last Result:");
        M5Cardputer.Display.setTextColor(KTOX_YELLOW);
        M5Cardputer.Display.println(last_result.c_str());

        M5Cardputer.Display.setTextColor(KTOX_RUST);
        M5Cardputer.Display.setCursor(0, 115);
        M5Cardputer.Display.println("ENTER:Back to Stream");

        first_run = false;
    }

    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange()) {
        auto status = M5Cardputer.Keyboard.keysState();
        if (status.enter) {
            current_state = STATE_RUNNING;
            first_run = true;
            delay(200);
        }
    }
}

// ==================== SETUP SCREENS ====================
void show_setup_wizard() {
    static int setup_step = 0;

    if (setup_step == 0) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setTextColor(KTOX_RED);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setCursor(0, 10);
        M5Cardputer.Display.println("KTOx Setup");
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(KTOX_GREEN);
        M5Cardputer.Display.setCursor(0, 50);
        M5Cardputer.Display.println("First Time Setup");
        M5Cardputer.Display.println("");
        M5Cardputer.Display.setTextColor(KTOX_WHITE);
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
        String ssid = get_text_input("WiFi SSID:", 63);
        if (ssid.length() > 0) {
            strcpy(settings.wifi_ssid, ssid.c_str());
            setup_step = 2;
        }

    } else if (setup_step == 2) {
        String pwd = get_text_input("WiFi Password:", 63, true);
        if (pwd.length() > 0) {
            strcpy(settings.wifi_password, pwd.c_str());
            setup_step = 3;
        }

    } else if (setup_step == 3) {
        String ip = get_text_input("KTOx IP (192.168.0.50):", 15);
        if (ip.length() > 0) {
            strcpy(settings.ktox_host, ip.c_str());
        }
        setup_step = 4;

    } else if (setup_step == 4) {
        String token = get_text_input("Auth Token (optional):", 255);
        strcpy(settings.auth_token, token.c_str());
        save_settings();

        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setTextColor(KTOX_GREEN);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(0, 10);
        M5Cardputer.Display.println("Settings Saved!");
        M5Cardputer.Display.println("");
        M5Cardputer.Display.setTextColor(KTOX_WHITE);
        M5Cardputer.Display.println("WiFi: " + String(settings.wifi_ssid));
        M5Cardputer.Display.println("Host: " + String(settings.ktox_host));
        if (strlen(settings.auth_token) > 0) {
            M5Cardputer.Display.println("Token: Set");
        }
        M5Cardputer.Display.println("");
        M5Cardputer.Display.setTextColor(KTOX_ORANGE);
        M5Cardputer.Display.println("Connecting to WiFi...");

        delay(2000);
        current_state = STATE_WIFI_CONNECTING;
    }
}

void show_config_menu() {
    static int menu_idx = 0;
    const char* config_items[] = {
        "WiFi SSID",
        "WiFi Password",
        "KTOx IP Address",
        "Back to Remote"
    };
    const int config_size = 4;

    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(KTOX_RED);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("CONFIG");

    M5Cardputer.Display.setTextSize(1);
    for (int i = 0; i < config_size; i++) {
        if (i == menu_idx) {
            M5Cardputer.Display.setTextColor(TFT_BLACK, KTOX_RED);
            M5Cardputer.Display.printf("> %s\n", config_items[i]);
            M5Cardputer.Display.setTextColor(KTOX_WHITE);
        } else {
            M5Cardputer.Display.printf("  %s\n", config_items[i]);
        }
    }

    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange()) {
        auto status = M5Cardputer.Keyboard.keysState();

        if (!status.word.empty()) {
            char key = status.word[0];

            if (key == 'w' || key == 'i') {
                menu_idx = (menu_idx - 1 + config_size) % config_size;
                delay(200);
            } else if (key == 's' || key == 'k') {
                menu_idx = (menu_idx + 1) % config_size;
                delay(200);
            } else if (key == ' ') {
                if (menu_idx == 0) {
                    String ssid = get_text_input("WiFi SSID:", 63);
                    if (ssid.length() > 0) strcpy(settings.wifi_ssid, ssid.c_str());
                    save_settings();
                } else if (menu_idx == 1) {
                    String pwd = get_text_input("WiFi Password:", 63, true);
                    if (pwd.length() > 0) strcpy(settings.wifi_password, pwd.c_str());
                    save_settings();
                } else if (menu_idx == 2) {
                    String ip = get_text_input("KTOx IP:", 15);
                    if (ip.length() > 0) strcpy(settings.ktox_host, ip.c_str());
                    save_settings();
                } else if (menu_idx == 3) {
                    current_state = STATE_RUNNING;
                    menu_idx = 0;
                    return;
                }
                delay(500);
            }
        }

        if (status.enter) {
            if (menu_idx == 3) {
                current_state = STATE_RUNNING;
                menu_idx = 0;
                return;
            }
        }
    }
}

// ==================== WiFi & WebSocket ====================
void setup_wifi() {
    static bool shown_screen = false;

    if (!shown_screen) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setTextColor(KTOX_YELLOW);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(0, 0);
        M5Cardputer.Display.println("WiFi Connection");
        M5Cardputer.Display.setTextColor(KTOX_WHITE);
        M5Cardputer.Display.printf("SSID: %s\n", settings.wifi_ssid);
        M5Cardputer.Display.setTextColor(KTOX_ORANGE);
        M5Cardputer.Display.println("Connecting...");

        WiFi.mode(WIFI_STA);
        WiFi.begin(settings.wifi_ssid, settings.wifi_password);

        shown_screen = true;
    }

    if (WiFi.status() == WL_CONNECTED) {
        M5Cardputer.Display.setTextColor(KTOX_GREEN);
        M5Cardputer.Display.println("Connected!");
        M5Cardputer.Display.setTextColor(KTOX_WHITE);
        M5Cardputer.Display.printf("IP: %s\n", WiFi.localIP().toString().c_str());

        Serial.println("WiFi connected!");
        Serial.println(WiFi.localIP());

        delay(2000);
        current_state = STATE_WS_CONNECTING;
        shown_screen = false;
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
        M5Cardputer.Display.setTextColor(KTOX_RED);
        M5Cardputer.Display.println("Failed!");
        delay(5000);
        current_state = STATE_SETUP;
        shown_screen = false;
    } else {
        M5Cardputer.Display.print(".");
    }
}

void setup_websocket() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(KTOX_YELLOW);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("KTOx Connection");
    M5Cardputer.Display.setTextColor(KTOX_WHITE);
    M5Cardputer.Display.printf("Host: %s:%d\n", settings.ktox_host, settings.ktox_port);
    M5Cardputer.Display.setTextColor(KTOX_ORANGE);
    M5Cardputer.Display.println("Connecting...");

    Serial.printf("Connecting to: %s:%d\n", settings.ktox_host, settings.ktox_port);

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
            Serial.println("[WSc] ✓ CONNECTED!");
            ws_connected = true;

            // If no auth token, send stream_profile immediately
            if (strlen(settings.auth_token) == 0) {
                Serial.printf("[WSc] No auth token, sending stream_profile...\n");
                DynamicJsonDocument profile_doc(512);
                profile_doc["type"] = "stream_profile";
                profile_doc["profile"] = "cardputer";
                profile_doc["format"] = "json";
                String profile_json;
                serializeJson(profile_doc, profile_json);
                webSocket.sendTXT(profile_json);
            } else {
                // Send auth token first
                Serial.printf("[WSc] Sending auth token...\n");
                DynamicJsonDocument auth_doc(512);
                auth_doc["type"] = "auth";
                auth_doc["token"] = settings.auth_token;
                String auth_json;
                serializeJson(auth_doc, auth_json);
                webSocket.sendTXT(auth_json);
            }

            Serial.println("[WSc] Waiting for frames...");
            M5Cardputer.Display.fillScreen(TFT_BLACK);
            M5Cardputer.Display.setTextColor(KTOX_GREEN);
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.setCursor(20, 50);
            M5Cardputer.Display.println("KTOx");
            M5Cardputer.Display.setTextColor(KTOX_RED);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(0, 90);
            M5Cardputer.Display.println("Connected!");
            if (strlen(settings.auth_token) > 0) {
                M5Cardputer.Display.println("Authenticating...");
            }
            M5Cardputer.Display.println("Receiving stream...");
            delay(1000);
            break;

        case WStype_TEXT: {
            Serial.printf("[WSc] Message received: %d bytes\n", length);

            DynamicJsonDocument doc(50000);
            DeserializationError error = deserializeJson(doc, payload);

            if (error) {
                Serial.printf("[WSc] JSON ERROR: %s\n", error.c_str());
                frame_stats.errors++;
                return;
            }

            const char* msg_type = doc["type"];
            Serial.printf("[WSc] Message type: %s\n", msg_type ? msg_type : "null");

            if (msg_type == nullptr) {
                Serial.println("[WSc] No type field!");
                return;
            }

            if (strcmp(msg_type, "frame") == 0) {
                const char* data = doc["data"];
                if (data) {
                    int data_len = strlen(data);
                    Serial.printf("[WSc] FRAME: %d bytes\n", data_len);
                    handle_frame_data(data);
                    frame_stats.received++;
                    Serial.printf("[WSc] Frame decoded! (total: %d)\n", frame_stats.decoded);
                } else {
                    Serial.println("[WSc] Frame message but NO DATA!");
                }
            } else if (strcmp(msg_type, "result") == 0) {
                last_result = doc["data"] | "Operation executed";
                Serial.printf("[WSc] Result: %s\n", last_result.c_str());
                current_state = STATE_EXECUTION;
            } else if (strcmp(msg_type, "auth_ok") == 0) {
                Serial.println("[WSc] ✓ Authentication successful!");
                // Send stream_profile after auth succeeds
                DynamicJsonDocument profile_doc(512);
                profile_doc["type"] = "stream_profile";
                profile_doc["profile"] = "cardputer";
                profile_doc["format"] = "json";
                String profile_json;
                serializeJson(profile_doc, profile_json);
                webSocket.sendTXT(profile_json);
            } else if (strcmp(msg_type, "auth_error") == 0) {
                Serial.println("[WSc] ✗ Authentication failed!");
                ws_connected = false;
            } else if (strcmp(msg_type, "stream_profile") == 0) {
                const char* status = doc["status"];
                if (status && strcmp(status, "ok") == 0) {
                    Serial.println("[WSc] ✓ Stream profile accepted!");
                    const char* profile = doc["profile"];
                    const char* format = doc["format"];
                    Serial.printf("[WSc] Profile: %s, Format: %s\n", profile ? profile : "?", format ? format : "?");
                } else {
                    Serial.println("[WSc] ✗ Stream profile rejected!");
                }
            } else {
                Serial.printf("[WSc] UNKNOWN type: %s\n", msg_type);
            }
            break;
        }

        case WStype_ERROR:
            Serial.println("[WSc] WebSocket error!");
            break;

        case WStype_BIN:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
        case WStype_PING:
        case WStype_PONG:
            // Not used in this application
            break;
    }
}

// ==================== FRAME HANDLING ====================
void handle_frame_data(const char* base64_data) {
    Serial.printf("[Frame] Base64 input size: %d bytes\n", strlen(base64_data));

    uint32_t jpeg_size = base64_decode_expected_len(strlen(base64_data));
    Serial.printf("[Frame] Expected JPEG size: %d bytes\n", jpeg_size);

    uint8_t jpeg_buffer[jpeg_size];

    int decoded_size = base64_decode((unsigned char*)base64_data,
                                      strlen(base64_data),
                                      jpeg_buffer);

    Serial.printf("[Frame] Decoded size: %d bytes\n", decoded_size);

    if (decoded_size <= 0) {
        Serial.println("[Frame] Base64 decode failed!");
        frame_stats.errors++;
        return;
    }

    Serial.println("[Frame] Calling TJpgDec.drawJpg()...");
    TJpgDec.drawJpg(0, 0, jpeg_buffer, decoded_size);
    Serial.println("[Frame] TJpgDec.drawJpg() complete");

    frame_stats.decoded++;
    frame_count++;

    static unsigned long last_stats = 0;
    if (millis() - last_stats > 5000) {
        last_stats = millis();
        Serial.printf("[Stats] Frames: recv=%d, decoded=%d, errors=%d\n",
                      frame_stats.received, frame_stats.decoded, frame_stats.errors);
    }
}

bool jpeg_decode_callback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    M5Cardputer.Display.pushImage(x, y, w, h, bitmap);
    return true;
}

// ==================== KEYBOARD INPUT ====================
void read_keyboard_input() {
    if (M5Cardputer.Keyboard.isChange()) {
        auto status = M5Cardputer.Keyboard.keysState();

        if (!status.word.empty()) {
            char key = status.word[0];

            if (key == 'h') {  // H - config menu
                current_state = STATE_CONFIG_MENU;
                delay(300);
                return;
            }

            if (key == 'm') {  // M - open main menu
                current_state = STATE_MAIN_MENU;
                menu_index = 0;
                delay(300);
                return;
            }

            if (key == '?') {  // ? - show help
                show_help_screen();
                return;
            }

            if (key == 't') {  // T - set target IP
                target_ip = get_text_input("Target IP:", 15);
                return;
            }

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

void show_help_screen() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(KTOX_RED);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("KTOx Help");

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(KTOX_YELLOW);
    M5Cardputer.Display.println("");
    M5Cardputer.Display.setTextColor(KTOX_WHITE);

    M5Cardputer.Display.println("[M] Open menu");
    M5Cardputer.Display.println("[H] Config");
    M5Cardputer.Display.println("[T] Set target IP");
    M5Cardputer.Display.println("[?] Help");
    M5Cardputer.Display.println("");
    M5Cardputer.Display.println("Stream Controls:");
    M5Cardputer.Display.println("WASD/IJKL Navigate");
    M5Cardputer.Display.println("SPACE/ENTER Select");
    M5Cardputer.Display.println("Q/ESC Actions");

    M5Cardputer.Display.setTextColor(KTOX_RUST);
    M5Cardputer.Display.setCursor(0, 115);
    M5Cardputer.Display.println("ENTER:Back");

    M5Cardputer.update();

    while (true) {
        if (M5Cardputer.Keyboard.isChange()) {
            auto status = M5Cardputer.Keyboard.keysState();
            if (status.enter) {
                delay(200);
                break;
            }
        }
        M5Cardputer.update();
        delay(10);
    }
}

// ==================== COMMAND EXECUTION ====================
void send_command(const char* command, const char* param) {
    if (!ws_connected) return;

    DynamicJsonDocument doc(512);
    doc["type"] = "command";
    doc["command"] = command;
    if (param && strlen(param) > 0) {
        doc["param"] = param;
    }

    String json_str;
    serializeJson(doc, json_str);
    webSocket.sendTXT(json_str);

    last_command = millis();
}

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

void execute_operation(const char* operation) {
    send_command(operation, "");
}

// ==================== STATUS BAR ====================
void draw_status_bar() {
    static unsigned long last_update = 0;
    if (millis() - last_update < 500) return;
    last_update = millis();

    char status[256];
    snprintf(status, sizeof(status), "[%s] %d frames | FPS:%.1f",
             ws_connected ? "●" : "○",
             frame_count,
             frame_count * 1000.0f / (millis() + 1));

    uint16_t status_color = ws_connected ? KTOX_GREEN : KTOX_RED;
    M5Cardputer.Display.setTextColor(status_color, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillRect(0, 118, 240, 17, TFT_BLACK);
    M5Cardputer.Display.drawRect(0, 118, 240, 17, status_color);
    M5Cardputer.Display.setCursor(2, 121);
    M5Cardputer.Display.print(status);

    // Show keyboard hints
    M5Cardputer.Display.setTextColor(KTOX_RUST);
    M5Cardputer.Display.setTextSize(0);  // tiny font
    M5Cardputer.Display.setCursor(2, 128);
    M5Cardputer.Display.print("M:Menu H:Config");
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
