/*
 * main.c
 *
 * This is the bootstrap loader code, both for the RAM loader (the one
 * that loads an image into RAM) and the FLASH loader (the one that
 * loads an image into FLASH).
 */

#include <string.h>
#include <sci.h>
#include <pinmux.h>
#include <gio.h>
#include <spi.h>
#include <system.h>
#include <reg_system.h>
#include <sys_core.h>
#include "crc32.h"
#include "bsl_target.h"
#include "loader_config.h"

#ifndef RAM_BOOTLOADER
#include "F021.h"
#endif

#define COM2_BAUD 38400

void _c_int00(void);

struct bsl_target bsl_target;
volatile bool do_start_app;

static uint8_t
serio_send_msg(struct bsl_protocol *p,
	       uint8_t *data, unsigned int len)
{
    sciSend(sciREG, len, data);
    return BSL_ACK;
}

static void
pet_watchdog(void)
{
    gioToggleBit(spiPORT1, SPI_PIN_CS1);
}

#ifdef RAM_BOOTLOADER
#define MEM_START 0x08002000
#define MEM_END   0x0801ffff

static void
program_init(void)
{
    /* Nothing to do. */
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
		end_addr > MEM_END)
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

static void
real_start_app(struct bsl_target *p)
{
    void (*app_start)(void) = (void (*)(void)) MEM_START;

    app_start();
}

#else

#define MEM_START 0x00010000
#define MEM_END   0x000fffff

/*
 * Technically this could be 16 or 32, but there's no way I could find
 * to get this from the data, so just assume the minimum.
 */
#define BANK_WIDTH 8

Fapi_DeviceInfoType devinfo;
#define MAX_BANKS 8
Fapi_FlashBankSectorsType bankinfo[MAX_BANKS];

static void
program_init(void)
{
    Fapi_StatusType status;
    unsigned int i;

    devinfo = Fapi_getDeviceInfo();
    if (devinfo.u16NumberOfBanks > MAX_BANKS) {
	sciSend(sciREG, 26, "init err: Too many banks\r\n");
	for (;;) ;
    }

    for (i = 0; i < devinfo.u16NumberOfBanks; i++) {
	status = Fapi_getBankSectors((Fapi_FlashBankType) i, &bankinfo[i]);
	if (status != Fapi_Status_Success) {
	    /*
	     * The TMS570ls0914 advertises 2 banks, but the second is
	     * the eeprom bank and doesn't work with this API.  Just
	     * compensate.
	     */
	    devinfo.u16NumberOfBanks = i;
	    break;
	}
    }
}

static bool
addr_to_sector(uint32_t addr, unsigned int *bank, unsigned int *sector,
	       uint32_t *sector_start, uint32_t *sector_size)
{
    unsigned int i, j;

    for (i = 0; i < devinfo.u16NumberOfBanks; i++) {
	uint32_t caddr = bankinfo[i].u32BankStartAddress;

	for (j = 0; j < bankinfo[i].u32NumberOfSectors; j++) {
	    if (addr >= caddr
		    && addr < caddr + (bankinfo[i].au16SectorSizes[j] * 1024)) {
		*bank = i;
		*sector = j;
		*sector_start = caddr;
		*sector_size = bankinfo[i].au16SectorSizes[j] * 1024;
		return true;
	    }
	    caddr += bankinfo[i].au16SectorSizes[j] * 1024;
	}
    }

    return false;
}

