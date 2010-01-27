/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/i386/traphdlr.c
 * PURPOSE:         Kernel Trap Handlers
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

UCHAR KiTrapPrefixTable[] =
{
    0xF2,                      /* REP                                  */
    0xF3,                      /* REP INS/OUTS                         */
    0x67,                      /* ADDR                                 */
    0xF0,                      /* LOCK                                 */
    0x66,                      /* OP                                   */
    0x2E,                      /* SEG                                  */
    0x3E,                      /* DS                                   */
    0x26,                      /* ES                                   */
    0x64,                      /* FS                                   */
    0x65,                      /* GS                                   */
    0x36,                      /* SS                                   */
};

UCHAR KiTrapIoTable[] =
{
    0xE4,                      /* IN                                   */
    0xE5,                      /* IN                                   */
    0xEC,                      /* IN                                   */
    0xED,                      /* IN                                   */
    0x6C,                      /* INS                                  */
    0x6D,                      /* INS                                  */
    0xE6,                      /* OUT                                  */
    0xE7,                      /* OUT                                  */
    0xEE,                      /* OUT                                  */
    0xEF,                      /* OUT                                  */
    0x6E,                      /* OUTS                                 */
    0x6F,                      /* OUTS                                 */    
};

BOOLEAN
FORCEINLINE
KiVdmTrap(IN PKTRAP_FRAME TrapFrame)
{
    /* Either the V8086 flag is on, or this is user-mode with a VDM */
    return ((TrapFrame->EFlags & EFLAGS_V86_MASK) ||
            ((KiUserTrap(TrapFrame)) && (PsGetCurrentProcess()->VdmObjects)));
}
 
/* TRAP EXIT CODE *************************************************************/

