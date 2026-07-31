#ifndef BSL_PROTOCOL_H
#define BSL_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#define BSL_MAX_BUFFER_SIZE 1024
#define BSL_MAX_MSG_SIZE (BSL_MAX_BUFFER_SIZE + 8)

/*
 * Commands from the host to the target.
 */
#define BSL_CMD_CONNECTION 0X12
#define BSL_CMD_UNLOCK_BOOTLOADER 0X21
#define BSL_CMD_FLASH_RANGE_ERASE 0X23
#define BSL_CMD_MASS_ERASE 0X15
#define BSL_CMD_PROGRAM_DATA 0X20
#define BSL_CMD_PROGRAM_DATA_FAST 0X24
#define BSL_CMD_MEMORY_READ_BACK 0X29
#define BSL_CMD_FACTORY_RESET 0X30
#define BSL_CMD_GET_DEVICE_INFO 0X19
#define BSL_CMD_STANDALONE_VERIFICATION 0X26
#define BSL_CMD_START_APPLICATION 0X40
#define BSL_CMD_CHANGE_BAUD_RATE 0X52

#define BSL_CMD_HEADER 0x80
#define BSL_RSP_HEADER 0x08

/*
 * Single byte responses to all packets.  Use for protocol errors, not
 * for operational responses.
 */
#define BSL_ACK 0x00
#define BSL_ERR_HEADER_INCORRECT 0x51
#define BSL_ERR_CHECKSUM_INCORRECT 0x52
#define BSL_ERR_PACKET_SIZE_ZERO 0x53
#define BSL_ERR_PACKET_SIZE_TOO_BIG 0x54
#define BSL_ERR_UNKNOWN_ERROR 0x55
#define BSL_ERR_UNKNOWN_BAUD_RATE 0x56

/*
 * Responses from the target to the host.
 */
#define BSL_RSP_MEMORY_READ_BACK 0X30
#define BSL_RSP_GET_DEVICE_INFO 0X31
#define BSL_RSP_STANDALONE_VERIFICATION 0X32
#define BSL_RSP_MESSAGE 0X3B
#define BSL_RSP_DETAILED_ERROR 0X3A

/*
 * Errors in responses.
 */
#define BSL_RSP_SUCCESS 0x00
#define BSL_RSP_ERR_BSL_LOCKED 0X01
#define BSL_RSP_ERR_BSL_PASSWORD 0X02
#define BSL_RSP_ERR_MULTIPLE_BSL_PASSWORD 0X03
#define BSL_RSP_ERR_UNKNOWN_COMMAND 0X04
#define BSL_RSP_ERR_INVALID_MEMORY_RANGE 0X05
#define BSL_RSP_ERR_INVALID_COMMAND 0X06
#define BSL_RSP_ERR_FACTORY_RESET_DISABLED 0X07
#define BSL_RSP_ERR_FACTORY_RESET_PASSWORD 0X08
#define BSL_RSP_ERR_READ_OUT 0X09
#define BSL_RSP_ERR_INVALID_ALIGNMENT 0X0A
#define BSL_RSP_ERR_INVALID_LENGTH 0X0B

#define BSL_BAUD_4800 1
#define BSL_BAUD_9600 2
#define BSL_BAUD_19200 3
#define BSL_BAUD_38400 4
#define BSL_BAUD_57600 5
#define BSL_BAUD_115200 6
#define BSL_BAUD_1000000 7
#define BSL_BAUD_2000000 8
#define BSL_BAUD_3000000 9

/* Offsets into device_info */
#define BSL_DEVINFO_COMMAND_INTERPRETER_VERSION 0
#define BSL_DEVINFO_BUILD_ID 2
#define BSL_DEVINFO_APPLICATION_VERSION 4
#define BSL_DEVINFO_ACTIVE_PLUG_IN_INTERFACE_VERSION 8
#define BSL_DEVINFO_MAX_BUFFER_SIZE 10
#define BSL_DEVINFO_BUFFER_START_ADDRESS 12
#define BSL_DEVINFO_BCR_CONFIGURATION_ID 16
#define BSL_DEVINFO_BSL_CONFIGURATION_ID 20

struct bsl_protocol {
    uint8_t rxbuffer[BSL_MAX_MSG_SIZE];
    unsigned int rxlen;
    unsigned int expected_len;
    volatile bool msg_ready;

    uint8_t txbuffer[BSL_MAX_MSG_SIZE];

    uint8_t device_info[24];

    void *cb_data;

    /* Send a message to the host. */
    void (*send_msg)(struct bsl_protocol *p,
		     uint8_t *data, unsigned int len);

    /* Erase all memory.  Returns a BCL_RSP_ERR_xxx */
    uint8_t (*erase_all)(struct bsl_protocol *p);

    /* Erase a range memory.  Returns a BCL_RSP_ERR_xxx */
    uint8_t (*erase_range)(struct bsl_protocol *p,
			   uint32_t start_addr, uint32_t end_addr);
    /* Write the given data.  Returns a BCL_RSP_ERR_xxx */
    uint8_t (*write_data)(struct bsl_protocol *p,
			  uint32_t addr, uint8_t *data, uint32_t len);
    /* Write the given data.  Returns a BCL_RSP_ERR_xxx */
    uint8_t (*read_data)(struct bsl_protocol *p,
			 uint32_t addr, uint8_t *data, uint32_t len);
    /* Return the CRC32 for the given data range.  Returns a BCL_RSP_ERR_xxx */
    uint8_t (*validate_data)(struct bsl_protocol *p,
			     uint32_t addr, uint32_t len, uint32_t *crc);
    /* Start the application. */
    void (*start_app)(struct bsl_protocol *p);
    /* Change the baud rate. */
    bool (*change_baud)(struct bsl_protocol *p, uint8_t baud);
};

void bsl_handle_byte(struct bsl_protocol *p, uint8_t byte);

/*
 * Handle an array of bytes from the serial port, just calls
 * bsl_handle_byte() on each byte.
 */
void bsl_handle_buffer(struct bsl_protocol *p, uint8_t *data, unsigned int len);

/*
 * Check to see if anything needs to be done, should be called
 * after bsl_handle_xxx().
 */
void bsl_check(struct bsl_protocol *p);

/* Set various values in the device_info array. */
void bsl_devinfo_set_command_interpreter_version(struct bsl_protocol *p,
						 unsigned int value);
void bsl_devinfo_set_build_id(struct bsl_protocol *p,
			      unsigned int value);
void bsl_devinfo_set_application_version(struct bsl_protocol *p,
					 unsigned int value);
void bsl_devinfo_set_active_plug_in_interface_version(struct bsl_protocol *p,
						      unsigned int value);
void bsl_devinfo_set_bsl_max_buffer_size(struct bsl_protocol *p,
					 unsigned int value);
void bsl_devinfo_set_bsl_buffer_start_address(struct bsl_protocol *p,
					      unsigned int value);
void bsl_devinfo_set_bcr_configuration_id(struct bsl_protocol *p,
					  unsigned int value);
void bsl_devinfo_set_bsl_configuration_id(struct bsl_protocol *p,
					  unsigned int value);

#endif /* BSL_PROTOCOL_H */
