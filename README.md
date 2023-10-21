LittleFS for ESP-IDF.

# What is LittleFS?

[LittleFS](https://github.com/ARMmbed/littlefs) is a small fail-safe filesystem 
for microcontrollers. We ported LittleFS to esp-idf (specifically, the ESP32) 
because SPIFFS was too slow, and FAT was too fragile.

# How to Use

There are two ways to add this component to your project

1. As a ESP-IDF managed component: In your project directory run

```
idf.py add-dependency joltwallet/littlefs==1.10.2
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
    * Alternatively, you can specify an `esp_partition_t*` to a `partition` and set `partition_label=NULL`.
3. `grow_on_mount` will expand an existing filesystem to fill the partition. Defaults to `false`.
    * LittleFS filesystems can only grow, they cannot shrink.

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
Tests were performed with the following configuration:

* ESP-IDF: v4.4
* Target: ESP32
* CPU Clock: 160MHz
* Flash SPI Freq: 80MHz
* Flash SPI Mode: QIO

In these tests, FAT has a cache size of 4096, and SPIFFS has a cahce size of 256 bytes.

#### Formatting a 512KB partition

```
FAT:         549,494 us
SPIFFS:   10,715,425 us
LittleFS:    110,997 us
```

#### Writing 5 88KB files

```
FAT:                            7,124,812 us
SPIFFS*:                       99,138,905 us
LittleFS (cache=128):           8,261,920 us
LittleFS (cache=512 default):   6,356,247 us
LittleFS (cache=4096):          6,026,592 us
*Only wrote 374,784 bytes instead of the benchmark 440,000, so this value is extrapolated
```

In the above test, SPIFFS drastically slows down as the filesystem fills up. Below
is the specific breakdown of file write times for SPIFFS. Not sure what happens 
on the last file write.


```
SPIFFS:

88000 bytes written in 2190635 us
88000 bytes written in 2190321 us
88000 bytes written in 5133605 us
88000 bytes written in 16570667 us
22784 bytes written in 73053677 us
```

#### Reading 5 88KB files

```
FAT:                            5,685,230 us
SPIFFS*:                        5,162,289 us
LittleFS (cache=128):           6,284,142 us
LittleFS (cache=512 default):   5,874,931 us
LittleFS (cache=4096):          5,731,385 us
*Only read 374,784 bytes instead of the benchmark 440,000, so this value is extrapolated
```

#### Deleting 5 88KB files

```
FAT:                              680,358 us
SPIFFS*:                        1,653,500 us
LittleFS (cache=128):              86,090 us
LittleFS (cache=512 default):      53,705 us
LittleFS (cache=4096):             27,709 us
*The 5th file was smaller, did not extrapolate value.
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

* Double check your crystal frequency `ESP32_XTAL_FREQ_SEL`; my board doesn't work with autodetect.

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
* May 3, 2023 - All logging tags have been changed to a unified `esp_littlefs`.

# Acknowledgement

This code base was heavily modeled after the SPIFFS esp-idf component.
