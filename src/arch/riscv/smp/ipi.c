/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */

#include <config.h>
#include <mode/smp/ipi.h>
#include <smp/lock.h>
#include <util.h>

#ifdef ENABLE_SMP_SUPPORT

static IpiRemoteCall_t remoteCall;   /* the remote call being requested */
static irq_t           ipiIrq[CONFIG_MAX_NUM_NODES];

static inline void init_ipi_args(IpiRemoteCall_t func,
                                 word_t data1, word_t data2, word_t data3,
                                 word_t mask)
{
    remoteCall = func;
    ipi_args[0] = data1;
    ipi_args[1] = data2;
    ipi_args[2] = data3;

    /* get number of cores involved in this IPI */
    totalCoreBarrier = popcountl(mask);
}

static void handleRemoteCall(IpiRemoteCall_t call, word_t arg0,
                             word_t arg1, word_t arg2, bool_t irqPath)
{
    /* we gets spurious irq_remote_call_ipi calls, e.g. when handling IPI
     * in lock while hardware IPI is pending. Guard against spurious IPIs! */
    if (clh_is_ipi_pending(getCurrentCPUIndex())) {
        switch ((IpiRemoteCall_t)call) {
        case IpiRemoteCall_Stall:
            ipiStallCoreCallback(irqPath);
            break;

#ifdef CONFIG_HAVE_FPU
        case IpiRemoteCall_switchFpuOwner:
            switchLocalFpuOwner((user_fpu_state_t *)arg0);
            break;
#endif /* CONFIG_HAVE_FPU */

        default:
            fail("Invalid remote call");
            break;
        }

        big_kernel_lock.node_owners[getCurrentCPUIndex()].ipi = 0;
        ipi_wait(totalCoreBarrier);
    }
}

void ipi_send_mask(irq_t ipi, word_t mask, bool_t isBlocking)
{

    generic_ipi_send_mask(ipi, mask, isBlocking);
}

irq_t ipi_get_irq(void)
{
    assert(ipiIrq[getCurrentCPUIndex()] != irqInvalid);
    return ipiIrq[getCurrentCPUIndex()];
}

void ipi_clear_irq(irq_t irq)
{
    assert(ipiIrq[getCurrentCPUIndex()] != irqInvalid);
    ipiIrq[getCurrentCPUIndex()] = irqInvalid;
    return;
}

/* this function is called with a single hart id. */
void ipi_send_target(irq_t irq, word_t hart_id)
{
    unsigned long hart_mask;
    word_t core_id = hartIDToCoreID(hart_id);
    assert(core_id < CONFIG_MAX_NUM_NODES);
    hart_mask = BIT(hart_id);
    while (ipiIrq[core_id] != irqInvalid) {
        NODE_UNLOCK_IF_HELD;
        arch_pause();
        NODE_LOCK(false);
    }
    ipiIrq[core_id] = irq;
    asm volatile("fence rw,rw");
    sbi_send_ipi(&hart_mask);
}

#endif
