/*******************************************************************************

License:

    This code is licenced under the GPL.

Module Name:

    smi.c

Abstract:

    SMI driver has the IOCTL calls for changing the BIOS variables.

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
  1.3           Yogindar            27-Sep-2006         Added support for em64T
                                                        only for Get,GetNext 
                                                        and Set Handlers and 
                                                        Added support for reading/
                                                        setting BIOS variables
                                                        thourgh EFI HII 
                                                        interface.
   1.4          Yogindar	        23-Mar-2007	        Handled compat_ioctl for
                                                        x86_64 kernel versions >
                                                        2.6.12. 32 bit 
							                            conversion functions 
							                            are depricated from 
							                            2.6.12 onwards.
*******************************************************************************/

/* includes */
#include <linux/module.h> /* Needed by all modules */
#include <linux/kernel.h> /* Needed for KERN_ALERT */
#include <linux/init.h> /* Needed for the macros */
#include <linux/ioport.h>
#include <linux/fs.h>
// #include <linux/smp_lock.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
/*#include <linux/semaphore.h>*/
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/version.h>
/* request for memory below 4 GB on x86_64 kernel */
#if defined (__x86_64__)
#define KMALLOC_FLAGS GFP_ATOMIC | GFP_DMA
/* ioctl32 conversion functions depricated */
/* Use config compat */
#if((LINUX_VERSION_CODE) < (KERNEL_VERSION(2,6,12)))
#include <linux/ioctl32.h>
#else
#ifndef CONFIG_COMPAT
#warning "32 bit binaries cannot use this driver!!!\n"
#else
#define USE_CONFIG_COMPAT 1
#endif
#endif
#else
#define KMALLOC_FLAGS GFP_ATOMIC
#endif

#include "smi.h" 

/* 
 * The name for our device, as it will appear in 
 * /proc/devices 
 */
#define DEVICE_NAME "smi"

/* The name of the device file */
#define DEVICE_FILE_NAME "/dev/smi"

/* 
 * The major device number. We can't rely on dynamic 
 * registration any more, because ioctls need to know 
 * it. 
 */
#define MAJOR_NUM 0

#define SUCCESS 0

/* Error codes */
#define ERR_MEM_ALLOC		    -1
#define ERR_CPY_TO_USR		    -2
#define ERR_CPY_TO_KER		    -3
#define ERR_DATA_NOT_COPIED 	-4
#define ERR_DATA_NOT_READY  	-5
#define ERR_DRV_UNREG		    -6
#define ERR_SMI_FAILED		    -7

/* IOCTL command codes */
#define GET_VAR			        1
#define GET_NEXT		        2
#define SET_VAR			        3
#define GET_VAR_READ		    4
#define GET_NEXT_VAR_READ	    5
#define SET_VAR_READ            6
#define SET_PORT                7 
//HII Changes - Begin
#define EXPORT_HII			    8
#define BIOS_COPY               9
#define BIOS_COPY_READ          10
//HII Changes - End


/* Global variables */
static unsigned int smi_major = 0;
static int smi_register_chrdev_success = 0;
volatile BYTE flag; 
static UINT16 smiport = 0;

static GET_NEXT_VARIABLE_SMI_PACKET *pNextPacket = NULL;
static GET_NEXT_VARIABLE_SMI_PACKET *pPhy_NextPacket = NULL;

static GET_VARIABLE_SMI_PACKET *pGetPacket = NULL;
static GET_VARIABLE_SMI_PACKET *pPhy_GetPacket = NULL;

static SET_VARIABLE_SMI_PACKET *pSetPacket = NULL;
static SET_VARIABLE_SMI_PACKET *pPhy_SetPacket = NULL;

//HII Changes - Begin
static BIOS_COPY_SMI_PACKET *pCopyPacket = NULL;
static BIOS_COPY_SMI_PACKET *pPhy_CopyPacket = NULL;
//HII Changes - End

static DUP_GET_PACKET *pDupGetPacket = NULL;
static DUP_NEXT_PACKET *pDupNextPacket = NULL;
static DUP_SET_PACKET *pDupSetPacket = NULL;
//HII Changes - Begin
static DUP_HII_PACKET *pDupHiiPacket = NULL;
static DUP_COPY_PACKET *pDupCopyPacket = NULL;
//HII Changes - End
static UINT32 result = 0;

//HII Changes - Begin
 UINT32 ebxreg = 0;
 UINT32 edxreg = 0;
static UINT8* dDestination = 0;
//HII Changes - End

/******************************************************************************
 * Function     : device_open
 * Description  : This function is called whenever a process attempts 
 *                to open the device file 
 * Parameters   : inode - inode structure passed by Kernel
 *                filp  - file structure passed by Kernel
 * Return value : Always returns 0.
 *****************************************************************************/
int device_open(struct inode *inode, struct file *filp)
{
	return SUCCESS;
}

