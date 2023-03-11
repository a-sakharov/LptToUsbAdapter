#include <Windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

void outb(uint8_t data, uint16_t port)
{
    _asm
    {
        push eax
        push edx

        mov al, data
        mov dx, port
        out dx, al

        pop edx
        pop eax
    }
}

uint8_t inb(uint16_t port)
{
    uint8_t data;

    _asm
    {
        push eax
        push edx

        mov dx, port
        in al, dx
        mov data, al

        pop edx
        pop eax
    }

    return data;
}


#define base 0x378           /* parallel port base address */
#define ISBIT(byte, num) (!!((byte) & (1<<(num))))

void MonitorData()
{
	uint8_t byte;
	char buffer[128];

#ifndef NOIO
	outb(0x20, base + 0x02); //make DATA input
	printf("PCON: %.2hX\n", inb(base + 0x02));
#endif

	printf("D7 D6 D5 D4 D3 D2 D1 D0\n");

	while (1)
	{
#ifndef NOIO
		byte = inb(base + 0x00);
#else
		byte = rand() % 0xFF;
#endif
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

#ifndef NOIO
	outb(0x00, base + 0x02); //make DATA output
#endif

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
#ifndef NOIO
		outb((uint8_t)input_bin, base + 0x00);
#endif
	}

}

void MonitorStatus()
{
	uint8_t byte;
	char buffer[128];

	printf("BUSY ACK# PERROR SELECT FAULT#\n");

	while (1)
	{
#ifndef NOIO
		byte = inb(base + 0x01);
#else
		byte = rand() % 0xFF;
#endif
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
#ifndef NOIO
		outb((uint8_t)input_bin, base + 0x02);
#endif
	}

}

double GetSecMsecTime()
{
	SYSTEMTIME systemtime;

	GetSystemTime(&systemtime);

	return ((double)time(NULL)) + ((double)systemtime.wMilliseconds / 1000.0f);
}

void TestTime()
{
	size_t i;
	const size_t max_cycles = 500000;
	double start;

	start = GetSecMsecTime();
	for (i = 0; i < max_cycles; ++i)
	{
		outb(0x00, 0x77A); //bypass port
	}
	start = GetSecMsecTime() - start;
	printf("Bypass %d times write: %f seconds (%f per one)\n", max_cycles, start, start/(double)max_cycles);

	start = GetSecMsecTime();
	for (i = 0; i < max_cycles; ++i)
	{
		outb(0x00, base + 0x02); //status port
	}
	start = GetSecMsecTime() - start;
	printf("Status %d times write: %f seconds (%f per one)\n", max_cycles, start, start / (double)max_cycles);


	start = GetSecMsecTime();
	for (i = 0; i < max_cycles; ++i)
	{
		_asm nop;
	}
	start = GetSecMsecTime() - start;
	printf("Dummy %d times: %f seconds (%f per one)\n", max_cycles, start, start / (double)max_cycles);

	system("pause");
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

	printf("%s",
		"Choose mode:"     "\n"
		"1 - monitor DATA" "\n"
		"2 - write DATA"   "\n"
		"3 - monitor PSTAT""\n"
		"4 - write PCON"    "\n"
		"5 - test intercept time" "\n"
	);

	char mode;
	mode = getc(stdin);

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


    printf("End\n");
    system("pause");
}
