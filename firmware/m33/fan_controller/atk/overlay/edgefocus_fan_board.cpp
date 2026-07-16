#include "edgefocus_fan_board.h"

#include "FanControllerApp.h"
#include "PwmMapping.h"
#include "res_mgr.h"
#include "stm32mp2xx_hal.h"
#include "stm32mp2xx_hal_tim.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

using edgefocus::fan::FanConfig;
using edgefocus::fan::m33::ControllerAppConfig;
using edgefocus::fan::m33::FanControllerApp;
using edgefocus::fan::m33::PlatformCallbacks;

constexpr std::uint32_t kPwmPeriodNs = 40000;
constexpr std::uint32_t kTimerTickHz = 1000000;
constexpr std::uint32_t kNanosecondsPerTick = 1000;
constexpr std::uint32_t kSafeDutyNs = 8000;
constexpr std::uint32_t kControlIntervalMs = 100;
constexpr std::uint32_t kTachWindowMs = 1000;
constexpr std::uint32_t kIrqPriority = 1;
constexpr bool kEnableTach = true;
/* Board bring-up guard: request/release ownership without touching GPIO/TIM. */
constexpr bool kResourceProbeOnly = false;
/* Diagnostic staircase: stage 8 is split into the three PWMN register writes. */
constexpr std::uint32_t kStopAfterStage = 9;
constexpr std::uint32_t kStopAfterPwmStartStep = 0;
constexpr bool kEnableRuntimeControlLoop = true;

TIM_HandleTypeDef g_tim17{};
EXTI_HandleTypeDef g_tach_exti{};
volatile std::uint32_t g_tach_pulses = 0;
std::uint32_t g_last_control_ms = 0;
std::uint32_t g_last_tach_ms = 0;
bool g_gpio_clock_owned = false;
bool g_pwm_pin_owned = false;
bool g_tach_pin_owned = false;
bool g_timer_owned = false;
bool g_timer_clock_enabled_by_app = false;
bool g_timer_handle_ready = false;
bool g_pwm_started = false;
std::uint32_t g_init_error = 0;

alignas(FanControllerApp) std::uint8_t g_app_storage[sizeof(FanControllerApp)];
FanControllerApp *g_app = nullptr;

enum class StageStatus
{
  Begin,
  Ok,
  Fail,
};

void reportStage(std::uint32_t stage, StageStatus status)
{
  char marker[24] = "EFAN_STAGE:0:";
  marker[11] = static_cast<char>('0' + stage);
  switch (status)
  {
    case StageStatus::Begin:
      std::strcat(marker, "BEGIN\n");
      break;
    case StageStatus::Ok:
      std::strcat(marker, "OK\n");
      break;
    case StageStatus::Fail:
      std::strcat(marker, "FAIL\n");
      break;
  }
  (void)EdgeFocus_FanBoard_Transmit(
      reinterpret_cast<const std::uint8_t *>(marker), std::strlen(marker));
  /* Leave enough time for the rpmsg-tty path to expose each boundary. */
  HAL_Delay(30);
}

void reportPwmStartStep(const char *step, StageStatus status)
{
  char marker[32] = "EFAN_PWM:";
  std::strcat(marker, step);
  std::strcat(marker, ":");
  switch (status)
  {
    case StageStatus::Begin:
      std::strcat(marker, "BEGIN\n");
      break;
    case StageStatus::Ok:
      std::strcat(marker, "OK\n");
      break;
    case StageStatus::Fail:
      std::strcat(marker, "FAIL\n");
      break;
  }
  (void)EdgeFocus_FanBoard_Transmit(
      reinterpret_cast<const std::uint8_t *>(marker), std::strlen(marker));
  HAL_Delay(30);
}

void reportDiagnostic(const char *name, bool value)
{
  char marker[32] = "EFAN_DIAG:";
  std::strcat(marker, name);
  std::strcat(marker, value ? ":1\n" : ":0\n");
  (void)EdgeFocus_FanBoard_Transmit(
      reinterpret_cast<const std::uint8_t *>(marker), std::strlen(marker));
  HAL_Delay(30);
}

