#include <cassert>
#include <cstring>
#include <cinttypes>
#include <cstdarg>
#include <cstdlib>

#include "rsp.hpp"

extern "C" {
#include "hle_internal.h"
}

#ifdef RECOMP_HAVE_PARALLEL_RSP
extern "C" {
#include "rsp_1.1.h"
unsigned int CALL parallelRSPDoRspCycles(unsigned int cycles);
void CALL parallelRSPInitiateRSP(RSP_INFO Rsp_Info, unsigned int* CycleCount);
}
#endif

static recomp::rsp::callbacks_t rsp_callbacks {};

void recomp::rsp::set_callbacks(const callbacks_t& callbacks) {
    rsp_callbacks = callbacks;
}

uint8_t dmem[0x1000];
uint16_t rspReciprocals[512];
uint16_t rspInverseSquareRoots[512];

extern "C" void HleVerboseMessage(void*, const char*, ...) {
}

extern "C" void HleInfoMessage(void*, const char*, ...) {
}

extern "C" void HleErrorMessage(void*, const char* message, ...) {
    std::fprintf(stderr, "[rsp-hle] error: ");
    va_list args;
    va_start(args, message);
    std::vfprintf(stderr, message, args);
    va_end(args);
    std::fprintf(stderr, "\n");
}

extern "C" void HleWarnMessage(void*, const char* message, ...) {
    static uint32_t warning_count = 0;
    if (warning_count++ >= 32) {
        return;
    }

    std::fprintf(stderr, "[rsp-hle] warning: ");
    va_list args;
    va_start(args, message);
    std::vfprintf(stderr, message, args);
    va_end(args);
    std::fprintf(stderr, "\n");
}

extern "C" void HleCheckInterrupts(void*) {
}

extern "C" void HleProcessDlistList(void*) {
}

extern "C" void HleProcessAlistList(void*) {
}

extern "C" void HleProcessRdpList(void*) {
}

extern "C" void HleShowCFB(void*) {
}

extern "C" int HleForwardTask(void*) {
    return 1;
}

extern "C" void rsp_break(hle_t*, unsigned int) {
}

extern "C" void mp3_task(hle_t*, unsigned int, uint32_t) {
}

namespace {
    bool run_hle_audio_task(uint8_t* rdram, const OSTask* task) {
        static hle_t hle_state{};
        static uint8_t hle_dmem[0x1000]{};
        static uint8_t hle_imem[0x1000]{};

        hle_state.dram = rdram;
        hle_state.dmem = hle_dmem;
        hle_state.imem = hle_imem;
        std::memcpy(&hle_dmem[0xFC0], task, sizeof(OSTask));
        alist_process_naudio(&hle_state);
        return true;
    }

#ifdef RECOMP_HAVE_PARALLEL_RSP
    constexpr uint32_t rdram_mask = 0x7FFFFF;
    constexpr uint32_t rsp_mem_mask = 0xFFF;
    constexpr RCPREG sp_status_halt = 0x0001;
    constexpr RCPREG sp_status_broke = 0x0002;

    alignas(16) uint8_t parallel_dmem[0x1000]{};
    alignas(16) uint8_t parallel_imem[0x1000]{};

    RCPREG mi_intr_reg = 0;
    RCPREG sp_mem_addr_reg = 0;
    RCPREG sp_dram_addr_reg = 0;
    RCPREG sp_rd_len_reg = 0;
    RCPREG sp_wr_len_reg = 0;
    RCPREG sp_status_reg = sp_status_halt;
    RCPREG sp_dma_full_reg = 0;
    RCPREG sp_dma_busy_reg = 0;
    RCPREG sp_pc_reg = 0;
    RCPREG sp_semaphore_reg = 0;
    RCPREG dpc_start_reg = 0;
    RCPREG dpc_end_reg = 0;
    RCPREG dpc_current_reg = 0;
    RCPREG dpc_status_reg = 0;
    RCPREG dpc_clock_reg = 0;
    RCPREG dpc_bufbusy_reg = 0;
    RCPREG dpc_pipebusy_reg = 0;
    RCPREG dpc_tmem_reg = 0;

    bool parallel_initialized = false;
    bool printed_audio_backend = false;

    void parallel_check_interrupts() {
    }

    void parallel_process_dlist() {
    }

    void parallel_process_alist() {
    }

    void parallel_process_rdp_list() {
    }

    void parallel_show_cfb() {
    }

    bool use_parallel_audio_rsp() {
        const char* requested_backend = std::getenv("PAPER_MARIO_AUDIO_RSP");
        return requested_backend == nullptr || std::strcmp(requested_backend, "hle") != 0;
    }

    void copy_rdram_to_rsp_mem(uint8_t* rsp_mem, uint32_t rsp_addr, const uint8_t* rdram, uint32_t dram_addr, uint32_t size) {
        for (uint32_t i = 0; i < size && i < 0x1000; i++) {
            rsp_mem[(rsp_addr + i) & rsp_mem_mask] = rdram[(dram_addr + i) & rdram_mask];
        }
    }

