/**
 **  DVRFlash (based on Pisa/PLScsi)
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

// For the Sleep/usleep function
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#ifndef u8
#define u8 unsigned char
#endif

#ifndef u16
#define u16 unsigned short
#endif

#ifndef u32
#define u32 unsigned long
#endif

#define SENSE_LENGTH 0xE /* offsetof SK ASC ASCQ < xE */
#define SECONDS -1       /* negative means max */

#define FIRM_SIZE		0x00100000	// Standard Firmware size (Gen & Kern)
#define MODE_SIZE		0x00000100	// Switch mode buffer size
#define A06K_SIZE		0x00010000	// DVR 106 Kernel size

#define MAX_CDB_SIZE	16

// Firmware types
#define FTYPE_UNDEFINED	0
#define	FTYPE_KERNEL	1
#define FTYPE_NORMAL	2

// Handy macro for exiting. xbuffer or fd = NULL is no problemo 
// (except for lousy Visual C++, that will CRASH on fd = NULL!!!!)
#define FREE_BUFFERS	{free(fbuffer[0]); free(fbuffer[1]); free(mbuffer); free(cdb);}
#define ERR_EXIT		{FREE_BUFFERS; if (fd != NULL) fclose(fd); scsiClose(scsi); exit(1);}

// Drive indentification
typedef struct
{
	char Desc[25];
	char Rev[5];
	char Date[9];
	char Maker[10];
	char Serial[17];
	char Interface[5];
	char Generation[5];
	char Kernel_Type[9];
	char Normal_Type[9];
	char Kernel_Rev[5];
} Drive_ID;

// Kernel Key ID struct
typedef struct
{
	const char* dID;	// drive ID
	const char* kID;	// kernel ID
	const u32   key;	// key
} keyID;

keyID keyTable[] = {
	{ "PIONEER DVD-RW  DVR-105 ", "PIONEER  DVR-105", 0 },
	{ "PIONEER DVD-RW  DVR-106 ", "PIONEER  DVR-106", 0 },
	{ "PIONEER DVD-RW  DVR-106D", "PIONEER  DVR-106", 0x5B6A8FE9},
	{ "ASUS    DRW-0402P/D     ", "PIONEER  DVR-106", 0x2AD55699},
};


int opt_verbose = 0;
int opt_debug   = 0;

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
	puts("COMPANY RESELLING PIONEER EQUIPMENT AS THEIR OWN BRAND (OEM)");
	puts("");
	puts("IF YOU UNDERSTAND THE RISKS ASSOCIATED WITH THIS PROGRAM AND");
	puts("DISCHARGE BOTH THE AUTHOR AND PIONEER CORPORATION FROM ANY");
	puts("DAMAGE OCCURING AS THE RESULT OF ITS USE, PLEASE INDICATE SO");
	puts("BY ANSWERING THE FOLLOWING QUESTION:");
	puts("");
	puts("Do you understand and agree to the statement above (y/n)?");
	fflush(stdin);
	scanf("%c",&c);
	if ((c!='y') && (c!='Y'))
	{
		fprintf(stderr, "Operation cancelled by user.\n");
		return -1;
	}
	puts("");
	return 0;
}

/* The handy ones */
u32 readlong(u8* buffer, u32 addr)
{
	return ((((u32)buffer[addr+3])<<24) + (((u32)buffer[addr+2])<<16) +
		(((u32)buffer[addr+1])<<8) + ((u32)buffer[addr]));
}

void writelong(u8* buffer, u32 addr, u32 value)
{
	buffer[addr] = (u8)value;
	buffer[addr+1] = (u8)(value>>8);
	buffer[addr+2] = (u8)(value>>16);
	buffer[addr+3] = (u8)(value>>24);
}

/* Bye bye lousy associative C++ tables; hello good old sturdy C */
int findKey(char* ID)
{
	int i;
	for (i=0; i<(sizeof(keyTable)/sizeof(keyID)); i++)
		if (!strcmp(ID, keyTable[i].dID))
			return i;
	return -1;
}


/* It's easy to recognise which OSes never planned for multitasking... */
void msleep(int msecs)
{	// Can't respect conventions Bill?
#ifdef _WIN32
		Sleep(msecs);
#else
		usleep(1000*msecs);
#endif
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
	static char sense[20];
	u32	s = 0xFFFFFFFF;
	int stat = scsiGetSense(scsi, sense, SENSE_LENGTH, SENSE_LENGTH);
	if ((stat != SENSE_LENGTH) || (sense[0] < 0x70) || (sense[0] > 0x71))
	{
		if (errmsg)
			fprintf(stderr, "%s (No Sense)\n", errmsg);
		return s;
	}

	s = ((sense[2]&0x0F)<<16) + (sense[12]<<8) + sense[13];
	if (errmsg)
		fprintf(stderr, "%s (Sense: %02X %02X %02X)\n", errmsg,
			(s>>16)&0xFF, (s>>8)&0xFF, s&0xFF);
	return s;
}

