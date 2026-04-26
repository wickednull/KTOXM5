#ifndef CONFIG_H
#define CONFIG_H

// No manual configuration needed!
// Everything is configured via the app's setup menu

#define DEBUG 1

#if DEBUG
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINT(x)
#define DEBUG_PRINTF(...)
#endif

#endif
