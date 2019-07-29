# How to Use

```
git clone --recursive
```

# Initialize and Mount Filesystem

# Limitations
Currently only an absolute maximum of 16 files can be opened at once. This can be
increased by changing the `esp_littlefs_t.fd_used` bitmask to a larger datatype, 
like a `uint16_t`. 

# Acknowledgement
This code base was heavily modeled after the SPIFFS esp-idf component.
