
#include <string.h>
#include "bsl_target.h"
#include "crc32.h"

void
bsl_target_setup(struct bsl_target *t,
		 uint8_t (*send_msg)(struct bsl_protocol *p,
				     uint8_t *data, unsigned int len),
		 void (*rx_msg_ready)(struct bsl_protocol *p),
		 void *cb_data)
{
    memset(t, 0, sizeof(*t));

    t->p.is_host = false;
    t->p.rx_header_id = BSL_CMD_HEADER;
    t->p.tx_header_id = BSL_RSP_HEADER;
    t->p.cb_data = t;
    t->p.send_msg = send_msg;
    t->p.rx_msg_ready = rx_msg_ready;
    t->cb_data = cb_data;
}

static void
bsl_send_ack(struct bsl_protocol *p, uint8_t ack)
{
    p->send_msg(p, &ack, 1);
}

static void
reset_receive(struct bsl_protocol *p)
{
    p->msg_ready = false;
    p->rxlen = 0;
    p->expected_len = 0;
}

static void
reset_receive_ack(struct bsl_protocol *p, uint8_t ack)
{
    reset_receive(p);
    bsl_send_ack(p, ack);
}

static void
reset_receive_msg(struct bsl_target *t, uint8_t rsp)
{
    struct bsl_protocol *p = &t->p;

    reset_receive_ack(p, BSL_ACK);
    if (rsp == BSL_ERR_DETAILED) {
	p->txbuffer[4] = rsp;
	bsl_set_uint16(p->txbuffer + 5, t->flash_err);
	bsl_send_buffer(p, BSL_RSP_DETAILED_ERROR, 3);
    } else {
	p->txbuffer[4] = rsp;
	bsl_send_buffer(p, BSL_RSP_MESSAGE, 1);
    }
}

/*
 * Check to see if anything needs to be done, should be called
 * after bsl_handle_xxx().
 */
void
bsl_target_check(struct bsl_target *t)
{
    struct bsl_protocol *p = &t->p;
    uint32_t msg_crc, calc_crc;
    uint8_t rsp, *data;
    unsigned int len;

    if (!p->msg_ready)
	return;

    if (p->rxlen < p->expected_len) {
	reset_receive_ack(p, BSL_ERR_PACKET_SIZE_TOO_BIG);
	return;
    }
    calc_crc = ~crc32(p->rxbuffer + 3, p->rxlen - 7);
    msg_crc = bsl_get_uint32(p->rxbuffer + p->rxlen - 4);
    if (calc_crc != msg_crc) {
	reset_receive_ack(p, BSL_ERR_CHECKSUM_INCORRECT);
	return;
    }

    if (p->rxlen < 1) {
	reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	return;
    }

    len = p->rxlen - 8;
    data = p->rxbuffer + 4;

    switch(p->rxbuffer[3]) {
    case BSL_CMD_CONNECTION:
	if (len != 0) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	reset_receive_ack(p, BSL_ACK);
	break;

    case BSL_CMD_UNLOCK_BOOTLOADER:
	if (len != 32) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	/* We don't check here. */
	reset_receive_msg(t, BSL_RSP_SUCCESS);
	break;

    case BSL_CMD_FLASH_RANGE_ERASE:
	if (len != 8) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	rsp = t->erase_range(t, bsl_get_uint32(data), bsl_get_uint32(data + 4));
	reset_receive_msg(t, rsp);
	break;

    case BSL_CMD_MASS_ERASE:
	if (len != 0) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	rsp = t->erase_all(t);
	reset_receive_msg(t, rsp);
	break;

    case BSL_CMD_PROGRAM_DATA:
	if (len < 8) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	rsp = t->write_data(t, bsl_get_uint32(data), data + 4, len - 4);
	reset_receive_msg(t, rsp);
	break;

    case BSL_CMD_PROGRAM_DATA_FAST:
	if (len < 8) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	t->write_data(t, bsl_get_uint32(data), data + 4, len - 4);
	reset_receive_ack(p, BSL_ACK);
	break;

    case BSL_CMD_MEMORY_READ_BACK:
	if (len != 8) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	len = bsl_get_uint32(data + 4);
	if (len > BSL_MAX_BUFFER_SIZE) {
	    reset_receive_msg(t, BSL_RSP_ERR_INVALID_LENGTH);
	    break;
	}
	rsp = t->read_data(t, bsl_get_uint32(data), p->txbuffer + 4, len);
	if (rsp) {
	    reset_receive_msg(t, rsp);
	} else {
	    reset_receive_ack(p, BSL_ACK);
	    bsl_send_buffer(p, BSL_RSP_MEMORY_READ_BACK, len);
	}
	break;

    case BSL_CMD_FACTORY_RESET:
	if (len != 16) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	/* Don't care about password. */
	reset_receive_msg(t, BSL_RSP_SUCCESS);
	break;

    case BSL_CMD_GET_DEVICE_INFO:
	if (len != 0) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	memcpy(p->txbuffer + 4, p->device_info, sizeof(p->device_info));
	reset_receive_ack(p, BSL_ACK);
	bsl_send_buffer(p, BSL_RSP_GET_DEVICE_INFO, sizeof(p->device_info));
	break;

    case BSL_CMD_STANDALONE_VERIFICATION:
	if (len != 8) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	rsp = t->validate_data(t, bsl_get_uint32(data),
			       bsl_get_uint32(data + 4),
			       &calc_crc);
	if (rsp) {
	    reset_receive_msg(t, rsp);
	} else {
	    bsl_set_uint32(p->txbuffer + 4, calc_crc);
	    reset_receive_ack(p, BSL_ACK);
	    bsl_send_buffer(p, BSL_RSP_STANDALONE_VERIFICATION, 4);
	}
	break;

    case BSL_CMD_START_APPLICATION:
	if (len != 0) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	reset_receive_ack(p, BSL_ACK);
	t->start_app(t);
	break;

    case BSL_CMD_CHANGE_BAUD_RATE:
	if (len != 1) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	if (t->change_baud(t, *data))
	    reset_receive_ack(p, BSL_ACK);
	else
	    reset_receive_ack(p, BSL_ERR_UNKNOWN_BAUD_RATE);
	break;

    default:
	reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	break;
    }
}
