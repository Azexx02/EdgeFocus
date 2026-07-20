#ifndef GIMBAL_RX_H
#define GIMBAL_RX_H

#include <stddef.h>
#include <stdint.h>

#include "gimbal_protocol.h"

typedef enum {
  GIMBAL_RX_NONE = 0,
  GIMBAL_RX_LINE,
  GIMBAL_RX_BAD_FORMAT,
  GIMBAL_RX_EMERGENCY
} GimbalRxEvent;

typedef struct {
  char line[GIMBAL_PROTOCOL_MAX_LINE + 1U];
  size_t length;
  int discard_line;
} GimbalRxFramer;

void gimbal_rx_init(GimbalRxFramer *framer);
GimbalRxEvent gimbal_rx_feed(GimbalRxFramer *framer, uint8_t byte,
                             const char **line, size_t *length);

#endif