    void initialize_parallel_rsp(uint8_t* rdram) {
        if (parallel_initialized) {
            return;
        }

        RSP_INFO info{};
        info.MemoryBswaped = 1;
        info.RDRAM = rdram;
        info.DMEM = parallel_dmem;
        info.IMEM = parallel_imem;
        info.MI_INTR_REG = &mi_intr_reg;
        info.SP_MEM_ADDR_REG = &sp_mem_addr_reg;
        info.SP_DRAM_ADDR_REG = &sp_dram_addr_reg;
        info.SP_RD_LEN_REG = &sp_rd_len_reg;
        info.SP_WR_LEN_REG = &sp_wr_len_reg;
        info.SP_STATUS_REG = &sp_status_reg;
        info.SP_DMA_FULL_REG = &sp_dma_full_reg;
        info.SP_DMA_BUSY_REG = &sp_dma_busy_reg;
        info.SP_PC_REG = &sp_pc_reg;
        info.SP_SEMAPHORE_REG = &sp_semaphore_reg;
        info.DPC_START_REG = &dpc_start_reg;
        info.DPC_END_REG = &dpc_end_reg;
        info.DPC_CURRENT_REG = &dpc_current_reg;
        info.DPC_STATUS_REG = &dpc_status_reg;
        info.DPC_CLOCK_REG = &dpc_clock_reg;
        info.DPC_BUFBUSY_REG = &dpc_bufbusy_reg;
        info.DPC_PIPEBUSY_REG = &dpc_pipebusy_reg;
        info.DPC_TMEM_REG = &dpc_tmem_reg;
        info.CheckInterrupts = parallel_check_interrupts;
        info.ProcessDList = parallel_process_dlist;
        info.ProcessAList = parallel_process_alist;
        info.ProcessRdpList = parallel_process_rdp_list;
        info.ShowCFB = parallel_show_cfb;

        unsigned int cycle_count = 0;
        parallelRSPInitiateRSP(info, &cycle_count);
        parallel_initialized = true;
    }

    bool run_parallel_audio_task(uint8_t* rdram, const OSTask* task) {
        initialize_parallel_rsp(rdram);

        if (!printed_audio_backend) {
            std::fprintf(stderr, "[rsp] Paper Mario audio backend: ParaLLEl-RSP\n");
            printed_audio_backend = true;
        }

        std::memcpy(&parallel_dmem[0xFC0], task, sizeof(OSTask));
        copy_rdram_to_rsp_mem(parallel_imem, 0, rdram, task->t.ucode_boot & rdram_mask, task->t.ucode_boot_size);

        mi_intr_reg = 0;
        sp_mem_addr_reg = 0;
        sp_dram_addr_reg = 0;
        sp_rd_len_reg = 0xFF8;
        sp_wr_len_reg = 0xFF8;
        sp_status_reg = 0;
        sp_dma_full_reg = 0;
        sp_dma_busy_reg = 0;
        sp_pc_reg = 0;
        sp_semaphore_reg = 0;
        dpc_start_reg = 0;
        dpc_end_reg = 0;
        dpc_current_reg = 0;
        dpc_status_reg = 0;
        dpc_clock_reg = 0;
        dpc_bufbusy_reg = 0;
        dpc_pipebusy_reg = 0;
        dpc_tmem_reg = 0;

        parallelRSPDoRspCycles(0x1000000);

        const bool finished = (sp_status_reg & (sp_status_halt | sp_status_broke)) != 0;
        if (!finished) {
            std::fprintf(stderr, "[rsp] ParaLLEl-RSP audio task did not halt; falling back to HLE for this task.\n");
            return false;
        }

        return true;
    }
#endif
}

// From Ares emulator. For license details, see rsp_vu.h
void recomp::rsp::constants_init() {
    rspReciprocals[0] = u16(~0);
    for (u16 index = 1; index < 512; index++) {
        u64 a = index + 512;
        u64 b = (u64(1) << 34) / a;
        rspReciprocals[index] = u16((b + 1) >> 8);
    }

    for (u16 index = 0; index < 512; index++) {
        u64 a = (index + 512) >> ((index % 2 == 1) ? 1 : 0);
        u64 b = 1 << 17;
        //find the largest b where b < 1.0 / sqrt(a)
        while (a * (b + 1) * (b + 1) < (u64(1) << 44)) b++;
        rspInverseSquareRoots[index] = u16(b >> 1);
    }
}

// Runs a recompiled RSP microcode
bool recomp::rsp::run_task(uint8_t* rdram, const OSTask* task) {
    if (task->t.type == M_AUDTASK) {
#ifdef RECOMP_HAVE_PARALLEL_RSP
        if (use_parallel_audio_rsp() && run_parallel_audio_task(rdram, task)) {
            return true;
        }
#endif
        return run_hle_audio_task(rdram, task);
    }

    assert(rsp_callbacks.get_rsp_microcode != nullptr);
    RspUcodeFunc* ucode_func = rsp_callbacks.get_rsp_microcode(task);

    if (ucode_func == nullptr) {
        fprintf(stderr, "No registered RSP ucode for %" PRIu32 " (returned `nullptr`)\n", task->t.type);
        return false;
    }

    // Load the OSTask into DMEM
    memcpy(&dmem[0xFC0], task, sizeof(OSTask));

    // Load the ucode data into DMEM
    dma_rdram_to_dmem(rdram, 0x0000, task->t.ucode_data, 0xF80 - 1);

    // Run the ucode
    RspExitReason exit_reason = ucode_func(rdram, task->t.ucode);

    // Ensure that the ucode exited correctly
    if (exit_reason != RspExitReason::Broke) {
        fprintf(stderr, "RSP ucode %" PRIu32 " exited unexpectedly. exit_reason: %i\n", task->t.type, static_cast<int>(exit_reason));
        assert(exit_reason == RspExitReason::Broke);
        return false;
    }

    return true;
}
