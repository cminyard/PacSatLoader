/*
 * main.c
 */

#include <string.h>
#include <sci.h>
#include <pinmux.h>
#include <gio.h>
#include <spi.h>
#include <reg_system.h>
#include <sys_core.h>
#include "crc32.h"
#include "bsl_target.h"

#define COM2_BAUD 38400

void _c_int00(void);

struct bsl_target bsl_target;
volatile bool do_start_app;

#define MEM_START 0x08002000
#define MEM_END   0x08020000

static uint8_t
serio_send_msg(struct bsl_protocol *p,
	       uint8_t *data, unsigned int len)
{
    sciSend(sciREG, len, data);
    return BSL_ACK;
}

static uint8_t
erase_all(struct bsl_target *p)
{
    memset((void *) MEM_START, 0, MEM_END - MEM_START);
    return BSL_ACK;
}

static uint8_t
erase_range(struct bsl_target *p,
	    uint32_t start_addr, uint32_t end_addr)
{
    if (end_addr < start_addr ||
		start_addr < MEM_START ||
		end_addr > (MEM_END - 1))
	return BSL_RSP_ERR_INVALID_MEMORY_RANGE;

    memset((void *) start_addr, 0, end_addr - start_addr + 1);
    return BSL_ACK;
}

static uint8_t
write_data(struct bsl_target *p,
	   uint32_t addr, uint8_t *data, uint32_t len)
{
    if (addr < MEM_START || addr + len > MEM_END || addr + len < addr)
	return BSL_RSP_ERR_INVALID_MEMORY_RANGE;

    memcpy((void *) addr, data, len);
    return BSL_ACK;
}

static uint8_t
read_data(struct bsl_target *p,
	  uint32_t addr, uint8_t *data, uint32_t len)
{
    if (addr < MEM_START || addr + len > MEM_END || addr + len < addr)
	return BSL_RSP_ERR_INVALID_MEMORY_RANGE;

    memcpy(data, (void *) addr, len);
    return BSL_ACK;
}

static uint8_t
validate_data(struct bsl_target *p,
	      uint32_t addr, uint32_t len, uint32_t *crc)
{
    if (addr < MEM_START || addr + len > MEM_END || addr + len < addr)
	return BSL_RSP_ERR_INVALID_MEMORY_RANGE;

    *crc = crc32((void *) addr, len);
    return BSL_ACK;
}

static void
start_app(struct bsl_target *p)
{
    /* delay this so the ack goes back. */
    do_start_app = true;
}

static bool
change_baud(struct bsl_target *p, uint8_t baud)
{
    return BSL_ERR_UNKNOWN_BAUD_RATE;
}

static void
pet_watchdog(void)
{
    gioToggleBit(spiPORT1, SPI_PIN_CS1);
}

extern uint32_t resetEntry;
uint32_t *int_vec_ptr __attribute__((section(".vecptr")));

void startup(void)
{
    int_vec_ptr = &resetEntry;

    gioInit();
    muxInit();
    sciInit();
    spiInit();
    sciDisableNotification(sciREG, SCI_TX_INT | SCI_RX_INT);
    sciSetBaudrate(sciREG, COM2_BAUD);

    sciSend(sciREG, 20, "RAM Bootloader Startup\r\n");

    bsl_target_setup(&bsl_target, serio_send_msg, NULL, NULL);
    bsl_target.erase_all = erase_all;
    bsl_target.erase_range = erase_range;
    bsl_target.write_data = write_data;
    bsl_target.read_data = read_data;
    bsl_target.validate_data = validate_data;
    bsl_target.start_app = start_app;
    bsl_target.change_baud = change_baud;

    for (;;) {
	pet_watchdog();

	if (do_start_app) {
	    void (*app_start)(void) = (void (*)(void)) MEM_START;
	    app_start();
	}

	if (sciIsRxReady(sciREG)) {
	    bsl_handle_byte(&bsl_target.p, sciReceiveByte(sciREG));
	    bsl_target_check(&bsl_target);
	}
    }
}

#pragma CODE_STATE(_c_entry, 32)
#pragma INTERRUPT(_c_entry, RESET)

void _c_entry(void)
{
    volatile unsigned int time;

    /* Do basic config so we can use the stack and get to the main loader. */
    _coreInitRegisters_();
    _coreInitStackPointer_();
    _coreEnableEventBusExport_();

    /* The ECLK pin controls the bootstrap loader function. */
    systemREG1->SYSPC2 = 0; /* ECLK is a GIO input. */
    systemREG1->SYSPC1 = 0; /* ECLK in GIO mode. */
    systemREG1->SYSPC9 = 1; /* Pull up ECLK. */
    systemREG1->SYSPC8 = 0; /* Enable ECLK pull. */

    /* If ECLK is high, just go into normal startup. */
    if (systemREG1->SYSPC3 & 1 == 0) {
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
