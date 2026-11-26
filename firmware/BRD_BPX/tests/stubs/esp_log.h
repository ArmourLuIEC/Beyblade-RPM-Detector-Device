#ifndef TEST_ESP_LOG_H
#define TEST_ESP_LOG_H

#include <stdio.h>

#define ESP_LOGE(tag, format, ...) fprintf(stderr, "%s: " format "\n", tag, __VA_ARGS__)

#endif
