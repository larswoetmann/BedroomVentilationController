# Install the configuration without a card reader

This temporary sketch writes the bundled snapshot of `ventilation.cfg.example`
to `/ventilation.cfg` on the Adafruit Metro ESP32-S3 built-in SD card.
It overwrites an existing configuration after verifying every byte of the
temporary file. Other files are left untouched. It does not format the card.
Each reset with Serial Monitor connected installs the bundled configuration again.

1. Insert a FAT32-formatted SD card with the board powered off.
2. Connect the Metro's USB-C port to your PC with a data cable.
3. Open `InstallVentilationConfig.ino` in Arduino IDE.
4. Select **Adafruit Metro ESP32-S3**, the board's port, and set
   **USB CDC On Boot → Enabled** for this helper.
5. Upload, then open Serial Monitor at **115200 baud**. The sketch waits for
   Serial Monitor before writing. Look for `SUCCESS`.
6. Open the normal `BedroomVentilationController.ino`, keep the Metro board
   selected, restore **USB CDC On Boot → Disabled**, and upload it.

The SD file remains after uploading the normal firmware. The helper temporarily
replaces the running controller firmware. To customize the bundled configuration
before installation, edit the raw text in `config_contents.h`.

Pin mapping: SCK 39, MISO 21, MOSI 42, CS 45, matching the official Metro variant.
