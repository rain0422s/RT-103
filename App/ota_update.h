#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*ota_reply_fn)(const char *text, void *ctx);

bool ota_command_process(char *line, ota_reply_fn reply, void *ctx);
bool ota_binary_process(const uint8_t *data, uint16_t len,
			ota_reply_fn reply, void *ctx);
bool ota_update_confirm_boot(void);

#endif
