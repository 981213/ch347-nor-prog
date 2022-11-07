#include "stdafx.h"

#include <time.h>
#include <cstring>

#include "spi-op.h"
#include "spi_flash.h"

#define FLASH_SIZE_INCREASEMENT		(32 << 10)
#define FLASH_SIZE_SAMPLE_INTERVAL	(4 << 10)

#define DATA_READ_LENGTH			0x20000

#define min(a, b) (((a) > (b)) ? (b) : (a))

static int flash_probed;
static const spi_flash_id *flash_id;
static unsigned int erase_size;
static unsigned char erase_op;
static unsigned char addr_width;
static unsigned char sst_write;

static inline void AddrToCmd3(unsigned int addr, unsigned char *cmd)
{
	cmd[0] = (addr >> 16) & 0xff;
	cmd[1] = (addr >> 8) & 0xff;
	cmd[2] = (addr) & 0xff;
}

static inline void AddrToCmd4(unsigned int addr, unsigned char *cmd)
{
	cmd[0] = (addr >> 24) & 0xff;
	cmd[1] = (addr >> 16) & 0xff;
	cmd[2] = (addr >> 8) & 0xff;
	cmd[3] = (addr) & 0xff;
}

static inline void AddrToCmd(unsigned int addr, unsigned char *cmd)
{
	if (addr_width == 4)
		AddrToCmd4(addr, cmd);
	else
		AddrToCmd3(addr, cmd);
}

static inline unsigned int CmdSize(void)
{
	if (flash_id->size > SIZE_16MB)
		return 5;
	else
		return 4;
}

static bool WriteEnable(void)
{
	unsigned char op = SPI_CMD_WREN;

	return SPIWrite(&op, 1);
}

static bool WriteDisable(void)
{
	unsigned char op = SPI_CMD_WRDI;

	return SPIWrite(&op, 1);
}

static bool ReadStatusRegister(unsigned int &sr)
{
	unsigned char op = SPI_CMD_RDSR;
	unsigned char val;

	if (!SPIWriteThenRead(&op, 1, &val, 1))
		return false;

	sr = val;
	return true;
}

static bool WriteStatusRegister(unsigned char sr)
{
	unsigned char op[2];

	op[0] = SPI_CMD_WRSR;
	op[1] = sr & 0xff;

	return SPIWrite(op, 2);
}

static bool SetAddressMode(int enable4b)
{
	unsigned char op[2];
	int need_wren = 0;

	if (addr_width != 4)
		return true;

	/* ??????? */
	switch (JEDEC_MFR(flash_id->jedec_id))
	{
	case MFR_MICROM:
		need_wren = 1;
	case MFR_EON:
	case MFR_MACRONIX:
	case MFR_WINBOND:
	case MFR_GIGADEVICE:
		if (need_wren)
			if (!WriteEnable())
				return false;

		op[0] = enable4b ? SPI_CMD_ENTER_4B_MODE : SPI_CMD_EXIT_4B_MODE;
		if (!SPIWrite(op, 1))
			return false;

		if (need_wren)
			if (!WriteDisable())
				return false;

		break;
	case MFR_ISSI:
	case MFR_SPANSION:
		op[0] = SPI_CMD_WRBR;
		op[1] = (!!enable4b) << 7;
		if (!SPIWrite(op, 2))
			return false;
		break;
	}

	if (enable4b)
		return true;

	/* ???? 3 ?????????????? 16MB ???? */
	switch (JEDEC_MFR(flash_id->jedec_id))
	{
	case MFR_EON:
		op[0] = SPI_CMD_EXIT_HBL_MODE;
		if (!SPIWrite(op, 1))
			return false;
		break;
	case MFR_MACRONIX:
		/* MX25L25655E ????? */
		if (flash_id->jedec_id == 0xc22619)
			break;
	case MFR_MICROM:
	case MFR_WINBOND:
	case MFR_GIGADEVICE:
		if (!WriteEnable())
			return false;
		op[0] = SPI_CMD_WREAR;
		op[1] = 0;
		if (!SPIWrite(op, 2))
			return false;
		if (!WriteDisable())
			return false;
		break;
	case MFR_ISSI:
	case MFR_SPANSION:
		/* ?????????? */
		break;
	}

	return true;
}

static bool FlashPoll(void)
{
	unsigned int sr;

	do
	{
		if (!ReadStatusRegister(sr))
			return false;
	} while (sr & 1);

	return true;
}

bool FlashProbe(void)
{
	unsigned char op = SPI_CMD_RDID;
	unsigned char id[5], mask = 0;
	unsigned int jedec_id, ext_id, sr;
	int pre_unlock = 0;

	if (flash_probed)
		return true;

	SPIWriteThenRead(&op, 1, id, 5);

	jedec_id = id[0];
	jedec_id = jedec_id << 8;
	jedec_id |= id[1];
	jedec_id = jedec_id << 8;
	jedec_id |= id[2];

	ext_id = id[3] << 8 | id[4];

	if (!jedec_id || (jedec_id == 0xffffff))
	{
		fprintf(stderr, "Error: no valid flash found.\n");
		return false;
	}

	flash_id = spi_flash_id_lookup(jedec_id, ext_id);

	if (flash_id)
	{
		if (flash_id->flags & SF_4K_SECTOR)
		{
			if (flash_id->flags & SF_4K_PMC)
				erase_op = SPI_CMD_4KB_PMC_ERASE;
			else
				erase_op = SPI_CMD_SECTOR_ERASE;
			erase_size = SECTOR_4KB;
		}
		else if (flash_id->flags & SF_32K_BLOCK)
		{
			erase_op = SPI_CMD_32KB_BLOCK_ERASE;
			erase_size = SECTOR_32KB;
		}
		else if (flash_id->flags & SF_64K_BLOCK)
		{
			erase_op = SPI_CMD_64KB_BLOCK_ERASE;
			erase_size = SECTOR_64KB;
		}
		else if (flash_id->flags & SF_256K_BLOCK)
		{
			erase_op = SPI_CMD_64KB_BLOCK_ERASE;
			erase_size = SECTOR_256KB;
		}

		if (flash_id->flags & SF_INIT_SR)
			pre_unlock = 1;

		if (flash_id->flags & SF_SST)
			sst_write = 1;

		addr_width = flash_id->size > SIZE_16MB ? 4 : 3;
	}
	else
	{
		fprintf(stderr, "Error: unrecognised flash found.\n");
		return false;
	}

	if (pre_unlock)
	{
		if (!WriteEnable())
			return false;
		if (!WriteStatusRegister(0))
			return false;
	}
	else if (flash_id && flash_id->flags & SF_BP_ALL)
	{
		if (!ReadStatusRegister(sr))
			return false;

		if (flash_id->flags & SF_BP0_2)
			mask |= SR_BP0_2_MASK;

		if (flash_id->flags & SF_BP3)
			mask |= SR_BP3_MASK;

		if (flash_id->flags & SF_BP4)
			mask |= SR_BP4_MASK;

		if (sr & mask)
		{
			sr &= ~mask;
			if (!WriteEnable())
				return false;
			if (!WriteStatusRegister(sr))
				return false;
			if (!FlashPoll())
				return false;
		}
	}

	printf("Flash: %s\n", flash_id->model);
	printf("Capacity: %dKiB\n", flash_id->size >> 10);
	printf("Sector size: %dKiB\n", erase_size >> 10);
	printf("\n");

	flash_probed = 1;

	return true;
}

unsigned int FlashGetSize(void)
{
	return flash_id->size;
}

bool FlashRead(unsigned int addr, unsigned int len, unsigned char *buf)
{
	unsigned int flash_offset, len_read, len_to_read, len_left;
	struct timespec start, end;
	double time_used;
    unsigned char op[6];

	if (!len)
		return true;

	if (!buf)
		return false;

	flash_offset = addr % flash_id->size;

	if (!SetAddressMode(1))
		return false;

	op[0] = SPI_CMD_READ_FAST;
    op[4] = 0;
    op[5] = 0;

	ProgressInit();
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);

	len_read = 0;
	len_left = len;

	while (len_left)
	{
		len_to_read = len_left > DATA_READ_LENGTH ? DATA_READ_LENGTH : len_left;

        AddrToCmd(flash_offset + len_read, &op[1]);
		if (!SPIWriteThenRead(op, CmdSize() + 1, buf + len_read, len_to_read))
			return false;

		len_read += len_to_read;
		len_left -= len_to_read;

		ProgressShow(len_read * 100 / len);
	}

	clock_gettime(CLOCK_MONOTONIC_RAW, &end);

	ProgressDone();
	time_used = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000000000.0;
	printf("Time used: %.2fs\n", time_used);
	printf("Speed: %.2fKiB/s\n", (double) len / time_used / 1024);

	if (!SetAddressMode(0))
		return false;

	return true;
}

static bool FlashEraseSector(unsigned int addr)
{
	unsigned char cmd[5];

	cmd[0] = erase_op;
	AddrToCmd(addr, &cmd[1]);

	if (!WriteEnable())
		return false;

	if (!SPIWrite(cmd, CmdSize()))
		return false;

	return FlashPoll();
}