VOID
FASTCALL
DECLSPEC_NORETURN
KiEoiHelper(IN PKTRAP_FRAME TrapFrame)
{
    /* Disable interrupts until we return */
    _disable();
    
    /* Check for APC delivery */
    KiCheckForApcDelivery(TrapFrame);
    
    /* Now exit the trap for real */
    KiExitTrap(TrapFrame, KTE_SKIP_PM_BIT);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiServiceExit(IN PKTRAP_FRAME TrapFrame,
              IN NTSTATUS Status)
{
    /* Disable interrupts until we return */
    _disable();
    
    /* Check for APC delivery */
    KiCheckForApcDelivery(TrapFrame);
    
    /* Copy the status into EAX */
    TrapFrame->Eax = Status;
    
    /* Now exit the trap for real */
    KiExitTrap(TrapFrame, KTE_SKIP_SEG_BIT | KTE_SKIP_VOL_BIT);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiServiceExit2(IN PKTRAP_FRAME TrapFrame)
{
    /* Disable interrupts until we return */
    _disable();
    
    /* Check for APC delivery */
    KiCheckForApcDelivery(TrapFrame);
    
    /* Now exit the trap for real */
    KiExitTrap(TrapFrame, 0);
}

/* TRAP HANDLERS **************************************************************/

VOID
FASTCALL
DECLSPEC_NORETURN
KiDebugHandler(IN PKTRAP_FRAME TrapFrame,
               IN ULONG Parameter1,
               IN ULONG Parameter2,
               IN ULONG Parameter3)
{
    /* Check for VDM trap */
    ASSERT((KiVdmTrap(TrapFrame)) == FALSE);

    /* Enable interrupts if the trap came from user-mode */
    if (KiUserTrap(TrapFrame)) _enable();

    /* Dispatch the exception  */
    KiDispatchExceptionFromTrapFrame(STATUS_BREAKPOINT,
                                     TrapFrame->Eip - 1,
                                     3,
                                     Parameter1,
                                     Parameter2,
                                     Parameter3,
                                     TrapFrame); 
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiNpxHandler(IN PKTRAP_FRAME TrapFrame,
             IN PKTHREAD Thread,
             IN PFX_SAVE_AREA SaveArea)
{
    ULONG Cr0, Mask, Error, ErrorOffset, DataOffset;
    
    /* Check for VDM trap */
    ASSERT((KiVdmTrap(TrapFrame)) == FALSE);

    /* Check for kernel trap */
    if (!KiUserTrap(TrapFrame))
    {
        /* Kernel might've tripped a delayed error */
        SaveArea->Cr0NpxState |= CR0_TS;
        
        /* Only valid if it happened during a restore */
        //if ((PVOID)TrapFrame->Eip == FrRestore)
        {
            /* It did, so just skip the instruction */
            //TrapFrame->Eip += 3; /* sizeof(FRSTOR) */
            //KiEoiHelper(TrapFrame);
        }
    }

    /* User or kernel trap -- get ready to issue an exception */
    if (Thread->NpxState == NPX_STATE_NOT_LOADED)
    {
        /* Update CR0 */
        Cr0 = __readcr0();
        Cr0 &= ~(CR0_MP | CR0_EM | CR0_TS);
        __writecr0(Cr0);

        /* Save FPU state */
        //Ke386SaveFpuState(SaveArea);

        /* Mark CR0 state dirty */
        Cr0 |= NPX_STATE_NOT_LOADED;
        Cr0 |= SaveArea->Cr0NpxState;
        __writecr0(Cr0);

        /* Update NPX state */
        Thread->NpxState = NPX_STATE_NOT_LOADED;
        KeGetCurrentPrcb()->NpxThread = NULL;
    }

    /* Clear the TS bit and re-enable interrupts */
    SaveArea->Cr0NpxState &= ~CR0_TS;
    _enable();
    
    /* Check if we should get the FN or FX error */
    if (KeI386FxsrPresent)
    {
        /* Get it from FX */
        Mask = SaveArea->U.FxArea.ControlWord;
        Error = SaveArea->U.FxArea.StatusWord;
        
        /* Get the FPU exception address too */
        ErrorOffset = SaveArea->U.FxArea.ErrorOffset;
        DataOffset = SaveArea->U.FxArea.DataOffset;
    }
    else
    {
        /* Get it from FN */
        Mask = SaveArea->U.FnArea.ControlWord;
        Error = SaveArea->U.FnArea.StatusWord;
        
        /* Get the FPU exception address too */
        ErrorOffset = SaveArea->U.FnArea.ErrorOffset;
        DataOffset = SaveArea->U.FnArea.DataOffset;
    }

    /* Get legal exceptions that software should handle */
    Error &= (FSW_INVALID_OPERATION |
              FSW_DENORMAL |
              FSW_ZERO_DIVIDE |
              FSW_OVERFLOW |
              FSW_UNDERFLOW |
              FSW_PRECISION);
    Error &= ~Mask;
    
    if (Error & FSW_STACK_FAULT)
    {
        /* Issue stack check fault */
        KiDispatchException2Args(STATUS_FLOAT_STACK_CHECK,
                                 ErrorOffset,
                                 0,
                                 DataOffset,
                                 TrapFrame);
    }
    
    /* Check for invalid operation */
    if (Error & FSW_INVALID_OPERATION)
    {
        /* Issue fault */
        KiDispatchException1Args(STATUS_FLOAT_INVALID_OPERATION,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }
    
    /* Check for divide by zero */
    if (Error & FSW_ZERO_DIVIDE)
    {
        /* Issue fault */
        KiDispatchException1Args(STATUS_FLOAT_DIVIDE_BY_ZERO,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }
    
    /* Check for denormal */
    if (Error & FSW_DENORMAL)
    {
        /* Issue fault */
        KiDispatchException1Args(STATUS_FLOAT_INVALID_OPERATION,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }
    
    /* Check for overflow */
    if (Error & FSW_OVERFLOW)
    {
        /* Issue fault */
        KiDispatchException1Args(STATUS_FLOAT_OVERFLOW,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }
    
    /* Check for underflow */
    if (Error & FSW_UNDERFLOW)
    {
        /* Issue fault */
        KiDispatchException1Args(STATUS_FLOAT_UNDERFLOW,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }

    /* Check for precision fault */
    if (Error & FSW_PRECISION)
    {
        /* Issue fault */
        KiDispatchException1Args(STATUS_FLOAT_INEXACT_RESULT,
                                 ErrorOffset,
                                 0,
                                 TrapFrame);
    }
    
    /* Unknown FPU fault */
    KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 1, Error, 0, 0, TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap00Handler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);
    
    /* Check for VDM trap */
    ASSERT((KiVdmTrap(TrapFrame)) == FALSE);

    /*  Enable interrupts */
    _enable();
    
    /* Dispatch the exception */
    KiDispatchException0Args(STATUS_INTEGER_DIVIDE_BY_ZERO,
                             TrapFrame->Eip,
                             TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap01Handler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);
    
    /* Check for VDM trap */
    ASSERT((KiVdmTrap(TrapFrame)) == FALSE);

    /* Enable interrupts if the trap came from user-mode */
    if (KiUserTrap(TrapFrame)) _enable();
    
    /*  Mask out trap flag and dispatch the exception */
    TrapFrame->EFlags &= ~EFLAGS_TF;
    KiDispatchException0Args(STATUS_SINGLE_STEP,
                             TrapFrame->Eip,
                             TrapFrame);
}

VOID
DECLSPEC_NORETURN
KiTrap02(VOID)
{
    PKTSS Tss, NmiTss;
    PKTHREAD Thread;
    PKPROCESS Process;
    PKGDTENTRY TssGdt;
    KTRAP_FRAME TrapFrame;
    KIRQL OldIrql;
    
    //
    // In some sort of strange recursion case, we might end up here with the IF
    // flag incorrectly on the interrupt frame -- during a normal NMI this would
    // normally already be set.
    //
    // For sanity's sake, make sure interrupts are disabled for sure.
    // NMIs will already be since the CPU does it for us.
    //
    _disable();

    //
    // Get the current TSS, thread, and process
    //
    Tss = PCR->TSS;
    Thread = ((PKIPCR)PCR)->PrcbData.CurrentThread;
    Process = Thread->ApcState.Process;
    
    //
    // Save data usually not in the TSS
    //
    Tss->CR3 = Process->DirectoryTableBase[0];
    Tss->IoMapBase = Process->IopmOffset;
    Tss->LDT = Process->LdtDescriptor.LimitLow ? KGDT_LDT : 0;
    
    //
    // Now get the base address of the NMI TSS
    //
    TssGdt = &((PKIPCR)KeGetPcr())->GDT[KGDT_NMI_TSS / sizeof(KGDTENTRY)];
    NmiTss = (PKTSS)(ULONG_PTR)(TssGdt->BaseLow |
                                TssGdt->HighWord.Bytes.BaseMid << 16 |
                                TssGdt->HighWord.Bytes.BaseHi << 24);
                    
    //
    // Switch to it and activate it, masking off the nested flag
    //
    // Note that in reality, we are already on the NMI tss -- we just need to
    // update the PCR to reflect this
    //      
    PCR->TSS = NmiTss;
    __writeeflags(__readeflags() &~ EFLAGS_NESTED_TASK);
    TssGdt->HighWord.Bits.Dpl = 0;
    TssGdt->HighWord.Bits.Pres = 1;
    TssGdt->HighWord.Bits.Type = I386_TSS;
    
    //
    // Now build the trap frame based on the original TSS
    //
    // The CPU does a hardware "Context switch" / task switch of sorts and so it
    // takes care of saving our context in the normal TSS.
    //
    // We just have to go get the values...
    //
    RtlZeroMemory(&TrapFrame, sizeof(KTRAP_FRAME));
    TrapFrame.HardwareSegSs = Tss->Ss0;
    TrapFrame.HardwareEsp = Tss->Esp0;
    TrapFrame.EFlags = Tss->EFlags;
    TrapFrame.SegCs = Tss->Cs;
    TrapFrame.Eip = Tss->Eip;
    TrapFrame.Ebp = Tss->Ebp;
    TrapFrame.Ebx = Tss->Ebx;
    TrapFrame.Esi = Tss->Esi;
    TrapFrame.Edi = Tss->Edi;
    TrapFrame.SegFs = Tss->Fs;
    TrapFrame.ExceptionList = PCR->Tib.ExceptionList;
    TrapFrame.PreviousPreviousMode = -1;
    TrapFrame.Eax = Tss->Eax;
    TrapFrame.Ecx = Tss->Ecx;
    TrapFrame.Edx = Tss->Edx;
    TrapFrame.SegDs = Tss->Ds;
    TrapFrame.SegEs = Tss->Es;
    TrapFrame.SegGs = Tss->Gs;
    TrapFrame.DbgEip = Tss->Eip;
    TrapFrame.DbgEbp = Tss->Ebp;
    
    //
    // Store the trap frame in the KPRCB
    //
    KiSaveProcessorState(&TrapFrame, NULL);
    
    //
    // Call any registered NMI handlers and see if they handled it or not
    //
    if (!KiHandleNmi())
    {
        //
        // They did not, so call the platform HAL routine to bugcheck the system
        //
        // Make sure the HAL believes it's running at HIGH IRQL... we can't use
        // the normal APIs here as playing with the IRQL could change the system
        // state
        //
        OldIrql = PCR->Irql;
        PCR->Irql = HIGH_LEVEL;
        HalHandleNMI(NULL);
        PCR->Irql = OldIrql;
    }

    //
    // Although the CPU disabled NMIs, we just did a BIOS Call, which could've
    // totally changed things.
    //
    // We have to make sure we're still in our original NMI -- a nested NMI 
    // will point back to the NMI TSS, and in that case we're hosed.
    //
    if (PCR->TSS->Backlink != KGDT_NMI_TSS)
    {
        //
        // Restore original TSS
        //
        PCR->TSS = Tss;
        
        //
        // Set it back to busy
        //
        TssGdt->HighWord.Bits.Dpl = 0;
        TssGdt->HighWord.Bits.Pres = 1;
        TssGdt->HighWord.Bits.Type = I386_ACTIVE_TSS;
        
        //
        // Restore nested flag
        //
        __writeeflags(__readeflags() | EFLAGS_NESTED_TASK);
        
        //
        // Handled, return from interrupt
        //
        __asm__ __volatile__ ("iret\n");
    }
    
    //
    // Unhandled: crash the system
    //
    KiSystemFatalException(EXCEPTION_NMI, NULL);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap03Handler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);
    
    /* Continue with the common handler */
    KiDebugHandler(TrapFrame, BREAKPOINT_BREAK, 0, 0);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap04Handler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);
    
    /* Check for VDM trap */
    ASSERT((KiVdmTrap(TrapFrame)) == FALSE);

     /* Enable interrupts */
    _enable();
    
    /* Dispatch the exception */
    KiDispatchException0Args(STATUS_INTEGER_OVERFLOW,
                             TrapFrame->Eip - 1,
                             TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap05Handler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);
    
    /* Check for VDM trap */
    ASSERT((KiVdmTrap(TrapFrame)) == FALSE);
    
    /* Check for kernel-mode fault */
    if (!KiUserTrap(TrapFrame)) KiSystemFatalException(EXCEPTION_BOUND_CHECK, TrapFrame);

    /* Enable interrupts */
    _enable();
    
    /* Dispatch the exception */
    KiDispatchException0Args(STATUS_ARRAY_BOUNDS_EXCEEDED,
                             TrapFrame->Eip,
                             TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap06Handler(IN PKTRAP_FRAME TrapFrame,
                IN ULONG EFlags)
{
    PUCHAR Instruction;
    ULONG i;
    KIRQL OldIrql;
    
    /* Check for V86 GPF */
    if (__builtin_expect(EFlags & EFLAGS_V86_MASK, 1))
    {
        /* Enter V86 trap */
        KiEnterV86Trap(TrapFrame);
        
        /* Must be a VDM process */
        if (__builtin_expect(!PsGetCurrentProcess()->VdmObjects, 0))
        {
            /* Enable interrupts */
            _enable();
            
            /* Setup illegal instruction fault */
            KiDispatchException0Args(STATUS_ILLEGAL_INSTRUCTION,
                                     TrapFrame->Eip,
                                     TrapFrame);
        }
        
        /* Go to APC level */
        OldIrql = KfRaiseIrql(APC_LEVEL);
        _enable();
        
        /* Check for BOP */
        if (!VdmDispatchBop(TrapFrame))
        {
            /* Should only happen in VDM mode */
            UNIMPLEMENTED;
            while (TRUE);   
        }
        
        /* Bring IRQL back */
        KfLowerIrql(OldIrql);
        _disable();
        
        /* Do a quick V86 exit if possible */
        if (__builtin_expect(TrapFrame->EFlags & EFLAGS_V86_MASK, 1)) KiExitV86Trap(TrapFrame);
        
        /* Exit trap the slow way */
        KiEoiHelper(TrapFrame);
    }

    /* Save trap frame */
    KiEnterTrap(TrapFrame);
    
    /* Enable interrupts */
    Instruction = (PUCHAR)TrapFrame->Eip;
    _enable();
        
    /* Check for user trap */
    if (KiUserTrap(TrapFrame))
    {
        /* FIXME: Use SEH */
        
        /* Scan next 4 opcodes */
        for (i = 0; i < 4; i++)
        {
            /* Check for LOCK instruction */
            if (Instruction[i] == 0xF0)
            {
                /* Send invalid lock sequence exception */
                KiDispatchException0Args(STATUS_INVALID_LOCK_SEQUENCE,
                                         TrapFrame->Eip,
                                         TrapFrame);
            }
        }
        
        /* FIXME: SEH ends here */
    }
    
    /* Kernel-mode or user-mode fault (but not LOCK) */
    KiDispatchException0Args(STATUS_ILLEGAL_INSTRUCTION,
                             TrapFrame->Eip,
                             TrapFrame);
    
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap07Handler(IN PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread, NpxThread;
    PFX_SAVE_AREA SaveArea, NpxSaveArea;
    ULONG Cr0;
    
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* Try to handle NPX delay load */
    while (TRUE)
    {
        /* Get the current thread */
        Thread = KeGetCurrentThread();

        /* Get the NPX frame */
        SaveArea = KiGetThreadNpxArea(Thread);

        /* Check if emulation is enabled */
        if (SaveArea->Cr0NpxState & CR0_EM)
        {
            /* Not implemented */
            UNIMPLEMENTED;
            while (TRUE);
        }
    
        /* Save CR0 and check NPX state */
        Cr0 = __readcr0();
        if (Thread->NpxState != NPX_STATE_LOADED)
        {
            /* Update CR0 */
            Cr0 &= ~(CR0_MP | CR0_EM | CR0_TS);
            __writecr0(Cr0);
        
            /* Get the NPX thread */
            NpxThread = KeGetCurrentPrcb()->NpxThread;
            if (NpxThread)
            {
                /* Get the NPX frame */
                NpxSaveArea = KiGetThreadNpxArea(NpxThread);
                
                /* Save FPU state */
                //Ke386SaveFpuState(NpxSaveArea);

                /* Update NPX state */
                Thread->NpxState = NPX_STATE_NOT_LOADED;
           }
       
            /* Load FPU state */
            //Ke386LoadFpuState(SaveArea);
        
            /* Update NPX state */
            Thread->NpxState = NPX_STATE_LOADED;
            KeGetCurrentPrcb()->NpxThread = Thread;
        
            /* Enable interrupts */
            _enable();
        
            /* Check if CR0 needs to be reloaded due to context switch */
            if (!SaveArea->Cr0NpxState) KiEoiHelper(TrapFrame);
        
            /* Otherwise, we need to reload CR0, disable interrupts */
            _disable();
        
            /* Reload CR0 */
            Cr0 = __readcr0();
            Cr0 |= SaveArea->Cr0NpxState;
            __writecr0(Cr0);
        
            /* Now restore interrupts and check for TS */
            _enable();
            if (Cr0 & CR0_TS) KiEoiHelper(TrapFrame);
        
            /* We're still here -- clear TS and try again */
            __writecr0(__readcr0() &~ CR0_TS);
            _disable();
        }
        else
        {
            /* This is an actual fault, not a lack of FPU state */
            break;
        }
    }
    
    /* TS should not be set */
    if (Cr0 & CR0_TS)
    {
        /*
         * If it's incorrectly set, then maybe the state is actually still valid
         * but we could've lock track of that due to a BIOS call.
         * Make sure MP is still set, which should verify the theory.
         */
        if (Cr0 & CR0_MP)
        {
            /* Indeed, the state is actually still valid, so clear TS */
            __writecr0(__readcr0() &~ CR0_TS);
            KiEoiHelper(TrapFrame);
        }
        
        /* Otherwise, something strange is going on */
        KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 2, Cr0, 0, 0, TrapFrame);
    }
    
    /* It's not a delayed load, so process this trap as an NPX fault */
    KiNpxHandler(TrapFrame, Thread, SaveArea);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap08Handler(IN PKTRAP_FRAME TrapFrame)
{
    /* FIXME: Not handled */
    KiSystemFatalException(EXCEPTION_DOUBLE_FAULT, TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap09Handler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* Enable interrupts and kill the system */
    _enable();
    KiSystemFatalException(EXCEPTION_NPX_OVERRUN, TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap0AHandler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* Check for VDM trap */
    ASSERT((KiVdmTrap(TrapFrame)) == FALSE);
    
    /* Kill the system */
    KiSystemFatalException(EXCEPTION_INVALID_TSS, TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap0BHandler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* FIXME: Kill the system */
    UNIMPLEMENTED;
    KiSystemFatalException(EXCEPTION_SEGMENT_NOT_PRESENT, TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap0CHandler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* FIXME: Kill the system */
    UNIMPLEMENTED;
    KiSystemFatalException(EXCEPTION_STACK_FAULT, TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap0DHandler(IN PKTRAP_FRAME TrapFrame,
                IN ULONG EFlags)
{
    ULONG i, j, Iopl;
    BOOLEAN Privileged = FALSE;
    PUCHAR Instructions;
    UCHAR Instruction = 0;
    KIRQL OldIrql;
    
    /* Check for V86 GPF */
    if (__builtin_expect(EFlags & EFLAGS_V86_MASK, 1))
    {
        /* Enter V86 trap */
        KiEnterV86Trap(TrapFrame);
        
        /* Must be a VDM process */
        if (__builtin_expect(!PsGetCurrentProcess()->VdmObjects, 0))
        {
            /* Enable interrupts */
            _enable();
            
            /* Setup illegal instruction fault */
            KiDispatchException0Args(STATUS_ILLEGAL_INSTRUCTION,
                                     TrapFrame->Eip,
                                     TrapFrame);
        }
        
        /* Go to APC level */
        OldIrql = KfRaiseIrql(APC_LEVEL);
        _enable();
        
        /* Handle the V86 opcode */
        if (__builtin_expect(Ki386HandleOpcodeV86(TrapFrame) == 0xFF, 0))
        {
            /* Should only happen in VDM mode */
            UNIMPLEMENTED;
            while (TRUE);   
        }
        
        /* Bring IRQL back */
        KfLowerIrql(OldIrql);
        _disable();
        
        /* Do a quick V86 exit if possible */
        if (__builtin_expect(TrapFrame->EFlags & EFLAGS_V86_MASK, 1)) KiExitV86Trap(TrapFrame);
        
        /* Exit trap the slow way */
        KiEoiHelper(TrapFrame);
    }
    
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* Check for user-mode GPF */
    if (KiUserTrap(TrapFrame))
    {   
        /* Should not be VDM */
        ASSERT(KiVdmTrap(TrapFrame) == FALSE);
        
        /* Enable interrupts and check error code */
        _enable();
        if (!TrapFrame->ErrCode)
        {            
            /* FIXME: Use SEH */
            Instructions = (PUCHAR)TrapFrame->Eip;
            
            /* Scan next 15 bytes */
            for (i = 0; i < 15; i++)
            {
                /* Skip prefix instructions */
                for (j = 0; j < sizeof(KiTrapPrefixTable); j++)
                {
                    /* Is this a prefix instruction? */
                    if (Instructions[i] == KiTrapPrefixTable[j])
                    {
                        /* Stop looking */
                        break;
                    }
                }
                
                /* Is this NOT any prefix instruction? */
                if (j == sizeof(KiTrapPrefixTable))
                {
                    /* We can go ahead and handle the fault now */
                    Instruction = Instructions[i];
                    break;
                }
            }
            
            /* If all we found was prefixes, then this instruction is too long */
            if (i == 15)
            {
                /* Setup illegal instruction fault */
                KiDispatchException0Args(STATUS_ILLEGAL_INSTRUCTION,
                                         TrapFrame->Eip,
                                         TrapFrame);
            }
            
            /* Check for privileged instructions */
            if (Instruction == 0xF4)                            // HLT
            {
                /* HLT is privileged */
                Privileged = TRUE;
            }
            else if (Instruction == 0x0F)
            {
                /* Test if it's any of the privileged two-byte opcodes */
                if (((Instructions[i + 1] == 0x00) &&              // LLDT or LTR
                     (((Instructions[i + 2] & 0x38) == 0x10) ||        // LLDT
                      (Instructions[i + 2] == 0x18))) ||               // LTR
                    ((Instructions[i + 1] == 0x01) &&              // LGDT or LIDT or LMSW
                     (((Instructions[i + 2] & 0x38) == 0x10) ||        // LLGT
                      (Instructions[i + 2] == 0x18) ||                 // LIDT
                      (Instructions[i + 2] == 0x30))) ||               // LMSW
                    (Instructions[i + 1] == 0x08) ||               // INVD
                    (Instructions[i + 1] == 0x09) ||               // WBINVD
                    (Instructions[i + 1] == 0x35) ||               // SYSEXIT
                    (Instructions[i + 1] == 0x26) ||               // MOV DR, XXX
                    (Instructions[i + 1] == 0x06) ||               // CLTS
                    (Instructions[i + 1] == 0x20) ||               // MOV CR, XXX
                    (Instructions[i + 1] == 0x24) ||               // MOV YYY, DR
                    (Instructions[i + 1] == 0x30) ||               // WRMSR
                    (Instructions[i + 1] == 0x33))                 // RDPMC
                {
                    /* These are all privileged */
                    Privileged = TRUE;
                }
            }
            else
            {
                /* Get the IOPL and compare with the RPL mask */
                Iopl = (TrapFrame->EFlags & EFLAGS_IOPL) >> 12;
                if ((TrapFrame->SegCs & RPL_MASK) > Iopl)
                {
                    /* I/O privilege error -- check for known instructions */
                    if ((Instruction == 0xFA) || (Instruction == 0xFB)) // CLI or STI
                    {
                        /* These are privileged */
                        Privileged = TRUE;
                    }
                    else
                    {
                        /* Last hope: an IN/OUT instruction */
                        for (j = 0; j < sizeof(KiTrapIoTable); j++)
                        {
                            /* Is this an I/O instruction? */
                            if (Instruction == KiTrapIoTable[j])
                            {
                                /* Then it's privileged */
                                Privileged = TRUE;
                                break;
                            }
                        }
                    }
                }
            }
            
            /* So now... was the instruction privileged or not? */
            if (Privileged)
            {
                /* Whew! We have a privileged instruction, so dispatch the fault */
                KiDispatchException0Args(STATUS_PRIVILEGED_INSTRUCTION,
                                         TrapFrame->Eip,
                                         TrapFrame);
            }
        }
            
        /* If we got here, send an access violation */
        KiDispatchException2Args(STATUS_ACCESS_VIOLATION,
                                 TrapFrame->Eip,
                                 0,
                                 0xFFFFFFFF,
                                 TrapFrame);
    }

    /* Check for custom VDM trap handler */
    if (KeGetPcr()->VdmAlert)
    {
        /* Not implemented */
        UNIMPLEMENTED;
        while (TRUE);
    }
    
    /* 
     * Check for a fault during checking of the user instruction.
     *
     * Note that the SEH handler will catch invalid EIP, but we could be dealing
     * with an invalid CS, which will generate another GPF instead.
     *
     */
    if (((PVOID)TrapFrame->Eip >= (PVOID)KiTrap0DHandler) &&
        ((PVOID)TrapFrame->Eip < (PVOID)KiTrap0DHandler))
    {
        /* Not implemented */
        UNIMPLEMENTED;
        while (TRUE);   
    }
    
    /*
     * NOTE: The ASM trap exit code would restore segment registers by doing
     * a POP <SEG>, which could cause an invalid segment if someone had messed
     * with the segment values.
     *
     * Another case is a bogus SS, which would hit a GPF when doing the ired.
     * This could only be done through a buggy or malicious driver, or perhaps
     * the kernel debugger.
     *
     * The kernel normally restores the "true" segment if this happens.
     *
     * However, since we're restoring in C, not ASM, we can't detect
     * POP <SEG> since the actual instructions will be different.
     *
     * A better technique would be to check the EIP and somehow edit the
     * trap frame before restarting the instruction -- but we would need to
     * know the extract instruction that was used first.
     *
     * We could force a special instrinsic to use stack instructions, or write
     * a simple instruction length checker.
     *
     * Nevertheless, this is a lot of work for the purpose of avoiding a crash
     * when the user is purposedly trying to create one from kernel-mode, so
     * we should probably table this for now since it's not a "real" issue.
     */
     
     /*
      * NOTE2: Another scenario is the IRET during a V8086 restore (BIOS Call)
      * which will cause a GPF since the trap frame is a total mess (on purpose)
      * as built in KiEnterV86Mode.
      *
      * The idea is to scan for IRET, scan for the known EIP adress, validate CS
      * and then manually issue a jump to the V8086 return EIP.
      */
     Instructions = (PUCHAR)TrapFrame->Eip;
     if (Instructions[0] == 0xCF)
     {
         /*
          * Some evil shit is going on here -- this is not the SS:ESP you're 
          * looking for! Instead, this is actually CS:EIP you're looking at!
          * Why? Because part of the trap frame actually corresponds to the IRET
          * stack during the trap exit!
          */
          if ((TrapFrame->HardwareEsp == (ULONG)Ki386BiosCallReturnAddress) &&
              (TrapFrame->HardwareSegSs == (KGDT_R0_CODE | RPL_MASK)))
          {
              /* Exit the V86 trap! */
              Ki386BiosCallReturnAddress(TrapFrame);
          }
          else
          {
              /* Otherwise, this is another kind of IRET fault */
              UNIMPLEMENTED;
              while (TRUE);
          }
     }
     
     /* So since we're not dealing with the above case, check for RDMSR/WRMSR */
     if ((Instructions[0] == 0xF) &&            // 2-byte opcode
        (((Instructions[1] >> 8) == 0x30) ||        // RDMSR
         ((Instructions[2] >> 8) == 0x32)))         // WRMSR
     {
        /* Unknown CPU MSR, so raise an access violation */
        KiDispatchException0Args(STATUS_ACCESS_VIOLATION,
                                 TrapFrame->Eip,
                                 TrapFrame);
     }

     /* Check for lazy segment load */
     if (TrapFrame->SegDs != (KGDT_R3_DATA | RPL_MASK))
     {
         /* Fix it */
         TrapFrame->SegDs = (KGDT_R3_DATA | RPL_MASK);
     }
     else if (TrapFrame->SegEs != (KGDT_R3_DATA | RPL_MASK))
     {
        /* Fix it */
        TrapFrame->SegEs = (KGDT_R3_DATA | RPL_MASK);
     }
     
     /* Do a direct trap exit: restore volatiles only */
     KiExitTrap(TrapFrame, KTE_SKIP_PM_BIT | KTE_SKIP_SEG_BIT);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap0EHandler(IN PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    ULONG_PTR Cr2;
    NTSTATUS Status;

    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* Check for custom VDM trap handler */
    if (KeGetPcr()->VdmAlert)
    {
        /* Not implemented */
        UNIMPLEMENTED;
        while (TRUE);
    }

    /* Check if this is the base frame */
    Thread = KeGetCurrentThread();
    if (KeGetTrapFrame(Thread) != TrapFrame)
    {
        /* It isn't, check if this is a second nested frame */
        if (((ULONG_PTR)KeGetTrapFrame(Thread) - (ULONG_PTR)TrapFrame) <=
            FIELD_OFFSET(KTRAP_FRAME, EFlags))
        {
            /* The stack is somewhere in between frames, we need to fix it */
            UNIMPLEMENTED;
            while (TRUE);
        }
    }
    
    /* Save CR2 */
    Cr2 = __readcr2();
    
    /* Check for Pentium LOCK errata */
    if (KiI386PentiumLockErrataPresent)
    {
        /* Not yet implemented */
        UNIMPLEMENTED;
        while (TRUE);
    }
    
    /* HACK: Check if interrupts are disabled and enable them */
    if (!(TrapFrame->EFlags & EFLAGS_INTERRUPT_MASK))
    {
        /* Enable interupts */
        _enable();
#ifdef HACK_ABOVE_FIXED
        if (!(TrapFrame->EFlags & EFLAGS_INTERRUPT_MASK))
        {
            /* This is illegal */
            KeBugCheckWithTf(IRQL_NOT_LESS_OR_EQUAL,
                             Cr2,
                             -1,
                             TrapFrame->ErrCode & 1,
                             TrapFrame->Eip,
                             TrapFrame);
        }
#endif
    }

    /* Call the access fault handler */
    Status = MmAccessFault(TrapFrame->ErrCode & 1,
                           (PVOID)Cr2,
                           TrapFrame->SegCs & MODE_MASK,
                           TrapFrame);
    if (Status == STATUS_SUCCESS) KiEoiHelper(TrapFrame);
    
    /* Check for S-LIST fault */
    if (TrapFrame->Eip == (ULONG_PTR)ExpInterlockedPopEntrySListFault)
    {
        /* Not yet implemented */
        UNIMPLEMENTED;
        while (TRUE);   
    }
    
    /* Check for syscall fault */
#if 0
    if ((TrapFrame->Eip == (ULONG_PTR)CopyParams) ||
        (TrapFrame->Eip == (ULONG_PTR)ReadBatch))
    {
        /* Not yet implemented */
        UNIMPLEMENTED;
        while (TRUE);
    }
#endif
    /* Check for VDM trap */
    ASSERT((KiVdmTrap(TrapFrame)) == FALSE);
    
    /* Either kernel or user trap (non VDM) so dispatch exception */
    if (Status == STATUS_ACCESS_VIOLATION)
    {
        /* This status code is repurposed so we can recognize it later */
        KiDispatchException2Args(KI_EXCEPTION_ACCESS_VIOLATION,
                                 TrapFrame->Eip,
                                 TrapFrame->ErrCode & 1,
                                 Cr2,
                                 TrapFrame);
    }
    else if ((Status == STATUS_GUARD_PAGE_VIOLATION) ||
             (Status == STATUS_STACK_OVERFLOW))
    {
        /* These faults only have two parameters */
        KiDispatchException2Args(Status,
                                 TrapFrame->Eip,
                                 TrapFrame->ErrCode & 1,
                                 Cr2,
                                 TrapFrame);
    }
    
    /* Only other choice is an in-page error, with 3 parameters */
    KiDispatchExceptionFromTrapFrame(STATUS_IN_PAGE_ERROR,
                                     TrapFrame->Eip,
                                     3,
                                     TrapFrame->ErrCode & 1,
                                     Cr2,
                                     Status,
                                     TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap0FHandler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* FIXME: Kill the system */
    UNIMPLEMENTED;
    KiSystemFatalException(EXCEPTION_RESERVED_TRAP, TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap10Handler(IN PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    PFX_SAVE_AREA SaveArea;
    
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* Check if this is the NPX thrad */
    Thread = KeGetCurrentThread();
    SaveArea = KiGetThreadNpxArea(Thread);
    if (Thread != KeGetCurrentPrcb()->NpxThread)
    {
        /* It isn't, enable interrupts and set delayed error */
        _enable();
        SaveArea->Cr0NpxState |= CR0_TS;
        
        /* End trap */
        KiEoiHelper(TrapFrame);
    }
    
    /* Otherwise, proceed with NPX fault handling */
    KiNpxHandler(TrapFrame, Thread, SaveArea);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap11Handler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* Enable interrupts and kill the system */
    _enable();
    KiSystemFatalException(EXCEPTION_ALIGNMENT_CHECK, TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiTrap13Handler(IN PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    PFX_SAVE_AREA SaveArea;
    ULONG Cr0, MxCsrMask, Error;
    
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* Check if this is the NPX thrad */
    Thread = KeGetCurrentThread();
    if (Thread != KeGetCurrentPrcb()->NpxThread)
    {
        /* It isn't, kill the system */
        KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 13, (ULONG_PTR)Thread, 0, 0, TrapFrame);
    }

    /* Get the NPX frame */
    SaveArea = KiGetThreadNpxArea(Thread);

    /* Check for VDM trap */
    ASSERT((KiVdmTrap(TrapFrame)) == FALSE);

    /* Check for user trap */
    if (!KiUserTrap(TrapFrame))
    {
        /* Kernel should not fault on XMMI */
        KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 13, 0, 0, 2, TrapFrame);
    }
    
    /* Update CR0 */
    Cr0 = __readcr0();
    Cr0 &= ~(CR0_MP | CR0_EM | CR0_TS);
    __writecr0(Cr0);
    
    /* Save FPU state */
    //Ke386SaveFpuState(SaveArea);
    
    /* Mark CR0 state dirty */
    Cr0 |= NPX_STATE_NOT_LOADED;
    Cr0 |= SaveArea->Cr0NpxState;
     __writecr0(Cr0);
    
    /* Update NPX state */
    Thread->NpxState = NPX_STATE_NOT_LOADED;
    KeGetCurrentPrcb()->NpxThread = NULL;
    
    /* Clear the TS bit and re-enable interrupts */
    SaveArea->Cr0NpxState &= ~CR0_TS;
    _enable();

    /* Now look at MxCsr to get the mask of errors we should care about */
    MxCsrMask = ~((USHORT)SaveArea->U.FxArea.MXCsr >> 7);
    
    /* Get legal exceptions that software should handle */
    Error = (USHORT)SaveArea->U.FxArea.MXCsr & (FSW_INVALID_OPERATION |
                                                FSW_DENORMAL |
                                                FSW_ZERO_DIVIDE |
                                                FSW_OVERFLOW |
                                                FSW_UNDERFLOW |
                                                FSW_PRECISION);
    Error &= MxCsrMask;
    
    /* Now handle any of those legal errors */
    if (Error & (FSW_INVALID_OPERATION |
                 FSW_DENORMAL |
                 FSW_ZERO_DIVIDE |
                 FSW_OVERFLOW |
                 FSW_UNDERFLOW |
                 FSW_PRECISION))
    {
        /* By issuing an exception */
        KiDispatchException1Args(STATUS_FLOAT_MULTIPLE_TRAPS,
                                 TrapFrame->Eip,
                                 0,
                                 TrapFrame);
    }
    
    /* Unknown XMMI fault */
    KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN, 13, 0, 0, 1, TrapFrame);
}

/* SOFTWARE SERVICES **********************************************************/

VOID
FASTCALL
KiGetTickCountHandler(IN PKTRAP_FRAME TrapFrame)
{
    UNIMPLEMENTED;
    while (TRUE);
}

VOID
FASTCALL
KiCallbackReturnHandler(IN PKTRAP_FRAME TrapFrame)
{
    UNIMPLEMENTED;
    while (TRUE);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiRaiseAssertionHandler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);

    /* Decrement EIP to point to the INT2C instruction (2 bytes, not 1 like INT3) */
    TrapFrame->Eip -= 2;

    /* Dispatch the exception */
    KiDispatchException0Args(STATUS_ASSERTION_FAILURE,
                             TrapFrame->Eip,
                             TrapFrame);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiDebugServiceHandler(IN PKTRAP_FRAME TrapFrame)
{
    /* Save trap frame */
    KiEnterTrap(TrapFrame);
    
    /* Increment EIP to skip the INT3 instruction */
    TrapFrame->Eip++;
    
    /* Continue with the common handler */
    KiDebugHandler(TrapFrame, TrapFrame->Eax, TrapFrame->Ecx, TrapFrame->Edx);
}

VOID
FASTCALL
DECLSPEC_NORETURN
KiSystemCall(IN ULONG SystemCallNumber,
             IN PVOID Arguments)
{
    PKTHREAD Thread;
    PKTRAP_FRAME TrapFrame;
    PKSERVICE_TABLE_DESCRIPTOR DescriptorTable;
    ULONG Id, Offset, StackBytes, Result;
    PVOID Handler;
    
    /* Loop because we might need to try this twice in case of a GUI call */
    while (TRUE)
    {
        /* Decode the system call number */
        Offset = (SystemCallNumber >> SERVICE_TABLE_SHIFT) & SERVICE_TABLE_MASK;
        Id = SystemCallNumber & SERVICE_NUMBER_MASK;
    
        /* Get current thread, trap frame, and descriptor table */
        Thread = KeGetCurrentThread();
        TrapFrame = Thread->TrapFrame;
        DescriptorTable = (PVOID)((ULONG_PTR)Thread->ServiceTable + Offset);

        /* Validate the system call number */
        if (__builtin_expect(Id > DescriptorTable->Limit, 0))
        {
            /* Check if this is a GUI call */
            if (__builtin_expect(!(Offset & SERVICE_TABLE_TEST), 0))
            {
                /* Fail the call */
                Result = STATUS_INVALID_SYSTEM_SERVICE;
                goto ExitCall;
            }

            /* Convert us to a GUI thread -- must wrap in ASM to get new EBP */        
            Result = KiConvertToGuiThread();
            if (__builtin_expect(!NT_SUCCESS(Result), 0))
            {
                /* Figure out how we should fail to the user */
                UNIMPLEMENTED;
                while (TRUE);
            }
            
            /* Try the call again */
            continue;
        }
        
        /* If we made it here, the call is good */
        break;
    }
    
    /* Check if this is a GUI call */
    if (__builtin_expect(Offset & SERVICE_TABLE_TEST, 0))
    {
        /* Get the batch count and flush if necessary */
        if (NtCurrentTeb()->GdiBatchCount) KeGdiFlushUserBatch();
    }
    
    /* Increase system call count */
    KeGetCurrentPrcb()->KeSystemCalls++;
    
    /* FIXME: Increase individual counts on debug systems */
    //KiIncreaseSystemCallCount(DescriptorTable, Id);
    
    /* Get stack bytes */
    StackBytes = DescriptorTable->Number[Id];
    
    /* Probe caller stack */
    if (__builtin_expect((Arguments < (PVOID)MmUserProbeAddress) && !(KiUserTrap(TrapFrame)), 0))
    {
        /* Access violation */
        UNIMPLEMENTED;
        while (TRUE);
    }
    
    /* Get the handler and make the system call */
    Handler = (PVOID)DescriptorTable->Base[Id];
    Result = KiSystemCallTrampoline(Handler, Arguments, StackBytes);
    
    /* Make sure we're exiting correctly */
    KiExitSystemCallDebugChecks(Id, TrapFrame);
    
    /* Restore the old trap frame */
ExitCall:
    Thread->TrapFrame = (PKTRAP_FRAME)TrapFrame->Edx;

    /* Exit from system call */
    KiServiceExit(TrapFrame, Result);
}

VOID
FORCEINLINE
DECLSPEC_NORETURN
KiSystemCallHandler(IN PKTRAP_FRAME TrapFrame,
                    IN ULONG ServiceNumber,
                    IN PVOID Arguments,
                    IN PKTHREAD Thread,
                    IN KPROCESSOR_MODE PreviousMode,
                    IN KPROCESSOR_MODE PreviousPreviousMode,
                    IN USHORT SegFs)
{
    /* No error code */
    TrapFrame->ErrCode = 0;
    
    /* Save previous mode and FS segment */
    TrapFrame->PreviousPreviousMode = PreviousPreviousMode;
    TrapFrame->SegFs = SegFs;
        
    /* Save the SEH chain and terminate it for now */    
    TrapFrame->ExceptionList = KeGetPcr()->Tib.ExceptionList;
    KeGetPcr()->Tib.ExceptionList = EXCEPTION_CHAIN_END;
        
    /* Clear DR7 and check for debugging */
    TrapFrame->Dr7 = 0;
    if (__builtin_expect(Thread->DispatcherHeader.DebugActive & 0xFF, 0))
    {
        UNIMPLEMENTED;
        while (TRUE);
    }

    /* Set thread fields */
    Thread->TrapFrame = TrapFrame;
    Thread->PreviousMode = PreviousMode;
    
    /* Set debug header */
    KiFillTrapFrameDebug(TrapFrame);
    
    /* Enable interrupts and make the call */
    _enable();
    KiSystemCall(ServiceNumber, Arguments);   
}

VOID
__attribute__((regparm(3)))
DECLSPEC_NORETURN
KiFastCallEntryHandler(IN ULONG ServiceNumber,
                       IN PVOID Arguments,
                       IN PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
        
    /* Fixup segments */
    Ke386SetFs(KGDT_R0_PCR);
    Ke386SetDs(KGDT_R3_DATA | RPL_MASK);
    Ke386SetEs(KGDT_R3_DATA | RPL_MASK);
    
    /* Set up a fake INT Stack and enable interrupts */
    TrapFrame->HardwareSegSs = KGDT_R3_DATA | RPL_MASK;
    TrapFrame->HardwareEsp = (ULONG_PTR)Arguments;
    TrapFrame->EFlags = __readeflags() | EFLAGS_INTERRUPT_MASK;
    TrapFrame->SegCs = KGDT_R3_CODE | RPL_MASK;
    TrapFrame->Eip = SharedUserData->SystemCallReturn;
    __writeeflags(0x2);
    
    /* Get the current thread */
    Thread = KeGetCurrentThread();
    
    /* Arguments are actually 2 frames down (because of the double indirection) */
    Arguments = (PVOID)(TrapFrame->HardwareEsp + 8);

    /* Call the shared handler (inline) */
    KiSystemCallHandler(TrapFrame,
                        ServiceNumber,
                        Arguments,
                        Thread,
                        UserMode,
                        Thread->PreviousMode,
                        KGDT_R3_TEB | RPL_MASK);
}

VOID
__attribute__((regparm(3)))
DECLSPEC_NORETURN
KiSystemServiceHandler(IN ULONG ServiceNumber,
                       IN PVOID Arguments,
                       IN PKTRAP_FRAME TrapFrame)
{
    USHORT SegFs;
    PKTHREAD Thread;

    /* Save and fixup FS */
    SegFs = Ke386GetFs();
    Ke386SetFs(KGDT_R0_PCR);
        
    /* Get the current thread */
    Thread = KeGetCurrentThread();
    
    /* Chain trap frames */
    TrapFrame->Edx = (ULONG_PTR)Thread->TrapFrame;
    
    /* Clear direction flag */
    Ke386ClearDirectionFlag();
    
    /* Call the shared handler (inline) */
    KiSystemCallHandler(TrapFrame,
                        ServiceNumber,
                        Arguments,
                        Thread,
                        KiUserTrap(TrapFrame),
                        Thread->PreviousMode,
                        SegFs);
}

/* CPU AND SOFTWARE TRAPS *****************************************************/

KiTrap(KiTrap00,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiTrap01,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiTrap03,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiTrap04,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiTrap05,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiTrap06,         KI_PUSH_FAKE_ERROR_CODE | KI_FAST_V86_TRAP);
KiTrap(KiTrap07,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiTrap08,         0);
KiTrap(KiTrap09,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiTrap0A,         0);
KiTrap(KiTrap0B,         0);
KiTrap(KiTrap0C,         0);
KiTrap(KiTrap0D,         KI_FAST_V86_TRAP);
KiTrap(KiTrap0E,         0);
KiTrap(KiTrap0F,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiTrap10,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiTrap11,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiTrap13,         KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiGetTickCount,   KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiCallbackReturn, KI_PUSH_FAKE_ERROR_CODE);       
KiTrap(KiRaiseAssertion, KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiDebugService,   KI_PUSH_FAKE_ERROR_CODE);
KiTrap(KiSystemService,  KI_PUSH_FAKE_ERROR_CODE | KI_NONVOLATILES_ONLY);
KiTrap(KiFastCallEntry,  KI_FAST_SYSTEM_CALL);

/* HARDWARE INTERRUPTS ********************************************************/

/*
 * This code can only be used once the HAL handles system interrupt code in C.
 *
 * This is because the HAL, when ending a system interrupt, might see pending
 * DPC or APC interrupts, and attempt to piggyback on the interrupt context in
 * order to deliver them. Once they have been devlivered, it will then "end" the
 * interrupt context by doing a call to the ASM EOI Handler which naturally will
 * throw up on our C-style KTRAP_FRAME.
 *
 * Once it works, expect a noticeable speed boost during hardware interrupts.
 */
#ifdef HAL_INTERRUPT_SUPPORT_IN_C

typedef
FASTCALL
VOID
(PKI_INTERRUPT_DISPATCH)(
    IN PKTRAP_FRAME TrapFrame,
    IN PKINTERRUPT Interrupt
);

VOID
FORCEINLINE
KiExitInterrupt(IN PKTRAP_FRAME TrapFrame,
                IN KIRQL OldIrql,
                IN BOOLEAN Spurious)
{
    if (Spurious) KiEoiHelper(TrapFrame);
    
    _disable();
    
    DPRINT1("Calling HAL to restore IRQL to: %d\n", OldIrql);
    HalEndSystemInterrupt(OldIrql, 0);
    
    DPRINT1("Exiting trap\n");
    KiEoiHelper(TrapFrame);
}

VOID
FASTCALL
KiInterruptDispatch(IN PKTRAP_FRAME TrapFrame,
                    IN PKINTERRUPT Interrupt)
{       
    KIRQL OldIrql;

    KeGetCurrentPrcb()->InterruptCount++;
    
    DPRINT1("Calling HAL with %lx %lx\n", Interrupt->SynchronizeIrql, Interrupt->Vector);
    if (HalBeginSystemInterrupt(Interrupt->SynchronizeIrql,
                                Interrupt->Vector,
                                &OldIrql))
    {
        /* Acquire interrupt lock */
        KxAcquireSpinLock(Interrupt->ActualLock);
        
        /* Call the ISR */
        DPRINT1("Calling ISR: %p with context: %p\n", Interrupt->ServiceRoutine, Interrupt->ServiceContext);
        Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
        
        /* Release interrupt lock */
        KxReleaseSpinLock(Interrupt->ActualLock);
        
        /* Now call the epilogue code */
        DPRINT1("Exiting interrupt\n");
        KiExitInterrupt(TrapFrame, OldIrql, FALSE);
    }
    else
    {
        /* Now call the epilogue code */
        DPRINT1("Exiting Spurious interrupt\n");
        KiExitInterrupt(TrapFrame, OldIrql, TRUE);
    }
}

VOID
FASTCALL
KiChainedDispatch(IN PKTRAP_FRAME TrapFrame,
                  IN PKINTERRUPT Interrupt)
{   
    KeGetCurrentPrcb()->InterruptCount++;
    
    UNIMPLEMENTED;
    while (TRUE);
}

VOID
FASTCALL
KiInterruptHandler(IN PKTRAP_FRAME TrapFrame,
                   IN PKINTERRUPT Interrupt)
{   
    /* Enter interrupt frame */
    KiEnterInterruptTrap(TrapFrame);

    /* Call the correct dispatcher */
    ((PKI_INTERRUPT_DISPATCH*)Interrupt->DispatchAddress)(TrapFrame, Interrupt);
}
#endif

/* EOF */
