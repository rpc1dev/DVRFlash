#include "pseudoplscsi.h"

// This file is for OS X ONLY!!!!

/*
 *	This is stripped down implementation of plscsi, limited to
 *	functions used for DVRFlash.
 */

static bool match(Scsi *scsi, const char *name);
static UInt32 DoCommand(Scsi *scsi, UInt8 *cdb, unsigned long cdblength, int direction, UInt8 *buffer, unsigned long bufferlength, unsigned long timeout, UInt32 *actuallength);

static UInt8 CDBLength[256] =
	{
//	 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
	 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,	//  0	000
	 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,	//  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,	//  2	001
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,	//  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,	//  4	010
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,	//  5
	99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,	//  6   011
	99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,98,	//  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,	//  8	100
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,	//  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,	//  a	101
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,	//  b
	99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,	//  c   110
	99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,	//  d
	99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,	//  e   111
	99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99 	//  f
	};



/* ------------------------------------ */
bool match(Scsi *scsi, const char *name)
{
UInt32 sns;
UInt8 cdb[6];
UInt8 buf[255];

cdb[0] = 0x12;
cdb[1] = 0x00;
cdb[2] = 0x00;
cdb[3] = 0x00;
cdb[4] = sizeof(buf);
cdb[5] = 0x00;

sns = DoCommand(scsi, cdb, 6, X1_DATA_IN, buf, sizeof(buf), 0, NULL);

if (sns) return 0;
if (strncmp((char *) buf+8, name, strlen(name))) return 0;
return 1;
}



/* ------------------------------------ */
UInt32 DoCommand(Scsi *scsi, UInt8 *cdb, unsigned long cdblength, int direction, UInt8 *buffer, unsigned long bufferlength, unsigned long timeout, UInt32 *actuallength)
{
IOVirtualRange range;
SCSITaskStatus taskstatus;
IOReturn ior;
int DataTransferDirection;
UInt64 count;
UInt8 officialcdblength;

if (!scsi) return 0xffffffff;
if (!scsi->Device) return 0xffffffff;

memset(&scsi->sense, 0, sizeof(scsi->sense));

officialcdblength = CDBLength[* (UInt8 *) cdb];
switch (officialcdblength)
	{
case 99:	// reserved or vendor specific, assume it's OK.
	officialcdblength = cdblength;
	break;
case 98:	// special case for the 7F command.
	officialcdblength = ( (UInt8 *) cdb)[7] + 8;
	officialcdblength = (officialcdblength + 3) & ~3;
	break;
default:	// official length properly set
	break;
	}

if (officialcdblength != cdblength)
	{ // probably a programming error, so warn the user!!!
	int i;
	printf("=== PLEASE REPORT FOLLOWING ERROR ===\n");
	printf("CDB length should be %d instead of %d:\n", officialcdblength, cdblength);
	cdblength = officialcdblength;
	for (i= 0; i < cdblength; i++)
		{
		printf("%02X ",( (UInt8 *) cdb)[i]);
		}
	printf("\n");
	}


switch(direction)
	{
	case X1_DATA_IN:  DataTransferDirection = kSCSIDataTransfer_FromTargetToInitiator; break;
	case X2_DATA_OUT: DataTransferDirection = kSCSIDataTransfer_FromInitiatorToTarget; break;
	default: DataTransferDirection = kSCSIDataTransfer_NoDataTransfer; bufferlength = 0; break;
	}

range.address = (IOVirtualAddress) buffer;
range.length = bufferlength;

ior = (*scsi->Device)->SetCommandDescriptorBlock(scsi->Device, cdb, cdblength);
if (ior != kIOReturnSuccess) return 0xFFFFFFFF;

ior = (*scsi->Device)->SetScatterGatherEntries(scsi->Device, &range, 1, bufferlength, DataTransferDirection);
if (ior != kIOReturnSuccess) return 0xFFFFFFFF;


ior = (*scsi->Device)->SetTimeoutDuration(scsi->Device, timeout ? timeout : 10000);
if (ior != kIOReturnSuccess) return 0xFFFFFFFF;

ior = (*scsi->Device)->ExecuteTaskSync(scsi->Device, &scsi->sense, &taskstatus, &count);
if (actuallength) *actuallength = count & 0xFFFFFFFF;
if ((ior == kIOReturnSuccess) && (taskstatus == kSCSITaskStatus_CHECK_CONDITION))
	{
	return ((scsi->sense.SENSE_KEY & 0x0F) << 16) | (scsi->sense.ADDITIONAL_SENSE_CODE << 8) | scsi->sense.ADDITIONAL_SENSE_CODE_QUALIFIER;
	}
if (ior != kIOReturnSuccess) return 0xFFFFFFFF;

return 0x00000000;
}



