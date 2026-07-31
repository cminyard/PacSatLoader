
#include "bsl_protocol.h"
#include "crc32.h"

static uint32_t
get_uint32(uint8_t *b)
{
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

static void
set_uint32(uint8_t *b, uint32_t v)
{
    b[0] = v & 0xff;
    b[1] = (v >> 8) & 0xff;
    b[2] = (v >> 16) & 0xff;
    b[3] = (v >> 24) & 0xff;
}

static uint16_t
get_uint16(uint8_t *b)
{
    return b[0] | (b[1] << 8);
}

static void
set_uint16(uint8_t *b, uint16_t v)
{
    b[0] = v & 0xff;
    b[1] = (v >> 8) & 0xff;
}

/*
 * Handle a byte from the serial port.  This only stores the data, and
 * can be called from an interrupt handler or other constrained context.
 * You must schedule bsl_check() to be called after adding data.
 */
void bsl_handle_byte(struct bsl_protocol *p, uint8_t byte)
{
    if (p->msg_ready)
	/* We can only handle one message at a time. */
	return;
    if (p->rxlen == 0 && byte != BSL_CMD_HEADER)
	return;

    if (p->rxlen < BSL_MAX_MSG_SIZE)
	p->rxbuffer[p->rxlen] = byte;
    p->rxlen++;

    if (p->rxlen > 3 && p->rxlen == p->expected_len)
	p->msg_ready = true;
    if (p->rxlen == 3) {
	p->expected_len = get_uint16(p->rxbuffer + 1);
	p->expected_len += 8;
    }
}

/*
 * Handle an array of bytes from the serial port, just calls
 * bsl_handle_byte() on each byte.
 */
void
bsl_handle_buffer(struct bsl_protocol *p, uint8_t *data, unsigned int len)
{
    unsigned int i;

    for (i = 0; i < len; i++)
	bsl_handle_byte(p, data[i]);
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

/* Put data in p->txbuffer[4..len + 4] and call this to send. */
static void
send_buffer(struct bsl_protocol *p, uint8_t rsp, unsigned int len)
{
    uint32_t crc;

    p->txbuffer[0] = BSL_RSP_HEADER;
    set_uint16(p->txbuffer + 1, len + 1);
    p->txbuffer[3] = rsp;
    crc = crc32(p->txbuffer + 4, len + 1);
    set_uint32(p->txbuffer + len + 4, crc);
    p->send_msg(p, p->txbuffer, len + 8);
}

static void
reset_receive_msg(struct bsl_protocol *p, uint8_t rsp)
{
    reset_receive_ack(p, BSL_ACK);
    p->txbuffer[4] = rsp;
    send_buffer(p, BSL_RSP_MESSAGE, 1);
}

/*
 * Check to see if anything needs to be done, should be called
 * after bsl_handle_xxx().
 */
void
bsl_check(struct bsl_protocol *p)
{
    uint32_t msg_crc, calc_crc;
    uint8_t rsp, *data;
    unsigned int len;

    if (!p->msg_ready)
	return;

    if (p->rxlen < p->expected_len) {
	reset_receive_ack(p, BSL_ERR_PACKET_SIZE_TOO_BIG);
	return;
    }
    calc_crc = crc32(p->rxbuffer + 4, p->rxlen - 8);
    msg_crc = get_uint32(p->rxbuffer + p->rxlen - 4);
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
	reset_receive_msg(p, BSL_RSP_SUCCESS);
	break;

    case BSL_CMD_FLASH_RANGE_ERASE:
	if (len != 8) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	rsp = p->erase_range(p, get_uint32(data), get_uint32(data + 4));
	reset_receive_msg(p, rsp);
	break;

    case BSL_CMD_MASS_ERASE:
	if (len != 0) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	rsp = p->erase_all(p);
	reset_receive_msg(p, rsp);
	break;

    case BSL_CMD_PROGRAM_DATA:
	if (len != 8) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	rsp = p->write_data(p, get_uint32(data), data + 4, len - 4);
	reset_receive_msg(p, rsp);
	break;

    case BSL_CMD_PROGRAM_DATA_FAST:
	if (len != 8) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	p->write_data(p, get_uint32(data), data + 4, len - 4);
	reset_receive_ack(p, BSL_ACK);
	break;

    case BSL_CMD_MEMORY_READ_BACK:
	if (len != 8) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	if (len > BSL_MAX_BUFFER_SIZE) {
	    reset_receive_msg(p, BSL_RSP_ERR_INVALID_LENGTH);
	    break;
	}
	len = get_uint32(data + 4);
	rsp = p->read_data(p, get_uint32(data), p->txbuffer + 4, len);
	if (rsp) {
	    reset_receive_msg(p, rsp);
	} else {
	    reset_receive_ack(p, BSL_ACK);
	    send_buffer(p, BSL_RSP_MEMORY_READ_BACK, len);
	}
	break;

    case BSL_CMD_FACTORY_RESET:
	if (len != 16) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	/* Don't care about password. */
	reset_receive_msg(p, BSL_RSP_SUCCESS);
	break;

    case BSL_CMD_GET_DEVICE_INFO:
	if (len != 0) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	memcpy(p->txbuffer + 4, p->device_info, sizeof(p->device_info));
	reset_receive_ack(p, BSL_ACK);
	send_buffer(p, BSL_RSP_GET_DEVICE_INFO, sizeof(p->device_info));
	break;

    case BSL_CMD_STANDALONE_VERIFICATION:
	if (len != 8) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	rsp = p->validate_data(p, get_uint32(data), get_uint32(data + 4),
			       &calc_crc);
	if (rsp) {
	    reset_receive_msg(p, rsp);
	} else {
	    set_uint32(p->txbuffer + 4, calc_crc);
	    reset_receive_ack(p, BSL_ACK);
	    send_buffer(p, BSL_RSP_STANDALONE_VERIFICATION, 4);
	}
	break;

    case BSL_CMD_START_APPLICATION:
	if (len != 0) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	reset_receive_ack(p, BSL_ACK);
	p->start_app(p);
	break;

    case BSL_CMD_CHANGE_BAUD_RATE:
	if (len != 1) {
	    reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	    break;
	}
	if (p->change_baud(p, *data))
	    reset_receive_ack(p, BSL_ACK);
	else
	    reset_receive_ack(p, BSL_ERR_UNKNOWN_BAUD_RATE);
	break;

    default:
	reset_receive_ack(p, BSL_ERR_HEADER_INCORRECT);
	break;
    }
}

/* Set various values in the device_info array. */
void
bsl_devinfo_set_command_interpreter_version(struct bsl_protocol *p,
					    unsigned int value)
{
    set_uint16(p->device_info + BSL_DEVINFO_COMMAND_INTERPRETER_VERSION,
	       value);
}

void
bsl_devinfo_set_build_id(struct bsl_protocol *p,
			 unsigned int value)
{
    set_uint16(p->device_info + BSL_DEVINFO_BUILD_ID,
	       value);
}

void
bsl_devinfo_set_application_version(struct bsl_protocol *p,
				    unsigned int value)
{
    set_uint32(p->device_info + BSL_DEVINFO_APPLICATION_VERSION,
	       value);
}

void
bsl_devinfo_set_active_plug_in_interface_version(struct bsl_protocol *p,
						 unsigned int value)
{
    set_uint16(p->device_info + BSL_DEVINFO_ACTIVE_PLUG_IN_INTERFACE_VERSION,
	       value);
}

void
bsl_devinfo_set_bsl_max_buffer_size(struct bsl_protocol *p,
				    unsigned int value)
{
    set_uint16(p->device_info + BSL_DEVINFO_MAX_BUFFER_SIZE,
	       value);
}

void
bsl_devinfo_set_bsl_buffer_start_address(struct bsl_protocol *p,
					 unsigned int value)
{
    set_uint32(p->device_info + BSL_DEVINFO_BUFFER_START_ADDRESS,
	       value);
}

void
bsl_devinfo_set_bcr_configuration_id(struct bsl_protocol *p,
				     unsigned int value)
{
    set_uint32(p->device_info + BSL_DEVINFO_BCR_CONFIGURATION_ID,
	       value);
}

void
bsl_devinfo_set_bsl_configuration_id(struct bsl_protocol *p,
				     unsigned int value)
{
    set_uint32(p->device_info + BSL_DEVINFO_BSL_CONFIGURATION_ID,
	       value);
}
