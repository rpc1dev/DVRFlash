// This file is for OS X ONLY!!!!

/*
 *	This is stripped down implementation of plscsi, limited to
 *	functions used for DVRFlash.
 */

#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#if 0 // < 10.3
#include <IOKit/cdb/IOSCSILib.h>
#include <IOKit/cdb/IOCDBLib.h>
#include <IOKit/scsi-commands/SCSITaskLib.h>
#else // >= 10.3
#include <IOKit/scsi/SCSITaskLib.h>
#endif
#include <CoreFoundation/CoreFoundation.h>

enum {X0_DATA_NOT, X1_DATA_IN, X2_DATA_OUT};

typedef struct {
	unsigned long Timeout;
	unsigned long SenseLength;
	SCSITaskInterface **Device;
	SCSI_Sense_Data sense;		/* each IO operations gets its own sense */
	} Scsi;

Scsi *newScsi(void);
bool scsiOpen(Scsi *scsi, const char *name);
void scsiClose(Scsi *scsi);
void scsiLimitSense(Scsi *scsi, unsigned long length);
void scsiLimitSeconds(Scsi *scsi, unsigned long seconds, int unknown);
int scsiSay(Scsi *scsi, const char *cdb, unsigned long cdblen, char *buffer, unsigned long buflen, int dir);
int scsiGetSense(Scsi *scsi, char *buffer, int charsLength, int elseLength);
