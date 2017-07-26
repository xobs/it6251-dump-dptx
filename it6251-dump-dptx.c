#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>

#ifndef I2C_FUNC_I2C
#include <linux/i2c.h>
#endif

#define I2C_FILENAME "/dev/i2c-2"
#define DP_I2C 0x5c
#define LVDS_I2C_ADDR 0x5e
#define EDID_SEG_WRITE (0 << 6)

#define HEX_CHARS "0123456789abcdef"

static void serial_puth(uint32_t pair, int digits)
{
	if (digits >= 8)
		putchar(HEX_CHARS[(pair >> 28) & 0xf]);
	if (digits >= 7)
		putchar(HEX_CHARS[(pair >> 24) & 0xf]);
	if (digits >= 6)
		putchar(HEX_CHARS[(pair >> 20) & 0xf]);
	if (digits >= 5)
		putchar(HEX_CHARS[(pair >> 16) & 0xf]);
	if (digits >= 4)
		putchar(HEX_CHARS[(pair >> 12) & 0xf]);
	if (digits >= 3)
		putchar(HEX_CHARS[(pair >> 8) & 0xf]);
	if (digits >= 2)
		putchar(HEX_CHARS[(pair >> 4) & 0xf]);
	if (digits >= 1)
		putchar(HEX_CHARS[(pair >> 0) & 0xf]);
}

static int serial_print_hex_offset(const void *block, int count, int offset)
{
	int byte;
	const uint8_t *b = block;
	count += offset;
	b -= offset;
	for ( ; offset < count; offset += 16) {
		serial_puth(offset, 8);

		for (byte = 0; byte < 16; byte++) {
			if (byte == 8)
				putchar(' ');
			putchar(' ');
			if (offset + byte < count)
				serial_puth(b[offset + byte] & 0xff, 2);
			else
				printf("  ");
		}

		printf("  |");
		for (byte = 0; byte < 16 && byte + offset < count; byte++)
			putchar(isprint(b[offset + byte]) ?
					b[offset + byte] :
					'.');
			printf("|\n");
	}
	return 0;
}

int serial_print_hex(const void *block, int count)
{
	return serial_print_hex_offset(block, count, 0);
}

static int i2c_write_byte(uint8_t dev_addr,
			  uint8_t reg_addr, uint8_t val) {
	struct i2c_rdwr_ioctl_data session;
	struct i2c_msg messages[1];
	uint8_t data_buf[2];
	int i2cfd;

	i2cfd = open(I2C_FILENAME, O_RDWR);
	if (i2cfd < 0) {
		perror("Unable to open i2c");
		return -1;
	}

	data_buf[0] = reg_addr;
	data_buf[1] = val;

	messages[0].addr = dev_addr;
	messages[0].flags = 0;
	messages[0].len = sizeof(data_buf);
	messages[0].buf = data_buf;

	session.msgs = messages;
	session.nmsgs = 1;

	if (ioctl(i2cfd, I2C_RDWR, &session) < 0) {
		close(i2cfd);
		return -1;
	}

	close(i2cfd);

	return 0;
}

static int i2c_read_byte(uint8_t dev_addr, uint8_t reg_addr) {
	struct i2c_rdwr_ioctl_data session;
	struct i2c_msg messages[2];
	uint8_t ret;
	int i2cfd;

	i2cfd = open(I2C_FILENAME, O_RDWR);
	if (i2cfd < 0) {
		perror("Unable to open i2c");
		return -1;
	}

	messages[0].addr = dev_addr;
	messages[0].flags = 0;
	messages[0].len = sizeof(reg_addr);
	messages[0].buf = &reg_addr;

	messages[1].addr = dev_addr;
	messages[1].flags = I2C_M_RD;
	messages[1].len = sizeof(ret);
	messages[1].buf = &ret;

	session.msgs = messages;
	session.nmsgs = 2;

	if (ioctl(i2cfd, I2C_RDWR, &session) < 0) {
		close(i2cfd);
		return -1;
	}

	close(i2cfd);

	return ret;
}

static void dptx_auxwait(void)
{
	int auxwaitcnt;

	for(auxwaitcnt = 0; auxwaitcnt < 200; auxwaitcnt++) {
		if ((i2c_read_byte(DP_I2C, 0x2b) & 0x20) != 0x20)
			return;
		usleep(1000);
	}

	printf("ERROR: AUX Channel Hang => ");

	switch (i2c_read_byte(DP_I2C, 0x9f) & 0x03)
	{
		case 0 : printf("No Error !!!\n"); break;
		case 1 : printf("Defer > 7 Times !!!\n"); break;
		case 2 : printf("Receive NAck Response !!!\n"); break;
		default: printf("TimeOut !!!\n"); break;
	}
}

