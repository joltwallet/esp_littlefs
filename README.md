LittleFS for ESP-IDF.

# What is LittleFS?

[LittleFS](https://github.com/littlefs-project/littlefs) is a small fail-safe filesystem 
for microcontrollers. We ported LittleFS to esp-idf (specifically, the ESP32) 
because SPIFFS was too slow, and FAT was too fragile.

# How to Use

In your project, add this as a submodule to your `components/` directory.

```
git submodule add https://github.com/joltwallet/esp_littlefs.git
git submodule update --init --recursive
```

The library can be configured via `make menuconfig` under `Component config->LittleFS`.

### Example
User @wreyford has kindly provided a demo repo showing the use of `esp_littlefs`:
https://github.com/wreyford/demo_esp_littlefs

# Documentation

This fork supports multiple backends.
* RAM backend untested

    name: ram
* FLASH backend tested

    name: flash
* SDCARD backend untested

    name: sd
* CUSTOM backend

# Builtin backends

```c
esp_littlefs_backendname_create_conf_t conf = ESP_LITTLEFS_BACKENDNAME_CREATE_CONFIG_DEFAULT();
// set config here
lfs_t * lfs;
ESP_ERROR_CHECK(esp_littlefs_backendname_create(&lfs, &conf));
// use lfs
// destroy the lfs - for future compatibility always use the correct function for the each backend
ESP_ERROR_CHECK(esp_littlefs_backendname_delete(&lfs));
```

# Custom backend

A custom backend can be built on top of esp_littlefs_abs.h or just by manually creating a lfs_t.

# mount into vfs

```c
lfs_t * lfs;
// init lfs with a backend here
esp_littlefs_vfs_mount_conf_t conf = ESP_LITTLEFS_VFS_MOUNT_CONFIG_DEFAULT();
conf.lfs = lfs;
// set config here
ESP_ERROR_CHECK(esp_littlefs_vfs_mount(&conf));
// use lfs over vfs
// unmount from vfs
ESP_ERROR_CHECK(esp_littlefs_vfs_unmount(lfs));
// destroy the lfs with the correct method for the used backend
```

# Performance - Test data may not reflect this forks performance

Here are some naive benchmarks to give a vague indicator on performance.

Formatting a ~512KB partition: (This test is currently broken)

```
FAT:         963,766 us
SPIFFS:   10,824,054 us
LittleFS:  2,067,845 us
```

Writing 5 88KB files:

```
FAT:         13,601,171 us
SPIFFS*:    118,883,197 us
LittleFS**:   6,582,045 us
LittleFS***:  5,734,811 us
*Only wrote 374,784 bytes instead of the benchmark 440,000, so this value is extrapolated
**CONFIG_LITTLEFS_CACHE_SIZE=128
***CONFIG_LITTLEFS_CACHE_SIZE=512 (default value)
```

In the above test, SPIFFS drastically slows down as the filesystem fills up. Below
is the specific breakdown of file write times for SPIFFS. Not sure what happens 
on the last file write.


```
SPIFFS:

88000 bytes written in 1325371 us
88000 bytes written in 1327848 us
88000 bytes written in 5292095 us
88000 bytes written in 19191680 us
22784 bytes written in 74082963 us
```

Reading 5 88KB files:

```
FAT:          3,111,817 us
SPIFFS*:      3,392,886 us
LittleFS**:   3,425,796 us
LittleFS***:  3,210,140 us
*Only read 374,784 bytes instead of the benchmark 440,000, so this value is extrapolated
**CONFIG_LITTLEFS_CACHE_SIZE=128
***CONFIG_LITTLEFS_CACHE_SIZE=512 (default value)
```

Deleting 5 88KB files:

```
FAT:         934,769 us
SPIFFS*:      822,730 us
LittleFS**:   31,502 us
LittleFS***:  20,063 us
*The 5th file was smaller, did not extrapolate value.
**CONFIG_LITTLEFS_CACHE_SIZE=128
***CONFIG_LITTLEFS_CACHE_SIZE=512 (default value)
```


# Tips, Tricks, and Gotchas

* A freshly formatted LittleFS will have 2 blocks in use, making it seem like 2*block_size are in use.

# Running Unit Tests

To flash the unit-tester app and the unit-tests, run


``` sh
cd $ENV{IDF_PATH}/tools/unit-test-app
idf.py -D EXTRA_COMPONENT_DIRS=path\to\folder\that\contains\esp_littlefs\folder -T esp_littlefs menuconfig // change partition table to the partition_table_unit_test_app.csv from this project
idf.py -D EXTRA_COMPONENT_DIRS=path\to\folder\that\contains\esp_littlefs\folder -T esp_littlefs flash monitor
```

The following information is from the original repo. I have yet to test this:

To test on an encrypted partition, add the `encrypted` flag to the `flash_test` partition
in `partition_table_unit_test_app.csv`. I.e.

```
flash_test,  data, spiffs,    ,        512K, encrypted
```

Also make sure that `CONFIG_SECURE_FLASH_ENC_ENABLED=y` in `menuconfig`.

The unit tester can then be flashed via the command:

```
make TEST_COMPONENTS='src' encrypted-flash monitor
```
# Breaking Changes

* July 22, 2020 - Changed attribute type for file timestamp from `0` to `0x74` ('t' ascii value).

# Acknowledgement

This code was heavily modeled after the original repo 😉.
This code base was heavily modeled after the SPIFFS esp-idf component.
