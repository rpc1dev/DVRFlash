/**
 **  DVRFlash (based on fPLScsi)
 **
 **  Our own little replacement of the Pioneer DVR-1xx Flasher :þ
 **
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "getopt.h"
#include "plscsi.h"

// Define our msleep function
#ifdef _WIN32
#include <Windows.h>
#define msleep(msecs) Sleep(msecs)
#else
#include <unistd.h>
#define	msleep(msecs) usleep(1000*msecs)
#endif

#if __APPLE__
#include <inttypes.h>
#define u8  uint8_t
#define u16 uint16_t
#define u32 uint32_t
#else // ! __APPLE__
#ifndef u8
#define u8 unsigned char
#endif
#ifndef u16
#define u16 unsigned short
#endif
#ifndef u32
#define u32 unsigned long
#endif
#endif // __APPLE__

// Scsi parameters
#define SENSE_LENGTH             0xE			/* offsetof SK ASC ASCQ < xE */
#define MAX_SECONDS              300			/* negative means max */

// Some fixes for windows
#if (_WIN32 || __MSDOS__)
#define NULL_FD fopen("NUL", "w")
#else
#define NULL_FD fopen("/dev/null", "w")
#endif

#define MAX_SIZE		0x00200000  // Maximum Firmware file size
#define FIRM_SIZE		0x00100000	// Standard Firmware size (Gen & Kern)
#define A09F_SIZE		0x00128000  // Minnimum DVR 109+ Firmware size
#define MODE_SIZE		0x00000100	// Switch mode buffer size + temporary buffer
#define A06K_SIZE		0x00010000	// DVR 106+ Kernel size
#define A09K_SIZE		0x00020000  // DVR 109+ Kernel size
#define NAME_SIZE		256			// We were a bit short on firmware & device name size

#define MAX_CDB_SIZE	16

// Firmware types
#define FTYPE_UNDEFINED	0
#define	FTYPE_KERNEL	1
#define FTYPE_NORMAL	2

// Universal 106/107/108/K12 Key
#define UNIVERSAL_KEY	0x9A782361

// DVR109 Keys
#define ADV109_KEY		0x51B7DAC2
#define GEN109_KEY		0x5C6A8FE9
#define ASUS09_KEY		0x2BD55699
#define ASUSNL_KEY		0x9CE45EF6
#define DATA09_KEY		0x150000B0
#define MEDION_KEY		0xD1A2ED86
#define OEMEXT_KEY		UNIVERSAL_KEY
#define OEMINT_KEY		0x1B0000B0

// Handy macro for exiting. xbuffer or fd = NULL is no problemo 
// (except for lousy Visual C++, that will CRASH on fd = NULL!!!!)
#define FREE_BUFFERS	{free(fbuffer[0]); free(fbuffer[1]); free(mbuffer);}
#define ERR_EXIT		{FREE_BUFFERS; if (fd != NULL) fclose(fd); scsiClose(scsi); fflush(stdin); exit(1);}
// Fixed 1.1: The infamous Linux/DOS stdin fix
#define FLUSHER			{while(getchar() != 0x0A);}

// Drive indentification
typedef struct
{
	char Desc[25];
	char Rev[5];
	char Date[9];
	char Maker[10];
} Drive_ID;

typedef struct  // 2.1: Added Kernel & Normal Sizes for larger 109 sizes.
{
	char Serial[17];
	char Interface[5];
	int  Generation;
	char Kernel_Type[9];
	char Normal_Type[9];
	int  Kernel_Size;
	int  Normal_Size;
	char Kernel_Rev[5];
} Extra_ID;

// Global variables, set to static to avoid name confusion, e.g. with stat()
static int	opt_verbose = 0;
static int	opt_debug   = 0;
static int  opt_yes		= 0;
static int	stat        = 0;
static u8   *mbuffer	= NULL;
static u8	cdb[MAX_CDB_SIZE] = {0};
static Scsi	*scsi;
static u32	seed        = 0;	// For the 103/104 downgrade

/* Print the diclaimer */
int printDisclaimer()
{
	char c;
	puts("                       DISCLAIMER");
	puts("");
	puts("THIS PROGRAM IS PROVIDED \"AS IS\" WITHOUT WARRANTY OF ANY KIND,");
	puts("EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO,"); 
	puts("THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A");
	puts("PARTICULAR PURPOSE.");
	puts("");
	puts("THE ENTIRE RISK AS TO THE ABILITY OF THIS PROGRAM TO FLASH A");
	puts("PIONEER OR COMPATIBLE DVR DRIVE IS WITH YOU. SHOULD THE");
	puts("PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF ALL NECESSARY");
	puts("SERVICING, REPAIR OR CORRECTION.");
	puts("");
	puts("THIS PROGRAM IS NOT ENDORSED BY PIONEER CORPORATION OR ANY");
	puts("COMPANY RESELLING PIONEER EQUIPMENT AS THEIR OWN BRAND");
	puts("");
	puts("IF YOU UNDERSTAND THE RISKS ASSOCIATED WITH THIS PROGRAM AND");
	puts("DISCHARGE BOTH THE AUTHOR AND PIONEER CORPORATION FROM ANY");
	puts("DAMAGE OCCURING AS THE RESULT OF ITS USE, PLEASE INDICATE SO");
	puts("BY ANSWERING THE FOLLOWING QUESTION:");
	puts("");
	puts("Do you understand and agree to the statement above (y/n)?");
        fflush(stdin);
	if (opt_yes)
		c='y';
	else
	{
		c = (char) getchar();
		FLUSHER;
	}
	if ((c!='y') && (c!='Y'))
	{
		fprintf(stderr, "Operation cancelled by user.\n");
		return -1;
	}
	puts("");
	return 0;
}

/* The handy ones, IN BIG ENDIAN MODE THIS TIME!!!*/
u32 readlong(u8* buffer, u32 addr)
{
	return ((((u32)buffer[addr+0])<<24) + (((u32)buffer[addr+1])<<16) +
		(((u32)buffer[addr+2])<<8) + ((u32)buffer[addr+3]));
}

void writelong(u8* buffer, u32 addr, u32 value)
{
	buffer[addr]   = (u8)(value>>24);
	buffer[addr+1] = (u8)(value>>16);
	buffer[addr+2] = (u8)(value>>8);
	buffer[addr+3] = (u8)value;
}

/* Display a nice countdown */
void countdown(unsigned int secs)
{
	int t, i;
	for (t=secs; t>0; t--)
	{
		for (i=0; i<(((int)log10((float)secs))-((int)log10((float)t))); i++)
			printf(" ");
		printf("%d", t);
		fflush(stdout);
		msleep(1000);
		for (i=0; i<(1+(int)log10((float)secs)); i++)
		{
			printf("\x8");
			fflush(stdout);
		}
	}
	printf("OK.\n");
}