/******************************************************************************
 * Function     : device_release
 * Description  : This function is called when a process closes the 
 *                device file. Frees up all the allocated memory.
 * Parameters   : inode - inode structure passed by Kernel
 *                filp  - file structure passed by Kernel
 * Return value : Always returns 0.
 *****************************************************************************/
int device_release(struct inode *inode, struct file *filp)
{
	/* Free all memory */
	if(pDupNextPacket)
	{
		kfree(pDupNextPacket);
		pDupNextPacket = NULL;
	}
	if(pDupGetPacket)
	{
		kfree(pDupGetPacket);
		pDupGetPacket = NULL;
	}
	if(pDupSetPacket)
	{
		kfree(pDupSetPacket);
		pDupSetPacket = NULL;
	}
	
	if(pNextPacket)
	{
		kfree(pNextPacket);
		pNextPacket = NULL;		
	}
	if(pGetPacket)
	{
		kfree(pGetPacket);
		pGetPacket = NULL;		
	}
	if(pSetPacket)
	{
		kfree(pSetPacket);
		pSetPacket = NULL;		
	}
	return SUCCESS;
}/* device_release ends */

/******************************************************************************
 * Function     : device_ioctl
 * Description  : This function is called whenever a process tries to 
 *                do an ioctl on our device file. 
 * Parameters   : inode - inode structure passed by Kernel
 *                filp  - file structure passed by Kernel
 *                cmd   - The number of the ioctl.
 *                arg   - The parameter to it. 
 * Return value : Returns one of the following values SUCCESS, ERR_MEM_ALLOC
 *                ERR_DATA_NOT_COPIED, ERR_CPY_TO_KER, ERR_DATA_NOT_READY,
 *                ERR_CPY_TO_USR
 *****************************************************************************/
