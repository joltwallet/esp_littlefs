LittleFS for ESP-IDF.

# What is LittleFS?

[LittleFS](https://github.com/ARMmbed/littlefs) is a small fail-safe filesystem 
for microcontrollers. We ported LittleFS to esp-idf (specifically, the ESP32) 
because SPIFFS was too slow, and FAT was too fragile.

# How to Use

There are two ways to add this component to your project

1. As a ESP-IDF managed component: In your project directory run

```
idf.py add-dependency joltwallet/littlefs==1.5.1
```

2. As a submodule: In your project, add this as a submodule to your `components/` directory.

```
git submodule add https://github.com/joltwallet/esp_littlefs.git
git submodule update --init --recursive
```

The library can be configured via `idf.py menuconfig` under `Component config->LittleFS`.

### Example
User @wreyford has kindly provided a [demo repo](https://github.com/wreyford/demo_esp_littlefs) showing the use of `esp_littlefs`. A modified copy exists in the `example/` directory.


# Documentation

See the official [ESP-IDF SPIFFS documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/spiffs.html), basically all the functionality is the 
same; just replace `spiffs` with `littlefs` in all function calls.

Also see the comments in `include/esp_littlefs.h`

Slight differences between this configuration and SPIFFS's configuration is in the `esp_vfs_littlefs_conf_t`:

1. `max_files` field doesn't exist since we removed the file limit, thanks to @X-Ryl669
2. `partition_label` is not allowed to be `NULL`. You must specify the partition name from your partition table. This is because there isn't a define `littlefs` partition subtype in `esp-idf`. The subtype doesn't matter.

### Filesystem Image Creation

At compile time, a filesystem image can be created and flashed to the device by adding the following to your project's `CMakeLists.txt` file:

```
littlefs_create_partition_image(partition_name path_to_folder_containing_files)
```

For example, if your partition table looks like:

```
# Name,   Type, SubType,  Offset,  Size, Flags
nvs,      data, nvs,      0x9000,  0x6000,
phy_init, data, phy,      0xf000,  0x1000,
factory,  app,  factory,  0x10000, 1M,
graphics,  data, spiffs,         ,  0xF0000, 
```

and your project has a folder called `device_graphics`, your call should be:

```
littlefs_create_partition_image(graphics device_graphics)
```



# Performance

Here are some naive benchmarks to give a vague indicator on performance.

Formatting a ~512KB partition:

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

* LittleFS operates on blocks, and blocks have a size of 4096 bytes on the ESP32.

* A freshly formatted LittleFS will have 2 blocks in use, making it seem like 8KB are in use.

# Running Unit Tests

To flash the unit-tester app and the unit-tests, clone or symbolicly link this
component to `$IDF_PATH/tools/unit-test-app/components/littlefs`. Make sure the
folder name is `littlefs`, not `esp_littlefs`. Then, run the following:

```
cd $IDF_PATH/tools/unit-test-app
idf.py menuconfig  # See notes
idf.py -T littlefs -p YOUR_PORT_HERE flash monitor
```

In `menuconfig`:

* Set the partition table to `components/littlefs/partition_table_unit_test_app.csv`

* Double check your crystal frequency; my board doesn't work with autodetect.

To test on an encrypted partition, add the `encrypted` flag to the `flash_test` partition
in `partition_table_unit_test_app.csv`. I.e.

```
flash_test,  data, spiffs,    ,        512K, encrypted
```

Also make sure that `CONFIG_SECURE_FLASH_ENC_ENABLED=y` in `menuconfig`.

The unit tester can then be flashed via the command:

```
idf.py -T littlefs -p YOUR_PORT_HERE encrypted-flash monitor
```

# Breaking Changes

* July 22, 2020 - Changed attribute type for file timestamp from `0` to `0x74` ('t' ascii value).

# Acknowledgement

This code base was heavily modeled after the SPIFFS esp-idf component.
