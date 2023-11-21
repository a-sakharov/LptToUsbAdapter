#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

#ifdef _WIN32
#include <Windows.h>
#include <intrin.h>

#define Inb(port) __inbyte(port)
#define Outb(port, data) __outbyte(port, data)
#define DoNop() __nop()

double GetSecMsecTime()
{
	SYSTEMTIME systemtime;

	GetSystemTime(&systemtime);

	return ((double)time(NULL)) + ((double)systemtime.wMilliseconds / 1000.0f);
}

void flush_stdin()
{
	fflush(stdin);
}

#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/io.h>

#define Sleep(x) usleep((x)*1000)
#define DoNop() asm("nop")
#define Inb(port) inb(port)
#define Outb(port, data) outb(data, port)

void flush_stdin()
{
	int c;
	while ((c = getchar()) != '\n' && c != EOF) {}
}

double GetSecMsecTime()
{
    struct timeval timenow;
    
    gettimeofday(&timenow, NULL);

	return ((double)timenow.tv_sec) + ((double)timenow.tv_usec / 1000000.0f);
}

#endif

#ifdef NOIO
#undef Inb
#undef Outb
#define Inb(port) 0
#define Outb(port, data) 0
#endif

#define BYPASS 0x77A
#define LPT_BASE 0x378           /* parallel port base address */
#define ISBIT(byte, num) (!!((byte) & (1<<(num))))

void MonitorData()
{
	uint8_t byte;
	char buffer[128];

	Outb(LPT_BASE + 0x02, 0x20); //make DATA input
	printf("PCON: %.2hX\n", Inb(LPT_BASE + 0x02));

	printf("D7 D6 D5 D4 D3 D2 D1 D0\n");

	while (1)
	{
		byte = Inb(LPT_BASE + 0x00);

		snprintf(buffer, sizeof(buffer), "%d  %d  %d  %d  %d  %d  %d  %d",
			ISBIT(byte, 7), ISBIT(byte, 6), ISBIT(byte, 5), ISBIT(byte, 4),
			ISBIT(byte, 3), ISBIT(byte, 2), ISBIT(byte, 1), ISBIT(byte, 0)
		);
		printf("\r%s", buffer);
		Sleep(100);
	}

}

void WriteData()
{
	char user_input[32];
	unsigned long input_bin;

	Outb(LPT_BASE + 0x02, 0x00); //make DATA output

	while (1)
	{
		printf("Enter new value for DATA: ");
		fgets(user_input, sizeof(user_input), stdin);
		input_bin = strtoul(user_input, NULL, 0);
		if (input_bin > 255)
		{
			printf("Invalid input!\n");
			continue;
		}
		Outb(LPT_BASE + 0x00, (uint8_t)input_bin);
	}

}

void MonitorStatus()
{
	uint8_t byte;
	char buffer[128];

	printf("BUSY ACK# PERROR SELECT FAULT#\n");

	while (1)
	{
		byte = Inb(LPT_BASE + 0x01);
		snprintf(buffer, sizeof(buffer), "%d    %d    %d      %d      %d",
			ISBIT(byte, 7), ISBIT(byte, 6), ISBIT(byte, 5), ISBIT(byte, 4), ISBIT(byte, 3)
		);
		printf("\r%s", buffer);
		Sleep(100);
	}

}

void WriteControl()
{
	char user_input[32];
	unsigned long input_bin;

	while (1)
	{
		printf("Enter new value for PCON (SELECTIN#, INIT#, AUTOFD#, STROBE#): ");
		fgets(user_input, sizeof(user_input), stdin);
		input_bin = strtoul(user_input, NULL, 0);
		if (input_bin > 15)
		{
			printf("Invalid input!\n");
			continue;
		}
		Outb(LPT_BASE + 0x02, (uint8_t)input_bin);
	}

}

#define TEST_TIME(times, test_name, test_instruction)	\
{												\
	size_t i;									\
	double start;								\
	start = GetSecMsecTime();					\
	for (i = 0; i < times; ++i)					\
	{											\
		test_instruction;						\
	}											\
	start = GetSecMsecTime() - start;			\
	printf(test_name " %zu times: %f seconds (%f per one)\n", times, start, start / (double)times);\
}

void TestTime()
{
	const size_t max_cycles = 500000;

	TEST_TIME(max_cycles, "Bypass                ", Outb(BYPASS, 0x00)); //bypass port
	TEST_TIME(max_cycles, "Data write            ", Outb(LPT_BASE + 0x00, 0x00)); //DATA port write
	TEST_TIME(max_cycles, "Data read             ", Inb(LPT_BASE + 0x00)); //DATA port read
	TEST_TIME(max_cycles, "Status write          ", Outb(LPT_BASE + 0x01, 0x00)); //status port write
	TEST_TIME(max_cycles, "Address + strobe write", Outb(LPT_BASE + 0x03, 0x00)); //ADDRSTR port write
	TEST_TIME(max_cycles, "Address + strobe read ", Inb(LPT_BASE + 0x03)); //ADDRSTR port read
	TEST_TIME(max_cycles, "Data + strobe write   ", Outb(LPT_BASE + 0x04, 0x00)); //DATASTR port write
	TEST_TIME(max_cycles, "Data + strobe read    ", Inb(LPT_BASE + 0x04)); //DATASTR port read
	TEST_TIME(max_cycles, "NOP                   ", DoNop()); //NOP

#ifdef _WIN32
	system("pause");
#endif
}

int main()
{
	setvbuf(stdout, NULL, _IONBF, 0);

#ifndef _WIN32
	printf("Execute ioperm? (y/n): ");
	char use_ioperm = getchar(); flush_stdin();
	if (use_ioperm == 'y' || use_ioperm == 'Y')
	{
		if (ioperm(BYPASS, 1, 1) || ioperm(LPT_BASE, 5, 1))
		{
			printf("Error performing ioperm\n");
			return -1;
		}
	}
#endif

	printf("%s",
		"Choose mode:"     "\n"
		"1 - monitor DATA" "\n"
		"2 - write DATA"   "\n"
		"3 - monitor PSTAT""\n"
		"4 - write PCON"    "\n"
		"5 - test intercept time" "\n"
	);

	char mode = getchar(); flush_stdin();

	switch (mode)
	{
	case '1':
		MonitorData();
		break;

	case '2':
		WriteData();
		break;

	case '3':
		MonitorStatus();
		break;

	case '4':
		WriteControl();
		break;

	case '5':
		TestTime();
		break;

	default:
		printf("Invalid selection!");
		break;
	}

	printf("End.\n");
	return 0;


    //printf("End\n");
    //system("pause");
}