int device_ioctl(struct file *filp, unsigned int cmd, 
             unsigned long arg)
{
	//int i;
	
switch(cmd)
{
	case SET_PORT:
    {
        /* 
         * SET_PORT IOCTL call is used to set the smi port number.
         * It is the responsibility of the calling process to set the 
         * smi port value 
         */
        smiport = (UINT16) arg;
        break;
    }
	
	case GET_VAR:
	{

        /* 
         * GET_VAR IOCTL call is used to get the EFI variable attribute and 
         * data. The data should be read by the program using GET_VAR_READ
         * IOCTL call.
         */

        /* 
         * All memory allocations only if pointers are null
		 * else return error as data is ready for read, 
		 * unless it is read we cannot free it.
         */
		
		if((pDupGetPacket == NULL) && (pGetPacket == NULL))
		{
			/* Allocate memory in Kernel space */
			pDupGetPacket = (DUP_GET_PACKET *)
				kmalloc(sizeof(DUP_GET_PACKET), KMALLOC_FLAGS);
			
			if(pDupGetPacket == NULL)
			{
				return ERR_MEM_ALLOC;
			}
			
			/* Get Packet structure allocation */
			pGetPacket = (GET_VARIABLE_SMI_PACKET*)
				kmalloc(sizeof(GET_VARIABLE_SMI_PACKET), KMALLOC_FLAGS);
			if( pGetPacket == NULL )
			{
			    if(pDupGetPacket != NULL)
			    {
				    kfree(pDupGetPacket);
                    pDupGetPacket = NULL;
			    }
				return ERR_MEM_ALLOC;
			}

			/* initialize allocated memory to zero */
			memset(pDupGetPacket, 0, sizeof(DUP_GET_PACKET));
			memset(pGetPacket, 0, sizeof(GET_VARIABLE_SMI_PACKET));
		}
		else
		{
            /* Data is not copied from the */
			return ERR_DATA_NOT_COPIED;
		}

		/* Copy the GetVariable structure from User space */
		result = copy_from_user(pDupGetPacket,(DUP_GET_PACKET*) arg, 
                            sizeof(DUP_GET_PACKET));
		if(result != 0)
		{
			return ERR_CPY_TO_KER;
		}

		flag = 0x0ef; 

	
		/* 
         * populate the structure. Addresses needs to be converted from
         * virtual to physical.
         */
		pGetPacket->VariableNameAddress = 
            (UINT32)virt_to_phys(pDupGetPacket->DupName);
		pGetPacket->DataAddress = 
            (UINT32)virt_to_phys(pDupGetPacket->DupData);
		pGetPacket->Attributes = pDupGetPacket->DupAttributes;
		pGetPacket->DataSize = pDupGetPacket->DupDataSize;
		memcpy(&(pGetPacket->VendorGuid), &(pDupGetPacket->DupGuid), 
                sizeof(EFI_GUID));

		pPhy_GetPacket = (GET_VARIABLE_SMI_PACKET*)virt_to_phys(pGetPacket);
		
		/* pass PHYSICAL address of packet to SMI handler */
		__asm__ __volatile__("movw $0x01ef,%%ax\n\t"
			"movl pPhy_GetPacket, %%ebx "
	     		:
	     		:
	    		:"%eax"
		   );
		__asm__ __volatile__("movw smiport, %%dx"
				     :
				     :
				     :"%dx"
				    );  		
		__asm__ __volatile__ ("outb %%al, %%dx"  
				      :
				      :
				      :"%al"
				     );
		
		__asm__ __volatile__("movl %%eax, result\n\t"
				   :
				   :
				   :"%eax"
				);

		
		/* copy the completion code */
		pDupGetPacket->compCode = result;

		if ( result != 0 )
		{
			//Tracker - 28385 - Begin
			return result;
			//Tracker - 28385 - End
		}

		break;

	}/* case GET_VAR ends */
	case GET_VAR_READ:
	{
        /* 
         * GET_VAR_READ IOCTL call is used to read the EFI variable attribute 
         * and data by the calling program. 
         */

		if((pDupGetPacket == NULL) || (pGetPacket == NULL))
		{
			return ERR_DATA_NOT_READY;
		}
		/*
         * since we have given the address of the data buffer
		 * it would have got populated automatically.
		 * copy the attributes and data size
         */
		pDupGetPacket->DupAttributes = pGetPacket->Attributes;
		pDupGetPacket->DupDataSize = pGetPacket->DataSize;


		/* copy the data back to user space */
		result = copy_to_user((DUP_GET_PACKET*)arg, pDupGetPacket, 
                    sizeof(DUP_GET_PACKET));
		if(result != 0)
		{
			/* unnable to copy the data to user space */
		    if(pDupGetPacket)
		    {
			    kfree(pDupGetPacket);
			    pDupGetPacket = NULL;
		    }
    		
		    if(pGetPacket)
		    {
			    kfree(pGetPacket);
			    pGetPacket = NULL;
		    }
			return ERR_CPY_TO_USR;
		}
		
		/* Free the allocated memory */
		if(pDupGetPacket)
		{
			kfree(pDupGetPacket);
			pDupGetPacket = NULL;
		}
		
		if(pGetPacket)
		{
			kfree(pGetPacket);
			pGetPacket = NULL;
		}

		break;

	}/* case GET_VAR_READ ends */
	case GET_NEXT:	
	{
        /* 
         * GET_NEXT IOCTL call is used to get the next EFI variable name and 
         * GUID. The data should be read by the program using GET_NEXT_VAR_READ
         * IOCTL call.
         */

		/* 
         * All memory allocations only if ponters are null
		 * else return error as data is ready for read, 
		 * unless it is read we cannot free it.
         */
		
		if((pDupNextPacket == NULL) && (pNextPacket == NULL))
		{
			pDupNextPacket = (DUP_NEXT_PACKET *)
			kmalloc(sizeof(DUP_NEXT_PACKET), KMALLOC_FLAGS);
		
			if(pDupNextPacket == NULL)
			{
				return ERR_MEM_ALLOC;
			}
		

			pNextPacket = (GET_NEXT_VARIABLE_SMI_PACKET *)
			kmalloc(sizeof(GET_NEXT_VARIABLE_SMI_PACKET), KMALLOC_FLAGS);

			/* check if memory allocation fails */
			if (pNextPacket == NULL)
			{
                if( pDupNextPacket != NULL )
                {
                    kfree(pDupNextPacket);
                    pDupNextPacket = NULL;
                }
				return ERR_MEM_ALLOC;
			}

			/* Initialize the allocated memory with zero */
			memset(pDupNextPacket, 0, sizeof(DUP_NEXT_PACKET));
			memset(pNextPacket, 0, sizeof(GET_NEXT_VARIABLE_SMI_PACKET));
		}
		else
		{
			return ERR_DATA_NOT_COPIED;
		}
		
		/* copy the input data from User space to kernel space */

		result = copy_from_user(pDupNextPacket,(DUP_NEXT_PACKET*)arg, 
                    sizeof(DUP_NEXT_PACKET));
		if(result != 0)
		{
            if( pNextPacket != NULL )
            {
                kfree(pNextPacket);
                pNextPacket = NULL;
            }
            if( pDupNextPacket != NULL )
            {
                kfree(pDupNextPacket);
                pDupNextPacket = NULL;
            }
			/* unable to copy data from user space to kernel space */
			return ERR_CPY_TO_KER;
		}

		/*
         * start populating the SMI structure assign the physical memory 
         * pointer to the structure that is being passed to the SMI handler
		 * 1. Copy GUID
		 * 2. Copy Variable Address (physical)
		 * 3. Copy the Variable Size
         */
		flag = 0x0ef; 

		pNextPacket->VariableNameAddress = 
                (UINT32)virt_to_phys(pDupNextPacket->DupName);
		memcpy(&(pNextPacket->VendorGuid), &(pDupNextPacket->DupGuid),
			   sizeof(EFI_GUID));
		pNextPacket->VariableNameSize = pDupNextPacket->DupSize;

		pPhy_NextPacket = 
                (GET_NEXT_VARIABLE_SMI_PACKET*)virt_to_phys(pNextPacket);

		/* pass PHYSICAL address of packet to SMI handler. */
		__asm__ __volatile__("movw $0x02ef,%%ax\n\t"
			"movl pPhy_NextPacket, %%ebx " 	
			:
	     		:
	    		:"%eax"
		   );
		__asm__ __volatile__("movw smiport, %%dx"
				     :
				     :
				     :"%dx"
				    );  		
		__asm__ __volatile__ ("outb %%al, %%dx"  
				      :
				      :
				      :"%al"
				     );
		
		__asm__ __volatile__("movl %%eax, result\n\t"
				   :
				   :
				   :"%eax"
				);

	
		/* copy the completion code of Get Next operation */
		pDupNextPacket->compCode = result;
		if ( result != 0 )
		{
			//Tracker - 28385 - Begin
			return result;
			//Tracker - 28385 - End
		}

		break;
	}/* case GET_NEXT ends */
	case GET_NEXT_VAR_READ:
	{
        /* 
         * GET_VAR_READ IOCTL call is used to read the next variable name and 
         * GUID by the calling program. 
         */

		if((pDupNextPacket == NULL) || (pNextPacket == NULL))
		{
			return ERR_DATA_NOT_READY;
		}

		/*
         * Populate the return structure;
		 * 1. copy GUID
		 * 2. copy variable name size
         */

		memcpy(&(pDupNextPacket->DupGuid),&(pNextPacket->VendorGuid),
				sizeof(EFI_GUID));

		pDupNextPacket->DupSize = pNextPacket->VariableNameSize;

		/* copying the data back to user space */
		result = copy_to_user((DUP_NEXT_PACKET*)arg, pDupNextPacket, 
                    sizeof(DUP_NEXT_PACKET));
		if(result != 0)
		{
			/* unnable to copy data from kernel space to user space */
                if( pNextPacket != NULL )
                {
                    kfree(pNextPacket);
                    pNextPacket = NULL;
                }
                if( pDupNextPacket != NULL )
                {
                    kfree(pDupNextPacket);
                    pDupNextPacket = NULL;
                }
			return ERR_CPY_TO_USR;
		}
		/* free the allocated memory */
		if(pDupNextPacket)
		{
			kfree(pDupNextPacket);
			pDupNextPacket = NULL;
		}

		if(pNextPacket)
		{	
			kfree(pNextPacket);
			pNextPacket = NULL;
		}
		break;
	}/* case GET_NEXT_VAR_READ ends */
	case SET_VAR:
	{
        /* 
         * SET_VAR IOCTL call is used to set EFI variable data. 
         * The completion code of set should be read by the program using 
         * SET_NEXT_VAR_READ IOCTL call.
         */

		if((pDupSetPacket == NULL) && (pSetPacket == NULL))
		{
			/* Allocating memory in kernel space */
			pDupSetPacket = (DUP_SET_PACKET *)
				kmalloc(sizeof(DUP_SET_PACKET), KMALLOC_FLAGS);

			if(pDupSetPacket == NULL)
			{
				return ERR_MEM_ALLOC;
			}

			/* Allocating memory for Set Packet */
			pSetPacket = (SET_VARIABLE_SMI_PACKET*)
				kmalloc(sizeof(SET_VARIABLE_SMI_PACKET), KMALLOC_FLAGS);

			if( pSetPacket == NULL )
			{
                if( pDupSetPacket != NULL )
                {
                    kfree(pDupSetPacket);
                    pDupSetPacket = NULL;
                }
				return ERR_MEM_ALLOC;
			}

			/* initializing the allocated memory with zero */
			memset(pDupSetPacket, 0, sizeof(DUP_SET_PACKET));
			memset(pSetPacket, 0, sizeof(SET_VARIABLE_SMI_PACKET));
		}
		else
		{
			return ERR_DATA_NOT_COPIED;
		}

		/* copy the SetVariable structure from User space */
		result = copy_from_user(pDupSetPacket,(DUP_SET_PACKET*) arg, 
                    sizeof(DUP_SET_PACKET));
		if(result != 0)
		{
                if( pSetPacket != NULL )
                {
                    kfree(pSetPacket);
                    pSetPacket = NULL;
                }
                if( pDupSetPacket != NULL )
                {
                    kfree(pDupSetPacket);
                    pDupSetPacket = NULL;
                }
			/* unable to copy data from user space to kernel space */
			return ERR_CPY_TO_KER;
		}

		/* populate the set packet structure */
		pSetPacket->VariableNameAddress = 
                (UINT32)virt_to_phys(pDupSetPacket->DupName);
		pSetPacket->DataAddress = 
                (UINT32)virt_to_phys(pDupSetPacket->DupData);
		pSetPacket->Attributes = pDupSetPacket->DupAttributes;
		pSetPacket->DataSize = pDupSetPacket->DupDataSize;
		memcpy(&(pSetPacket->VendorGuid), &(pDupSetPacket->DupGuid), 
                sizeof(EFI_GUID));

		pPhy_SetPacket = (SET_VARIABLE_SMI_PACKET*)virt_to_phys(pSetPacket);

		/* pass PHYSICAL address of packet to SMI handler */
		__asm__ __volatile__("movw $0x03ef,%%ax\n\t"
				"movl pPhy_SetPacket, %%ebx " 	
	     		:
	     		:
	    		:"%eax"
		   );
		__asm__ __volatile__("movw smiport, %%dx"
				     :
				     :
				     :"%dx"
				    );  		
		__asm__ __volatile__ ("outb %%al, %%dx"  
				      :
				      :
				      :"%al"
				     );
		
		
		__asm__ __volatile__("movl %%eax, result\n\t"
				   :
				   :
				   :"%eax"
				);
	
		pDupSetPacket->compCode = result;
		
		if ( result != 0 )
		{
			//Tracker - 28385 - Begin
			return result;
			//Tracker - 28385 - End
		}

		break;
	}/* case SET_VAR ends */
	case SET_VAR_READ:
	{
        /* 
         * SET_VAR_READ IOCTL call is used to read the completion code of set
         * operation by the calling program. 
         */
		
		if((pDupSetPacket == NULL) || (pSetPacket == NULL))
		{
			return ERR_DATA_NOT_READY;
		}
		/* 
         * completion code is already copied in to pDupSetPacket
		 * just copy the packet data back to the user space
		 * free the allocated memory
         */
		result = copy_to_user((DUP_SET_PACKET*)arg, pDupSetPacket, 
                    sizeof(DUP_SET_PACKET));
		if(result != 0)
		{
            if( pSetPacket != NULL )
            {
                kfree(pSetPacket);
                pSetPacket = NULL;
            }
            if( pDupSetPacket != NULL )
            {
                kfree(pDupSetPacket);
                pDupSetPacket = NULL;
            }
			/* unable to copy the data to user space */
			return ERR_CPY_TO_USR;
		}
		if(pDupSetPacket)
		{
			kfree(pDupSetPacket);
			pDupSetPacket = NULL;

		}
		if(pSetPacket)
		{
			kfree(pSetPacket);
			pSetPacket = NULL;		
		}
		break;
		
	}/* case SET_VAR_READ ends */
//HII changes - Begin
	case EXPORT_HII:	
	{
        ULONG dExportDatabaseAddress= 0x00; 

        /* 
         * EXPORT_HII IOCTL call is used to get the EFI variable attribute and 
         * data. The data should be read by the program using GET_VAR_READ
         * IOCTL call.
         */

        /* 
         * All memory allocations only if pointers are null
		 * else return error as data is ready for read, 
		 * unless it is read we cannot free it.
         */
		
        if( pDupHiiPacket == NULL)
	    {
		    pDupHiiPacket = (DUP_HII_PACKET *)
				kmalloc(sizeof(DUP_HII_PACKET), KMALLOC_FLAGS);

			if(pDupHiiPacket == NULL)
			{
				return ERR_MEM_ALLOC;
			}
        }

		flag = 0x0ef; 
	
		/* 
         * populate the structure. Addresses needs to be converted from
         * virtual to physical.
         */
		/* pass PHYSICAL address of packet to SMI handler */
		__asm__ __volatile__("movw $0x04ef,%%ax\n\t"
	     		:
	     		:
	    		:"%eax"
		   );
		__asm__ __volatile__("movw smiport, %%dx"
				     :
				     :
				     :"%dx"
				    );  		
		__asm__ __volatile__ ("outb %%al, %%dx"  
				      :
				      :
				      :"%al"
				     );
		
		__asm__ __volatile__("mov %%ebx, ebxreg\n\t"
				   :
				   :
				   :"%ebx"
				);
		__asm__ __volatile__("mov %%edx, edxreg\n\t"
				   :
				   :
				   :"%edx"
				);

		__asm__ __volatile__("movl %%eax, result\n\t"
				   :
				   :
				   :"%eax"
				);

    dExportDatabaseAddress = (ebxreg & 0x0000FFFF)|(edxreg  << 16);

    pDupHiiPacket->offset = (UINT32) dExportDatabaseAddress;
		
		/* copy the completion code */
		pDupHiiPacket->compCode = result;
		/* copy the completion code */
      /*         printk("ebxreg 0x%08x\n", ebxreg);
               printk("edxreg 0x%08x\n", edxreg);
               printk("Export Hii returned 0x%08x\n", pDupHiiPacket->compCode); 
               printk("Export Hii offset 0x%08x\n", pDupHiiPacket->offset); 
*/
		/* copy the data back to user space */
		result = copy_to_user((DUP_HII_PACKET*)arg, pDupHiiPacket, 
                    sizeof(DUP_HII_PACKET));
		if(result != 0)
		{
		    if(pDupHiiPacket)
		    {
			    kfree(pDupHiiPacket);
			    pDupHiiPacket = NULL;
		    }
			/* unnable to copy the data to user space */
			return ERR_CPY_TO_USR;
		}
		
		/* Free the allocated memory */
		if(pDupHiiPacket)
		{
			kfree(pDupHiiPacket);
			pDupHiiPacket = NULL;
		}

		if ( result != 0 )
		{
			return ERR_SMI_FAILED;
		}

		break;

        }
	case BIOS_COPY:
	{

        /* 
         * BIOS_COPY IOCTL call is used to get the EFI variable attribute and 
         * data. The data should be read by the program using GET_VAR_READ
         * IOCTL call.
         */

        /* 
         * All memory allocations only if pointers are null
		 * else return error as data is ready for read, 
		 * unless it is read we cannot free it.
         */
              
	
		if((pDupCopyPacket == NULL) && (pCopyPacket == NULL))
		{
			/* Allocate memory in Kernel space */
			pDupCopyPacket = (DUP_COPY_PACKET *)
				kmalloc(sizeof(DUP_COPY_PACKET), KMALLOC_FLAGS);
			
			if(pDupCopyPacket == NULL)
			{
				return ERR_MEM_ALLOC;
			}
			
			/* Get Packet structure allocation */
			pCopyPacket = (BIOS_COPY_SMI_PACKET*)
				kmalloc(sizeof(BIOS_COPY_SMI_PACKET), KMALLOC_FLAGS);
			if( pCopyPacket == NULL )
			{
                if(pDupCopyPacket)
                {
                    kfree(pDupCopyPacket);
                    pDupCopyPacket = NULL;
                }
				return ERR_MEM_ALLOC;
			}

			/* initialize allocated memory to zero */
			memset(pDupCopyPacket, 0, sizeof(DUP_COPY_PACKET));
			memset(pCopyPacket, 0, sizeof(BIOS_COPY_SMI_PACKET));
		}
		else
		{
            /* Data is not copied from the */
			return ERR_DATA_NOT_COPIED;
		}

		/* Copy the GetVariable structure from User space */
		result = copy_from_user(pDupCopyPacket,(DUP_COPY_PACKET*) arg, 
                            sizeof(DUP_COPY_PACKET));
		if(result != 0)
		{
		    if(pDupCopyPacket)
		    {
			    kfree(pDupCopyPacket);
			    pDupCopyPacket = NULL;
		    }
    		
		    if(pCopyPacket)
		    {
			    kfree(pCopyPacket);
			    pCopyPacket = NULL;
		    }
			return ERR_CPY_TO_KER;
		}
  /*              printk("pDupCopyPacket->dSize = %d\n", pDupCopyPacket->dSize);*/
                dDestination = pDupCopyPacket->dDestination;
		pDupCopyPacket->dDestination = (UINT8 *)
				kmalloc(pDupCopyPacket->dSize , KMALLOC_FLAGS);
			
		if(pDupCopyPacket->dDestination  == NULL)
		{
		    /* Free the allocated memory */
		    if(pDupCopyPacket)
		    {
			    kfree(pDupCopyPacket);
			    pDupCopyPacket = NULL;
		    }
    		
		    if(pCopyPacket)
		    {
			    kfree(pCopyPacket);
			    pCopyPacket = NULL;
		    }
			return ERR_MEM_ALLOC;
		}
		result = copy_from_user(pDupCopyPacket->dDestination,dDestination, pDupCopyPacket->dSize);
		if(result != 0)
		{
            if(pDupCopyPacket->dDestination)
            {
                kfree(pDupCopyPacket->dDestination);
                pDupCopyPacket->dDestination = NULL;
            }
		    /* Free the allocated memory */
		    if(pDupCopyPacket)
		    {
			    kfree(pDupCopyPacket);
			    pDupCopyPacket = NULL;
		    }
    		
		    if(pCopyPacket)
		    {
			    kfree(pCopyPacket);
			    pCopyPacket = NULL;
		    }
			return ERR_CPY_TO_KER;
		}
			

		flag = 0x0ef; 
/*                printk("pDupCopyPacket->dDestination = 0x%08x\n", pDupCopyPacket->dDestination);
                printk("pDupCopyPacket->dSource = 0x%08x\n", pDupCopyPacket->dSource);
                printk("pDupCopyPacket->dSize = 0x%08x\n", pDupCopyPacket->dSize); */
	
		pCopyPacket->dPhysicalDestination = 
            (UINT32)virt_to_phys(pDupCopyPacket->dDestination);
		pCopyPacket->dPhysicalSource = (UINT32)pDupCopyPacket->dSource;
		pCopyPacket->dSize = pDupCopyPacket->dSize;

		pPhy_CopyPacket = (BIOS_COPY_SMI_PACKET*)virt_to_phys(pCopyPacket);
		
		/* pass PHYSICAL address of packet to SMI handler */
		__asm__ __volatile__("movw $0x05ef,%%ax\n\t"
			"movl pPhy_CopyPacket, %%ebx "
	     		:
	     		:
	    		:"%eax"
		   );
		__asm__ __volatile__("movw smiport, %%dx"
				     :
				     :
				     :"%dx"
				    );  		
		__asm__ __volatile__ ("outb %%al, %%dx"  
				      :
				      :
				      :"%al"
				     );
		
		__asm__ __volatile__("movl %%eax, result\n\t"
				   :
				   :
				   :"%eax"
				);

		
		/* copy the completion code */
		pDupCopyPacket->compCode = result;
		if ( result != 0 )
		{
            if(pDupCopyPacket->dDestination)
            {
                kfree(pDupCopyPacket->dDestination);
                pDupCopyPacket->dDestination = NULL;
            }
		    /* Free the allocated memory */
		    if(pDupCopyPacket)
		    {
			    kfree(pDupCopyPacket);
			    pDupCopyPacket = NULL;
		    }
    		
		    if(pCopyPacket)
		    {
			    kfree(pCopyPacket);
			    pCopyPacket = NULL;
		    }

			return ERR_SMI_FAILED;
		}

		break;

	}/* case GET_VAR ends */
	case BIOS_COPY_READ:
	{
        /* 
         * GET_VAR_READ IOCTL call is used to read the EFI variable attribute 
         * and data by the calling program. 
         */

		if((pDupCopyPacket == NULL) || (pCopyPacket == NULL))
		{
			return ERR_DATA_NOT_READY;
		}
		/*
         * since we have given the address of the data buffer
		 * it would have got populated automatically.
		 * copy the attributes and data size
         */
		pDupCopyPacket->dSize = pCopyPacket->dSize;
		result = copy_to_user(dDestination, pDupCopyPacket->dDestination,pDupCopyPacket->dSize);
		if(result != 0)
		{
		    /* Free the allocated memory */
		    if(pDupCopyPacket)
		    {
			    kfree(pDupCopyPacket);
			    pDupCopyPacket = NULL;
		    }
    		
		    if(pCopyPacket)
		    {
			    kfree(pCopyPacket);
			    pCopyPacket = NULL;
		    }
			/* unnable to copy the data to user space */
			return ERR_CPY_TO_USR;
		}
		if(pDupCopyPacket->dDestination)
		{
			kfree(pDupCopyPacket->dDestination);
                	pDupCopyPacket->dDestination = dDestination;
		}

		/* copy the data back to user space */
		result = copy_to_user((DUP_COPY_PACKET*)arg, pDupCopyPacket, 
                    sizeof(DUP_COPY_PACKET));
		if(result != 0)
		{
		    /* Free the allocated memory */
		    if(pDupCopyPacket)
		    {
			    kfree(pDupCopyPacket);
			    pDupCopyPacket = NULL;
		    }
    		
		    if(pCopyPacket)
		    {
			    kfree(pCopyPacket);
			    pCopyPacket = NULL;
		    }
			/* unnable to copy the data to user space */
			return ERR_CPY_TO_USR;
		}
		
		/* Free the allocated memory */
		if(pDupCopyPacket)
		{
			kfree(pDupCopyPacket);
			pDupCopyPacket = NULL;
		}
		
		if(pCopyPacket)
		{
			kfree(pCopyPacket);
			pCopyPacket = NULL;
		}

		break;

	}/* case GET_VAR_READ ends */
//HII changes - End
}/* switch */

 return SUCCESS;

}/* device_ioctl ends */