/* Print Sense status and errors */
u32 getSense(Scsi *scsi, char* errmsg = NULL)
{
// Sense variables
u32	 s = 0xFFFFFFFF;
char sense[20];

	int stat = scsiGetSense(scsi, sense, SENSE_LENGTH, SENSE_LENGTH);
	if ((stat != SENSE_LENGTH) || (sense[0] < 0x70) || (sense[0] > 0x71))
	{
		if (errmsg)
			if(opt_yes)
				printf("ERROR - %s (No Sense)\n", errmsg);
			else
				fprintf(stderr, "%s (No Sense)\n", errmsg);
		return s;
	}

	s = ((sense[2]&0x0F)<<16) + (sense[12]<<8) + sense[13];
	if (errmsg)
	{
		if(opt_yes)
			printf("ERROR - %s (Sense: %02X %02X %02X)\n", errmsg,
				(s>>16)&0xFF, (s>>8)&0xFF, s&0xFF);
		else
			fprintf(stderr, "%s (Sense: %02X %02X %02X)\n", errmsg,
				(s>>16)&0xFF, (s>>8)&0xFF, s&0xFF);
	}
	return s;
}


/* Display a Progression Bar */
void progressBar(float current = 0.0, float max = 100.0)
{
	static int c;	// We need this variable to keep its value between calls
	// Progression bar
	char percent[] = "|============|============|============|============|";
	if (current == 0.0)
	{
		printf ("0%%          25%%          50%%          75%%         100%%\n");
		c = 0;
  		putchar (percent[c++]);
  		fflush (stdout);
	}
	else
	{
		while ((current / max) > ((float) c / (sizeof (percent) - 1)))
		{
	  		putchar (percent[c++]);
	  		fflush (stdout);
		}
		if (current >= max)
			printf("\n");
	}
}

// We could probably use macros, but it's fast enough as it is
u8  pseudoRandom()
{
	seed = ((((seed&0xFFFF)*0x41C6) + ((seed>>16)*0x4E6D))<<16) + ((seed&0xFFFF)*0x4E6D) + 0x3039;
	return ((u8)(seed>>16));
}

u8  pseudoRandom2()
{
	seed = ((((seed&0xFFFF)*0x41C6) + ((seed>>16)*0x4E6D))<<16) + ((seed&0xFFFF)*0x4E6D) + 0x3039;
	return ((u8)(((seed>>16)&0x7FFF)%0x100));
}


/* Enable DVR-103/104 downgrade */
int Downgrade()
{
	u8*	dbuffer		= NULL;
	int	i			= 0;
	u32 s			= 0;
	int	match		= 0;

	if (opt_verbose)
		printf("  DVR-103/104 downgrade:\n"); 

	if ( (dbuffer = (u8*) calloc(0x400, 1)) == NULL )
	{
		if(opt_yes)
			printf ("    ERROR - Could not allocate downgrade buffer\n");
		else
			fprintf (stderr, "    Could not allocate downgrade buffer\n");
        return -1;
	}


	// 3B 01 F3 00 00 00 00 00 00 00 - Reset key
	memset(cdb,0x00,MAX_CDB_SIZE);

	cdb[0] = 0x3B;	// Write Buffer
	cdb[1] = 0x01;
	cdb[2] = 0xF3;

	// 1.4: Write buffer command HAS a data out phase, even an empty one!
	stat = scsiSay(scsi, (char*) cdb, 10, (char*) dbuffer, 0x00, X2_DATA_OUT);
	if (stat)
	{	
		getSense(scsi, "    Unlock - Reset");
		free (dbuffer);
		return 1;	// 1.4: this probably means drive is not downgrade protected.
	}

	// 3C 01 F2 00 00 00 00 04 00 00 - Read pseudorandom data
	memset(cdb,0x00,MAX_CDB_SIZE);

	cdb[0] = 0x3C;	// Read Buffer
	cdb[1] = 0x01;
	cdb[2] = 0xF2;
	cdb[7] = 0x04;	// Retrieve $400 bytes of data, including the key

	stat = scsiSay(scsi, (char*) cdb, 10, (char*) dbuffer, 0x400, X1_DATA_IN);

	if (stat)
	{	// Stat has to be right this time
		getSense(scsi, "    Read seeded data");
		free (dbuffer);
		return -1;
	}

	// Get a seed match on first 4 bytes. Eventhough we don't know it for sure, 
	// there's a 99.9999% chance these first 4 bytes never get modified.
	// Also, there is mathematically no chance that different seeds can generate
	// the same starting 4 byte sequence, so we're pretty safe here too.
	// Even if the worst should happen, running DVRFlash again should do the trick ;)
 
	if (opt_verbose)
		printf("    Attempting to find seed...\n");

	// lookups rarely produce elegant code...
	match = 0;
	for (s=0; s<0x10000; s++)
	{	// Try the 65536 possible seeds
		seed = s;
		for (i=0;i<4;i++)
		{
			if (dbuffer[i] != pseudoRandom())
				break;
			if (i == 3)
				match = -1;
		}
		if (match)
			break;
	}

	if (!match)
	{
		if(opt_yes)
			printf("    ERROR - Could not find seed!\n");
		else
			fprintf(stderr, "    Could not find seed!\n");
		free (dbuffer);
		return -1;
	}
	if (opt_verbose)
		printf("    Found seed: %04X ;)\n", (unsigned int)s);

	// Generate the 1 extra pseudorandom value and fill response buffer with it
	seed = s;
	for (i=0; i<0x400; i++)
	// We need to re-generate 0x400 pseudo random values to get to that one response
		pseudoRandom();
	// Now we can retreive the response/unlock value, and fill our buffer with it
	i = pseudoRandom2() ^ 0xFF;
	memset(dbuffer, i, 0x400);

	// 3B 01 F2 00 00 00 00 01 00 00 - Write $100 bytes of junk
	memset(cdb,0x00,MAX_CDB_SIZE);

	cdb[0] = 0x3B;	// Write Buffer
	cdb[1] = 0x01;
	cdb[2] = 0xF2;
	cdb[7] = 0x01;	

	stat = scsiSay(scsi, (char*) cdb, 10, (char*) dbuffer, 0x100, X2_DATA_OUT);
	if (stat)
	{	
		getSense(scsi, "    Unlock - Step 1");
		free (dbuffer);
		return -1;
	}

	// 3B 01 F2 00 00 00 00 04 00 00 - Unlock
	memset(cdb,0x00,MAX_CDB_SIZE);

	cdb[0] = 0x3B;	// Write Buffer
	cdb[1] = 0x01;
	cdb[2] = 0xF2;
	cdb[7] = 0x04;	

	stat = scsiSay(scsi, (char*) cdb, 10, (char*) dbuffer, 0x400, X2_DATA_OUT);
	if (stat)
	{	
		getSense(scsi, "    Unlock - Step 2");
		free (dbuffer);
		return -1;
	}

	free (dbuffer);
	return 0;
}

