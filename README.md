# KTOXM5
Control KTOX_Pi with M5 stack Card Computer

## Notes for PlatformIO builds
- This project targets the **M5Cardputer** and expects the KTOX_Pi WebSocket protocol (`type: "frame"` for JPEG frames and `type: "input"` for button events).
- Build command:
  - `python3 -m platformio run`
- Expected firmware output for M5Launcher testing:
  - `.pio/build/m5stack-cardputer/firmware.bin`
