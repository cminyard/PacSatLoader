#ifndef BSL_TARGET_H
#define BSL_TARGET_H

#include "bsl_protocol.h"

struct bsl_target {
    struct bsl_protocol p;

    /* For use by the user. */
    void *cb_data;

    /* Used for BSL_ERR_DETAILED. */
    uint16_t flash_err;

    /* Erase all memory.  Returns a BCL_RSP_ERR_xxx */
    uint8_t (*erase_all)(struct bsl_target *p);

    /* Erase a memory range.  Returns a BCL_RSP_ERR_xxx */
    uint8_t (*erase_range)(struct bsl_target *p,
			   uint32_t start_addr, uint32_t end_addr);
    /* Write the given data.  Returns a BCL_RSP_ERR_xxx */
    uint8_t (*write_data)(struct bsl_target *p,
			  uint32_t addr, uint8_t *data, uint32_t len);
    /* Write the given data.  Returns a BCL_RSP_ERR_xxx */
    uint8_t (*read_data)(struct bsl_target *p,
			 uint32_t addr, uint8_t *data, uint32_t len);
    /* Return the CRC32 for the given data range.  Returns a BCL_RSP_ERR_xxx */
    uint8_t (*validate_data)(struct bsl_target *p,
			     uint32_t addr, uint32_t len, uint32_t *crc);
    /* Start the application. */
    void (*start_app)(struct bsl_target *p);
    /* Change the baud rate. */
    bool (*change_baud)(struct bsl_target *p, uint8_t baud);
};

void bsl_target_setup(struct bsl_target *t,
		      uint8_t (*send_msg)(struct bsl_protocol *p,
					  uint8_t *data, unsigned int len),
		      void (*rx_msg_ready)(struct bsl_protocol *p),
		      void *cb_data);

/*
 * Check to see if anything needs to be done, should be called after
 * the rx_msg_ready() callback is called, or if you don't do that,
 * after every bsl_handle_xxx().
 */
void bsl_target_check(struct bsl_target *t);

#endif /* BSL_TARGET_H */