int SetKernKey(char* buffer, long opt_key, int generation, char* dtype_id)
{
	if (opt_key>0)								// Force Key based on Option Entry
		writelong((u8*)buffer,0x10,opt_key);
	else if (!strncmp("PIO_ADV", dtype_id, 7))	// Pioneer A09
		writelong((u8*)buffer,0x10,ADV109_KEY);
	else if (!strncmp("GENERAL", dtype_id, 7))	// Pioneer 109
		writelong((u8*)buffer,0x10,GEN109_KEY);
	else if (!strncmp("ASUS   ", dtype_id, 7))	// ASUS 1608P
		writelong((u8*)buffer,0x10,ASUS09_KEY);
	else if (!strncmp("ASUS_NL", dtype_id, 7))	// ASUS OEM 1608P
		writelong((u8*)buffer,0x10,ASUSNL_KEY);
	else if (!strncmp("PIODATA", dtype_id, 7))	// PioDATA 109
		writelong((u8*)buffer,0x10,DATA09_KEY);
	else if (!strncmp("MEDION ", dtype_id, 7))	// Medion 109
		writelong((u8*)buffer,0x10,MEDION_KEY);
	else if (!strncmp("OEM_EXT ", dtype_id, 8))	// Buffalo
		writelong((u8*)buffer,0x10,OEMEXT_KEY);
	else if (!strncmp("OEM_EXT2", dtype_id, 8))	// Buffalo2
		writelong((u8*)buffer,0x10,OEMEXT_KEY);
	else if (!strncmp("OEM_INT", dtype_id, 7))	// Buffalo3
		writelong((u8*)buffer,0x10,OEMINT_KEY);
	else if (!strncmp("APPLE  ", dtype_id, 7))	// Fake Apple Brand
		writelong((u8*)buffer,0x10,OEMEXT_KEY);
	else if (!strncmp("ACER   ", dtype_id, 7))
		writelong((u8*)buffer,0x10,OEMEXT_KEY); // Fake Acer Brand
	else if (!strncmp("SONYDT ", dtype_id, 7))
		writelong((u8*)buffer,0x10,OEMEXT_KEY); // Fake SONY Brand
	else
	{
		if(opt_yes)
			printf("ERROR - Unsupported Drive Type %s\n", dtype_id);
		else
			fprintf(stderr,"Unsupported Drive Type %s\n", dtype_id);
		return -1;
	}
	if ( (generation == 10 || generation == 11) && opt_key == 0 && (char)buffer[0x10] != (char)0x9A)
			(char)buffer[0x10]++;
	return 0;
}

// 2.1 - Added Kernel Type for DVR-109 Keys
int SetKern(char* buffer, long opt_key, int generation, char* dtype_id)
{
 	// Copy the Kernel data, including the key if required
	switch(generation)
	{
	case 1: // DVR-103
		strncpy(buffer, "PIONEER DVR-S301",16);
		break;
	case 3: // DVR-104
		strncpy(buffer, "PIONEER DVD-R104",16);
		break;
	case 4: // DVR-105
		strncpy(buffer, "PIONEER  DVR-105",16);
		break;
	case 52: // DVR-K12
	case 53: // DVR-K12D
	case 6: // DVR-106
		strncpy(buffer, "PIONEER  DVR-106",16);
		writelong((u8*)buffer,0x10,UNIVERSAL_KEY);
		break;
	case 54: // DVR-K13 (?)
	case 7: // DVR-107
		strncpy(buffer, "PIONEER  DVR-107",16);
		writelong((u8*)buffer,0x10,UNIVERSAL_KEY);
		break;
	case 8: // DVR-108
	case 60: // DVR-K14 (?)
	case 62: // DVR-K04 (?)
		strncpy(buffer, "PIONEER  DVR-108",16);
		writelong((u8*)buffer,0x10,UNIVERSAL_KEY);
		break;
	case 9: // DVR-109
	case 64: // DVR-K15
	case 66: // DVR-K05
		strncpy(buffer, "PIONEER  DVR-109",16); // 2.1 - Key dependant on firmware type.
		return SetKernKey(buffer, opt_key, generation, dtype_id);
		break;
	case 10: // DVR=110
	case 11: // DVR-110D
		strncpy(buffer, "PIONEER  DVR-110",16); // 2.2 - Key dependant on firmware type.
		if (!SetKernKey(buffer, opt_key, generation, dtype_id))
		{
//			(char)buffer[0x10]++;
			return 0;
		} else return -1;
		break;
	default:
		if(opt_yes)
			printf("ERROR - Spock gone crazy error\n");
		else
			fprintf(stderr,"Spock gone crazy error\n");
		return -1;
		break;
	}
	return 0;
}

/* 1.5: inquiry the drive up to 20 times until it reacts */
void TickleDrive(Scsi *scsi)
{
int i;

for (i = 0; i < 20; i++)
	{
	// just do something to make the drive react?.
	memset(cdb,0x00,MAX_CDB_SIZE);
	cdb[0] = 0x12;		// Inquiry
	cdb[4] = 0x60;	// size
	stat = scsiSay(scsi, (char*) cdb, 6, (char*) mbuffer, 0x60, X1_DATA_IN);
	if (!stat) break;
	msleep(100);	// drive still not reacting, wait 0.1s and retry
	}
}

/* ------------------------------------------------------------------------ */
/* DVD Detection routines (stolen from dvdzone ;þ)                          */
/* ------------------------------------------------------------------------ */
char *regionString(unsigned char m)
{
static char result[16];
int i;
int r;

m &= 0xff;

if (m == 0xff) return "none";

m = ~m;
i = 0;

for (r = 1; r <= 8; r++)
  {
  if (m & 1)
    {
    result[i++] = '0' + r;
    m &= ~1;
    if (!m) break;
    result[i++] = '+';
    }
  m = m >> 1;
  }

result[i] = 0;

return result;
}

/* ------------------------------------------------------------------------ */
int isDVD(Scsi *scsi)
{
int result;

memset(cdb,0x00,MAX_CDB_SIZE);
memset(mbuffer,0x00,MODE_SIZE);
// Use MODE_SENSE with page 2A (MMC2 Capabilities and Mechanical Status Page)
// sg devices on Linux doesn't seem to handle get configuration (but scd devices work)
cdb[0] = 0x5A;		// Let's use mode 10 rather than mode 6 
cdb[2] = 0x2A;
cdb[8] = 0xFF;

result = scsiSay(scsi, (char *) cdb, 10, (char *) mbuffer, 0xFF, X1_DATA_IN);
if (result < 0) return 0;
if (mbuffer[8] != 0x2A) return 0;	// Security check: code page should be returned

// bit 3 of response byte 2 (+8) indicates DVD-ROM read capability
if (mbuffer[10] & 0x08)
	return 1;

return 0;
}

/* ------------------------------------------------------------------------ */
int getInquiry(Scsi *scsi, char *vendor, char *model, char *revision)
{
INT result;

memset(cdb,0x00,MAX_CDB_SIZE);
cdb[0] = 0x12;
cdb[4] = 0x60;

result = scsiSay(scsi, (char *) cdb, 6, (char *) mbuffer, 0x60, X1_DATA_IN);
if ( result || (mbuffer[39] != '/') || (mbuffer[42] != '/') )
// We did not read 60 bytes or the date is not right - probably not a DVR
	return 0;

memmove(vendor, mbuffer+8, 8); vendor[8] = 0;
memmove(model, mbuffer+8+8, 16); model[16] = 0;
memmove(revision, mbuffer+8+8+16, 4); revision[4] = 0;

return 1;
}

/* ------------------------------------------------------------------------ */
int getRPC(Scsi *scsi, char *buffer)
{
INT result;

memset(cdb,0x00,MAX_CDB_SIZE);
cdb[0]  = 0xA4;
cdb[9]  = 0x08;
cdb[10] = 0x08;

// Needed for DVD Region Killer in Windows
buffer[6] = 0;

result = scsiSay(scsi, (char *) cdb, 12, (char *) buffer, 8, X1_DATA_IN);

if (result) return 0;

return 1;
}

