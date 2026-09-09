/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
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
#include "fatfs.h"

uint8_t retUSER;    /* Return value for USER */
char USERPath[4];   /* USER logical drive path */
FATFS USERFatFS;    /* File system object for USER logical drive */
FIL USERFile;       /* File object for USER */

/* USER CODE BEGIN Variables */
#include "drv_rtc79410.h"


/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the USER driver ###########################*/
  retUSER = FATFS_LinkDriver(&USER_Driver, USERPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  /*
   * La fecha y hora para el directorio de la tarjeta. La llama FatFs sola al
   * crear o cerrar un archivo, porque `_FS_NORTC` está en 0.
   *
   * ⚠ Si la hora NO es confiable devuelve 0, que FatFs interpreta como "sin
   * fecha", en vez de estampar 2001-01-01. Mismo criterio que el centinela
   * -9999 del frame y el `SIN_DATO` de la consola: **no inventar un dato que no
   * se tiene**. Un archivo sin fecha se nota; uno fechado en 2001 se copia a un
   * informe sin que nadie lo mire dos veces.
   *
   * `drv_rtc_validez()` mira la firma en la SRAM del propio chip, así que
   * detecta el arranque frío; el chequeo del año es el refuerzo contra una hora
   * mal fijada. Ver tkSys.c.
   */
  RtcTimeType_t xHora;

  if ( ( drv_rtc_validez() != rtcHORA_VALIDA ) || ( !drv_rtc_leer( &xHora ) ) )
  {
    return 0UL;
  }

  /* El año de FatFs cuenta desde 1980; el del RTC son 2 dígitos desde 2000. */
  DWORD ulAnio = ( DWORD ) ( 2000U + xHora.year ) - 1980UL;

  return ( ulAnio                  << 25 ) |
         ( ( DWORD ) xHora.month   << 21 ) |
         ( ( DWORD ) xHora.day     << 16 ) |
         ( ( DWORD ) xHora.hour    << 11 ) |
         ( ( DWORD ) xHora.min     <<  5 ) |
         ( ( DWORD ) xHora.sec     >>  1 );   /* en unidades de 2 segundos */
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
