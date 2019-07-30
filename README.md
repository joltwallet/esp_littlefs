LittleFS for ESP-IDF.

# What is LittleFS?

[LittleFS](https://github.com/ARMmbed/littlefs) is a small fail-safe filesystem 
for microcontrollers. We ported LittleFS to esp-idf (specifically, the ESP32) 
because SPIFFS was too slow, and FAT was too fragile.

# How to Use

In your project, add this as a submodule to your `components/` directory.

```
git submodule add https://github.com/joltwallet/esp_littlefs.git
```

The library can be configured via `make menuconfig` under `Component config->LittleFS`.

# Documentation

See the official ESP-IDF SPIFFS documentation, basically all the functionality is the 
same; just replace `spiffs` with `littlefs` in all function calls.

Also see the comments in `include/esp_littlefs.h`

# Limitations

Currently only an absolute maximum of *16* files can be opened at once. This can be
increased by changing the `esp_littlefs_t.fd_used` bitmask to a larger datatype, 
like a `uint16_t`. 

# Tips, Tricks, and Gotchas

* LittleFS operates on blocks, and blocks have a size of 4096 bytes on the ESP32.

* A freshly formatted LittleFS will have 2 blocks in use, making it seem like 8KB are in use.

# Running Unit Tests

Clone the [ESP-IDF unit test app](https://github.com/espressif/esp-idf/tree/master/tools/unit-test-app) 
and add this component to the `components/` directory. Run `make menuconfig` to
set up parameters specific to your development environment. Finally, flash the 
unit test app via `make flash TEST_COMPONENTS='littlefs' monitor`.

# Acknowledgement

This code base was heavily modeled after the SPIFFS esp-idf component.