/* Display a Progression Bar */
void progressBar(float current = 0.0, float max = 100.0)
{
	static char c;	// We need this variable to keep its value between calls
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

/* Here we go! */
int main (int argc, char *argv[])
{
	char devname[16] = "\\\\.\\I:";
	char fname[2][16];			// firmware name(s)
	int  ftype[2] = {FTYPE_UNDEFINED, FTYPE_UNDEFINED};
	int  kern_id		= -1;
	int  norm_id		= -1;
	u8   *fbuffer[2], *mbuffer, *cdb;
	Scsi *scsi; 
	Drive_ID id;

	// Flags
	int firmware_type 	= -1;	// Undefined by default
	int opt_error 		= 0;	// getopt
	int opt_force       = 0;
	int opt_kernel		= 0;	// Switch drive to kernel mode
	int is_kernel		= 0;	// is drive in kernel mode?
	int nb_firmwares	= 0;	// firmware file(s) provide 
	int skip_disclaimer = 0;

	// General purpose
	int i;
	char c;
	int stat;
	size_t read;
	FILE *fd = NULL;

/*
 * Init
 */
	setbuf (stdin, NULL);
	fbuffer[0] = NULL;
	fbuffer[1] = NULL;
	mbuffer    = NULL;
	cdb        = NULL;
	scsi       = NULL;

	while ((i = getopt (argc, argv, "bhfkvs")) != -1)
		switch (i)
	{
		case 'v':		// Print verbose messages
			opt_verbose = -1;
			break;
		case 'f':       // Force unscrambling
			opt_force++;
			break;
		case 'k':       // Force scrambling
			opt_kernel = -1;
			break;
		case 'b':       // Force scrambling
			opt_debug = -1;
			break;
		case 's':
			skip_disclaimer = -1;
			break;
		case 'h':
		default:		// Unknown option
			opt_error++;
			break;
	}

	puts ("");
	puts ("DVRFlash v0.9e : Pioneer DVR firmware flasher");
	puts("Coded by Agent Smith in the year 2003 with a little help from >NIL:");
	puts ("");

	if ( ((argc-optind) < 1) || ((argc-optind) > 3) || opt_error)
	{
		puts ("usage: DVRFlash [-f] [-k] [-v] device [kernel] [general]");
		puts ("Most features are autodetected, but if you want to specify options:");
		puts ("                -f : force flashing");
		puts ("                -k : put drive in Kernel mode");
		puts ("                -v : verbose");
//		puts ("                -r : reset device);
		puts ("");
		exit (1);
	}

/*
 * TO_DO 
 * - Check for 103/104
 * - Better structure for Kernel mode data and add brute force for 106 key
 */

	if ((!skip_disclaimer) && (printDisclaimer()))
		ERR_EXIT;

	// Copy device name
#ifdef SPTX
	if ( (strlen(argv[optind]) != 2) || (argv[optind][0] < 'C') || 
		 (argv[optind][0] > 'Z') || (argv[optind][1] != ':') )
	{
		fprintf(stderr, "Invalid device name. Device must be from C: to Z:\n");
		ERR_EXIT;
	}
	strncpy (devname+4, argv[optind], 3);
#else
	strncpy (devname, argv[optind], 15);
#endif
	devname[15] = 0;
	optind++;

    // Copy firmware name(s)
	for (i=0; i<(argc-optind); i++)
	{	
		strncpy (fname[i], argv[optind+i], 15);
		fname[i][15] = 0;
		nb_firmwares++;
	}

	// calloc is handy to get everything set to 0
	if ( ( (fbuffer[0] = (u8*) calloc(FIRM_SIZE, 1)) == NULL) ||
		 ( (fbuffer[1] = (u8*) calloc(FIRM_SIZE, 1)) == NULL) ||
         ( (mbuffer = (u8*) calloc(MODE_SIZE, 1)) == NULL) ||
		 ( (cdb = (u8*) calloc(MAX_CDB_SIZE, 1)) == NULL) )
	{
		fprintf (stderr, "Could not allocate buffers\n");
        ERR_EXIT;
	}

	for (i=0; i<nb_firmwares; i++)
	{
		if ((fd = fopen (fname[i], "rb")) == NULL)
		{
			if (opt_verbose)
				perror ("fopen()");
			fprintf (stderr, "Can't open firmware file '%s'\n", fname[i]);
			ERR_EXIT;
		}
	
		// Read firmware
		if (opt_verbose)
			printf("Reading firmware '%s'...\n", fname[i]);
		read = fread (fbuffer[i], 1, FIRM_SIZE, fd);
		if ((read != FIRM_SIZE) && (read != A06K_SIZE))
		{
			if (opt_verbose)
				perror ("fread()");
			fprintf(stderr, "'%s': Unexpected firmware size or read error\n", fname[i]);
		}
		if (!strncmp("Kernel", (char*)fbuffer[i]+0x110, 6))
			ftype[i] = FTYPE_KERNEL;
		else if (!strncmp("Normal", (char*)fbuffer[i]+0x110, 6))
			ftype[i] = FTYPE_NORMAL;
		else
		{
			fprintf(stderr, "'%s': Invalid Pioneer firmware\n", fname[i]);
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
	scsi = newScsi();

	if (scsiOpen(scsi, devname))
	{
		fprintf(stderr, "Could not open device %s\n", devname);
		ERR_EXIT;
	}

	(void) scsiLimitSense(scsi, SENSE_LENGTH);
	(void) scsiLimitSeconds(scsi, SECONDS, 0);

/*
 * General Inquiry - Any device should answer that
 */
	memset(cdb,0x00,MAX_CDB_SIZE);
	cdb[0] = 0x12;	// Inquiry
	cdb[4] = 0x60;	// Retreive $60 bytes of inquiry

	stat = scsiSay(scsi, (char*) cdb, 6, (char*) mbuffer, 0x60, X1_DATA_IN);
	if (stat < 0)
	{	// negative stat indicates an error
		getSense(scsi,"\nCould not get drive inquiry information");
		ERR_EXIT;
	}

	printf("\nDrive Information:\n");
	memset(&id, 0, sizeof(id));
	memcpy(id.Desc, mbuffer+8, 24);
	memcpy(id.Rev, mbuffer+32, 4);
	memcpy(id.Date, mbuffer+37, 8);
	memcpy(id.Maker, mbuffer+47, 9);

	printf("  Description    - %s\n", id.Desc);
	printf("  Firmware Rev.  - %s\n", id.Rev);

	if ( stat || (id.Date[2] != '/') || (id.Date[5] != '/') )
	{	// We did not read 60 bytes or the date is not right - probably not a DVR
		printf("\nThis drive does not appear to be a Pioneer DVR drive - Aborting.\n");
		ERR_EXIT;
	}

	printf("  Firmware Date  - %s\n", id.Date);
    printf("  Manufacturer   - %s\n", id.Maker);

	if ( (!strcmp(id.Date, "00/00/00")) && (!strcmp(id.Rev, "0000")) )
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

	memcpy(id.Serial, mbuffer, 16);
	memcpy(id.Interface, mbuffer+16, 4);
	memcpy(id.Generation, mbuffer+20, 4);
	memcpy(id.Kernel_Type, mbuffer+24, 8);
	memcpy(id.Normal_Type, mbuffer+32, 8);
	memcpy(id.Kernel_Rev, mbuffer+40, 4);

	if (opt_verbose)
	{
		printf("Additional Drive Information:\n");
		if (!is_kernel)
			printf("  Serial number  - %s\n", id.Serial);
		printf("  Interface type - %s\n", id.Interface);
		printf("  DVR generation - %s\n", id.Generation);
		printf("  Kernel type    - %s\n", id.Kernel_Type);
		if (!is_kernel)
			printf("  Normal type    - %s\n", id.Normal_Type);
		printf("  Kernel version - %s\n\n", id.Kernel_Rev);
	}

/*
 * Is this a supported drive?
 */
	if (findKey(id.Desc) == -1)
	{
		fprintf(stderr, "The %s is not supported yet - Aborting\n", id.Desc);
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
			fprintf(stderr,"Spock gone crazy error 01\n");
			ERR_EXIT;
		}
		if (ftype[0] == FTYPE_KERNEL)	
		{
			fprintf(stderr,"Only Kernel firmware was supplied\n");
			if (!opt_force)
				ERR_EXIT
			else
			{
				fprintf(stderr,"Continue because force option was specified\n");
				kern_id = 0;
			}
		}
		else
			norm_id = 0;
		if (strncmp((char*)fbuffer[0]+0x60, id.Desc, 24))
		{
			fprintf(stderr,"Firmware and Drive type mismatch\n");
			if (opt_force < 2)
			{
				fprintf(stderr,"If you want to convert your drive, you need to supply both Kernel and General\n");
				ERR_EXIT
			}
			else
				fprintf(stderr,"Continue because superforce option was specified\n");
		}
		break;
	case 2:
		if ( (!((ftype[0] == FTYPE_KERNEL) && (ftype[1] == FTYPE_NORMAL))) &&
		     (!((ftype[1] == FTYPE_KERNEL) && (ftype[0] == FTYPE_NORMAL))) )
		{
			fprintf(stderr,"If you supply 2 firmwares, they must be of Kernel AND Normal types\n");
			ERR_EXIT;
		}
		// Now we are sure that we have one firmware of each
		if (ftype[0] == FTYPE_KERNEL)
		{
			kern_id = 0;
			norm_id = 1;
		}
		else
		{
			kern_id = 1;
			norm_id = 0;
		}
		if (strncmp((char*)fbuffer[0]+0x60, (char*)fbuffer[1]+0x60, 24))
		{
			fprintf(stderr,"The two firmware files supplied are not for the same drive!\n");
			ERR_EXIT
		}
		if (strncmp((char*)fbuffer[0]+0x60, id.Desc, 24))
		{
			fprintf(stderr,"Firmwares and Drive type mismatch\n");
			if (!opt_force)
				ERR_EXIT
			else
				fprintf(stderr,"Continue because force option was specified\n");
		}
		break;
	}

	if (nb_firmwares > 0)
	{	// Additional tests
		if ( (strncmp(id.Interface, (char*)fbuffer[0]+0xB0, 4)) ||
             (strncmp(id.Generation, (char*)fbuffer[0]+0xB4, 4)) )
		{
			fprintf(stderr,"WARNING: Hardware and Firmware really don't match!\n");
			// Allow the infamous 105 <-> 106 conversion if superforce (-ff)
			if ( 
				 ( (!strncmp(id.Interface, "ATA ", 4)) && (!strncmp((char*)fbuffer[0]+0xB0, "ATA ", 4)) ) &&
				 (
				   ( (!strncmp(id.Generation, "0004", 4)) && (!strncmp((char*)fbuffer[0]+0xB4, "0006", 4)) ) ||
				   ( (!strncmp(id.Generation, "0006", 4)) && (!strncmp((char*)fbuffer[0]+0xB4, "0004", 4)) )
				 ) &&
				 (opt_force >= 2)
			   )
			{
				printf("This is your last chance to cancel...\n");
			}
			else
				ERR_EXIT;
		}

		// Check if a media is present
		if (!is_kernel)
		{	// No way we can check this if we are already in Kernel
			memset(cdb,0x00,MAX_CDB_SIZE);
			stat = scsiSay(scsi, (char*) cdb, 12, NULL, 0, X0_DATA_NOT);
			// Sense must return Sense 02/3A/-- on Test Unit Ready
			if ((!stat) || ((getSense(scsi) & 0xFFFF00) != 0x023A00))
			{
				fprintf (stderr, "Please remove any media from this drive before flashing\n");
				// Let's eject media while we're at it
				cdb[0] = 0x1B;
				cdb[4] = 0x02;
				stat = scsiSay(scsi, (char*) cdb, 6, NULL, 0, X0_DATA_NOT);
				ERR_EXIT;
			}
		}

		// Better safe than sorry
		if (opt_debug)
			printf("!!! DEBUG MODE !!! ");
		fflush(stdin);
		puts("Are you sure you want to flash this drive (y/n)?");
		scanf("%c",&c);
		if ((c!='y') && (c!='Y'))
		{
			fprintf(stderr, "Operation cancelled by user.\n");
			ERR_EXIT;
		}
		puts("");
	}


/*
 * Get the drive into kernel mode
 */
	if ( ((nb_firmwares > 0) || opt_kernel) && (!is_kernel) )
	{
		memset(cdb,0x00,MAX_CDB_SIZE);

		cdb[0] = 0x3B;	// Write Buffer
		cdb[1] = 0x04;
		cdb[2] = 0xFF;
		cdb[7] = 0x01;	// Send $100 bytes
	
		memset(mbuffer,0x00,MODE_SIZE);
		// Copy the Kernel key data
		i = findKey(id.Desc);	// We tested validity above
		strcpy((char*)mbuffer,keyTable[i].kID);
		writelong(mbuffer,0x10,keyTable[i].key);

		if (!opt_debug)
		{
			stat = scsiSay(scsi, (char*) cdb, 10, (char*) mbuffer, 0x100, X2_DATA_OUT);
			if (stat)
			{	// Stat has to be right this time
				getSense(scsi,"Could not set to Kernel mode");
				ERR_EXIT;
			}
		}

		memset(cdb,0x00,MAX_CDB_SIZE);
		memset(mbuffer,0x00,MODE_SIZE);

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


		if ( (!strcmp(id.Date, "00/00/00")) && (!strcmp(id.Rev, "0000")) )
		{
			printf("Drive is in kernel mode:\n");
			printf("  Description    - %s\n", id.Desc);
			printf("  Firmware Rev.  - %s\n", id.Rev);
			printf("  Firmware Date  - %s\n", id.Date);
			printf("  Manufacturer   - %s\n\n", id.Maker);
			is_kernel = -1;
		}
		else
		{
			fprintf(stderr,"Could not set the drive to Kernel mode!\n\n");
			if (!opt_debug)
				ERR_EXIT;
		}
	}
	
/*
 * Flash the kernel
 */
	if (kern_id != -1)
	{
		puts("Now sending the Kernel part...");
		memset(cdb,0x00,MAX_CDB_SIZE);

		cdb[0] = 0x3B;	// Write Buffer
		cdb[1] = 0x07;
		cdb[2] = 0xFE;
		cdb[7] = 0x80;	// Send $8000 bytes

		if (!opt_debug)
		{
			stat = scsiSay(scsi, (char*) cdb, 10, (char*) fbuffer[kern_id], 0x8000, X2_DATA_OUT);
			if (stat)
			{	// Stat must be OK
				getSense(scsi,"Could not send first part of Kernel data");
				ERR_EXIT;
			}
		}

		msleep(250);

		memset(cdb,0x00,MAX_CDB_SIZE);

		cdb[0] = 0x3B;	// Write Buffer
		cdb[1] = 0x07;
		cdb[2] = 0xFE;
		cdb[4] = 0x80;	// Offset = $8000
		cdb[7] = 0x80;	// Send $8000 bytes

		if (!opt_debug)
		{
			stat = scsiSay(scsi, (char*) cdb, 10, (char*) fbuffer[kern_id]+0x8000, 0x8000, X2_DATA_OUT);
			if (stat)
			{	// Stat must be OK
				getSense(scsi,"Could not send second part of Kernel data");
				ERR_EXIT;
			}
		}

		printf("Now internal Kernel reflashing. Please wait... ");
		countdown(5);
		puts("");
	}

/*
 * Flash the general
 */
	if (norm_id != -1)
	{
		puts("Now sending the Normal part:");

		// Send the firmware data
		for (i=0; i<FIRM_SIZE; i+=0x8000)
		{
			progressBar((float)i, FIRM_SIZE);
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
		puts("Please hold your breath for about 20 seconds...");
		// Fort some reason we need to wait after the Normal data has been sent
//		printf("\nPlease hold your breath... ");
//		countdown(15);
		puts("");

		// Send the flash command
		memset(cdb,0x00,MAX_CDB_SIZE);

		cdb[0] = 0x3B;	// Write Buffer
		cdb[1] = 0x05;
		cdb[2] = 0xFF;
		cdb[7] = 0x01;	// Send $100 bytes
	
		memset(mbuffer,0x00,MODE_SIZE);
		if ((i = findKey(id.Desc)) != -1)
		{
			strcpy((char*)mbuffer,keyTable[i].kID);
			// The following is not required for getting out of Kernel mode ... yet
			writelong(mbuffer,0x10,keyTable[i].key);
		}
		else
		{
			fprintf(stderr, "No key ID is defined for %s\n", id.Desc);
			fprintf(stderr, "Could not send the Flash command - Aborting\n");
			ERR_EXIT;
		}

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

		printf("  Description    - %s\n", id.Desc);
		printf("  Firmware Rev.  - %s\n", id.Rev);
		printf("  Firmware Date  - %s\n", id.Date);
	    printf("  Manufacturer   - %s\n", id.Maker);

		if ( (!strcmp(id.Date, "00/00/00")) && (!strcmp(id.Rev, "0000")) )
		{
			fprintf(stderr,"ERROR!!! Drive is still in Kernel mode!\n");
		}
		else
			puts("Flashing operation successful ;)");
	}

	scsiClose(scsi);

	FREE_BUFFERS;

	return 0;
}
