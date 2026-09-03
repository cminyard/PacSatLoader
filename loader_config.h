/*
 * loader_config.h
 *
 * Config items used by the loader (PacSatSw.cmd) are here.
 */

#ifndef LOADER_CONFIG_H
#define LOADER_CONFIG_H

#define USE_BOOTLOADER /* Define this if the bootloader is being used. */

/* Define one of these to tell which to use for bootloader detection. */
#define BOOTLOADER_USE_RX_LINE
#undef BOOTLOADER_USE_ECLK_LINE

#endif /* LOADER_CONFIG_H */
