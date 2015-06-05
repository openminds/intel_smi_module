/*******************************************************************************

License:

    This code is licenced under the GPL.

Module Name:

    smi.h

Abstract:

    Header files having the data structures definitions to compile SMI driver.

Revision History
  Rev           Author              Date                Description
  ---           ----------          -----------         -----------------------
  1.0           mvaidya             11-Oct-2005         Initial revision.
  1.1           vsridha             19-Oct-2005         writing to SMI_CMD port
                                                        is one byte, waiting for
                                                        SMI operation to be 
                                                        completed and having 
                                                        SMI time out of 250 ms.
  1.2           vsridha		        09-Jan-2006         Removing timeout check
							                            of 250 msecs. It gives
							                            problem with 2.4 kernel.
  1.3           Yogindar            27-Sep-2006         HII support definitions
                                                        are defined.
*******************************************************************************/

#ifndef _SMI_H_
#define _SMI_H_
#define __NO_VERSION__
#define MAX_BUF_SIZE	4096

#pragma pack (1)
typedef unsigned char	    BYTE;
typedef unsigned char       UCHAR;

typedef	long			    LONG;
typedef unsigned long	    ULONG;
typedef short			    SHORT;
typedef unsigned short	    USHORT;
typedef int				    INT;
typedef unsigned int	    UINT;

typedef unsigned char       UINT8;
typedef short			    INT16;
typedef unsigned short	    UINT16;
typedef int				    INT32;
typedef unsigned int	    UINT32;

typedef UINT16				WORD;

typedef struct _EFI_GUID
{
	UINT32  Data1;
	UINT16  Data2;
	UINT16  Data3;
	UINT8   Data4[8];
} EFI_GUID;

typedef struct 
{
	EFI_GUID	VendorGuid;
	UINT32		VariableNameAddress;
	UINT32		Attributes;
	UINT32		DataSize;
	UINT32		DataAddress;
}GET_VARIABLE_SMI_PACKET;

typedef struct 
{
	EFI_GUID	VendorGuid;
	UINT32		VariableNameAddress;
	UINT32		VariableNameSize;
}GET_NEXT_VARIABLE_SMI_PACKET;


typedef struct 
{
	EFI_GUID	VendorGuid;
	UINT32		VariableNameAddress;
	UINT32		Attributes;
	UINT32		DataSize;
	UINT32		DataAddress;
}SET_VARIABLE_SMI_PACKET;

typedef struct 
{
	UINT32		dPhysicalSource;
	UINT32		dPhysicalDestination;
	UINT32		dSize;
}BIOS_COPY_SMI_PACKET;

typedef struct 
{
	EFI_GUID	DupGuid;
	USHORT		DupName[MAX_BUF_SIZE];
	UINT32		DupSize;
	UINT32		compCode;
}DUP_NEXT_PACKET;

typedef struct 
{
	EFI_GUID	DupGuid;
	USHORT		DupName[MAX_BUF_SIZE];
	UINT32		DupAttributes;
	UINT32		DupDataSize;
	UINT32		DupData[MAX_BUF_SIZE*2];
	UINT32		compCode;
}DUP_GET_PACKET;

typedef struct 
{
	EFI_GUID	DupGuid;
	USHORT		DupName[MAX_BUF_SIZE];
	UINT32		DupAttributes;
	UINT32		DupDataSize;
	UINT32		DupData[MAX_BUF_SIZE*2];
	UINT32		compCode;
}DUP_SET_PACKET;

typedef struct
{
	UINT32		offset;
	BYTE		compCode;
}DUP_HII_PACKET;

typedef struct 
{
	UINT8*		dDestination;
	UINT8*		dSource;
	UINT32		dSize;
	BYTE		compCode;
}DUP_COPY_PACKET;

#endif //_SMI_H_