static uint8_t
erase_all(struct bsl_target *p)
{
    Fapi_StatusType status;
    unsigned int i;

    status = Fapi_initializeFlashBanks((uint32_t) (PLL1_FREQ + .1));
    if (status != Fapi_Status_Success)
	return BSL_ERR_UNKNOWN_ERROR;

    for (i = 0; i < devinfo.u16NumberOfBanks; i++) {
	status = Fapi_setActiveFlashBank((Fapi_FlashBankType) i);
	if (status != Fapi_Status_Success)
	    return BSL_ERR_UNKNOWN_ERROR;
	status = Fapi_enableMainBankSectors(0xffff);
	if (status != Fapi_Status_Success)
	    return BSL_ERR_UNKNOWN_ERROR;
	while (FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
	    ;
	status = Fapi_issueAsyncCommandWithAddress(Fapi_EraseBank,
				    (void *) bankinfo[i].u32BankStartAddress);
	if (status != Fapi_Status_Success)
	    return BSL_ERR_UNKNOWN_ERROR;
	while (FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
	    ;
	if (FAPI_GET_FSM_STATUS != Fapi_Status_Success) {
	    p->flash_err = FAPI_GET_FSM_STATUS;
	    return BSL_ERR_DETAILED;
	}
	pet_watchdog();
    }

    return BSL_ACK;
}

static uint8_t
erase_range(struct bsl_target *p,
	    uint32_t start_addr, uint32_t end_addr)
{
    uint32_t addr = start_addr, sect_start, sect_size;
    unsigned int bank = ~0, sector, new_bank;
    Fapi_StatusType status;

    if (end_addr < start_addr ||
		start_addr < MEM_START ||
		end_addr > MEM_END)
	return BSL_RSP_ERR_INVALID_MEMORY_RANGE;

    status = Fapi_initializeFlashBanks((uint32_t) (PLL1_FREQ + .1));
    if (status != Fapi_Status_Success)
	return BSL_ERR_UNKNOWN_ERROR;

    while (addr <= end_addr) {
	if (!addr_to_sector(addr, &new_bank, &sector, &sect_start, &sect_size))
	    return BSL_RSP_ERR_INVALID_MEMORY_RANGE;

	/*
	 * First address may not be on a sector boundary, but switch
	 * to a sector boundary so that adding sect_size will always
	 * go to the next sector.
	 */
	addr = sect_start;

	if (new_bank != bank) {
	    bank = new_bank;
	    status = Fapi_setActiveFlashBank((Fapi_FlashBankType) bank);
	    if (status != Fapi_Status_Success)
		return BSL_ERR_UNKNOWN_ERROR;
	    status = Fapi_enableMainBankSectors(0xffff);
	    if (status != Fapi_Status_Success)
		return BSL_ERR_UNKNOWN_ERROR;
	    while (FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
		;
	}
	status = Fapi_issueAsyncCommandWithAddress(Fapi_EraseSector,
						   (void *) sect_start);
	if (status != Fapi_Status_Success)
	    return BSL_ERR_UNKNOWN_ERROR;
	while (FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
	    ;
	if (FAPI_GET_FSM_STATUS != Fapi_Status_Success) {
	    p->flash_err = FAPI_GET_FSM_STATUS;
	    return BSL_ERR_DETAILED;
	}
	addr += sect_size;
	pet_watchdog();
    }

    return BSL_ACK;
}

static uint8_t
write_data(struct bsl_target *p,
	   uint32_t addr, uint8_t *data, uint32_t len)
{
    uint32_t end_addr = addr + len - 1, sect_start, sect_size;
    unsigned int bank = ~0, sector, new_bank;
    Fapi_StatusType status;

    if (addr < MEM_START || addr + len > MEM_END || addr + len < addr)
	return BSL_RSP_ERR_INVALID_MEMORY_RANGE;

    status = Fapi_initializeFlashBanks((uint32_t) (PLL1_FREQ + .1));
    if (status != Fapi_Status_Success)
	return BSL_ERR_UNKNOWN_ERROR;

    while (addr <= end_addr) {
	uint32_t sectlen, left;

	if (!addr_to_sector(addr, &new_bank, &sector, &sect_start, &sect_size))
	    return BSL_RSP_ERR_INVALID_MEMORY_RANGE;

	if (new_bank != bank) {
	    bank = new_bank;
	    status = Fapi_setActiveFlashBank((Fapi_FlashBankType) bank);
	    if (status != Fapi_Status_Success)
		return BSL_ERR_UNKNOWN_ERROR;
	    status = Fapi_enableMainBankSectors(0xffff);
	    if (status != Fapi_Status_Success)
		return BSL_ERR_UNKNOWN_ERROR;
	    while (FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
		;
	}

	sectlen = sect_start + sect_size - addr;
	if (sectlen > len)
	    sectlen = len;
	left = sectlen;
	while (left > 0) {
	    status = Fapi_issueProgrammingCommand((void *) addr,
						  data, BANK_WIDTH, 0, 0,
						  Fapi_AutoEccGeneration);
	    if (status != Fapi_Status_Success)
		return BSL_ERR_UNKNOWN_ERROR;
	    while (FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
		;
	    if (FAPI_GET_FSM_STATUS != Fapi_Status_Success) {
		p->flash_err = FAPI_GET_FSM_STATUS;
		return BSL_ERR_DETAILED;
	    }
	    data += BANK_WIDTH;
	    addr += BANK_WIDTH;
	    if (left < BANK_WIDTH)
		left = 0;
	    else
		left -= BANK_WIDTH;
	}
	len -= sectlen;

	pet_watchdog();
    }

    return BSL_ACK;
}

static void
real_start_app(struct bsl_target *p)
{

}
#endif

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

#ifdef RAM_BOOTLOADER
extern uint32_t resetEntry;
#else
extern uint32_t flash_resetEntry;
#endif
uint32_t *int_vec_ptr __attribute__((section(".vecptr")));

void startup(void)
{
#ifdef RAM_BOOTLOADER
    int_vec_ptr = &resetEntry;

    gioInit();
    muxInit();
    sciInit();
    spiInit();
    sciDisableNotification(sciREG, SCI_TX_INT | SCI_RX_INT);
    sciSetBaudrate(sciREG, COM2_BAUD);

    //sciSend(sciREG, 24, "RAM Bootloader Startup\r\n");
#else
    int_vec_ptr = &flash_resetEntry;
    /* The RAM bootloader has already initialized everything for us. */
    //sciSend(sciREG, 26, "FLASH Bootloader Startup\r\n");
#endif

    bsl_target_setup(&bsl_target, serio_send_msg, NULL, NULL);
    bsl_target.erase_all = erase_all;
    bsl_target.erase_range = erase_range;
    bsl_target.write_data = write_data;
    bsl_target.read_data = read_data;
    bsl_target.validate_data = validate_data;
    bsl_target.start_app = start_app;
    bsl_target.change_baud = change_baud;
    bsl_devinfo_set_bsl_max_buffer_size(&bsl_target.p, BSL_MAX_BUFFER_SIZE);

    program_init();

    for (;;) {
	pet_watchdog();

	if (do_start_app) {
	    /* Make sure all the data is sent first. */
#define SCI_TX_EMPTY (1U << 11) /* Why isn't this defined by TI? */
	    while (!(sciREG->FLR & SCI_TX_EMPTY))
		;

	    real_start_app(&bsl_target);
	}

	if (sciIsRxReady(sciREG)) {
	    bsl_handle_byte(&bsl_target.p, sciReceiveByte(sciREG));
	    bsl_target_check(&bsl_target);
	}
    }
}

#ifdef RAM_BOOTLOADER
#pragma CODE_STATE(_c_entry, 32)
#pragma INTERRUPT(_c_entry, RESET)

void _c_entry(void)
{
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
#else
void _c_entry(void)
{
    /* The RAM bootloader has already initialized everything for us. */
    startup();
}
#endif

void SerialRxCharacterInterrupt(sciBASE_t *sci,uint8_t byte)
{
    /* Should not happen. */
}
