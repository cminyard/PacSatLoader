/*
 * main.c
 */

#include <sci.h>
#include <pinmux.h>
#include <gio.h>
#include <reg_system.h>

#define COM2_BAUD 38400

void _c_int00(void);

extern uint32_t resetEntry;
uint32_t *int_vec_ptr __attribute__((section(".vecptr")));

void startup(void)
{
    gioInit();
    muxInit();
    sciInit();
    sciDisableNotification(sciREG, SCI_TX_INT | SCI_RX_INT);
    sciSetBaudrate(sciREG, COM2_BAUD);
}

void _c_entry(void)
{
    int_vec_ptr = &resetEntry;

    /* The ECLK pin controls the bootstrap loader function. */
    systemREG1->SYSPC2 = 0; /* ECLK is a GIO input. */
    systemREG1->SYSPC1 = 0; /* ECLK in GIO mode. */
    systemREG1->SYSPC9 = 1; /* Pull up ECLK. */
    systemREG1->SYSPC8 = 0; /* Enable ECLK pull. */

    /* If ECLK is high, just go into normal startup. */
    if (systemREG1->SYSPC3 & 1) {
        /*
         * The payload vectors will be at 0x10000, the first will be it's
         * start location.
         */
        void (*real_start)(void) = (void (*)(void)) 0x10000;

        real_start(); /* Will never return. */
    }

    /* ECLK is pulled low, start the bootstrap load processing. */
    _c_int00();
}

void SerialRxCharacterInterrupt(sciBASE_t *sci,uint8_t byte)
{
    /* Should not happen. */
}