bool startComplementaryPwm()
{
  if (TIM_CHANNEL_N_STATE_GET(&g_tim17, TIM_CHANNEL_1) !=
      HAL_TIM_CHANNEL_STATE_READY)
  {
    return false;
  }
  TIM_CHANNEL_N_STATE_SET(&g_tim17, TIM_CHANNEL_1,
                          HAL_TIM_CHANNEL_STATE_BUSY);

  reportPwmStartStep("CCXN", StageStatus::Begin);
  g_tim17.Instance->CCER &= ~TIM_CCER_CC1NE;
  g_tim17.Instance->CCER |= TIM_CCER_CC1NE;
  __DSB();
  HAL_Delay(1);
  if ((g_tim17.Instance->CCER & TIM_CCER_CC1NE) == 0)
  {
    reportPwmStartStep("CCXN", StageStatus::Fail);
    return false;
  }
  reportPwmStartStep("CCXN", StageStatus::Ok);
  if (kStopAfterPwmStartStep == 1)
  {
    return false;
  }

  reportPwmStartStep("MOE", StageStatus::Begin);
  reportDiagnostic("BKE", (g_tim17.Instance->BDTR & TIM_BDTR_BKE) != 0);
  reportDiagnostic("BK2E", (g_tim17.Instance->BDTR & TIM_BDTR_BK2E) != 0);
  reportDiagnostic("LOCK", (g_tim17.Instance->BDTR & TIM_BDTR_LOCK) != 0);
  reportDiagnostic("AF1", g_tim17.Instance->AF1 != 0);
  reportDiagnostic("AF2", g_tim17.Instance->AF2 != 0);
  reportDiagnostic("BIF_PRE", (g_tim17.Instance->SR & TIM_SR_BIF) != 0);
  reportDiagnostic("B2IF_PRE", (g_tim17.Instance->SR & TIM_SR_B2IF) != 0);
  __HAL_TIM_CLEAR_FLAG(&g_tim17, TIM_FLAG_BREAK | TIM_FLAG_BREAK2);
  reportDiagnostic("BIF_CLR", (g_tim17.Instance->SR & TIM_SR_BIF) != 0);
  reportDiagnostic("B2IF_CLR", (g_tim17.Instance->SR & TIM_SR_B2IF) != 0);
  g_tim17.Instance->BDTR |= TIM_BDTR_MOE;
  __DSB();
  HAL_Delay(1);
  reportDiagnostic("BIF_POST", (g_tim17.Instance->SR & TIM_SR_BIF) != 0);
  reportDiagnostic("B2IF_POST", (g_tim17.Instance->SR & TIM_SR_B2IF) != 0);
  reportDiagnostic("MOE", (g_tim17.Instance->BDTR & TIM_BDTR_MOE) != 0);
  if ((g_tim17.Instance->BDTR & TIM_BDTR_MOE) == 0)
  {
    reportPwmStartStep("MOE", StageStatus::Fail);
    return false;
  }
  reportPwmStartStep("MOE", StageStatus::Ok);
  if (kStopAfterPwmStartStep == 2)
  {
    return false;
  }

  reportPwmStartStep("CEN", StageStatus::Begin);
  g_tim17.Instance->CR1 |= TIM_CR1_CEN;
  __DSB();
  HAL_Delay(1);
  if ((g_tim17.Instance->CR1 & TIM_CR1_CEN) == 0)
  {
    reportPwmStartStep("CEN", StageStatus::Fail);
    return false;
  }
  reportPwmStartStep("CEN", StageStatus::Ok);
  return true;
}

void beginStage(std::uint32_t stage)
{
  g_init_error = stage;
  reportStage(stage, StageStatus::Begin);
}

