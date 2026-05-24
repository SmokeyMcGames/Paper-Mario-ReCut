#include "recomp.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <ultramodern/ultra64.h>
#include <ultramodern/ultramodern.hpp>

#define VI_NTSC_CLOCK 48681812

namespace {
    constexpr uint32_t ai_status_fifo_full = 1u << 31;
    constexpr uint32_t ai_status_dma_busy = 1u << 30;
    constexpr uint32_t bytes_per_stereo_frame = 2 * sizeof(int16_t);
    constexpr size_t ai_fifo_capacity = 2;

    struct AiDmaModel {
        std::mutex mutex;
        std::array<double, ai_fifo_capacity> fifo_frames{};
        size_t fifo_count = 0;
        uint32_t sample_rate = 48000;
        std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
    };

    AiDmaModel ai_dma;

    void advance_ai_dma_locked() {
        auto now = std::chrono::steady_clock::now();
        double elapsed_seconds = std::chrono::duration<double>(now - ai_dma.last_update).count();
        ai_dma.last_update = now;

        double frames_to_consume = elapsed_seconds * ai_dma.sample_rate;
        while (frames_to_consume > 0.0 && ai_dma.fifo_count != 0) {
            if (frames_to_consume < ai_dma.fifo_frames[0]) {
                ai_dma.fifo_frames[0] -= frames_to_consume;
                break;
            }

            frames_to_consume -= ai_dma.fifo_frames[0];
            for (size_t i = 1; i < ai_dma.fifo_count; i++) {
                ai_dma.fifo_frames[i - 1] = ai_dma.fifo_frames[i];
            }
            ai_dma.fifo_frames[ai_dma.fifo_count - 1] = 0.0;
            ai_dma.fifo_count--;
        }
    }

    void reset_ai_dma_frequency(uint32_t freq) {
        std::lock_guard lock{ ai_dma.mutex };
        advance_ai_dma_locked();
        ai_dma.sample_rate = freq == 0 ? 48000 : freq;
    }

    bool push_ai_dma_buffer(uint32_t byte_count) {
        std::lock_guard lock{ ai_dma.mutex };
        advance_ai_dma_locked();

        if (ai_dma.fifo_count >= ai_fifo_capacity) {
            return false;
        }

        ai_dma.fifo_frames[ai_dma.fifo_count++] = byte_count / static_cast<double>(bytes_per_stereo_frame);
        return true;
    }

    uint32_t get_ai_dma_length_bytes() {
        std::lock_guard lock{ ai_dma.mutex };
        advance_ai_dma_locked();

        if (ai_dma.fifo_count == 0) {
            return 0;
        }

        return static_cast<uint32_t>(ai_dma.fifo_frames[0] * bytes_per_stereo_frame);
    }

    uint32_t get_ai_dma_status() {
        std::lock_guard lock{ ai_dma.mutex };
        advance_ai_dma_locked();

        uint32_t status = 0;
        if (ai_dma.fifo_count >= ai_fifo_capacity) {
            status |= ai_status_fifo_full;
        }
        if (ai_dma.fifo_count != 0) {
            status |= ai_status_dma_busy;
        }
        return status;
    }
}

extern "C" void osAiSetFrequency_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t freq = ctx->r4;
    // This makes actual audio frequency more accurate to console, but may not be desirable
    //uint32_t dacRate = (uint32_t)(((float)VI_NTSC_CLOCK / freq) + 0.5f);
    //freq = VI_NTSC_CLOCK / dacRate;
    ctx->r2 = freq;
    reset_ai_dma_frequency(freq);
    ultramodern::set_audio_frequency(freq);
}

extern "C" void osAiSetNextBuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    if (!push_ai_dma_buffer(ctx->r5)) {
        ctx->r2 = static_cast<gpr>(-1);
        return;
    }

    ultramodern::queue_audio_buffer(rdram, ctx->r4, ctx->r5);
    ctx->r2 = 0;
}

extern "C" void osAiGetLength_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = get_ai_dma_length_bytes();
}

extern "C" void osAiGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = get_ai_dma_status();
}
