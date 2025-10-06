1. Build firmware: 
   1. pio run
   2. pio run -e esp32dev
2. Flag firmware
   1. pio run -t upload
   2. pio run -t upload --upload-port COM3
3. Erase flash
   1. pio run -t erase
   2. pio run -t erase --upload-port COM3
4. Mở Serial Monitor
   1. pio device monitor -p COM3 -b 115200
   2. pio device list
5. Build + Upload + Mở Serial Monitor (chạy liên tục):
   1. pio run -e gateway -t upload --upload-port COM10 && pio device monitor -p COM10 -b 115200