bool failStage(std::uint32_t stage)
{
  g_init_error = stage;
  reportStage(stage, StageStatus::Fail);
  return false;
}

bool writePwmDuty(void *, std::uint32_t duty_ns)
{
  std::uint32_t compare = 0;
  if (!g_pwm_started ||
      !edgefocus::fan::logicalDutyToComplementaryCompareTicks(
          duty_ns, kPwmPeriodNs, kNanosecondsPerTick, compare))
  {
    return false;
  }
  reportPwmStartStep("CCR", StageStatus::Begin);
  __HAL_TIM_SET_COMPARE(&g_tim17, TIM_CHANNEL_1,
                        compare);
  __DSB();
  HAL_Delay(1);
  if (__HAL_TIM_GET_COMPARE(&g_tim17, TIM_CHANNEL_1) != compare)
  {
    reportPwmStartStep("CCR", StageStatus::Fail);
    return false;
  }
  reportPwmStartStep("CCR", StageStatus::Ok);
  return true;
}

bool sendRpmsgFrame(void *, const std::uint8_t *data, std::size_t size)
{
  return EdgeFocus_FanBoard_Transmit(data, size);
}

void tachRisingCallback(void)
{
  ++g_tach_pulses;
}

std::uint32_t takeTachPulses()
{
  const std::uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const std::uint32_t pulses = g_tach_pulses;
  g_tach_pulses = 0;
  if (primask == 0)
  {
    __enable_irq();
  }
  return pulses;
}

void releaseResourceOwnership()
{
  if (g_timer_owned)
  {
    ResMgr_Release(RESMGR_RESOURCE_RIFSC, RESMGR_RIFSC_TIM17_ID);
    g_timer_owned = false;
  }
  if (g_tach_pin_owned)
  {
    ResMgr_Release(RESMGR_RESOURCE_RIF_GPIOB, RESMGR_GPIO_PIN(11));
    g_tach_pin_owned = false;
  }
  if (g_pwm_pin_owned)
  {
    ResMgr_Release(RESMGR_RESOURCE_RIF_GPIOB, RESMGR_GPIO_PIN(10));
    g_pwm_pin_owned = false;
  }
  if (g_gpio_clock_owned)
  {
    ResMgr_Release(RESMGR_RESOURCE_RIF_RCC, RESMGR_RCC_RESOURCE(91));
    g_gpio_clock_owned = false;
  }
}

void releaseHardware()
{
  HAL_NVIC_DisableIRQ(EXTI2_11_IRQn);
  if (g_pwm_started)
  {
    HAL_TIMEx_PWMN_Stop(&g_tim17, TIM_CHANNEL_1);
    g_pwm_started = false;
  }
  std::uint32_t gpio_mask = 0;
  if (g_pwm_pin_owned)
  {
    gpio_mask |= GPIO_PIN_10;
  }
  if (g_tach_pin_owned)
  {
    gpio_mask |= GPIO_PIN_11;
  }
  if (gpio_mask != 0)
  {
    HAL_GPIO_DeInit(GPIOB, gpio_mask);
  }
  if (g_timer_handle_ready)
  {
    HAL_TIM_PWM_DeInit(&g_tim17);
    g_timer_handle_ready = false;
  }
  if (g_timer_clock_enabled_by_app)
  {
    __HAL_RCC_TIM17_CLK_DISABLE();
    g_timer_clock_enabled_by_app = false;
  }
  releaseResourceOwnership();
}

