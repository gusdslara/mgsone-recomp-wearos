#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <android/log.h>

#include "runtime/memory.h"
#include "runtime/gpu/gpu.h"
#include "runtime/gpu/display_geometry.h"
#include "runtime/spu/spu.h"
#include "runtime/dma/dma.h"
#include "runtime/cdrom/cdrom_controller.h"
#include "runtime/cdrom/virtual_fs.h"
#include "runtime/cdrom/exe_extractor.h"
#include "runtime/bios/bios.h"
#include "runtime/input/input.h"
#include "runtime/mdec/mdec.h"
#include "runtime/timers/timers.h"
#include "runtime/save_state.h"
#include "runtime/mips_interpreter.h"
#include "runtime/psyq/psyq_hle.h"
#include "runtime/psyq/psyq_registry.h"
#include "runtime/psyq/psyq_state.h"
#include "runtime/cpu_context.h"
#include "runtime/games/mgs_profile.h"
#include "runtime/games/mgs_disc.h"
#include "runtime/games/crash3_profile.h"
#include "runtime/emuptr.h"

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <sys/resource.h>

#include "runtime/vblank_scheduler.h"

#define LOG_TAG "MGSNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" {
    #include "mgs_wear_input.h"
}

// 64K lookup table for 15-bit PS1 BGR555 -> 32-bit XRGB8888 conversion
static uint32_t s_rgb555Lut[65536];
static void initRgb555Lut() {
    for (uint32_t p = 0; p < 65536; ++p) {
        const uint32_t r = ((p & 0x1Fu) << 3) | ((p & 0x1Fu) >> 2);
        const uint32_t g = (((p >> 5) & 0x1Fu) << 3) | (((p >> 5) & 0x1Fu) >> 2);
        const uint32_t b = (((p >> 10) & 0x1Fu) << 3) | (((p >> 10) & 0x1Fu) >> 2);
        s_rgb555Lut[p] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
    }
}

void recomp_init_dispatch_table();
void recomp_dispatch(uint8_t *rdram, recomp_context *ctx, uint32_t target_pc);
void recomp_resume_dispatch(uint8_t *rdram, recomp_context *ctx, uint32_t target_pc);

static constexpr uint32_t CPU_CLOCK = 33868800u;

static bool isFileReadable(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static bool findDiscImage(std::string &disc1Path, std::string &disc2Path) {
    const std::vector<std::string> searchPaths = {
        "/data/user/0/com.mgsnative.wear/files",
        "/data/data/com.mgsnative.wear/files",
        "/storage/emulated/0/Android/data/com.mgsnative.wear/files/discs",
        "/sdcard/Android/data/com.mgsnative.wear/files/discs",
        "/sdcard/mgs",
        "/storage/emulated/0/mgs",
        "/sdcard/Download",
        "/storage/emulated/0/Download"
    };

    const std::vector<std::string> d1Names = {
        "mgs1.bin",
        "mgs_disc1.bin",
        "Metal Gear Solid (USA) (Disc 1).bin",
        "metal_gear_solid_disc1.bin",
        "mgs1.cue",
        "Metal Gear Solid (USA) (Disc 1).cue"
    };

    const std::vector<std::string> d2Names = {
        "mgs2.bin",
        "mgs_disc2.bin",
        "Metal Gear Solid (USA) (Disc 2).bin",
        "metal_gear_solid_disc2.bin",
        "mgs2.cue",
        "Metal Gear Solid (USA) (Disc 2).cue"
    };

    for (const auto &dir : searchPaths) {
        for (const auto &name : d1Names) {
            std::string full = dir + "/" + name;
            if (isFileReadable(full)) {
                disc1Path = full;
                break;
            }
        }
        if (!disc1Path.empty()) break;
    }

    if (disc1Path.empty()) {
        std::error_code ec;
        for (const auto &dir : searchPaths) {
            if (!std::filesystem::exists(dir, ec)) continue;
            for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                std::string stem = entry.path().filename().string();
                std::string ext = entry.path().extension().string();
                if ((ext == ".bin" || ext == ".cue" || ext == ".iso") &&
                    (stem.find("Disc 1") != std::string::npos ||
                     stem.find("disc1") != std::string::npos ||
                     stem.find("mgs1") != std::string::npos ||
                     stem.find("MGS1") != std::string::npos ||
                     stem.find("Metal Gear") != std::string::npos)) {
                    disc1Path = entry.path().string();
                    break;
                }
            }
            if (!disc1Path.empty()) break;
        }
    }

    for (const auto &dir : searchPaths) {
        for (const auto &name : d2Names) {
            std::string full = dir + "/" + name;
            if (isFileReadable(full)) {
                disc2Path = full;
                break;
            }
        }
        if (!disc2Path.empty()) break;
    }

    return !disc1Path.empty();
}