/* ------------------------------------------------------------------------ */
void showRPC(Scsi *scsi)
{
	char rpc_info[8];
	static char *flagText[4] = {"Region not set", "Region set", "Last chance", "Locked"};
	
	if(getRPC(scsi, rpc_info) && rpc_info[6])
	{
		printf("        Status : RPC-%lu (region locked)\n", ((unsigned long) rpc_info[6])+1);
		if (rpc_info[6] == 1)
		{
			printf("        Region : %s\n", regionString(rpc_info[5]));
			printf("       Changes : %lu region change%s remaining\n",(unsigned long) (rpc_info[4] & 0x07), ((rpc_info[4] & 0x07) > 1) ? "s" : "");
			printf("                 %lu vendor reset%s remaining\n",(unsigned long) ((rpc_info[4] >> 3) & 0x07), (((rpc_info[4] >> 3) & 0x07) >1) ? "s" : "");
			printf("                 state is '%s'\n\n", flagText[(rpc_info[4] >> 6) & 0x03]);
		} else
		{
			printf("                 Unknown RPC scheme\n");
			printf("                 (%02lx %02lx %02lx %02lx)\n\n", ((unsigned long) rpc_info[4]) & 0xff, ((unsigned long) rpc_info[5]) & 0xff,
                                                                ((unsigned long) rpc_info[6]) & 0xff, ((unsigned long) rpc_info[7]) & 0xff );
		}
	} else
		printf("        Status : RPC-1 (region free)\n\n");
}



/* ------------------------------------------------------------------------ */
int ProcessDevice(Scsi *scsi, char const *name)
{
char vendor[8+1];
char model[16+1];
char revision[4+1];
int  ret = 1;

if (scsiOpen(scsi, name))
  return 1;		// We're scanning for all devices, so this can fail 

// Windows queries fail without those
scsiLimitSense(scsi, SENSE_LENGTH);
scsiLimitSeconds(scsi, MAX_SECONDS, 0);

if ( (isDVD(scsi)) && (getInquiry(scsi, vendor, model, revision)) )
{
    printf("\n");
	printf("        Device : %s\n", name+((name[5]==':')?4:0));
	printf("        Vendor : %s\n", vendor);
    printf("         Model : %s\n", model);
    printf("      Revision : %s\n\n", revision);
	
	showRPC(scsi);
	
	ret = 0;
}

scsiClose(scsi);
return ret;
}

/* Here we go! */
int main (int argc, char *argv[])
{
	char	devname[NAME_SIZE] = "\\\\.\\I:";
	char	fname[2][NAME_SIZE];			// firmware name(s)
	int		ftype[2] = {FTYPE_UNDEFINED, FTYPE_UNDEFINED};
	size_t	fsize[2] = {0 , 0};
	int		kern_id		= -1;
	int		norm_id		= -1;
	u8		*fbuffer[2];

	// ID variables
	Drive_ID id;
	Extra_ID idx;

	// Flags
	int detected		= 1;	// Number of DVR Devices (assume 1)
	int opt_key			= 0;
	int opt_skip        = 0;
	int opt_error 		= 0;	// getopt
	int opt_force       = 0;
	int opt_kernel		= 0;	// Switch drive to kernel mode
	int is_kernel		= 0;	// is drive in kernel mode?
	int nb_firmwares	= 0;	// firmware file(s) provide 

	// General purpose
	char strGen[5];
	char str[80];
	int  i;
	char c;
	size_t read;
	FILE *fd = NULL;

/*
 * Init
 */
	fflush(stdin);
	fbuffer[0] = NULL;
	fbuffer[1] = NULL;
	mbuffer    = NULL;
	scsi       = NULL;

	while ((i = getopt (argc, argv, "bfhksvy01234567?")) != -1)
		switch (i)
	{
		case 'y':		// Pass 'Yes' to all prompts
			opt_yes = -1;
			break;
		case 'v':		// Print verbose messages
			opt_verbose = -1;
			break;
		case 'f':		// Force flashing
			opt_force++;
			break;
		case 'k':       // Kernel mode only
			opt_kernel = -1;
			break;
		case 'b':       // Debug mode (don't flash!)
			opt_debug = -1;
			break;
		case 's':		// Skip disclaimer and other bugging stuff
			opt_skip = -1;
			break;
		case '0':		// Force Universal Key
			opt_key = UNIVERSAL_KEY;
			break;
		case '1':		// Force PIO_ADV - DVR-A09
			opt_key = ADV109_KEY;
			break;
		case '2':		// Force GENERAL - DVR-109
			opt_key = GEN109_KEY;
			break;
		case '3':		// Force ASUS 1608P
			opt_key = ASUS09_KEY;
			break;
		case '4':
			opt_key = ASUSNL_KEY;
			break;
		case '5':		// Force PioDATA 109
			opt_key = DATA09_KEY;
			break;
		case '6':		// Force MEDION
			opt_key = MEDION_KEY;
			break;
		case '7':		// Force Buffalo
			opt_key = OEMEXT_KEY;
			break;
		case '8':		// Force OEM
			opt_key = OEMINT_KEY;
			break;
		case 'h':
		case '?':
		default:		// Unknown option
			opt_error++;
			break;
	}

	puts ("");
	puts ("DVRFlash v2.2 : Pioneer DVR firmware flasher");
	puts ("by Agent Smith, et al.,  November 2005");
	puts ("");

	if ( ((argc-optind) > 3) || opt_error)
	{
		puts ("usage: DVRFlash [-[f][k][s][v][y][#]] [device] [kernel] [general]");
		puts ("Most features are autodetected, but if you want to specify options:");
		puts ("If no device is given, DVRFlash will detect all DVR devices and exit");
		puts ("");
		puts ("                -f : force flashing - required if converting or flashing Kernel");
		puts ("                -k : put drive in Kernel mode");
		puts ("                -s : silent-mode - don't display disclaimer");
		puts ("                -v : verbose-mode - display more detail about drive");
		puts ("                -y : yes-mode - respond 'Y' to all prompts");
		puts ("                     STRONGLY NOT RECOMMENDED FOR NORMAL OPERATION");
		puts ("                -# : force alternate key for unknown DVR-109+ drives");
		puts ("                     0 = Universal Key (DVR-108)");
		puts ("                     1 = PIO-ADV");
		puts ("                     2 = GENERAL");
		puts ("                     3 = ASUS");
		puts ("                     4 = ASUS_NL");
		puts ("                     5 = PIODATA");
		puts ("                     6 = MEDION");
		puts ("                     7 = OEM_EXT");
		puts ("                     8 = OEM_INT");
		puts ("          -? or -h : display this help message.");
		puts ("");
		exit (1);
	}


	if (argv[optind] == NULL)
		detected= 0;

	// Who wants a disclaimer?
	if ((!opt_skip) && (detected) && (printDisclaimer()))
		ERR_EXIT;

	// New 1.2 - Display how we were called - 2.1 - Hide if interaction bypass is used
	if (!opt_yes)
	{
		printf("Commandline:\n  ");
		for (i=0; i<argc; i++)
			printf("%s ", argv[i]);
		printf("\n\n");
	}
	// Let's get started 
	scsi = newScsi();
	if (!scsi)
	{
		strncpy(str, "Internal error: newScsi() returned NULL.\n",42);
		if(opt_yes)
			printf("ERROR - %s",str);
		else
			fprintf(stderr, str);
		ERR_EXIT;
	}

	// First allocate our temporary buffer
    if ((mbuffer = (u8*) calloc(MODE_SIZE, 1)) == NULL)
	{
		strncpy(str, "Could not allocate mode buffer\n", 31);
		if(opt_yes)
			printf ("Error - %s", str);
		else
			fprintf (stderr, str);
        ERR_EXIT;
	}

	// New   1.6 - Call detection routine if no device is given
	if (!detected)
	{
		memset(devname, 0, sizeof(devname));
		printf("Device parameter was not given, detecting all DVR drives:\n");

		// Need to disable stderr on device detection for windows
		scsiSetErr(scsi, NULL_FD);
		for ( ; ; )
		{
			if (scsiReadName(scsi, devname, sizeof(devname)) < 0) break;
			if (!ProcessDevice(scsi, devname)) 
				detected++;
		}
		scsiSetErr(scsi, stderr);

		if (!opt_skip)
		{
			if (detected == 0)
				printf("\n  No DVR drive detected!\n");
			else
			{
				printf("\nNow run DVRFlash again, from the command prompt, using\n");
				printf("one of the device(s) listed above as first parameter\n");
			}
			// Take care of the stupid windows user who can't figure out what a commandline 
			// app is. If they double clicked, this'll keep the window open.
			if ( !opt_yes )
			{
				printf("\nPress the Return key to exit\n");
				FLUSHER;
			}
		}
		FREE_BUFFERS;
		exit(0);
	}

	// Copy device name 
	// Fixed 1.1 to allow both ASPI and SPTX on Windows
	// Fixed 1.2 - Some people don't know UPPER-f...ing-CASE!!!
	// Fixed 1.3 - On MacOS X, drive is selected by INQUIRY string.
//#if MACOSX
//	strncpy(devname, argv[optind], NAME_SIZE);
//	devname[NAME_SIZE-1] = 0;
//#else
	if ( (strlen(argv[optind]) == 2) && (argv[optind][1] == ':') )
	{	// Someone seems to be using a Windows drive letter, let's try the SPTX way
		if ( (argv[optind][0] >= 'a') && (argv[optind][0] <= 'z') )
			argv[optind][0] -= 0x20;
		if ( (argv[optind][0] < 'A') || (argv[optind][0] > 'Z') )
		{
			// NB, we could have used a #ifdef SPTX here, but the less #ifdef, the
			// more portable the code
			strncpy(str, "Illegal device name: ", 22);
			if(opt_yes)
				printf("ERROR - %s%s\n", str, argv[optind]);
			else
				fprintf(stderr, "%s%s\n", str, argv[optind]);
			ERR_EXIT;
		}
		strncpy (devname+4, argv[optind], 3);
	}
	else
		strncpy (devname, argv[optind], NAME_SIZE);

	devname[NAME_SIZE-1] = 0;
//#endif
	optind++;

	// Copy firmware name(s)
	for (i=0; i<(argc-optind); i++)
	{	
		strncpy (fname[i], argv[optind+i], NAME_SIZE);
		fname[i][NAME_SIZE-1] = 0;	// 2.1.1 -- Bug Fix.
		nb_firmwares++;
	}

	// calloc is handy to get everything set to 0
	if ( ( (fbuffer[0] = (u8*) calloc(MAX_SIZE, 1)) == NULL) ||
		 ( (fbuffer[1] = (u8*) calloc(MAX_SIZE, 1)) == NULL) )
	{
		strncpy(str, "Could not allocate buffers\n", 28);
		if(opt_yes)
			printf ("ERROR - %s", str);
		else
			fprintf (stderr, str);
        ERR_EXIT;
	}

	for (i=0; i<nb_firmwares; i++)
	{
		if ((fd = fopen (fname[i], "rb")) == NULL)
		{
			if (opt_verbose)
				perror ("fopen()");
			strncpy(str, "Can't open firmware file '", 27);
			if(opt_yes)
				printf ("ERROR - %s%s'\n", str, fname[i]);
			else
				fprintf (stderr, "%s%s'\n", str, fname[i]);
			ERR_EXIT;
		}
	
		// Read firmware
		if (opt_verbose)
			printf("Reading firmware '%s'...\n", fname[i]);
		read = fread (fbuffer[i], 1, MAX_SIZE, fd);
		if ((read > MAX_SIZE) || (read < A06K_SIZE))	// 2.1 - Just check if file size is within range.
		{
			if (opt_verbose)
				perror ("fread()");
			strncpy(str, "': Unexpected firmware size or read error\n", 43);
			if(opt_yes)
				printf("ERROR - '%s%s", fname[i], str);
			else
				fprintf(stderr, "'%s%s", fname[i], str);
		}
		if (!strncmp("Kernel", (char*)fbuffer[i]+0x110, 6))
		{
			ftype[i] = FTYPE_KERNEL;
			fsize[i] = read;
		}
		else if (!strncmp("Normal", (char*)fbuffer[i]+0x110, 6))
		{
			ftype[i] = FTYPE_NORMAL;
			fsize[i] = read;
		}
		else
		{
			strncpy(str, "': Invalid Pioneer firmware\n", 29);
			if(opt_yes)
				printf("ERROR - '%s%s", fname[i], str);
			else
				fprintf(stderr, "'%s%s", fname[i], str);
			ERR_EXIT;
		}
		if (opt_verbose)
		{
			printf("  firmware is of %s type ", (ftype[i]==FTYPE_KERNEL)?"Kernel":"Normal");
			printf("(%s %s)\n", (char*)fbuffer[i]+0x60, (char*)fbuffer[i]+0x90);
		}
		fclose (fd);
		fd = NULL;
	}

/*
 * Let's talk
 */
	if (scsiOpen(scsi, devname))
	{
		strncpy(str, "Could not open device ", 23);
		if(opt_yes)
			printf("ERROR - %s%s\n", str, devname);
		else
			fprintf(stderr, "%s%s\n", str, devname);
		ERR_EXIT;
	}

	scsiLimitSense(scsi, SENSE_LENGTH);
	scsiLimitSeconds(scsi, MAX_SECONDS, 0);

/*
 *	Standard Inquiry - Any device should answer that
 */
	memset(cdb,0x00,MAX_CDB_SIZE);
	cdb[0] = 0x12;	// Inquiry
	cdb[4] = 0x60;	// Retrieve $60 bytes of inquiry

	stat = scsiSay(scsi, (char*) cdb, 6, (char*) mbuffer, 0x60, X1_DATA_IN);
	if (stat < 0)
	{	// negative stat indicates an error
		// Fixed 1.1 - getsense() request caused a program crash on non existing devices
		strncpy(str, "Could not open device ", 23);
		if(opt_yes)
			printf("ERROR - %s%s\n", str, devname);
		else
			fprintf(stderr, "%s%s\n", str, devname);
		ERR_EXIT;
	}

	printf("\nDrive Information:\n");
	memset(&id, 0, sizeof(id));
	memcpy(id.Desc, mbuffer+8, 24);
	memcpy(id.Rev, mbuffer+32, 4);
	memcpy(id.Date, mbuffer+37, 8);
	memcpy(id.Maker, mbuffer+47, 9);

	printf("   Description : %s\n", id.Desc);
	printf("  Firmware Rev : %s\n", id.Rev);

	if ( stat || (id.Date[2] != '/') || (id.Date[5] != '/') )
	{	// We did not read 60 bytes or the date is not right - probably not a DVR
		printf("\nThis drive does not appear to be a Pioneer DVR drive - Aborting.\n");
		ERR_EXIT;
	}

	printf(" Firmware Date : %s\n", id.Date);
	printf("  Manufacturer : %s\n", id.Maker);

	if ( !strcmp(id.Rev, "0000") )
	{
		printf("! Drive is in kernel mode !\n\n");
		is_kernel = -1;
	}
	else
		printf("Drive is in normal mode.\n\n");

/*
 * Pioneer DVR specific inquiry
 */
	memset(cdb,0x00,MAX_CDB_SIZE);
	memset(mbuffer,0x00,MODE_SIZE);

	cdb[0] = 0x3C;	// Read Buffer
	cdb[1] = 0x02;
	cdb[2] = 0xF1;
	cdb[8] = 0x30;	// Retreive $30 bytes of inquiry

	stat = scsiSay(scsi, (char*) cdb, 10, (char*) mbuffer, 0x30, X1_DATA_IN);
	if (stat)
	{	
		getSense(scsi, "Could not get DVR specific information");
		ERR_EXIT;
	}

	memset(&idx, 0, sizeof(idx));
	memcpy(idx.Serial, mbuffer, 16);
	memcpy(idx.Interface, mbuffer+16, 4);
	memcpy(strGen, mbuffer+20, 4);
	strGen[4] = 0;
	idx.Generation = atoi(strGen);
	memcpy(idx.Kernel_Type, mbuffer+24, 8);
	memcpy(idx.Normal_Type, mbuffer+32, 8);
	
//	idx.Kernel_Size = A06K_SIZE;	//  2.1: Set later because of various firmware sizes for DVR-109
//	idx.Normal_Size = FIRM_SIZE;	
	
	memcpy(idx.Kernel_Rev, mbuffer+40, 4);

	if (opt_verbose)
	{
		printf("Additional Drive Information:\n");
		if (!is_kernel)
			printf(" Serial Number : %s\n", idx.Serial);
		printf("Interface type : %s\n", idx.Interface);
		printf("DVR generation : %04d\n", idx.Generation);
		printf("   Kernel type : %s\n", idx.Kernel_Type);
		if (!is_kernel)
			printf("   Normal type : %s\n", idx.Normal_Type);
		printf("Kernel version : %s\n\n", idx.Kernel_Rev);
		
		showRPC(scsi);

	}

/*
 * Is this a supported drive?   // 2.2: Added Support up to 110
 */
	if ( (strncmp(idx.Interface, "ATA ", 4)) || 
		 ( (idx.Generation != 1) && (idx.Generation != 3) && (idx.Generation != 4) 
		 && (idx.Generation != 6) && (idx.Generation != 7) && (idx.Generation != 8) 
		 && (idx.Generation != 9) && (idx.Generation != 10) && (idx.Generation != 11) 
		 && (idx.Generation != 52) && (idx.Generation != 53) && (idx.Generation != 54) 
		 && (idx.Generation != 60) && (idx.Generation != 62) && (idx.Generation != 64) 
		 && (idx.Generation != 66) ) )
	{
		strncpy(str, " is not supported by this utility - Aborting\n", 46);
		if(opt_yes)
			printf("ERROR - The %s%s", id.Desc, str);
		else
			fprintf(stderr, "The %s%s", id.Desc, str);
		ERR_EXIT;
	}

/*
 * Analyse the supplied firmwares
 */
	switch (nb_firmwares)
	{
	case 0:
		break;
	case 1:
		if (ftype[0] == FTYPE_UNDEFINED)
		{
			strncpy(str, "Spock gone crazy error 01\n", 27);
			if(opt_yes)
				printf("ERROR - %s", str);
			else
				fprintf(stderr, str);
			ERR_EXIT;
		}
		if (ftype[0] == FTYPE_KERNEL)	
		{
			strncpy(str, "Only Kernel firmware was supplied\n", 35);
			if(opt_yes)
				printf("ERROR - %s", str);
			else
				fprintf(stderr, str);
			if (!opt_force)
				ERR_EXIT
			else
			{
				strncpy(str, "Continue because force option was specified\n", 45);
				if(opt_yes)
					printf("ERROR - %s", str);
				else
					fprintf(stderr, str);
				kern_id = 0;
				idx.Kernel_Size = fsize[0];
				if(	(((idx.Generation <= 8) || ((idx.Generation >= 52) && (idx.Generation <= 62))) && (idx.Kernel_Size != A06K_SIZE)) || 
					((((idx.Generation >= 9) && (idx.Generation <= 11)) || (idx.Generation >= 64)) && (idx.Kernel_Size != A09K_SIZE))	)
				{
					strncpy(str, "Kernel file size mismatch\n", 27);
					if(opt_yes)
						printf("ERROR - %s", str);
					else
						fprintf(stderr, str);
					ERR_EXIT
				}
			}
		}
		else
		{
			norm_id = 0;
			idx.Normal_Size = fsize[0];
			if( (((idx.Generation <= 8) || ((idx.Generation >= 52) && (idx.Generation <= 62))) && (idx.Normal_Size != FIRM_SIZE)) || 
				((((idx.Generation >= 9) && (idx.Generation <= 11)) || (idx.Generation >= 64)) && (idx.Normal_Size < A09F_SIZE)) )
			{
				strncpy(str, "Firmware file size mismatch\n", 29);
				if(opt_yes)
					printf("ERROR - %s", str);
				else
					fprintf(stderr, str);
				ERR_EXIT
			}
		}
		
		if ( (strncmp((char*)fbuffer[0]+0x60, id.Desc, 24)) ||
			 (strncmp((char*)fbuffer[0]+0xd0, idx.Kernel_Type, 7)) )
		{
			strncpy(str, "Firmware and Drive type mismatch\n", 34);
			if(opt_yes)
				printf("ERROR - %s", str);
			else
				fprintf(stderr, str);
			if (opt_force < 2)
			{
				strncpy(str, "If you want to convert your drive, you need to supply both Kernel and General\n", 79);
				if(opt_yes)
					printf("ERROR - %s", str);
				else
					fprintf(stderr, str);
				ERR_EXIT
			}
			else
				strncpy(str, "Continue because superforce option was specified\n", 50);
				if(opt_yes)
					printf("ERROR - %s", str);
				else
					fprintf(stderr, str);
		}
		break;
	case 2:
		if ( (!((ftype[0] == FTYPE_KERNEL) && (ftype[1] == FTYPE_NORMAL))) &&
		     (!((ftype[1] == FTYPE_KERNEL) && (ftype[0] == FTYPE_NORMAL))) )
		{
			strncpy(str, "If you supply 2 firmwares, they must be of Kernel AND Normal types\n", 68);
			if(opt_yes)
				printf("ERROR - %s", str);
			else
				fprintf(stderr, str);
			ERR_EXIT;
		}
		// Now we are sure that we have one firmware of each
		if (ftype[0] == FTYPE_KERNEL)
		{
			kern_id = 0;
			idx.Kernel_Size = fsize[0];
			norm_id = 1;
			idx.Normal_Size = fsize[1];
		}
		else
		{
			kern_id = 1;
			idx.Kernel_Size = fsize[1];
			norm_id = 0;
			idx.Normal_Size = fsize[0];
		}
		if ( (strncmp((char*)fbuffer[0]+0x60, (char*)fbuffer[1]+0x60, 24)) ||
			 (strncmp((char*)fbuffer[0]+0xd0, (char*)fbuffer[1]+0xd0, 7)) )
		{
			strncpy(str, "The two firmware files supplied are not for the same drive!\n", 61);
			if(opt_yes)
				printf("ERROR - %s", str);
			else
				fprintf(stderr, str);
			ERR_EXIT
		}
		if ( (strncmp((char*)fbuffer[0]+0x60, id.Desc, 24)) ||
			 (strncmp((char*)fbuffer[0]+0xd0, idx.Kernel_Type, 7)) )
		{
			strncpy(str, "Firmwares and Drive type mismatch\n", 35);
			if(opt_yes)
				printf(str);
			else
				fprintf(stderr, str);
			if (!opt_force)
				ERR_EXIT
			else
			{
				strncpy(str, "Continue because force option was specified\n", 45);
				if(opt_yes)
					printf(str);
				else
					fprintf(stderr, str);
			}
		}
		if( (((idx.Generation <= 8) || ((idx.Generation >= 52) && (idx.Generation <= 62))) && (idx.Normal_Size != FIRM_SIZE) && (idx.Kernel_Size != A06K_SIZE)) || 
			((((idx.Generation >= 9) && (idx.Generation <= 11)) || (idx.Generation >= 64)) && (idx.Normal_Size < A09F_SIZE) && (idx.Kernel_Size != A09K_SIZE)) )
		{
			strncpy(str, "File size mismatch\n", 20);
			if(opt_yes)
				printf("ERROR - %s", str);
			else
				fprintf(stderr, str);
			ERR_EXIT
		}
		break;
	}

	if (nb_firmwares > 0)
	{	// Additional tests
		// 2.0 - XL and D mix and match
		// 2.1 - Only needed for DVR-108
		if (idx.Generation == 8 && opt_force < 2)
		{
			if ( (!strncmp(idx.Kernel_Type, "PIO_ADV",7)) ||
				(!strncmp((char*)fbuffer[0]+0xd0, "PIO_ADV", 7)) )
			{	 
				if ( ((!strncmp((char*)fbuffer[0]+0xd0, "PIO_ADV", 7)) &&
					(strncmp(idx.Kernel_Type, "PIO_ADV",7))) )
				{
					printf ("You are trying to flash an XL firmware onto what appears to be a \n");
					printf ("standard drive. Doing so can damage your drive: operation cancelled.\n");
					ERR_EXIT;
				}
				if ( ((strncmp((char*)fbuffer[0]+0xd0, "PIO_ADV", 7)) &&
					(!strncmp(idx.Kernel_Type, "PIO_ADV",7))) )
				{
					printf ("You are trying to flash an standard DVR firmware onto what appears to\n");
					printf ("be an XL drive. Doing so can damage your drive: operation cancelled.\n");
					ERR_EXIT;
				}
			}
		}
		// Check that hardware and firmware match
		if ( (strncmp(idx.Interface, (char*)fbuffer[0]+0xB0, 4)) ||
             (idx.Generation != atoi((char*)fbuffer[0]+0xB4)) )
		{
			strncpy(str, "WARNING: Hardware and Firmware really don't match!\n", 52);
			if(opt_yes)
				printf("ERROR - %s", str);
			else
				fprintf(stderr, str);
				
			if ( opt_force < 2 )
			{
				ERR_EXIT;
			} else
			{
				strncpy(str, "Continue because superforce option was specified\n", 50);
				if(opt_yes)
					printf("ERROR - %s", str);
				else
					fprintf(stderr, str);
			}
		}

		// Check if a media is present (if drive is NOT a DVR-103)
		if (!is_kernel && (idx.Generation != 1))
		{	// No way we can check this if we are already in Kernel
			memset(cdb,0x00,MAX_CDB_SIZE);
			// 1.3: cdb length must be 6 for TEST UNIT READY!
			stat = scsiSay(scsi, (char*) cdb, 6, NULL, 0, X0_DATA_NOT);
			// Sense must return Sense 02/3A/-- on Test Unit Ready
			if ((!stat) || ((getSense(scsi) & 0xFFFF00) != 0x023A00))
			{
				strncpy(str, "Please remove any media from this drive before flashing\n", 57);
				if(opt_yes)
					printf ("ERROR - %s", str);
				else
					fprintf (stderr, str);
				// Let's eject media while we're at it
				cdb[0] = 0x1B;
				cdb[4] = 0x02;
				scsiSay(scsi, (char*) cdb, 6, NULL, 0, X0_DATA_NOT);
				ERR_EXIT;
			}
			TickleDrive(scsi);
		}

		// Better safe than sorry
		if (opt_debug)
			printf("!!! DEBUG MODE !!! ");
		if (opt_yes)
			c = 'y';
		else
		{
			puts("Are you sure you want to flash this drive (y/n)?");
			c = (char) getchar();
			FLUSHER;
		}
		if ((char)(c | 0x20) !='y')
		{
			fprintf(stderr, "Operation cancelled by user.\n");
			ERR_EXIT;
		}
		puts("");
	}

/*
 * Get the drive into kernel mode
 */
	if ( ((nb_firmwares > 0) || (opt_kernel)) && (!is_kernel) )
	{
		printf("Switching drive to Kernel mode:\n");

		// Take care of the DVR-103/104 anti downgrade 
		if ((idx.Generation == 1) || (idx.Generation == 3))
		{
			i = Downgrade();
			switch (i)
			{
			case 0:	// Downgrade went fine
				break;
			case 1:
				strncpy(str, " Downgrade protection does not seem to exist on this drive ;)\n", 63);
				if(opt_yes)
					printf("    ERROR -%s", str);
				else
					fprintf(stderr,"   %s", str);
				break;
			default:
				strncpy(str, " Downgrade activation reported an error. Attempting to continue anyway.\n", 73);
				if(opt_yes)
					printf("    ERROR -%s", str);
				else
					fprintf(stderr,"   %s", str);
				break;
			}
		}

		// inquiry the drive until it reacts
		TickleDrive(scsi);

		// Send kernel command
		memset(cdb,0x00,MAX_CDB_SIZE);

		cdb[0] = 0x3B;	// Write Buffer
		cdb[1] = 0x04;
		cdb[2] = 0xFF;
		cdb[7] = 0x01;	// Send $100 bytes
	
		memset(mbuffer,0x00,MODE_SIZE);
		// Copy the Kernel data, including the key if required
		if (SetKern((char*) mbuffer, opt_key, idx.Generation, idx.Kernel_Type))
			ERR_EXIT;

		if (!opt_debug)
		{
		
			stat = scsiSay(scsi, (char*) cdb, 10, (char*) mbuffer, 0x100, X2_DATA_OUT);
			if (stat)
			{	// Let's try with Generic key
				printf(" Key failure. Trying Universal key.\n");
				writelong((u8*)mbuffer,0x10,UNIVERSAL_KEY);
				stat = scsiSay(scsi, (char*) cdb, 10, (char*) mbuffer, 0x100, X2_DATA_OUT);
				if (stat)
				{	// Stat has to be right this time
					getSense(scsi, "Could not set Kernel mode");
					ERR_EXIT;
				}
			}
		}

		memset(cdb,0x00,MAX_CDB_SIZE);
		memset(mbuffer,0x00,MODE_SIZE);

		// FIXME // On Windows, the DVR-103 can disappear from the device list just 
		//       // after switching to kernel mode - ouch!
		if (idx.Generation == 1)
		{
			// Wait 3 seconds apparently
			printf("Please wait... ");
			countdown(3);
			puts("");
		}

		cdb[0] = 0x12;	// Inquiry
		cdb[4] = 0x60;	// Retreive $60 bytes of inquiry

		stat = scsiSay(scsi, (char*) cdb, 6, (char*) mbuffer, 0x60, X1_DATA_IN);
		if (stat)
		{	// Stat has to be right this time
			getSense(scsi, "Could not get drive inquiry information");
			ERR_EXIT;
		}

		memset(&id, 0, sizeof(id));
		memcpy(id.Desc, mbuffer+8, 24);
		memcpy(id.Rev, mbuffer+32, 4);
		memcpy(id.Date, mbuffer+37, 8);
		memcpy(id.Maker, mbuffer+47, 9);

		if ( !strcmp(id.Rev, "0000") )
		{
			printf("   Description : %s\n", id.Desc);
			printf(" Firmware Rev. : %s\n", id.Rev);
			printf(" Firmware Date : %s\n", id.Date);
			printf("  Manufacturer : %s\n", id.Maker);
			printf("Drive is now in Kernel mode\n\n");
			is_kernel = -1;
			
		}
		else
		{
			strncpy(str, "Could not set drive to Kernel mode!\n\n", 38);
			if(opt_yes)
				printf("ERROR - %s", str);
			else
				fprintf(stderr, str);
			if (!opt_debug)
				ERR_EXIT;
		}
	}
	
/*
 * Flash the kernel		2.1: Modified to allow different firmware size
 */
	if ( (kern_id != -1) )
	{
		puts("Now sending the Kernel part:");

		// Send the kernel data
		for (i=0; i<idx.Kernel_Size; i+=0x8000)
		{
			memset(cdb,0x00,MAX_CDB_SIZE);

			cdb[0] = 0x3B;	// Write Buffer
			cdb[1] = 0x07;
			cdb[2] = 0xFE;
			cdb[3] = (i>>16)&0xFF;   // 2.1: Support for larger Kernel
			cdb[4] = (i>>8)&0xFF;
			cdb[7] = 0x80;	// Send $8000 bytes

			if (!opt_debug)
			{
				stat = scsiSay(scsi, (char*) cdb, 10, (char*) fbuffer[kern_id]+i, 0x8000, X2_DATA_OUT);
				if (stat)
				{	// Stat must be OK
					getSense(scsi,"Could not send Kernel data part ");
					printf("%i", i);
					ERR_EXIT;
				}
			}

			// Add some delay
			msleep(250);

		}
		msleep(100);
		printf("Now internal Kernel reflashing. Please wait... ");
		countdown(10);
		puts("");
	}

/*
 * Flash the normal part	2.1: Modified to allow different firmware size
 */
	if ( (norm_id != -1) )
	{
		puts("Now sending the Normal part:");

		// Send the firmware data
		for (i=0; i<idx.Normal_Size; i+=0x8000)
		{
			progressBar((float)i, idx.Normal_Size);
			memset(cdb,0x00,MAX_CDB_SIZE);

			cdb[0] = 0x3B;	// Write Buffer
			cdb[1] = 0x07;
			cdb[2] = 0xF0;
			cdb[3] = (i>>16)&0xFF;
			cdb[4] = (i>>8)&0xFF;
			cdb[7] = 0x80;	// Send $8000 bytes

			if (!opt_debug)
			{
				stat = scsiSay(scsi, (char*) cdb, 10, (char*) fbuffer[norm_id]+i, 0x8000, X2_DATA_OUT);
				if (stat)
				{	// Stat must be OK
					getSense(scsi,"Could not send Normal data");
					ERR_EXIT;
				}
			}

			// Add some delay
			msleep(100);

		}

		progressBar(1.0, 1.0);

		if (idx.Generation >= 4)
		{	// For some reason, on the 105/106/107 we need to wait after the Normal data has been sent
			puts("Please hold your breath for about 30 seconds...");
			puts("");
		}

		// Send the flash/restore to normal mode command
		memset(cdb,0x00,MAX_CDB_SIZE);

		cdb[0] = 0x3B;	// Write Buffer
		cdb[1] = 0x05;
		cdb[2] = 0xFF;
		cdb[7] = 0x01;	// Send $100 bytes
	
		memset(mbuffer,0x00,MODE_SIZE);
		// Copy the Kernel data, including the key if required
		if (SetKern((char*) mbuffer, opt_key, idx.Generation, idx.Kernel_Type))
			ERR_EXIT;

		if (!opt_debug)
		{
			stat = scsiSay(scsi, (char*) cdb, 10, (char*) mbuffer, 0x100, X2_DATA_OUT);
			if (stat)
			{	// Stat has to be right
				getSense(scsi,"Could not send the Flash command");
				ERR_EXIT;
			}
		}

		// Wait 10 seconds for internal reflashing
		printf("Now internal reflashing. Please wait... ");
		countdown(10);
		puts("");

		memset(cdb,0x00,MAX_CDB_SIZE);
		memset(mbuffer,0x00,MODE_SIZE);

		cdb[0] = 0x12;	// Inquiry
		cdb[4] = 0x60;	// Retreive $60 bytes of inquiry

		stat = scsiSay(scsi, (char*) cdb, 6, (char*) mbuffer, 0x60, X1_DATA_IN);
		if (stat)
		{	// Stat must be OK
			getSense(scsi, "Could not get drive inquiry information");
			ERR_EXIT;
		}

		printf("Updated Information:\n");
		memset(&id, 0, sizeof(id));
		memcpy(id.Desc, mbuffer+8, 24);
		memcpy(id.Rev, mbuffer+32, 4);
		memcpy(id.Date, mbuffer+37, 8);
		memcpy(id.Maker, mbuffer+47, 9);

		printf("   Description : %s\n", id.Desc);
		printf(" Firmware Rev. : %s\n", id.Rev);
		printf(" Firmware Date : %s\n", id.Date);
		printf("  Manufacturer : %s\n\n", id.Maker);

		if ( !strcmp(id.Rev, "0000") )
		{
			strncpy(str, "ERROR!!! Drive is still in Kernel mode!\n", 41);
			if(opt_yes)
				printf(str);
			else
				fprintf(stderr, str);
		}
		else
			puts("Flashing operation successful ;)\n");
	}

	scsiClose(scsi);
	FREE_BUFFERS;
	fflush(NULL);

	return 0;
}
