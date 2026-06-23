#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>

typedef void (*ota_reply_fn)(const char *text, void *ctx);

bool ota_command_process(char *line, ota_reply_fn reply, void *ctx);

#endif
