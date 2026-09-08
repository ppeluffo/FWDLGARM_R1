/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
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

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

#include "drv_sd.h"


/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN INIT */
    /*
     * ⚠ Acá NO se enciende la tarjeta ni se la arranca: eso lo hace la capa de
     * arriba (`fs_sd`) con drv_sd_power() + drv_sd_arrancar(), porque es ella la
     * que sabe cuándo vale la pena pagar el encendido y cuándo hay que apagar.
     * Este diskio sólo informa si la tarjeta está lista AHORA.
     *
     * Si se encendiera desde acá, FatFs prendería la SD sola en cada f_mount y
     * nadie sabría cuándo apagarla — que es justo el control que el diseño de
     * ventana quiere conservar.
     */
    ( void ) pdrv;

    Stat = drv_sd_power_estado() ? 0 : STA_NOINIT;
    return Stat;
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
  /* USER CODE BEGIN STATUS */
    ( void ) pdrv;

    Stat = drv_sd_power_estado() ? 0 : STA_NOINIT;
    return Stat;
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  /* USER CODE BEGIN READ */
    ( void ) pdrv;

    if( !drv_sd_power_estado() )
    {
        return RES_NOTRDY;
    }

    /* De a un sector: `drv_sd` no expone lectura múltiple (CMD18). Con archivos
       chicos y volcados espaciados no compensa agregarla; el día que un volcado
       sea lento, ése es el lugar donde mirar. */
    for( UINT i = 0U; i < count; i++ )
    {
        if( !drv_sd_leer_sector( sector + i, &buff[ i * _MAX_SS ] ) )
        {
            return RES_ERROR;
        }
    }

    return RES_OK;
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
  /* USER CODE BEGIN WRITE */
    ( void ) pdrv;

    if( !drv_sd_power_estado() )
    {
        return RES_NOTRDY;
    }

    for( UINT i = 0U; i < count; i++ )
    {
        if( !drv_sd_escribir_sector( sector + i, &buff[ i * _MAX_SS ] ) )
        {
            return RES_ERROR;
        }
    }

    return RES_OK;
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  /* USER CODE BEGIN IOCTL */
    ( void ) pdrv;

    if( !drv_sd_power_estado() )
    {
        return RES_NOTRDY;
    }

    switch( cmd )
    {
        case CTRL_SYNC:
            /* `drv_sd_escribir_sector()` no vuelve hasta que la tarjeta terminó
               su ciclo interno, así que no hay nada pendiente que forzar. */
            return RES_OK;

        case GET_SECTOR_COUNT:
            /* Sólo lo usa f_mkfs. Sale de la CSD, que `drv_sd_arrancar()` ya
               leyó al inicializar. */
            *( DWORD * ) buff = ( DWORD ) drv_sd_sectores();
            return RES_OK;

        case GET_SECTOR_SIZE:
            *( WORD * ) buff = _MAX_SS;
            return RES_OK;

        case GET_BLOCK_SIZE:
            /*
             * El tamaño del bloque de borrado, en sectores, y **1 no es la
             * verdad**: una SD borra de a bloques de decenas de KB. Se declara 1
             * porque el valor real sale del campo `ERASE_BLK_LEN` de la CSD, que
             * este driver no expone, y **1 es el valor seguro**: hace que f_mkfs
             * alinee de forma conservadora en vez de asumir una alineación que
             * la tarjeta no tiene.
             *
             * Sólo afecta al formateo, que en este equipo casi no se usa: las
             * tarjetas vienen formateadas de fábrica.
             */
            *( DWORD * ) buff = 1U;
            return RES_OK;

        default:
            return RES_PARERR;
    }
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */

