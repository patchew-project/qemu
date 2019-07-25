#ifndef _QTEST_FUZZ_H_
#define _QTEST_FUZZ_H_

typedef struct qtest_cmd {
	char name[32];
	uint8_t size;
} qtest_cmd;

typedef uint32_t addr_type;

static qtest_cmd commands[] = 
{
	{"clock_step", 0},
	{"clock_step", 0},
	{"clock_set", 1},
	{"outb", 2},
	{"outw", 2},
	{"outl", 2},
	{"inb", 1},
	{"inw", 1},
	{"inl", 1},
	{"writeb", 2},
	{"writew", 2},
	{"writel", 2},
	{"writeq", 2},
	{"readb", 1},
	{"readw", 1},
	{"readl", 1},
	{"readq", 1},
	{"read", 2},
	{"write", 3},
	{"b64read", 2},
	{"b64write", 10},
	{"memset", 3},
	{"write_dma", 2},
	{"out_dma", 2},
};
#endif