#ifdef USE_CONFIG_COMPAT

int device_compat_ioctl(struct file *file, unsigned cmd,
				 unsigned long arg)
{
	int ret;

	// lock_kernel();
	switch (cmd) {
	case SET_PORT:
	case GET_VAR:
	case GET_NEXT:
	case SET_VAR:
	case GET_VAR_READ:
	case GET_NEXT_VAR_READ:
	case SET_VAR_READ:
	case EXPORT_HII:
	case BIOS_COPY:
	case BIOS_COPY_READ:
		ret = device_ioctl(file, cmd, arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	// unlock_kernel();
	return ret;
}

#endif
/* Module Declarations *************************** */


/* 
 * This structure will hold the functions to be called 
 * when a process does something to the device we 
 * created. Since a pointer to this structure is kept in 
 * the devices table, it can't be local to
 * init_module.
 */

struct file_operations smi_fops = {
open:	device_open,
unlocked_ioctl:  device_ioctl,
#ifdef USE_CONFIG_COMPAT
	compat_ioctl: device_compat_ioctl,
#endif
release: device_release
};

/******************************************************************************
 * Function     : init_module
 * Description  : Initialize the module - Register the character device 
 * Parameters   : void
 * Return value : Returns one fo the following SUCCESS, EINTVAL or EBUSY.
 *****************************************************************************/
int init_module(void)
{
#if defined (__x86_64__)
#if((LINUX_VERSION_CODE) < (KERNEL_VERSION(2,6,12))) 
#ifdef CONFIG_X86_64
	int ret = 0;
#endif
#endif
#endif
	/* register the driver */
    smi_major = register_chrdev(MAJOR_NUM, DEVICE_NAME, &smi_fops);
    if (smi_major < 0)
    {
        return smi_major;
    }
    /* set the flag indicating success */
    smi_register_chrdev_success = 1;
#if defined (__x86_64__)
#if((LINUX_VERSION_CODE) < (KERNEL_VERSION(2,6,12)))
#ifdef CONFIG_X86_64
        // lock_kernel();
        ret = register_ioctl32_conversion(SET_PORT,     NULL);
        ret = register_ioctl32_conversion(GET_VAR,      NULL);
        ret = register_ioctl32_conversion(GET_VAR_READ, NULL);
        ret = register_ioctl32_conversion(GET_NEXT,     NULL);
        ret = register_ioctl32_conversion(GET_NEXT_VAR_READ,    NULL);
        ret = register_ioctl32_conversion(SET_VAR,      NULL);
        ret = register_ioctl32_conversion(SET_VAR_READ, NULL);
        ret = register_ioctl32_conversion(EXPORT_HII, NULL);
        ret = register_ioctl32_conversion(BIOS_COPY, NULL);
        ret = register_ioctl32_conversion(BIOS_COPY_READ, NULL); 
      // unlock_kernel();
#endif
#endif
#endif
    printk ("%s The major device number is %d.\n",
            "Registeration is a success", 
            smi_major);
    printk ("If you want to talk to the device driver,\n");
    printk ("you'll have to create a device file. \n");
    printk ("We suggest you use:\n");
    printk ("mknod %s c %d 0\n", DEVICE_FILE_NAME, 
            smi_major);
    printk ("The device file name is important, because\n");
    printk ("the ioctl program assumes that's the\n");
    printk ("file you'll use.\n");

    return SUCCESS; /* success */
}


/******************************************************************************
 * Function : cleanup_module
 * Description : Unregister the module.
 * Parameters : void
 * Return value : void
 *****************************************************************************/
void cleanup_module(void)
{
 #if defined (__x86_64__)
#if((LINUX_VERSION_CODE) < (KERNEL_VERSION(2,6,12)))
 #ifdef CONFIG_X86_64
       // lock_kernel();
	int ret;

        ret = unregister_ioctl32_conversion(SET_PORT);
        ret |= unregister_ioctl32_conversion(GET_VAR);
        ret |= unregister_ioctl32_conversion(GET_VAR_READ);
        ret |= unregister_ioctl32_conversion(GET_NEXT);
        ret |= unregister_ioctl32_conversion(GET_NEXT_VAR_READ);
        ret |= unregister_ioctl32_conversion(SET_VAR);
        ret |= unregister_ioctl32_conversion(SET_VAR_READ);
        ret |= unregister_ioctl32_conversion(EXPORT_HII);
        ret |= unregister_ioctl32_conversion(BIOS_COPY);
        ret |= unregister_ioctl32_conversion(BIOS_COPY_READ);
        // unlock_kernel();
#endif
#endif
#endif
 	/* Free all memory which are not freed */
	if(pDupNextPacket)
	{
		kfree(pDupNextPacket);
		pDupNextPacket = NULL;
	}
	if(pDupGetPacket)
	{
		kfree(pDupGetPacket);
		pDupGetPacket = NULL;
	}
	if(pDupSetPacket)
	{
		kfree(pDupSetPacket);
		pDupSetPacket = NULL;
	}
	
	if(pNextPacket)
	{
		kfree(pNextPacket);
		pNextPacket = NULL;		
	}
	if(pGetPacket)
	{
		kfree(pGetPacket);
		pGetPacket = NULL;		
	}
	if(pSetPacket)
	{
		kfree(pSetPacket);
		pSetPacket = NULL;		
	}

    if (smi_register_chrdev_success)
    {
        unregister_chrdev(smi_major, DEVICE_NAME);
    }

    return;
}

/* smi.c ends here */
