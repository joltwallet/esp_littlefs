/*
 * Force-included into cppcheck runs (see .github/workflows/pr.yml).
 *
 * cppcheck doesn't have the ESP-IDF headers on its include path, so the
 * version macros from esp_idf_version.h are unresolved; cppcheck >= 2.19
 * reports every `#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(...)` as a
 * syntaxError. Mirror the real definitions and analyze against the newest
 * ESP-IDF release CI builds.
 */
#pragma once
#define ESP_IDF_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(6, 0, 0)