/* ------------------------------------ */
Scsi *newScsi(void)
{
Scsi *x;

x = (Scsi *) malloc(sizeof(Scsi));
if (!x) return x;

x->Timeout = 30000;
x->SenseLength = 18;
x->Device = NULL;

return x;
}



/* ------------------------------------ */
bool scsiOpen(Scsi *scsi, const char *name)
{
mach_port_t master;
io_iterator_t iterator;
io_object_t object;
IOReturn ior;
CFMutableDictionaryRef dictionary;
CFMutableDictionaryRef subdictionary;
MMCDeviceInterface **mmcinterface;
IOCFPlugInInterface **plugininterface;
SCSITaskDeviceInterface **scsitaskinterface;
SCSITaskInterface **task;
SInt32 score;
int count;

// The name of the device to open is an inquiry string.
// This inquiry string is as such:
//  2 optional prefix chars to specify the matching drive number
// 16 chars are the vendor id
//  8 chars are the product id
//  4 chars are the firmware revision
// The string may be truncated to any length.
// To distinguish between similar drives, the inquiry string may be prefixed by
//  a character from 0 to 9 and A to Z (case insensitive), then a colon ':'
//  to select respectively the 1st to 10th, 11th to 36th matching device.
// Note that OS X has no known way to predict device ordering (and it may vary
//  with device (un)plugging).
// Example: "0:PIONEER DVD-RW  DVR-107D1.13"
//           n:vvvvvvvvvvvvvvvvddddddddffff
// n : drive number
// v : vendor identification
// d : device identification
// f : firmware identification
// This string will match the 1st PIONEER DVD-RW, model DVR-107D, revision 1.13 drive

if (!scsi) return -1;

scsiClose(scsi);

count = 0;
if ( (strlen(name) > 2) && (name[1] == ':'))
	{
	     if ( (name[0] >= '0') && (name[0] <= '9')) { count = name[0] - '0'; name += 2; }
	else if ( (name[0] >= 'a') && (name[0] <= 'z')) { count = name[0] - 'a' + 10; name += 2; }
	else if ( (name[0] >= 'A') && (name[0] <= 'Z')) { count = name[0] - 'A' + 10; name += 2; }
	else count = 0; // illegal syntax, ignore prefix.
	}

ior = IOMasterPort(bootstrap_port, &master);
if ( (ior != kIOReturnSuccess) || (master == NULL) )
	{
	fprintf(stderr, "Couln't get a master IOKit port.\n");
	return -1;
	}


// First, try finding matching authoring devices
dictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
if (dictionary == NULL)
	{
	fprintf(stderr, "Couln't create dictionary.\n");
	return -1;
	}

subdictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
if (subdictionary == NULL)
	{
	fprintf(stderr, "Couln't create dictionary.\n");
	return -1;
	}

CFDictionarySetValue(subdictionary, CFSTR(kIOPropertySCSITaskDeviceCategory), CFSTR(kIOPropertySCSITaskAuthoringDevice) );
CFDictionarySetValue(dictionary, CFSTR(kIOPropertyMatchKey), subdictionary);

ior = IOServiceGetMatchingServices(master, dictionary, &iterator);

if (iterator != NULL)
	{
	while ( (object = IOIteratorNext(iterator)) )
		{
		ior = IOCreatePlugInInterfaceForService(object, kIOMMCDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugininterface ,&score);
		if (ior == kIOReturnSuccess)
			{
			ior = (*plugininterface)->QueryInterface(plugininterface, CFUUIDGetUUIDBytes(kIOMMCDeviceInterfaceID), (LPVOID *) &mmcinterface);
			if (ior == kIOReturnSuccess)
				{
				scsitaskinterface = (*mmcinterface)->GetSCSITaskDeviceInterface(mmcinterface);
				if (scsitaskinterface != NULL)
					{
					ior = (*scsitaskinterface)->ObtainExclusiveAccess(scsitaskinterface);
					if (ior == kIOReturnSuccess)
						{
						task = (*scsitaskinterface)->CreateSCSITask(scsitaskinterface);
						scsi->Device = task;
						if (!match(scsi, name) || (count--))
							{
							(*task)->Release(task);
							scsi->Device = NULL;
							}
						}
					}
				(*scsitaskinterface)->Release(scsitaskinterface);
				}
			(*plugininterface)->Release(plugininterface);
			}
		IOObjectRelease(object);
		if (scsi->Device) break; // A matching device was found, stop searching for one.
		}
	IOObjectRelease(iterator);
	}
if (scsi->Device) return 0;

// Second, try finding matching non-authoring devices
dictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
if (dictionary == NULL)
	{
	fprintf(stderr, "Couln't create dictionary.\n");
	return -1;
	}

subdictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
if (subdictionary == NULL)
	{
	fprintf(stderr, "Couln't create dictionary.\n");
	return -1;
	}

CFDictionarySetValue(subdictionary, CFSTR(kIOPropertySCSITaskDeviceCategory), CFSTR(kIOPropertySCSITaskUserClientDevice) );
CFDictionarySetValue(dictionary, CFSTR(kIOPropertyMatchKey), subdictionary);

ior = IOServiceGetMatchingServices(master, dictionary, &iterator);

if (iterator != NULL)
	{
	while ( (object = IOIteratorNext(iterator)) )
		{
		ior = IOCreatePlugInInterfaceForService(object, kIOSCSITaskDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugininterface ,&score);
		if (ior == kIOReturnSuccess)
			{
			ior = (*plugininterface)->QueryInterface(plugininterface, CFUUIDGetUUIDBytes(kIOSCSITaskDeviceInterfaceID), (LPVOID *) &scsitaskinterface);
			if (ior == kIOReturnSuccess)
				{
				ior = (*scsitaskinterface)->ObtainExclusiveAccess(scsitaskinterface);
				if (ior == kIOReturnSuccess)
					{
					task = (*scsitaskinterface)->CreateSCSITask(scsitaskinterface);
					scsi->Device = task;
					if (!match(scsi, name) || (count--))
						{
						(*task)->Release (task);
						scsi->Device = NULL;
						}
					}
				(*scsitaskinterface)->Release(scsitaskinterface);
				}
			(*plugininterface)->Release(plugininterface);
			}
		IOObjectRelease(object);
		if (scsi->Device) break; // A matching device was found, stop searching for one.
		}
	IOObjectRelease(iterator);
	}
if (scsi->Device) return 0;

return -1;
}