static bool loadPsExeFromBuffer(const std::vector<uint8_t> &exeData,
                                ps1::Memory &memory,
                                uint32_t &entryPoint) {
    if (exeData.size() < 2048) {
        LOGE("EXE header too small (%zu bytes)", exeData.size());
        return false;
    }
    if (std::memcmp(exeData.data(), "PS-X EXE", 8) != 0) {
        LOGE("Invalid PS-X EXE magic header");
        return false;
    }

    const uint32_t initialPc = *reinterpret_cast<const uint32_t *>(&exeData[0x10]);
    const uint32_t destAddress = *reinterpret_cast<const uint32_t *>(&exeData[0x18]);
    const uint32_t fileSize = *reinterpret_cast<const uint32_t *>(&exeData[0x1C]);

    LOGI("PS-X EXE: initialPC=0x%08X, destAddress=0x%08X, fileSize=%u",
         initialPc, destAddress, fileSize);

    if (exeData.size() < 2048 + fileSize) {
        LOGE("EXE data truncated: size %zu < %u", exeData.size(), 2048 + fileSize);
        return false;
    }

    const uint32_t ramTarget = destAddress & 0x1FFFFFFF;
    for (uint32_t i = 0; i < fileSize; ++i) {
        if (ramTarget + i < ps1::Memory::RAM_SIZE) {
            memory.write8(destAddress + i, exeData[2048 + i]);
        }
    }

    entryPoint = initialPc;
    return true;
}

static void applyWearPadToPs1Input(const MGSWearPadState &wearPad,
                                   ps1::input::InputController &input) {
    static const uint16_t allButtons[] = {
        ps1::input::BTN_SELECT,
        ps1::input::BTN_L3,
        ps1::input::BTN_R3,
        ps1::input::BTN_START,
        ps1::input::BTN_UP,
        ps1::input::BTN_RIGHT,
        ps1::input::BTN_DOWN,
        ps1::input::BTN_LEFT,
        ps1::input::BTN_L2,
        ps1::input::BTN_R2,
        ps1::input::BTN_L1,
        ps1::input::BTN_R1,
        ps1::input::BTN_TRIANGLE,
        ps1::input::BTN_CIRCLE,
        ps1::input::BTN_CROSS,
        ps1::input::BTN_SQUARE,
    };

    for (uint16_t b : allButtons) {
        if ((wearPad.buttons & b) == 0) {
            input.press(b, 0);
        } else {
            input.release(b, 0);
        }
    }

    input.setAnalog(0, wearPad.left_stick_x, wearPad.left_stick_y,
                       wearPad.right_stick_x, wearPad.right_stick_y);
}



