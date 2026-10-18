#include "main.h"
#include "copro_sync.h"
#include "edgefocus_fan_board.h"

#include <string.h>

#define EDGEFOCUS_STAGED_BRINGUP 1
#define EDGEFOCUS_PROBE "EFAN_PROBE\n"

IPCC_HandleTypeDef hipcc;
VIRT_UART_HandleTypeDef huart0;
static volatile bool bringup_probe_received = false;

static void MX_IPCC_Init(void)
{
  hipcc.Instance = IPCC1;
  if (HAL_IPCC_Init(&hipcc) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_NVIC_SetPriority(IPCC1_RX_IRQn, DEFAULT_IRQ_PRIO, 0);
  HAL_NVIC_EnableIRQ(IPCC1_RX_IRQn);
}

static void VIRT_UART0_RxCpltCallback(VIRT_UART_HandleTypeDef *huart)
{
  if ((huart->RxXferSize == (sizeof(EDGEFOCUS_PROBE) - 1U)) &&
      (memcmp(huart->pRxBuffPtr, EDGEFOCUS_PROBE,
              sizeof(EDGEFOCUS_PROBE) - 1U) == 0))
  {
    bringup_probe_received = true;
    return;
  }
  EdgeFocus_FanBoard_OnRx(huart->pRxBuffPtr, huart->RxXferSize, HAL_GetTick());
}

bool EdgeFocus_FanBoard_Transmit(const uint8_t *data, size_t size)
{
  if ((data == NULL) || (size == 0U) || (size > RPMSG_BUFFER_SIZE) ||
      (size > UINT16_MAX))
  {
    return false;
  }
  return VIRT_UART_Transmit(&huart0, (uint8_t *)data, (uint16_t)size) ==
         VIRT_UART_OK;
}

int main(void)
{
  bool board_ready = false;
  uint32_t last_diagnostic_ms = 0U;

  HAL_Init();
  SystemCoreClockUpdate();

  MX_IPCC_Init();
  if (!IS_DEVELOPER_BOOT_MODE())
  {
    CoproSync_Init();
  }

#if defined(RPMSG_LATE_ATTACH)
  while (OPENAMP_check_for_message())
  {
    EdgeFocus_FanBoard_Poll(HAL_GetTick());
    HAL_Delay(1);
  }
#endif

  if (MX_OPENAMP_Init(RPMSG_REMOTE, NULL) != 0)
  {
    Error_Handler();
  }
  if (VIRT_UART_Init(&huart0) != VIRT_UART_OK)
  {
    Error_Handler();
  }
  if (VIRT_UART_RegisterCallback(&huart0, VIRT_UART_RXCPLT_CB_ID,
                                 VIRT_UART0_RxCpltCallback) != VIRT_UART_OK)
  {
    Error_Handler();
  }

  /*
   * Bring-up is deliberately communication-first: publish the RPMsg endpoint
   * before touching the custom carrier's GPIO/TIM resources so Linux can see
   * the last completed hardware stage if an access stalls M33.
   */
#if EDGEFOCUS_STAGED_BRINGUP
  /*
   * rpmsg_send needs a resolved destination.  Linux opens the tty first and
   * sends EFAN_PROBE; only then do stage markers and hardware initialization
   * begin.  This diagnostic gate is removed for the final production image.
   */
  while (!bringup_probe_received)
  {
    (void)OPENAMP_check_for_message();
    HAL_Delay(1);
  }
#endif
  board_ready = EdgeFocus_FanBoard_Init(HAL_GetTick());
  if (board_ready)
  {
    EdgeFocus_FanBoard_OnTransportReady(HAL_GetTick());
  }

  while (1)
  {
    uint32_t now_ms;
    OPENAMP_check_for_message();
    now_ms = HAL_GetTick();
    if (board_ready)
    {
      EdgeFocus_FanBoard_Poll(now_ms);
    }
    else if ((now_ms - last_diagnostic_ms) >= 1000U)
    {
      uint8_t diagnostic[] = "EFAN_BOOT_FAIL:0\n";
      const uint32_t error = EdgeFocus_FanBoard_InitError();
      diagnostic[15] = (error <= 9U) ? (uint8_t)('0' + error) : (uint8_t)'?';
      (void)EdgeFocus_FanBoard_Transmit(diagnostic, sizeof(diagnostic) - 1U);
      last_diagnostic_ms = now_ms;
    }
  }
}

void CoproSync_ShutdownCb(IPCC_HandleTypeDef *handle,
                          uint32_t channel_index,
                          IPCC_CHANNELDirTypeDef channel_dir)
{
  EdgeFocus_FanBoard_OnTransportLost();
  EdgeFocus_FanBoard_Shutdown();
  VIRT_UART_DeInit(&huart0);
  OPENAMP_DeInit();
  HAL_IPCC_NotifyCPU(handle, channel_index, channel_dir);
  while (1)
  {
  }
}

void Error_Handler(void)
{
  EdgeFocus_FanBoard_ForceSafe();
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  Error_Handler();
}
#endif