/* ------------------------------------ */
void scsiClose(Scsi *scsi)
{
if (!scsi) return;
if (scsi->Device) (*scsi->Device)->Release(scsi->Device);

scsi->Device = NULL;
}



/* ------------------------------------ */
void scsiLimitSense(Scsi *scsi, unsigned long length)
{
if (scsi) scsi->SenseLength = length;
}



/* ------------------------------------ */
void scsiLimitSeconds(Scsi *scsi, unsigned long seconds, int unknown)
{
if (scsi) scsi->Timeout = seconds * 1000;
}



/* ------------------------------------ */
int scsiSay(Scsi *scsi, const char *cdb, unsigned long cdblen, char *buffer, unsigned long buflen, int dir)
{
UInt32 actual = buflen;
UInt32 sns;

if (scsi && scsi->Device)
	sns = DoCommand(scsi, (UInt8 *) cdb, cdblen, dir, (UInt8 *) buffer, buflen, scsi->Timeout, &actual);
if (((dir == X1_DATA_IN) || (dir == X2_DATA_OUT)) && (actual != buflen)) return -1;

if (sns) return -1;

return 0;
}



/* ------------------------------------ */
int scsiGetSense(Scsi *scsi, char *buffer, int charsLength, int elseLength)
{

if (!scsi) return 0;
if (!scsi->Device) return 0;

memcpy(buffer, (char *) &scsi->sense, charsLength);

return charsLength;
}