bool initializeHardware()
{
  beginStage(1);
  if (__HAL_RCC_GPIOB_IS_CLK_ENABLED())
  {
    /* Linux already enabled the shared bank clock; no RCC write is needed. */
    g_gpio_clock_owned = false;
  }
  else
  {
    g_gpio_clock_owned =
        ResMgr_Request(RESMGR_RESOURCE_RIF_RCC, RESMGR_RCC_RESOURCE(91)) ==
        RESMGR_STATUS_ACCESS_OK;
    if (!g_gpio_clock_owned)
    {
      return failStage(1);
    }
    __HAL_RCC_GPIOB_CLK_ENABLE();
  }

  g_init_error = 2;
  g_pwm_pin_owned =
      ResMgr_Request(RESMGR_RESOURCE_RIF_GPIOB, RESMGR_GPIO_PIN(10)) ==
      RESMGR_STATUS_ACCESS_OK;
  if (!g_pwm_pin_owned)
  {
    (void)failStage(1);
    releaseResourceOwnership();
    return false;
  }

  if (kEnableTach)
  {
    g_init_error = 3;
    g_tach_pin_owned =
        ResMgr_Request(RESMGR_RESOURCE_RIF_GPIOB, RESMGR_GPIO_PIN(11)) ==
        RESMGR_STATUS_ACCESS_OK;
    if (!g_tach_pin_owned)
    {
      (void)failStage(1);
      releaseResourceOwnership();
      return false;
    }
  }

  g_init_error = 4;
  g_timer_owned =
      ResMgr_Request(RESMGR_RESOURCE_RIFSC, RESMGR_RIFSC_TIM17_ID) ==
      RESMGR_STATUS_ACCESS_OK;
  if (!g_timer_owned)
  {
    (void)failStage(1);
    releaseResourceOwnership();
    return false;
  }

  reportStage(1, StageStatus::Ok);

  if (kResourceProbeOnly)
  {
    /* Code 9 means RCC/PB10/TIM17 ownership requests all succeeded. */
    releaseResourceOwnership();
    g_init_error = 9;
    return false;
  }

  beginStage(2);
  const std::uint32_t timer_hz =
      HAL_RCCEx_GetTimerCLKFreq(RCC_PERIPHCLK_TIM17);
  if (timer_hz < kTimerTickHz || (timer_hz % kTimerTickHz) != 0)
  {
    (void)failStage(2);
    releaseHardware();
    return false;
  }
  reportStage(2, StageStatus::Ok);

  beginStage(3);
  const bool timer_clock_was_enabled = __HAL_RCC_TIM17_IS_CLK_ENABLED();
  reportStage(3, StageStatus::Ok);

  beginStage(4);
  if (!timer_clock_was_enabled)
  {
    __HAL_RCC_TIM17_CLK_ENABLE();
    g_timer_clock_enabled_by_app = true;
  }
  if (!__HAL_RCC_TIM17_IS_CLK_ENABLED())
  {
    (void)failStage(4);
    releaseHardware();
    return false;
  }
  reportPwmStartStep("RESET", StageStatus::Begin);
  __HAL_RCC_TIM17_FORCE_RESET();
  __HAL_RCC_TIM17_RELEASE_RESET();
  if ((RCC->TIM17CFGR & RCC_TIM17CFGR_TIM17RST) != 0)
  {
    reportPwmStartStep("RESET", StageStatus::Fail);
    (void)failStage(4);
    releaseHardware();
    return false;
  }
  reportPwmStartStep("RESET", StageStatus::Ok);
  reportStage(4, StageStatus::Ok);

  beginStage(5);
  g_tim17.Instance = TIM17;
  g_timer_handle_ready = true;
  g_tim17.Init.Prescaler = timer_hz / kTimerTickHz - 1;
  g_tim17.Init.CounterMode = TIM_COUNTERMODE_UP;
  g_tim17.Init.Period = kPwmPeriodNs / kNanosecondsPerTick - 1;
  g_tim17.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  g_tim17.Init.RepetitionCounter = 0;
  g_tim17.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&g_tim17) != HAL_OK)
  {
    (void)failStage(5);
    releaseHardware();
    return false;
  }
  TIM_BreakDeadTimeConfigTypeDef break_dead_time{};
  break_dead_time.OffStateRunMode = TIM_OSSR_DISABLE;
  break_dead_time.OffStateIDLEMode = TIM_OSSI_DISABLE;
  break_dead_time.LockLevel = TIM_LOCKLEVEL_OFF;
  break_dead_time.DeadTime = 0;
  break_dead_time.BreakState = TIM_BREAK_DISABLE;
  break_dead_time.BreakPolarity = TIM_BREAKPOLARITY_LOW;
  break_dead_time.BreakFilter = 0;
  break_dead_time.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
  break_dead_time.Break2State = TIM_BREAK2_DISABLE;
  break_dead_time.Break2Polarity = TIM_BREAK2POLARITY_LOW;
  break_dead_time.Break2Filter = 0;
  break_dead_time.Break2AFMode = TIM_BREAK2_AFMODE_INPUT;
  break_dead_time.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&g_tim17, &break_dead_time) != HAL_OK)
  {
    (void)failStage(5);
    releaseHardware();
    return false;
  }
  reportStage(5, StageStatus::Ok);

  beginStage(6);
  GPIO_InitTypeDef gpio{};
  gpio.Pin = GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLDOWN;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF7_TIM17;
  HAL_GPIO_Init(GPIOB, &gpio);

  if (kEnableTach)
  {
    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = 0;
    HAL_GPIO_Init(GPIOB, &gpio);

    EXTI_ConfigTypeDef exti{};
    exti.Line = EXTI2_LINE_11;
    exti.Trigger = EXTI_TRIGGER_RISING;
    exti.GPIOSel = EXTI_GPIOB;
    exti.Mode = EXTI_MODE_INTERRUPT;
    if (HAL_EXTI_SetConfigLine(&g_tach_exti, &exti) != HAL_OK ||
        HAL_EXTI_RegisterCallback(&g_tach_exti, HAL_EXTI_RISING_CB_ID,
                                  tachRisingCallback) != HAL_OK)
    {
      (void)failStage(6);
      releaseHardware();
      return false;
    }
    HAL_NVIC_SetPriority(EXTI2_11_IRQn, kIrqPriority, 0);
    HAL_NVIC_EnableIRQ(EXTI2_11_IRQn);
  }
  reportStage(6, StageStatus::Ok);
  if (kStopAfterStage == 6)
  {
    return false;
  }

  beginStage(7);
  TIM_OC_InitTypeDef channel{};
  channel.OCMode = TIM_OCMODE_PWM1;
  std::uint32_t initial_compare = 0;
  if (!edgefocus::fan::logicalDutyToComplementaryCompareTicks(
          kSafeDutyNs, kPwmPeriodNs, kNanosecondsPerTick, initial_compare))
  {
    (void)failStage(7);
    releaseHardware();
    return false;
  }
  channel.Pulse = initial_compare;
  /* Match the Linux prototype: sysfs polarity=normal; the board circuit is inverted. */
  channel.OCPolarity = TIM_OCPOLARITY_HIGH;
  channel.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  channel.OCFastMode = TIM_OCFAST_DISABLE;
  channel.OCIdleState = TIM_OCIDLESTATE_RESET;
  channel.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&g_tim17, &channel, TIM_CHANNEL_1) != HAL_OK)
  {
    (void)failStage(7);
    releaseHardware();
    return false;
  }
  reportStage(7, StageStatus::Ok);
  if (kStopAfterStage == 7)
  {
    return false;
  }

  beginStage(8);
  if (!startComplementaryPwm())
  {
    (void)failStage(8);
    releaseHardware();
    return false;
  }
  g_pwm_started = true;
  reportStage(8, StageStatus::Ok);
  if (kStopAfterStage == 8)
  {
    return false;
  }
  g_init_error = 0;
  return true;
}

}  // namespace

