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
#define EN_LTE_DCIN_Pin GPIO_PIN_13
#define EN_LTE_DCIN_GPIO_Port GPIOC
#define LTE_TXD_Pin GPIO_PIN_0
#define LTE_TXD_GPIO_Port GPIOA
#define LTE_RXD_Pin GPIO_PIN_1
#define LTE_RXD_GPIO_Port GPIOA
#define LTE_PWR_Pin GPIO_PIN_5
#define LTE_PWR_GPIO_Port GPIOA
#define EN_EV_TOYI_Pin GPIO_PIN_6
#define EN_EV_TOYI_GPIO_Port GPIOA
#define CTL_EV_TOYI_Pin GPIO_PIN_7
#define CTL_EV_TOYI_GPIO_Port GPIOA
#define EN_SENS12V_Pin GPIO_PIN_4
#define EN_SENS12V_GPIO_Port GPIOC
#define RS485_RTS_Pin GPIO_PIN_1
#define RS485_RTS_GPIO_Port GPIOB
#define EN_SENS3V3_Pin GPIO_PIN_2
#define EN_SENS3V3_GPIO_Port GPIOB
#define RS485_TX_Pin GPIO_PIN_10
#define RS485_TX_GPIO_Port GPIOB
#define RS485_RX_Pin GPIO_PIN_11
#define RS485_RX_GPIO_Port GPIOB
#define EN_PWR_SENS420_Pin GPIO_PIN_12
#define EN_PWR_SENS420_GPIO_Port GPIOB
#define EN_PWR_CPRES_Pin GPIO_PIN_15
#define EN_PWR_CPRES_GPIO_Port GPIOB
#define EN_PWR_RS485_Pin GPIO_PIN_6
#define EN_PWR_RS485_GPIO_Port GPIOC
#define EN_PWR_QMBUS_Pin GPIO_PIN_7
#define EN_PWR_QMBUS_GPIO_Port GPIOC
#define CNT0_Pin GPIO_PIN_12
#define CNT0_GPIO_Port GPIOA
#define CNT0_EXTI_IRQn EXTI15_10_IRQn
#define SD_SS_Pin GPIO_PIN_15
#define SD_SS_GPIO_Port GPIOA
#define SD_SCK_Pin GPIO_PIN_10
#define SD_SCK_GPIO_Port GPIOC
#define SD_MISO_Pin GPIO_PIN_11
#define SD_MISO_GPIO_Port GPIOC
#define SD_MOSI_Pin GPIO_PIN_12
#define SD_MOSI_GPIO_Port GPIOC
#define SD_DET_Pin GPIO_PIN_2
#define SD_DET_GPIO_Port GPIOD
#define EN_PWR_SD_Pin GPIO_PIN_3
#define EN_PWR_SD_GPIO_Port GPIOB
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

/*
 * ⛔ LED2 (PA2) YA NO EXISTE — sacado el 2026-09-09. No reponerlo.
 *
 * En R001 PA2 estaba declarado como LED2 en el `.ioc`, pero en la placa iba a
 * uno de los pines de SIM del módulo LTE (21/22), que el WH-LTE-7S1-E
 * **externaliza para su propia tarjeta**. Tenerlo manejado desde el micro
 * cargaba ese bus y **el módulo no podía leer su SIM**: sin SIM no había red, y
 * el equipo no transmitía un solo frame. Ver la sección del modem.
 *
 * Pablo lo dejó en *reset state* (sin asignar), que además es el de menor fuga.
 * Los defines se eliminan en vez de dejarlos apuntando a símbolos que CubeMX ya
 * no genera: así, si alguien vuelve a usar LED2, el error dice que no existe en
 * vez de un `LED2_GPIO_Port undeclared` que no explica nada.
 */

/*
 * Identificación del firmware.
 *
 * Vive acá, en un bloque USER CODE de main.h, y no en un header propio de
 * Application/, porque main.h ya lo incluye todo el mundo y agregar un archivo
 * nuevo obliga al baile de Refresh(F5) + regenerar el build en el IDE. El bloque
 * USER CODE sobrevive las regeneraciones de CubeMX.
 *
 * FW_FECHA sale del compilador: dice qué binario está corriendo de verdad, que
 * es la pregunta que más veces hubo que contestar en este bring-up. Un número de
 * versión se olvida de subir; la fecha de compilación no miente nunca.
 */
#define FW_NOMBRE              "FWDLGARM_R1"   /* el del banner, NO el del frame */
#define FW_VERSION             "0.0.73"
#define FW_FECHA               __DATE__ " " __TIME__

/*
 * ⚠ REGLA: la versión SUBE EN CADA ENTREGA A BANCO (Pablo, 2026-09-08).
 *
 * Es lo que permite decir sin ambigüedad qué firmware se probó, y era un
 * problema real: hasta hoy este define decía "0.0.8" mientras los tags de git
 * iban por `v0.0.14`. Se puso al día en 0.0.15 —continuando la serie de los
 * tags, para que no haya DOS numeraciones conviviendo— y de acá en más va de a
 * uno por cada binario que se entrega.
 *
 * Vamos por `0.0.X` durante toda la fase 2; **cuando la aplicación esté
 * terminada, la versión pasa a `1.0.0`**.
 *
 * ---------------------------------------------------------------------------
 * ESTOS TRES CAMPOS VIAJAN EN EL FRAME, y el servidor los usa para identificar
 * al equipo (decisión de Pablo, 2026-09-07):
 *
 *     ID=<imei>&HW=SPQ_ARM_R1&TYPE=FWDLGARM_R1&VER=0.0.15&CLASS=...
 *
 * `FW_HW` es la PLACA, no el micro, y por eso cambia respecto del equipo AVR
 * —que se declara `SPQ_AVRDA_R2`—. Es lo que le permite al servidor distinguir
 * un datalogger nuevo de uno viejo.
 */
#define FW_HW                  "SPQ_ARM_R1"

/*
 * ⚠ `FW_TYPE` NO es `FW_NOMBRE`: va SIN la revisión de placa.
 *
 * Es el **tipo de firmware** —la familia de producto— y sigue el patrón del AVR,
 * donde `TYPE=FWDLGX` no lleva sufijo y la revisión vive en `HW=SPQ_AVRDA_R2`.
 * Acá es igual: `TYPE=FWDLGARM` y `HW=SPQ_ARM_R1`.
 *
 * Corregido por Pablo el 2026-09-11. Los dos campos se ven parecidos y por eso
 * conviene que estén separados a la vista: **`FW_NOMBRE` es lo que dice el banner
 * de la consola, `FW_TYPE` es lo que viaja al servidor.** Unificarlos de nuevo
 * metería la revisión de placa en un campo que no la lleva.
 */
#define FW_TYPE                "FWDLGARM"

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
