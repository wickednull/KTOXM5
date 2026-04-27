# KTOXM5
Control KTOX_Pi with M5 stack Card Computer

## Build for M5Launcher (`firmware.bin`)
- This project targets the **M5Cardputer** and expects the KTOX_Pi WebSocket protocol (`type: "frame"` for JPEG frames and `type: "input"` for button events).

### 1) Clone and enter the project directory
```bash
git clone https://github.com/wickednull/KTOXM5.git
cd KTOXM5
```

### 2) Build
```bash
pio run
```

If `pio` is not installed:
```bash
python3 -m pip install --user platformio
python3 -m platformio run
```

### 3) Use the generated `.bin` in M5Launcher
Output file:
```text
.pio/build/m5stack-cardputer/firmware.bin
```

## Common errors
- `NotPlatformIOProjectError: ... platformio.ini file has not been found`  
  You are not in this repo folder. Run:
  ```bash
  cd KTOXM5
  pio run
  ```

- `UnknownPackageError: ... links2004/WebSocketsClient ...`  
  You are on an older checkout/config. Update and retry:
  ```bash
  cd KTOXM5
  git pull
  pio run
  ```

- `Error: Nothing to build. Please put your source code files to the .../src folder`  
  Pull latest first (this repo now includes `src/main.cpp` for PlatformIO):
  ```bash
  cd KTOXM5
  git pull
  pio run
  ```