static const uint8_t edid_header[] =
		{ 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static bool dptx_get_edid(uint8_t *edid, uint32_t count) {
	int offset;

	memset(edid, 0, count);
	memcpy(edid, edid_header, sizeof(edid_header));
	count -= sizeof(edid_header);

	for (offset = 0; offset < count; offset++) {
		uint8_t startadr0, startadr1, startadr2;

		startadr0 = (offset >> 0) & 0xff;
		startadr1 = (offset >> 8) & 0xff;
		startadr2 = (offset >> 16) & 0x0f;

		// Enable PC Aux Access
		i2c_write_byte(DP_I2C, 0x23, EDID_SEG_WRITE | 0x02);

		i2c_write_byte(DP_I2C, 0x24, startadr0); // Start Address[7:0]
		i2c_write_byte(DP_I2C, 0x25, startadr1); // Start Address[15:8]
		i2c_write_byte(DP_I2C, 0x26, startadr2); // WriteNum[3:0]+StartAdr[19:16]
		i2c_write_byte(DP_I2C, 0x2b, 0x0b); // EDID Read Fire
		dptx_auxwait();

		i2c_write_byte(DP_I2C, 0x23, EDID_SEG_WRITE | 0);

		edid[offset + sizeof(edid_header)] = i2c_read_byte(DP_I2C, 0x2c);
	}
	return true;
}

static bool dptx_dpcdwr(uint32_t offset, uint8_t value)
{
	uint8_t startadr0, startadr1, startadr2;

	startadr0 = (offset >> 0) & 0xff;
	startadr1 = (offset >> 8) & 0xff;
	startadr2 = (offset >> 16) & 0x0f;

	// Enable PC Aux Access
	i2c_write_byte(DP_I2C, 0x23, EDID_SEG_WRITE | 0x02);

	i2c_write_byte(DP_I2C, 0x24, startadr0); // Start Address[7:0]
	i2c_write_byte(DP_I2C, 0x25, startadr1); // Start Address[15:8]
	i2c_write_byte(DP_I2C, 0x26, startadr2); // WriteNum[3:0]+StartAdr[19:16]
	i2c_write_byte(DP_I2C, 0x27, value); // WriteData Byte 1
	i2c_write_byte(DP_I2C, 0x2b, 0x05); // Aux Write Fire
	dptx_auxwait();

	// Aux Read Fire
	i2c_write_byte(DP_I2C, 0x2b, 0x00);
	dptx_auxwait();

	// Disable PC Aux Access
	i2c_write_byte(DP_I2C, 0x23, EDID_SEG_WRITE | 0);

	if (i2c_read_byte(DP_I2C, 0x2c) != value)
		return false;
	else
		return true;
}

static uint8_t dptx_dpcdrd(uint32_t offset)
{
	uint8_t startadr0, startadr1, startadr2;

	startadr0 = (offset >> 0) & 0xff;
	startadr1 = (offset >> 8) & 0xff;
	startadr2 = (offset >> 16) & 0x0f;

	// Enable PC Aux Access
	i2c_write_byte(DP_I2C, 0x23, EDID_SEG_WRITE | 0x02);

	i2c_write_byte(DP_I2C, 0x24, startadr0); // Start Address[7:0]
	i2c_write_byte(DP_I2C, 0x25, startadr1); // Start Address[15:8]
	i2c_write_byte(DP_I2C, 0x26, startadr2); // WriteNum[3:0]+StartAdr[19:16]
	i2c_write_byte(DP_I2C, 0x2b, 0x00); // Aux Read Fire
	dptx_auxwait();

	i2c_write_byte(DP_I2C, 0x23, EDID_SEG_WRITE | 0);

	return i2c_read_byte(DP_I2C, 0x2c);
}

void dptx_show_vid_info(void) {
	uint16_t hsync_pol, vsync_pol, interlaced;
	uint16_t htotal, hde, hactive, hfront_porch, hsync_len, hback_porch;
	uint16_t vtotal, vde, vactive, vfront_porch, vsync_len, vback_porch;
	uint16_t reg;
	uint32_t frequency, sum;
	uint8_t sysstat, linkstat;


	sysstat = i2c_read_byte(DP_I2C, 0x0d);
	linkstat = i2c_read_byte(DP_I2C, 0x0e);

	if ((linkstat & 0x1f) != 0x10)
		printf("Link Training has something wrong, reg0E = %02x\n",
			linkstat);
	else
		printf("reg0E = %02x, Link Rate = %s",
			linkstat,
			(i2c_read_byte(DP_I2C, 0x0e) & 0x80) ? "HBR" : "LBR");

	printf("reg0D = %02x, %s %s %s\n", sysstat,
			(sysstat & 1) ? "Interrupt!" : "",
			(sysstat & 2) ? "HPD" : "Unplug",
			(sysstat & 4) ? "Video Stable" : "Video Unstable");

	if ((sysstat & 0x04) == 0)
		return;

	reg = i2c_read_byte(DP_I2C, 0xa0);
	hsync_pol = reg & 0x01;
	vsync_pol = (reg & 0x04) >> 2;
	interlaced = (reg & 0x10) >> 4;

	reg = i2c_read_byte(DP_I2C, 0xa1);
	htotal = ((i2c_read_byte(DP_I2C, 0xa2) & 0x1f) << 8) + reg;

	reg = i2c_read_byte(DP_I2C, 0xa3);
	hde = ((i2c_read_byte(DP_I2C, 0xa4) & 0x03) << 8) + reg;

	reg = i2c_read_byte(DP_I2C, 0xa5);
	hactive = ((i2c_read_byte(DP_I2C, 0xa6) & 0x1f) << 8) + reg;

	reg = i2c_read_byte(DP_I2C, 0xa7);
	hfront_porch = ((i2c_read_byte(DP_I2C, 0xa8) & 0x03) << 8) + reg;

	reg = i2c_read_byte(DP_I2C, 0xa9);
	hsync_len = ((i2c_read_byte(DP_I2C, 0xaa) & 0x03) << 8) + reg;

	reg = i2c_read_byte(DP_I2C, 0xab);
	vtotal = ((i2c_read_byte(DP_I2C, 0xac) & 0x0f) << 8) + reg;

	reg = i2c_read_byte(DP_I2C, 0xad);
	vde = ((i2c_read_byte(DP_I2C, 0xae) & 0x01) << 8) + reg;

	reg = i2c_read_byte(DP_I2C, 0xaf);
	vactive = ((i2c_read_byte(DP_I2C, 0xb0) & 0x0f) << 8) + reg;

	reg = i2c_read_byte(DP_I2C, 0xb1);
	vfront_porch = ((i2c_read_byte(DP_I2C, 0xb2) & 0x01) << 8) + reg;

	reg = i2c_read_byte(DP_I2C, 0xb3);
	vsync_len = ((i2c_read_byte(DP_I2C, 0xb4) & 0x01) << 8) + reg;

	/* Determine frequency */
	reg = i2c_read_byte(DP_I2C, 0x12);
	reg |= 0x80 ;
	i2c_write_byte(DP_I2C, 0x12, reg);
	reg &= 0x7F ;
	usleep(1000);
	i2c_write_byte(DP_I2C, 0x12, reg);

	sum = i2c_read_byte(DP_I2C, 0x13);
	sum = ((i2c_read_byte(DP_I2C, 0x14) & 0x0f) << 8) + reg;
	frequency = (13500L*2048L) / sum;
	printf("frequency = %d, xCnt = %d\n", frequency, sum);
	printf("Data Enable start: (%d, %d)\n", hde, vde);

	/* Print out modeline */
	printf("Modeline \"%dx%d\"", hactive, vactive);
	printf(" %0.3f", frequency / 1000000.0);

	vback_porch = vtotal - vactive - vfront_porch - vsync_len;
	hback_porch = htotal - hactive - hfront_porch - hsync_len;

	printf("  %d %d %d %d",
			hactive,
			hactive + hback_porch,
			hactive + hback_porch + hfront_porch,
			hactive + hback_porch + hfront_porch + hsync_len);
	printf("  %d %d %d %d",
			vactive,
			vactive + vback_porch,
			vactive + vback_porch + vfront_porch,
			vactive + vback_porch + vfront_porch + vsync_len);

	printf(" %cHSync", hsync_pol ? '-' : '+');
	printf(" %cVSync", vsync_pol ? '-' : '+');

	if (interlaced)
		printf(" Interlace");

	printf("\n");
}

int main(int argc, char **argv) {
	uint32_t offset, value;
	uint8_t edid[256];

	if (argc > 1) {
		if (!strcasecmp(argv[1], "--help") || !strcasecmp(argv[1], "-h")) {
			printf("Usage: %s [offset [value]]\n", argv[0]);
			printf("If no offset is specified, prints out the detected mode for the panel.\n");
			printf("If an offset is specified, reads from that AUX address.\n");
			printf("If a value is specified, it will also write to that address.\n");
			return 0;
		}
		offset = strtoul(argv[1], NULL, 0);
		if (argc > 2) {
			value = strtoul(argv[2], NULL, 0);
			printf("Setting 0x%x -> 0x%x... ", offset, value);
			if (dptx_dpcdwr(offset, value))
				printf("Ok\n");
			else
				printf("Fail\n");
		}
		else {
			value = dptx_dpcdrd(offset);
			printf("Value at 0x%x: 0x%x\n", offset, value);
		}
		return 0;
	}

	dptx_get_edid(edid, sizeof(edid));
	dptx_show_vid_info();
	serial_print_hex(edid, sizeof(edid));

	return 0;
}
