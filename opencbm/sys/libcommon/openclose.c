/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Copyright 2004 Spiro Trikaliotis
 *
 */

/*! ************************************************************** 
** \file sys/libcommon/openclose.c \n
** \author Spiro Trikaliotis \n
** \version $Id: openclose.c,v 1.10 2007-05-13 17:00:37 strik Exp $ \n
** \n
** \brief Functions for opening and closing the driver
**
****************************************************************/

#include <wdm.h>
#include "cbm_driver.h"
#include "iec.h"

// \TODO: this include is there only for the workaround (cf. cbm_execute_createopen())
// It has to go once the work-around has been removed.

#include "../libiec/i_iec.h"

/*! \brief Services IRPs containing the IRP_MJ_CREATE or IRP_MJ_CLOSE I/O function code.

 Services IRPs containing the IRP_MJ_CREATE or IRP_MJ_CLOSE I/O function code.

 \param Fdo
   Pointer to a DEVICE_OBJECT structure. 
   This is the device object for the target device, 
   previously created by the driver's AddDevice routine.

 \param Irp
   Pointer to an IRP structure that describes the requested I/O operation. 

 \return 
   If the routine succeeds, it returns STATUS_SUCCESS. 
   Otherwise, it returns one of the error status values.

 The driver's DriverEntry routine has stored this routine's address 
 in DriverObject->MajorFunction[IRP_MJ_CREATE] and
 DriverObject->MajorFunction[IRP_MJ_CLOSE].

 Generally, all Dispatch routines execute in an arbitrary thread 
 context at IRQL PASSIVE_LEVEL, but there are exceptions.
*/
NTSTATUS
cbm_createopenclose(IN PDEVICE_OBJECT Fdo, IN PIRP Irp)
{
    PIO_STACK_LOCATION irpSp;
    PDEVICE_EXTENSION pdx;
    NTSTATUS ntStatus;

    FUNC_ENTER();

    // get the device extension

    pdx = Fdo->DeviceExtension;

    DBG_IRPPATH_PROCESS("create/open");

    // get the current IRP stack location

    irpSp = IoGetCurrentIrpStackLocation(Irp);


    if (irpSp->MajorFunction == IRP_MJ_CREATE
        && irpSp->Parameters.Create.Options & FILE_DIRECTORY_FILE) 
    {
        // The caller wants to open a directory.
        // As we do not support directories, fail that request!

        ntStatus = STATUS_NOT_A_DIRECTORY;
        DBG_IRPPATH_COMPLETE("create/open");
        QueueCompleteIrp(NULL, Irp, ntStatus, 0);
    }
    else
    {
        switch (irpSp->MajorFunction)
        {
        case IRP_MJ_CREATE:
            PERF_EVENT_OPEN_QUEUE();
            break;

        case IRP_MJ_CLOSE:
            PERF_EVENT_CLOSE_QUEUE();
            break;

        default:
            DBG_ERROR((DBG_PREFIX "UNKNOWN IRP->MAJORFUNCTION: %08x",
                irpSp->MajorFunction));
            break;
        }

        // queue the IRP to be processed

        ntStatus = QueueStartPacket(&pdx->IrpQueue, Irp, FALSE, Fdo);
    }
    
    FUNC_LEAVE_NTSTATUS(ntStatus);
}

/*! \brief Execute IRPs containing the IRP_MJ_CREATEOPEN I/O function code.

 Executes IRPs containing the IRP_MJ_CREATEOPEN I/O function code.

 \param Pdx
   Pointer to a DEVICE_EXTENSION structure.

 \param Irp
   Pointer to an IRP structure that describes the requested I/O operation. 

 \return 
   If the routine succeeds, it returns STATUS_SUCCESS. 
   Otherwise, it returns one of the error status values.
*/
NTSTATUS
cbm_execute_createopen(IN PDEVICE_EXTENSION Pdx, IN PIRP Irp)
{
    NTSTATUS ntStatus;

    FUNC_ENTER();

    DBG_ASSERT(Pdx != NULL);

    PERF_EVENT_OPEN_EXECUTE();

    DBG_IRPPATH_EXECUTE("create/open");

    if (Pdx->ParallelPortLock)
    {
        // as we keep the parallel port locked all the time,
        // do not lock it now.
        ntStatus = STATUS_SUCCESS;

        // \TODO: try workaround: As the machine might have been 
        // suspended or hibernated without knowing it, test if
        // all lines are exactly as expected. If not, lock und unlock
        // the parallel port again
        //
        // \TODO When this work-around goes, make sure the include
        // if i_iec.h is removed, too.

        if (CBMIEC_GET(PP_RESET_IN) != 0)
        {
            DBG_PRINT((DBG_PREFIX "UNLOCK/LOCK pair!"));

            cbm_unlock_parport(Pdx);
            cbmiec_reset(Pdx);
            ntStatus = cbm_lock_parport(Pdx);
        }
    }
    else
    {
        // Try to allocate the parallel port

        ntStatus = cbm_lock_parport(Pdx);
    }

    // We're done, complete the IRP

    DBG_IRPPATH_COMPLETE("create/open");

    QueueCompleteIrp(&Pdx->IrpQueue, Irp, ntStatus, 0);

    FUNC_LEAVE_NTSTATUS(ntStatus);
}

/*! \brief Execute IRPs containing the IRP_MJ_CLOSE I/O function code.

 Executes IRPs containing the IRP_MJ_CLOSE I/O function code.

 \param Pdx
   Pointer to a DEVICE_EXTENSION structure.

 \param Irp
   Pointer to an IRP structure that describes the requested I/O operation. 

 \return 
   If the routine succeeds, it returns STATUS_SUCCESS. 
   Otherwise, it returns one of the error status values.
*/
NTSTATUS
cbm_execute_close(IN PDEVICE_EXTENSION Pdx, IN PIRP Irp)
{
    NTSTATUS ntStatus;

    FUNC_ENTER();

    DBG_ASSERT(Pdx != NULL);

    PERF_EVENT_CLOSE_EXECUTE();

    DBG_IRPPATH_PROCESS("close");

    if (Pdx->ParallelPortLock)
    {
        // release the bus (to be able to share it with other controllers)

        if (!Pdx->DoNotReleaseBus)
        {
            cbmiec_release_bus(Pdx);
        }

        // as we keep the parallel port locked all the time,
        // do not unlock it now.

        ntStatus = STATUS_SUCCESS;
    }
    else
    {
        // Unlock the parallel port

        ntStatus = cbm_unlock_parport(Pdx);
    }


    // Now, complete the IRP

    DBG_IRPPATH_COMPLETE("close");
    QueueCompleteIrp(&Pdx->IrpQueue, Irp, ntStatus, 0);

    FUNC_LEAVE_NTSTATUS(ntStatus);
}