extern "C" bool EdgeFocus_FanBoard_Init(std::uint32_t now_ms)
{
  reportStage(0, StageStatus::Begin);
  reportStage(0, StageStatus::Ok);
  if (!initializeHardware())
  {
    return false;
  }

  beginStage(9);
  reportPwmStartStep("APP_CFG", StageStatus::Begin);
  FanConfig fan_config{};
  fan_config.safe_duty_ns = kSafeDutyNs;
  fan_config.tach_enabled = kEnableTach;
  fan_config.tach_pulses_per_revolution = 2;
  ControllerAppConfig app_config{};
  reportPwmStartStep("APP_CFG", StageStatus::Ok);
  reportPwmStartStep("NONCE", StageStatus::Begin);
  /* UID registers are not readable from this M33 non-secure context. */
  app_config.boot_id =
      (static_cast<std::uint64_t>(now_ms) << 32) |
      (static_cast<std::uint64_t>(SysTick->VAL) << 8) |
      static_cast<std::uint64_t>(g_tim17.Instance->CNT & 0xffU);
  reportPwmStartStep("NONCE", StageStatus::Ok);
  app_config.advertise_tachometer = kEnableTach;
  const PlatformCallbacks callbacks{nullptr, writePwmDuty, sendRpmsgFrame};
  reportPwmStartStep("CTOR", StageStatus::Begin);
  g_app = new (g_app_storage) FanControllerApp(callbacks, app_config, fan_config);
  reportPwmStartStep("CTOR", StageStatus::Ok);
  g_last_control_ms = now_ms;
  g_last_tach_ms = now_ms;
  reportPwmStartStep("INIT", StageStatus::Begin);
  if (!g_app->initialize(now_ms))
  {
    (void)failStage(9);
    return false;
  }
  reportPwmStartStep("INIT", StageStatus::Ok);
  reportStage(9, StageStatus::Ok);
  g_init_error = 0;
  return true;
}

