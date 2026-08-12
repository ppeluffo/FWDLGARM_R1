/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* API nativa de FreeRTOS. Tienen que ir ANTES de cualquier uso de TickType_t,
   StaticTask_t, TaskHandle_t o tskIDLE_PRIORITY. */
#include "FreeRTOS.h"
#include "task.h"

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */
/* Cada tarea declara su prioridad, su stack y su memoria estática en su propio
   header, bajo Application/tasks/. Acá no va nada de eso. */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/*
 * portTICK_PERIOD_MS ESTÁ ENVENENADO A PROPÓSITO. No es un error: no se puede usar.
 *
 * El tick de este proyecto es de 512 Hz (1,95 ms), pero el port lo calcula como
 * 1000 / configTICK_RATE_HZ en aritmética entera, o sea 1000/512 = 1. El patrón clásico
 * del código heredado de FWDLGX/AVR
 *
 *     vTaskDelay( 500 / portTICK_PERIOD_MS );     // <- espera 976 ms, no 500
 *
 * compilaría perfecto y esperaría el DOBLE, sin avisar. Redefiniéndolo a un identificador
 * inexistente, cualquier uso falla en compilación diciendo qué hay que hacer en su lugar.
 *
 * Siempre pdMS_TO_TICKS(), que es exacto para múltiplos de 125 ms (512/1000 = 64/125) y
 * en el resto trunca hacia abajo menos de un tick, sin acumular. Ver CLAUDE.md.
 */
#undef  portTICK_PERIOD_MS
#define portTICK_PERIOD_MS  USAR_pdMS_TO_TICKS_NO_portTICK_PERIOD_MS

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
/* Los prototipos de las tareas viven en su header, bajo Application/tasks/. */
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED2_Pin GPIO_PIN_2
#define LED2_GPIO_Port GPIOA
#define RS485_RTS_Pin GPIO_PIN_1
#define RS485_RTS_GPIO_Port GPIOB
#define RS485_TX_Pin GPIO_PIN_10
#define RS485_TX_GPIO_Port GPIOB
#define RS485_RX_Pin GPIO_PIN_11
#define RS485_RX_GPIO_Port GPIOB
#define EN_PWR_CPRES_Pin GPIO_PIN_15
#define EN_PWR_CPRES_GPIO_Port GPIOB
#define EN_PWR_RS485_Pin GPIO_PIN_6
#define EN_PWR_RS485_GPIO_Port GPIOC
#define EN_PWR_QMBUS_Pin GPIO_PIN_7
#define EN_PWR_QMBUS_GPIO_Port GPIOC
#define TERM_SENSE_Pin GPIO_PIN_5
#define TERM_SENSE_GPIO_Port GPIOB
#define TERM_TX_Pin GPIO_PIN_6
#define TERM_TX_GPIO_Port GPIOB
#define TERM_RX_Pin GPIO_PIN_7
#define TERM_RX_GPIO_Port GPIOB
#define LED_Pin GPIO_PIN_9
#define LED_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
/* Alias propios del LED. Viven acá y no en main.c para que los vean también los
   otros .c de la aplicación (p. ej. Application/tasks/tkCtl.c). */
#define LED_PORT               LED_GPIO_Port
#define LED_PIN                LED_Pin

#define LED2_PORT              LED2_GPIO_Port
#define LED2_PIN               LED2_Pin

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
