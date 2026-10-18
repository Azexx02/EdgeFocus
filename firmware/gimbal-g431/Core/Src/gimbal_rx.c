#include "gimbal_rx.h"

#include <string.h>

void gimbal_rx_init(GimbalRxFramer *framer)
{
  if (framer != NULL) memset(framer, 0, sizeof(*framer));
}

GimbalRxEvent gimbal_rx_feed(GimbalRxFramer *framer, uint8_t byte,
                             const char **line, size_t *length)
{
  if (line != NULL) *line = NULL;
  if (length != NULL) *length = 0U;
  if (framer == NULL) return GIMBAL_RX_BAD_FORMAT;
  if (byte == '!') {
    framer->length = 0U;
    framer->discard_line = 0;
    return GIMBAL_RX_EMERGENCY;
  }
  if (byte == '\r' || byte == '\n') {
    if (framer->discard_line) {
      framer->discard_line = 0;
      framer->length = 0U;
      return GIMBAL_RX_NONE;
    }
    if (framer->length == 0U) return GIMBAL_RX_NONE;
    framer->line[framer->length] = '\0';
    if (line != NULL) *line = framer->line;
    if (length != NULL) *length = framer->length;
    framer->length = 0U;
    return GIMBAL_RX_LINE;
  }
  if (framer->discard_line) return GIMBAL_RX_NONE;
  if (framer->length >= GIMBAL_PROTOCOL_MAX_LINE) {
    framer->length = 0U;
    framer->discard_line = 1;
    return GIMBAL_RX_BAD_FORMAT;
  }
  framer->line[framer->length++] = (char)byte;
  return GIMBAL_RX_NONE;
}