extern "C" std::uint32_t EdgeFocus_FanBoard_InitError(void)
{
  return g_init_error;
}

extern "C" void EdgeFocus_FanBoard_Poll(std::uint32_t now_ms)
{
  if (g_app == nullptr || !kEnableRuntimeControlLoop)
  {
    return;
  }
  if (now_ms - g_last_control_ms >= kControlIntervalMs)
  {
    g_last_control_ms = now_ms;
    g_app->tick(now_ms);
  }
  if (kEnableTach && now_ms - g_last_tach_ms >= kTachWindowMs)
  {
    const std::uint32_t window_ms = now_ms - g_last_tach_ms;
    g_last_tach_ms = now_ms;
    g_app->onTachWindow(takeTachPulses(), window_ms, now_ms);
  }
}

extern "C" void EdgeFocus_FanBoard_OnRx(const std::uint8_t *data,
                                          std::size_t size,
                                          std::uint32_t now_ms)
{
  if (g_app != nullptr)
  {
    g_app->onRpmsgFrame(data, size, now_ms);
  }
}

extern "C" void EdgeFocus_FanBoard_OnTransportReady(std::uint32_t now_ms)
{
  if (g_app != nullptr)
  {
    g_app->onTransportReady(now_ms);
  }
}

extern "C" void EdgeFocus_FanBoard_OnTransportLost(void)
{
  if (g_app != nullptr)
  {
    g_app->onTransportLost();
  }
}

extern "C" void EdgeFocus_FanBoard_ForceSafe(void)
{
  if (g_pwm_started)
  {
    std::uint32_t compare = 0;
    if (!edgefocus::fan::logicalDutyToComplementaryCompareTicks(
            kSafeDutyNs, kPwmPeriodNs, kNanosecondsPerTick, compare))
    {
      return;
    }
    __HAL_TIM_SET_COMPARE(&g_tim17, TIM_CHANNEL_1,
                          compare);
  }
}

extern "C" void EdgeFocus_FanBoard_Shutdown(void)
{
  EdgeFocus_FanBoard_ForceSafe();
  releaseHardware();
}

extern "C" void EXTI2_11_IRQHandler(void)
{
  HAL_EXTI_IRQHandler(&g_tach_exti);
}