int SDL_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    initRgb555Lut();
    setpriority(PRIO_PROCESS, 0, -10);

    LOGI("Starting MGS1 Native Wear OS Runtime (Very Low Graphics / Cool Mode)...");

    // Lock orientation to portrait so SDL never rotates the EGL surface
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        LOGE("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    MGSWear_InitInput();

    SDL_Window *window = SDL_CreateWindow(
        "Metal Gear Solid - Wear OS",
        480, 480,
        SDL_WINDOW_FULLSCREEN
    );
    if (!window) {
        LOGE("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *sdlRenderer = SDL_CreateRenderer(window, nullptr);
    if (!sdlRenderer) {
        LOGE("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(sdlRenderer, 0);

    int winW = 480, winH = 480;
    SDL_GetWindowSizeInPixels(window, &winW, &winH);

    // Disable PGXP tracking and enhanced surfaces for ultra-low CPU & thermal load on Galaxy Watch 7
    setenv("PS1_DISPATCH_PERMISSIVE", "1", 0);
    ps1::configurePGXPTracking(false);
    ps1::games::mgs::configure(true, false);
    ps1::games::crash3::configure({});
    ps1::mips::configureDynamicInterpreter(true, 0x80000000u, 0x80200000u);

    std::string discPath, disc2Path;
    if (!findDiscImage(discPath, disc2Path)) {
        LOGE("MGS1 Disc 1 image not found!");
        SDL_DestroyRenderer(sdlRenderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    LOGI("Found MGS1 Disc 1: %s", discPath.c_str());

    // Core PS1 Hardware Subsystems allocated on HEAP to avoid Android thread stack overflow
    auto memory = std::make_unique<ps1::Memory>();
    auto gpu = std::make_unique<ps1::gpu::GPU>();
    gpu->setEnhancedRaster(false);
    gpu->setTrueDepth(false);
    gpu->setProjectiveNearClip(false);

    auto spu = std::make_unique<ps1::spu::SPU>();
    auto dma = std::make_unique<ps1::DMA>();
    auto irqCtrl = std::make_unique<ps1::InterruptController>();
    auto timers = std::make_unique<ps1::Timers>();
    auto input = std::make_unique<ps1::input::InputController>();
    input->setPadType(0, ps1::input::PadType::Analog);

    // Attach persistent Memory Cards on watch storage
    {
        std::filesystem::path mcDir = std::filesystem::path(discPath).parent_path() / "memcards";
        std::error_code ec;
        std::filesystem::create_directories(mcDir, ec);
        for (unsigned slot = 0; slot < 2; ++slot) {
            auto mcPath = (mcDir / ("card" + std::to_string(slot + 1) + ".mcd")).string();
            input->getMemoryCard(slot).attachFile(mcPath);
        }
    }

    auto stateCoordinator = std::make_unique<ps1::SaveStateCoordinator>();
    auto mdec = std::make_unique<ps1::mdec::MDEC>();
    auto cdromCtrl = std::make_unique<ps1::cdrom::CdromController>();
    auto fs = std::make_unique<ps1::cdrom::VirtualFs>();

    ps1::emuptr_set_ram(memory->ramPtr());

    memory->setGPU(gpu.get());
    memory->setSPU(spu.get());
    memory->setDMA(dma.get());
    memory->setCDROM(cdromCtrl.get());
    memory->setInput(input.get());
    memory->setMDEC(mdec.get());
    memory->setTimers(timers.get());
    memory->setInterruptController(irqCtrl.get());

    dma->setMemory(memory.get());
    dma->setGPU(gpu.get());
    dma->setSPU(spu.get());
    dma->setCDROM(cdromCtrl.get());
    dma->setMDEC(mdec.get());

    cdromCtrl->attachVirtualFs(fs.get());
    fs->setUnpacedDataFiles({"/MGS/STAGE.DIR", "/MGS/RADIO.DAT", "/MGS/FACE.DAT"});
    fs->setUnpacedDataGuard(ps1::games::mgs::fastDataAllowed);
    ps1::games::mgs::configureFastLoading(true, cdromCtrl.get());

    cdromCtrl->setXaCallback([&spu](const int16_t *samples, uint32_t count) {
        spu->pushXaSamples(samples, count);
    });

    auto ctx = std::make_unique<recomp_context>();
    ctx->reset();
    ctx->mem = memory.get();
    ctx->r[29] = 0x801FFFF0;

    auto bios = std::make_unique<ps1::bios::Bios>(*ctx, *fs, *memory);
    ctx->bios = bios.get();
    bios->setGPU(gpu.get());
    bios->setInputController(input.get());
    bios->setCdromController(cdromCtrl.get());
    bios->setDma(dma.get());
    spu->enableGuestClock();
    bios->setSpu(spu.get());

    gpu->writeGP1(0x03000000); // Enable display

    {
        ps1::psyq::HleConfig hleCfg;
        hleCfg.drainCallbacks = [&bios]() { bios->drainPendingCallbacks(); };
        hleCfg.writeGP0 = [&gpu](uint32_t w) {
            gpu->writeGP0(w, UINT32_MAX, nullptr, ps1::PGXPSubmissionPath::HleGenerated);
        };
        hleCfg.readGPUREAD = [&gpu]() { return gpu->readGPUREAD(); };
        hleCfg.writeGP0Tracked =
            [&gpu](uint32_t w, uint32_t source, const ps1::PGXPValue *precision,
                   uint32_t listStart, uint32_t nodeAddress,
                   uint32_t traversalOrdinal, uint32_t orderingTableAnchorAddress,
                   bool orderingTableAnchorExact) {
                gpu->writeGP0BatchWord(
                    w, source, precision, ps1::PGXPSubmissionPath::HleOrderingTable,
                    {listStart, nodeAddress, traversalOrdinal,
                     orderingTableAnchorAddress, orderingTableAnchorExact, true});
            };
        hleCfg.beginGP0Batch = [&gpu]() { gpu->beginCommandBatch(); };
        hleCfg.endGP0Batch = [&gpu]() { gpu->endCommandBatch(); };
        hleCfg.activeGP0SubmissionId = [&gpu]() { return gpu->activeSubmissionId(); };
        hleCfg.observeGP0List = [&gpu](uint32_t start, const ps1::Memory &mem) {
            return gpu->observePresentationList(start, mem.ramPtr());
        };
        hleCfg.writeGP1 = [&gpu](uint32_t w) { gpu->writeGP1(w); };
        hleCfg.deliverVBlankEvent = [&bios]() { bios->deliverVBlankBoundary(); };
        hleCfg.stateCheckpoint = [&stateCoordinator, &bios](recomp_context *checkpointCtx, uint32_t resumePc) {
            bios->checkShutdown();
            if (!bios->hasNativeThreadContinuations())
                stateCoordinator->checkpoint(*checkpointCtx, resumePc);
            bios->checkShutdown();
        };
        hleCfg.clockWaitCheckpoint = [&bios](recomp_context *) {
            bios->checkShutdown();
        };
        bios->setStateCheckpoint([&](uint32_t pc) {
            bios->checkShutdown();
            stateCoordinator->checkpoint(*ctx, pc);
            bios->checkShutdown();
        });
        ps1::psyq::configure(hleCfg);
        psyq_registry_init_defaults();
        psyq_register_rayman_boot();
    }

    cdromCtrl->setInterruptCallback([&bios](uint8_t intType) {
        bios->queueCdromEvent(intType);
    });

    // Mount Disc 1
    if (!fs->loadDisc(discPath)) {
        LOGE("Failed to mount disc: %s", discPath.c_str());
        SDL_DestroyRenderer(sdlRenderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (!disc2Path.empty()) {
        fs->registerDisc(disc2Path);
    }
    ps1::games::mgs::configureDiscChange(fs.get(), cdromCtrl.get());

    // Read SLUS_005.94 directly from mounted ISO9660 filesystem
    auto exeOpt = fs->readFile("/SLUS_005.94;1");
    if (!exeOpt) exeOpt = fs->readFile("/SLUS_005.94");
    if (!exeOpt) exeOpt = fs->readFile("SLUS_005.94");
    uint32_t entryPoint = 0x800148B8;
    if (!exeOpt || !loadPsExeFromBuffer(*exeOpt, *memory, entryPoint)) {
        LOGE("Failed to extract SLUS_005.94 from disc image!");
        SDL_DestroyRenderer(sdlRenderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    recomp_init_dispatch_table();
    LOGI("Recompiled dispatch table initialized. Entry point: 0x%08X", entryPoint);

    // SDL3 Audio Stream (44100 Hz Stereo S16)
    SDL_AudioSpec audioSpec{};
    audioSpec.format = SDL_AUDIO_S16;
    audioSpec.channels = 2;
    audioSpec.freq = 44100;
    SDL_AudioStream *audioStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec, nullptr, nullptr);
    if (audioStream) {
        SDL_ResumeAudioStreamDevice(audioStream);
        LOGI("SDL3 audio stream opened at 44100 Hz stereo");
    }

    // Streaming texture for 1x native PS1 display (resized dynamically if aperture changes)
    uint32_t texW = 320, texH = 240;
    SDL_Texture *frameTex = SDL_CreateTexture(
        sdlRenderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, texW, texH);
    if (frameTex) {
        SDL_SetTextureScaleMode(frameTex, SDL_SCALEMODE_NEAREST);
    }

    std::vector<ps1::gpu::Color16> vramSnapshot(
        static_cast<size_t>(ps1::gpu::GPU::VRAM_WIDTH) * ps1::gpu::GPU::VRAM_HEIGHT);
    uint64_t lastContentRevision = UINT64_MAX;

    constexpr uint32_t guestRefreshHz = 60u;
    constexpr uint32_t guestScanlines = 263u;
    constexpr uint32_t guestCyclesPerVblank = CPU_CLOCK / guestRefreshHz;
    constexpr uint32_t baseCyclesPerScanline = guestCyclesPerVblank / guestScanlines;
    constexpr uint32_t extraScanlineCycles = guestCyclesPerVblank % guestScanlines;

    std::atomic<bool> gameFinished{false};
    std::atomic<bool> gameThreadDone{false};
    std::atomic<uint64_t> scheduledVblanks{0};
    std::mutex tickerMutex;
    std::condition_variable tickerStopped;
    const auto vblankPeriod = std::chrono::nanoseconds(1000000000ull / guestRefreshHz);

    std::thread vblankThread([&]() {
        using namespace std::chrono;
        auto next = steady_clock::now() + vblankPeriod;
        while (!gameFinished.load(std::memory_order_acquire)) {
            std::unique_lock lock(tickerMutex);
            if (tickerStopped.wait_until(lock, next, [&] {
                    return gameFinished.load(std::memory_order_acquire);
                })) {
                break;
            }
            lock.unlock();
            const auto now = steady_clock::now();
            next += vblankPeriod;
            if (next <= now) next = now + vblankPeriod;
            scheduledVblanks.fetch_add(1, std::memory_order_release);
        }
    });

    std::thread gameThread([&]() {
        setpriority(PRIO_PROCESS, 0, -16);
        bios->setGameThreadId(std::this_thread::get_id());
        ctx->pc = entryPoint;
        while (!gameFinished.load(std::memory_order_acquire)) {
            try {
                if (ctx->nativeResumePc != 0)
                    recomp_resume_dispatch(memory->ramPtr(), ctx.get(), ctx->nativeResumePc);
                else
                    recomp_dispatch(memory->ramPtr(), ctx.get(), ctx->pc);
                break;
            } catch (const ps1::bios::ShutdownRequested &) {
                break;
            } catch (const ps1::SaveStateTimelineReload &) {
                bios->restartNativeThreads();
                ctx->nativeResumePc = ctx->pc;
            } catch (const std::exception &e) {
                LOGE("[GameThread] Exception: %s", e.what());
                break;
            } catch (...) {
                LOGE("[GameThread] Unknown exception");
                break;
            }
        }
        gameFinished.store(true, std::memory_order_release);
        gameThreadDone.store(true, std::memory_order_release);
    });

    uint64_t processedVblanks = 0;
    uint16_t prevPressedMask = 0;
    std::vector<int16_t> audioBuf(735 * 2);
    bool running = true;

    while (running && !gameFinished.load(std::memory_order_acquire)) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                // Surface resized (e.g., SDL briefly rotated the EGL surface during init)
                // Re-read actual pixel dimensions so render viewport stays correct
                SDL_GetWindowSizeInPixels(window, &winW, &winH);
                LOGI("SDL surface resized -> %dx%d, recalculating viewport", winW, winH);
            }
        }

        // Poll Wear OS touch + gyroscope input
        MGSWearPadState wearPad{};
        MGSWear_PollInput(&wearPad);
        applyWearPadToPs1Input(wearPad, *input);

        const uint64_t dueVblanks = scheduledVblanks.load(std::memory_order_acquire);
        const auto &pacingState = ps1::psyq::psyq_state();
        const uint32_t currentGuestVblank = pacingState.vsyncCounter.load(std::memory_order_acquire);
        const bool guestVblankRequested = ps1::timing::guestVblankRequested(
            pacingState.blockingVSyncPacing.load(std::memory_order_acquire),
            pacingState.blockingVSyncTarget.load(std::memory_order_acquire),
            currentGuestVblank,
            pacingState.autonomousClockWaiters.load(std::memory_order_acquire) +
                (pacingState.cdClockDemand.load(std::memory_order_acquire) ? 1u : 0u));

        const ps1::timing::VblankSlice vblankSlice = ps1::timing::planVblankSlice(
            processedVblanks, guestVblankRequested ? dueVblanks : processedVblanks, 2);

        bool guestAdvanced = false;
        while (processedVblanks < vblankSlice.end) {
            guestAdvanced = true;

            cdromCtrl->accrueClockCycles(guestCyclesPerVblank);
            if (spu->guestClockEnabled()) {
                spu->accrueSamples(ps1::spu::SAMPLE_RATE / guestRefreshHz);
            }

            uint32_t timerIrqs = 0;
            uint32_t timerIrqCounts[3] = {};
            for (uint32_t scanline = 0; scanline < guestScanlines; ++scanline) {
                const uint32_t cycles =
                    baseCyclesPerScanline + (scanline < extraScanlineCycles ? 1u : 0u);
                timerIrqs |= timers->tick(cycles, true, false);
                for (std::size_t timer = 0; timer < 3; ++timer)
                    timerIrqCounts[timer] += timers->lastIrqCount(timer);
            }

            static constexpr uint32_t timerBits[3] = {
                ps1::IRQ_TMR0, ps1::IRQ_TMR1, ps1::IRQ_TMR2};
            for (std::size_t timer = 0; timer < 3; ++timer) {
                if ((timerIrqs & timerBits[timer]) == 0) continue;
                irqCtrl->raiseInterrupt(timerBits[timer]);
                if ((irqCtrl->readIMask() & timerBits[timer]) != 0) {
                    bios->triggerRootCounterEvents(static_cast<uint32_t>(timer),
                                                   timerIrqCounts[timer]);
                }
            }

            auto &timingState = ps1::psyq::psyq_state();
            const uint32_t newCount =
                timingState.vsyncCounter.load(std::memory_order_relaxed) + 1;

            gpu->snapshotDisplayBuffer(newCount);
            input->latchVBlank();

            irqCtrl->raiseInterrupt(ps1::IRQ_VBLANK);
            timingState.vsyncCounter.store(newCount, std::memory_order_release);
            timingState.pendingVblanks.fetch_add(1, std::memory_order_release);
            ++processedVblanks;

            // Pump SPU audio (735 stereo samples per 60Hz VBlank = 44100 Hz)
            if (audioStream) {
                spu->generateSamples(audioBuf.data(), 735);
                if (SDL_GetAudioStreamAvailable(audioStream) < 44100 * 4 / 5) {
                    SDL_PutAudioStreamData(audioStream, audioBuf.data(),
                                           static_cast<int>(audioBuf.size() * sizeof(int16_t)));
                }
            }
        }

        if (guestAdvanced) {
            const uint64_t scheduledBeforeRebase =
                scheduledVblanks.load(std::memory_order_acquire);
            const ps1::timing::PacingCreditRebase pacingRebase =
                ps1::timing::planPacingCreditRebase(processedVblanks, scheduledBeforeRebase);
            if (pacingRebase.coalesced != 0) {
                scheduledVblanks.exchange(pacingRebase.retainedDue, std::memory_order_acq_rel);
            }
        } else if (!guestVblankRequested && dueVblanks > processedVblanks) {
            const ps1::timing::PacingCreditRebase idleRebase =
                ps1::timing::planPacingCreditRebase(processedVblanks, dueVblanks);
            if (idleRebase.coalesced != 0) {
                scheduledVblanks.exchange(idleRebase.retainedDue, std::memory_order_acq_rel);
            }
        }

        if (cdromCtrl->hasInterrupt()) irqCtrl->raiseInterrupt(ps1::IRQ_CDROM);
        if (dma->hasInterrupt()) irqCtrl->raiseInterrupt(ps1::IRQ_DMA);
        if (input->hasInterrupt()) {
            irqCtrl->raiseInterrupt(ps1::IRQ_PAD_MC);
            input->clearInterrupt();
        }
        if (!spu->guestClockEnabled() && spu->hasIrq()) {
            irqCtrl->raiseInterrupt(ps1::IRQ_SPU);
            spu->clearIrq();
        }

        if (guestAdvanced) {
            ps1::gpu::GPU::DisplayState dispState{};
            const auto copyStatus = gpu->copyDisplayVRAMIfChanged(
                vramSnapshot, lastContentRevision, &dispState);
            if (copyStatus == ps1::gpu::GPU::DisplayCopyStatus::Copied) {
                lastContentRevision = dispState.contentRevision;
                const ps1::gpu::DisplayAperture ap = ps1::gpu::deriveDisplayAperture(
                    dispState, ps1::gpu::DisplayAreaPolicy::Programmed);
                const uint32_t apW = std::max(1u, std::min(ap.width, 640u));
                const uint32_t apH = std::max(1u, std::min(ap.height, 480u));

                if (apW != texW || apH != texH) {
                    texW = apW;
                    texH = apH;
                    if (frameTex) SDL_DestroyTexture(frameTex);
                    frameTex = SDL_CreateTexture(
                        sdlRenderer, SDL_PIXELFORMAT_XRGB8888,
                        SDL_TEXTUREACCESS_STREAMING, texW, texH);
                    if (frameTex) SDL_SetTextureScaleMode(frameTex, SDL_SCALEMODE_NEAREST);
                }

                if (frameTex) {
                    void *dstPixels = nullptr;
                    int dstPitch = 0;
                    if (SDL_LockTexture(frameTex, nullptr, &dstPixels, &dstPitch)) {
                        for (uint32_t y = 0; y < texH; ++y) {
                            const uint32_t vramY = (ap.y + y) & 511u;
                            const ps1::gpu::Color16 *srcRow =
                                &vramSnapshot[vramY * ps1::gpu::GPU::VRAM_WIDTH];
                            uint32_t *dstRow = reinterpret_cast<uint32_t *>(
                                static_cast<uint8_t *>(dstPixels) + y * dstPitch);
                            if (ap.enabled) {
                                for (uint32_t x = 0; x < texW; ++x) {
                                    dstRow[x] = s_rgb555Lut[srcRow[(ap.x + x) & 1023u].raw];
                                }
                            } else {
                                std::memset(dstRow, 0, texW * sizeof(uint32_t));
                            }
                        }

                        SDL_UnlockTexture(frameTex);
                    }
                }

                SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
                SDL_RenderClear(sdlRenderer);
                if (frameTex) {
                    const int screenMode = MGSWear_GetScreenMode();
                    SDL_FRect dstRect{};

                    if (screenMode == 0) {
                        // Mode 0: The Big One (Default)
                        // Authentic 4:3 PS1 aspect ratio scaled to fill circular watch screen (zero black borders, perfectly proportioned)
                        const float targetH = static_cast<float>(winH);
                        const float targetW = targetH * (4.0f / 3.0f);
                        dstRect = {
                            (winW - targetW) * 0.5f,
                            0.0f,
                            targetW,
                            targetH
                        };
                    } else if (screenMode == 1) {
                        // Mode 1: Authentic 4:3 Inscribed (Fit to width, classic PS1 proportions)
                        const float targetW = static_cast<float>(winW);
                        const float targetH = targetW * (3.0f / 4.0f);
                        dstRect = {
                            0.0f,
                            (winH - targetH) * 0.5f,
                            targetW,
                            targetH
                        };
                    } else {
                        // Mode 2: 1:1 Square Fill (touches circular edges at 4 cardinal points)
                        dstRect = { 0.0f, 0.0f, static_cast<float>(winW), static_cast<float>(winH) };
                    }
                    SDL_RenderTexture(sdlRenderer, frameTex, nullptr, &dstRect);
                }
                SDL_RenderPresent(sdlRenderer);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }

    bios->requestShutdown();
    {
        std::lock_guard lock(tickerMutex);
        gameFinished.store(true, std::memory_order_release);
    }
    stateCoordinator->cancel();
    tickerStopped.notify_all();
    if (gameThread.joinable()) gameThread.join();
    if (vblankThread.joinable()) vblankThread.join();

    if (audioStream) SDL_DestroyAudioStream(audioStream);
    if (frameTex) SDL_DestroyTexture(frameTex);
    SDL_DestroyRenderer(sdlRenderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