bool FlashErase(unsigned int addr, unsigned int len)
{
	unsigned int num_sectors, sector_left, size_erased;
	struct timespec start, end;
	double time_used;

	if (addr % erase_size)
	{
		fprintf(stderr, "Error: start address is not on erase boundary.\n");
		return false;
	}

	if ((addr + len) % erase_size)
	{
		fprintf(stderr, "Error: end address is not on erase boundary.\n");
		return false;
	}

	if ((addr > flash_id->size) || (addr + len > flash_id->size))
	{
		fprintf(stderr, "Error: end address exceeds flash capacity.\n");
		return false;
	}

	if (!SetAddressMode(1))
		return false;

	ProgressInit();
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);

	size_erased = 0;

	/* ???????????????????? */
	num_sectors = len / erase_size;

	sector_left = num_sectors;
	while (sector_left)
	{
		if (!FlashEraseSector(addr))
			return false;
		addr += erase_size;
		size_erased += erase_size;
		sector_left--;

		ProgressShow(size_erased * 100 / len);
	}

	clock_gettime(CLOCK_MONOTONIC_RAW, &end);
	time_used = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000000000.0;

	ProgressDone();

	printf("Time used: %.2fs\n", time_used);
	printf("Speed: %.2fKiB/s, %.2fsec/s\n", len / time_used / 1024, num_sectors / time_used);

	if (!SetAddressMode(0))
		return false;

	return true;
}

bool FlashChipErase(void)
{
	unsigned char cmd;
	bool ret;
	struct timespec start, end;
	double time_used;

	cmd = SPI_CMD_CHIP_ERASE;

	if (!WriteEnable())
		return false;

	if (!SPIWrite(&cmd, 1))
		return false;

	clock_gettime(CLOCK_MONOTONIC_RAW, &start);

	ret = FlashPoll();

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    time_used = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000000000.0;

	printf("Time used: %.2fs\n", time_used);

	return ret;
}

static bool FlashSinglePageProgram(unsigned int addr, unsigned char *buff, unsigned int len)
{
    unsigned char op[5];

    op[0] = SPI_CMD_PAGE_PROG;
    AddrToCmd(addr, &op[1]);

    if(!SPIWriteTwo(op, CmdSize(), buff, len))
        return false;

	return FlashPoll();
}

static bool FlashPageProgram(unsigned int addr, unsigned char *buff, unsigned int len)
{
	unsigned int bytes_written = 0, bytes_to_write, bytes_left;
	unsigned int dst;
	unsigned char *src;
	struct timespec start, end;
	double time_used;

	if (!SetAddressMode(1))
		return false;

	ProgressInit();
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);

	bytes_left = len;
	while (bytes_written < len)
	{
		src = buff + bytes_written;
		dst = addr + bytes_written;
		bytes_to_write = min(bytes_left, PAGE_SIZE - (dst % PAGE_SIZE));

		if (!WriteEnable())
			return false;

		if (!FlashSinglePageProgram(dst, src, bytes_to_write))
			return false;

		bytes_left -= bytes_to_write;
		bytes_written += bytes_to_write;

		ProgressShow(bytes_written * 100 / len);
	}

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    time_used = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000000000.0;

	ProgressDone();

	printf("Time used: %.2fs\n", time_used);
	printf("Speed: %.2fKiB/s\n", len / time_used / 1024);

	if (!SetAddressMode(0))
		return false;

	return true;
}

static bool FlashSSTAAIProgram(unsigned int addr, unsigned char *buff, unsigned int len)
{
	unsigned char op[6];
	unsigned int dst = 0, bytes_written = 0;
	int addr_sent = 0;
	struct timespec start, end;
	double time_used;

	ProgressInit();
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);

	if (addr % 2)
	{
		if (!WriteEnable())
			return false;

		if (!FlashSinglePageProgram(addr, buff, 1))
			return false;

		dst++;
	}

	if (!WriteEnable())
		return false;

	op[0] = SPI_CMD_AAI_WP;
	AddrToCmd3(addr + dst, &op[1]);

	while (len - dst >= 2)
	{
		if (!addr_sent)
		{
			op[4] = buff[dst++];
			op[5] = buff[dst++];

			if (!SPIWrite(op, 6))
				return false;

			addr_sent = 1;
		}
		else
		{
			op[1] = buff[dst++];
			op[2] = buff[dst++];

			if (!SPIWrite(op, 3))
				return false;
		}

		if (!FlashPoll())
			return false;

		bytes_written += 2;

		if (bytes_written % 256 == 0)
			ProgressShow(bytes_written * 100 / len);
	}

	if (!WriteDisable())
		return false;

	if (!FlashPoll())
		return false;

	if (dst < len)
	{
		if (!WriteEnable())
			return false;

		if (!FlashSinglePageProgram(addr + dst, buff + dst, 1))
			return false;

		dst++;
	}

    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    time_used = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000000000.0;

	ProgressDone();

	printf("Time used: %.2fs\n", time_used);
	printf("Speed: %.2fKiB/s\n", len / time_used / 1024);

	if (!WriteDisable())
		return false;

	return true;
}

bool FlashWrite(unsigned int addr, unsigned char *buff, unsigned int len)
{
	if (!buff)
		return false;

	if ((addr > flash_id->size) || (addr + len > flash_id->size))
	{
		fprintf(stderr, "Error: write address exceeds flash capacity.\n");
		return false;
	}

	if (sst_write)
		return FlashSSTAAIProgram(addr, buff, len);
	else
		return FlashPageProgram(addr, buff, len);
}
