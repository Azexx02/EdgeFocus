#ifndef GIMBAL_CONFIG_H
#define GIMBAL_CONFIG_H

/*
 * Bench bring-up configuration.
 *
 * GIMBAL_CONTROL_ENABLE opens the UART commands that can turn on Boost,
 * enable the STSPIN830 drivers, and step the motors. Keep the bench supply
 * current-limited during the first powered tests.
 */
#define GIMBAL_CONTROL_ENABLE 1
#define GIMBAL_FOC_HOLD_ENABLE 1

/* Mechanical travel, in degrees from the learned HOME position. */
#define GIMBAL_X_LIMIT_DEG 180U
#define GIMBAL_Y_LIMIT_DEG 359U
#define GIMBAL_Y_CONTINUOUS 1

/* Set to -1 if the encoder raw count decreases when the axis moves positive. */
#define GIMBAL_X_POSITIVE_RAW_SIGN -1
#define GIMBAL_Y_POSITIVE_RAW_SIGN 1
#define GIMBAL_X_ELECTRICAL_RAW_SIGN -1
#define GIMBAL_Y_ELECTRICAL_RAW_SIGN -1
#define GIMBAL_X_FOC_TORQUE_POLARITY 1
#define GIMBAL_Y_FOC_TORQUE_POLARITY 1

/*
 * The board has an external enable input. Set this to 1 when the MP557 side
 * drives that line as an interlock. For direct motor bench tests, leaving it
 * at 0 avoids disabling commands when the enable pin is not wired yet.
 */
#define GIMBAL_REQUIRE_ENABLE_INPUT 0

/* Raw ADC trip point. 4095 keeps the software current trip disabled. */
#define GIMBAL_CURRENT_LIMIT_RAW 4095U

/*
 * Power-on homing:
 * 1) slowly scan to the negative mechanical stop,
 * 2) slowly scan to the positive stop,
 * 3) set the negative stop as 0 deg, move to travel/2, and hold it.
 */
#define GIMBAL_AUTO_HOME_ON_BOOT 1
#define GIMBAL_IDLE_DAC_MV 300U
#define GIMBAL_X_HOMING_DAC_MV 2200U
#define GIMBAL_X_HOMING_DAC_MAX_MV 3300U
#define GIMBAL_X_HOMING_DAC_STEP_MV 250U
#define GIMBAL_Y_HOMING_DAC_MV 500U

#define GIMBAL_X_HOMING_DUTY_TICKS 3600U
#define GIMBAL_X_HOMING_DUTY_MAX_TICKS 4200U
#define GIMBAL_X_HOMING_DUTY_STEP_TICKS 300U
#define GIMBAL_Y_HOMING_DUTY_TICKS 260U
#define GIMBAL_X_HOLD_DUTY_TICKS 1200U
#define GIMBAL_Y_HOLD_DUTY_TICKS 900U
#define GIMBAL_X_HOLD_MIN_DUTY_TICKS 260U
#define GIMBAL_Y_HOLD_MIN_DUTY_TICKS 220U
#define GIMBAL_X_HOLD_DAC_MV 1500U
#define GIMBAL_Y_HOLD_DAC_MV 1600U
#define GIMBAL_FOC_PERIOD_MS 10U
#define GIMBAL_FOC_WATCHDOG_MS 100U
#define GIMBAL_FOC_DEADBAND_RAW 6U
#define GIMBAL_FOC_TORQUE_ENTER_RAW 10U
#define GIMBAL_FOC_TORQUE_REVERSE_RAW 14U
#define GIMBAL_FOC_FULL_TORQUE_ERROR_RAW 48U
#define GIMBAL_X_FOC_D_GAIN 2
#define GIMBAL_Y_FOC_D_GAIN 4
#define GIMBAL_FOC_D_TERM_LIMIT_RAW 24
#define GIMBAL_FOC_DUTY_SLEW_TICKS 35U
#define GIMBAL_FOC_STARTUP_CYCLES 30U
#define GIMBAL_FOC_ALIGN_TIME_MS 250U
#define GIMBAL_FOC_LOST_ERROR_RAW 512U
#define GIMBAL_FOC_LOST_COUNT 30U
#define GIMBAL_X_CENTER_DAC_MV 2400U
#define GIMBAL_Y_CENTER_DAC_MV 320U
#define GIMBAL_X_CENTER_DUTY_TICKS 4000U
#define GIMBAL_Y_CENTER_DUTY_TICKS 110U
#define GIMBAL_CENTER_MAX_STEPS 650U

/* X-axis collision detection while closed-loop positioning. */
#define GIMBAL_X_STALL_RAW 4U
#define GIMBAL_X_STALL_COUNT 10U
#define GIMBAL_X_LIMIT_ZONE_DEG 3U

/* X axis wiring bundle is stiffer in the clockwise direction. */
#define GIMBAL_X_CLOCKWISE_MECH_DIR -1
#define GIMBAL_X_CLOCKWISE_HOMING_DAC_MV 3300U
#define GIMBAL_X_CLOCKWISE_HOMING_DUTY_TICKS 4200U
#define GIMBAL_X_CLOCKWISE_CENTER_DAC_MV 3000U
#define GIMBAL_X_CLOCKWISE_CENTER_DUTY_TICKS 4200U
#define GIMBAL_HOMING_STEP_MS 30U
#define GIMBAL_HOMING_FOC_STEP_RAW 1U
#define GIMBAL_HOMING_FOC_MAX_LEAD_RAW 64U
#define GIMBAL_HOMING_STALL_RAW 8U
#define GIMBAL_HOMING_STALL_COUNT 8U
#define GIMBAL_HOMING_WINDOW_STEPS 12U
#define GIMBAL_HOMING_WINDOW_RAW 32U
#define GIMBAL_HOMING_SCAN_TIMEOUT_MS 45000U
#define GIMBAL_HOMING_FIRST_STOP_MIN_DEG 15U
#define GIMBAL_X_HOMING_MAX_STEPS 2400U
#define GIMBAL_Y_HOMING_MAX_STEPS 760U

#endif
