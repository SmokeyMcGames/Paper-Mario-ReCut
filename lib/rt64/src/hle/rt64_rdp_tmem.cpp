//
// RT64
//

#include "rt64_rdp_tmem.h"

#include <cassert>
#include <charconv>
#include <cinttypes>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#include "xxHash/xxh3.h"

#include "common/rt64_tmem_hasher.h"

#include "rt64_state.h"

namespace RT64 {
    namespace {
        struct RGBA8 {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint8_t a = 255;
        };

        std::string orderedDumpBaseName(uint32_t sequence, uint64_t hash) {
            char baseName[80];
            snprintf(baseName, sizeof(baseName), "%06u_%016" PRIx64 ".v%u", sequence, hash, TMEMHasher::CurrentHashVersion);
            return std::string(baseName);
        }

        uint8_t expand5(uint32_t value) {
            return uint8_t((value << 3U) | (value >> 2U));
        }

        uint8_t expand4(uint32_t value) {
            return uint8_t((value << 4U) | value);
        }

        uint8_t expandIA4Intensity(uint32_t value) {
            uint32_t intensity = value & 0xEU;
            return uint8_t((intensity << 4U) | (intensity << 1U) | (intensity >> 2U));
        }

        RGBA8 decodeRGBA16(uint32_t value) {
            return {
                expand5((value >> 11U) & 0x1FU),
                expand5((value >> 6U) & 0x1FU),
                expand5((value >> 1U) & 0x1FU),
                (value & 1U) ? uint8_t(255) : uint8_t(0)
            };
        }

        RGBA8 decodeIA16(uint32_t value) {
            const uint8_t intensity = uint8_t((value >> 8U) & 0xFFU);
            return { intensity, intensity, intensity, uint8_t(value & 0xFFU) };
        }

        uint32_t textureDrawBytesPerRow(const LoadTile &loadTile, uint16_t width) {
            const bool rgba32 = (loadTile.fmt == G_IM_FMT_RGBA) && (loadTile.siz == G_IM_SIZ_32b);
            return std::max(uint32_t(width) << (rgba32 ? G_IM_SIZ_16b : loadTile.siz) >> 1U, 1U);
        }

        uint8_t loadTmemMasked(const uint8_t *tmem, uint32_t relativeAddress, uint32_t maskAddress, uint32_t orAddress, bool oddRow, uint32_t textureStart, uint32_t rowSize) {
            if (rowSize == 0) {
                rowSize = 1;
            }

            const uint32_t rowStart = (relativeAddress / rowSize) * rowSize;
            const uint32_t wordIndex = (relativeAddress - rowStart) / 4U;
            const uint32_t swapWordIndex = wordIndex ^ 1U;
            const uint32_t finalAddress = oddRow
                ? textureStart + rowStart + (swapWordIndex * 4U) + (relativeAddress & 0x3U)
                : textureStart + relativeAddress;
            return tmem[((finalAddress & maskAddress) | orAddress) & 0xFFFU];
        }

        RGBA8 sampleTmem(const uint8_t *tmem, const LoadTile &loadTile, uint16_t width, int32_t x, int32_t y, uint32_t tlut) {
            const bool oddRow = (y & 1) != 0;
            const bool oddColumn = (x & 1) != 0;
            const bool rgba32 = (loadTile.fmt == G_IM_FMT_RGBA) && (loadTile.siz == G_IM_SIZ_32b);
            const bool usesTlut = tlut > 0;
            const uint32_t tmemShift = rgba32 ? G_IM_SIZ_16b : loadTile.siz;
            const uint32_t addressMask = (rgba32 || usesTlut) ? 0x7FFU : 0xFFFU;
            const uint32_t textureStart = uint32_t(loadTile.tmem) << 3U;
            const uint32_t stride = std::max(uint32_t(loadTile.line) << 3U, textureDrawBytesPerRow(loadTile, width));
            const uint32_t pixelAddress = uint32_t(y) * stride + ((uint32_t(x) << tmemShift) >> 1U);
            const uint8_t pixelValue0 = loadTmemMasked(tmem, pixelAddress + 0U, addressMask, 0x0U, oddRow, textureStart, stride);
            const uint8_t pixelValue1 = loadTmemMasked(tmem, pixelAddress + 1U, addressMask, 0x0U, oddRow, textureStart, stride);
            const uint32_t pixelValue4 = (pixelValue0 >> (oddColumn ? 0U : 4U)) & 0xFU;

            if (usesTlut) {
                const uint32_t paletteAddress = (loadTile.siz == G_IM_SIZ_4b)
                    ? 0x800U + (uint32_t(loadTile.palette) << 7U) + (pixelValue4 << 3U)
                    : 0x800U + (uint32_t(pixelValue0) << 3U);
                const uint32_t paletteValue = uint32_t(tmem[(paletteAddress + 1U) & 0xFFFU]) | (uint32_t(tmem[paletteAddress & 0xFFFU]) << 8U);
                if (tlut == G_TT_RGBA16) {
                    return decodeRGBA16(paletteValue);
                }
                else if (tlut == G_TT_IA16) {
                    return decodeIA16(paletteValue);
                }

                return { 0, 0, 0, 255 };
            }

            const uint32_t pixelAddress2 = rgba32 ? pixelAddress : pixelAddress + 2U;
            const uint32_t orAddress = rgba32 ? 0x800U : 0x0U;
            const uint8_t pixelValue2 = loadTmemMasked(tmem, pixelAddress2 + 0U, addressMask, orAddress, oddRow, textureStart, stride);
            const uint8_t pixelValue3 = loadTmemMasked(tmem, pixelAddress2 + 1U, addressMask, orAddress, oddRow, textureStart, stride);

            switch (loadTile.siz) {
            case G_IM_SIZ_4b:
                switch (loadTile.fmt) {
                case G_IM_FMT_IA: {
                    const uint8_t intensity = expandIA4Intensity(pixelValue4);
                    return { intensity, intensity, intensity, (pixelValue4 & 1U) ? uint8_t(255) : uint8_t(0) };
                }
                case G_IM_FMT_I:
                case G_IM_FMT_RGBA:
                case G_IM_FMT_CI: {
                    const uint8_t intensity = expand4(pixelValue4);
                    return { intensity, intensity, intensity, 255 };
                }
                default:
                    return { 0, 0, 0, 255 };
                }
            case G_IM_SIZ_8b:
                switch (loadTile.fmt) {
                case G_IM_FMT_IA: {
                    const uint8_t intensity = expand4((pixelValue0 >> 4U) & 0xFU);
                    const uint8_t alpha = expand4(pixelValue0 & 0xFU);
                    return { intensity, intensity, intensity, alpha };
                }
                case G_IM_FMT_I:
                case G_IM_FMT_RGBA:
                case G_IM_FMT_CI:
                    return { pixelValue0, pixelValue0, pixelValue0, 255 };
                default:
                    return { 0, 0, 0, 255 };
                }
            case G_IM_SIZ_16b: {
                const uint32_t value16 = uint32_t(pixelValue1) | (uint32_t(pixelValue0) << 8U);
                switch (loadTile.fmt) {
                case G_IM_FMT_RGBA:
                    return decodeRGBA16(value16);
                case G_IM_FMT_IA:
                    return decodeIA16(value16);
                case G_IM_FMT_CI:
                case G_IM_FMT_I:
                    return { pixelValue0, pixelValue1, pixelValue0, pixelValue1 };
                default:
                    return { 0, 0, 0, 255 };
                }
            }
            case G_IM_SIZ_32b:
                switch (loadTile.fmt) {
                case G_IM_FMT_RGBA:
                    return { pixelValue0, pixelValue1, pixelValue2, pixelValue3 };
                case G_IM_FMT_CI:
                case G_IM_FMT_IA:
                case G_IM_FMT_I:
                    return oddColumn
                        ? RGBA8 { pixelValue0, pixelValue1, pixelValue0, pixelValue1 }
                        : RGBA8 { pixelValue2, pixelValue3, pixelValue2, pixelValue3 };
                default:
                    return { 0, 0, 0, 255 };
                }
            default:
                return { 0, 0, 0, 255 };
            }
        }

        bool decodeTextureToRGBA(const uint8_t *tmem, const LoadTile &loadTile, uint16_t width, uint16_t height, uint32_t tlut, std::vector<uint8_t> &rgba) {
            if ((width == 0) || (height == 0) || (width > 0x1000) || (height > 0x1000)) {
                return false;
            }

            if ((loadTile.fmt == G_IM_FMT_YUV) || (loadTile.fmt == G_IM_FMT_DEPTH)) {
                return false;
            }

            rgba.resize(size_t(width) * size_t(height) * 4U);
            for (uint16_t y = 0; y < height; y++) {
                for (uint16_t x = 0; x < width; x++) {
                    const RGBA8 color = sampleTmem(tmem, loadTile, width, x, y, tlut);
                    const size_t pixelOffset = (size_t(y) * size_t(width) + size_t(x)) * 4U;
                    rgba[pixelOffset + 0U] = color.r;
                    rgba[pixelOffset + 1U] = color.g;
                    rgba[pixelOffset + 2U] = color.b;
                    rgba[pixelOffset + 3U] = color.a;
                }
            }

            return true;
        }

        bool hasHashInFilename(const std::string &fileName, uint64_t hash) {
            return fileName.find(ReplacementDatabase::hashToString(hash)) != std::string::npos;
        }

        bool parseDumpFilename(const std::filesystem::path &path, uint32_t &sequence, uint64_t &hash) {
            const std::string stem = path.stem().u8string();
            const size_t underscore = stem.find('_');
            const size_t version = stem.find(".v", underscore == std::string::npos ? 0 : underscore + 1);
            if ((underscore == std::string::npos) || (version == std::string::npos) || (underscore == 0) || (version <= underscore + 1)) {
                return false;
            }

            const std::string sequenceText = stem.substr(0, underscore);
            const std::string hashText = stem.substr(underscore + 1, version - underscore - 1);
            uint32_t parsedSequence = 0;
            uint64_t parsedHash = 0;
            const auto sequenceResult = std::from_chars(sequenceText.data(), sequenceText.data() + sequenceText.size(), parsedSequence, 10);
            const auto hashResult = std::from_chars(hashText.data(), hashText.data() + hashText.size(), parsedHash, 16);
            if ((sequenceResult.ec != std::errc{}) || (hashResult.ec != std::errc{})) {
                return false;
            }

            sequence = parsedSequence;
            hash = parsedHash;
            return true;
        }

        bool dumpTexturePNG(const std::filesystem::path &directory, uint32_t sequence, uint64_t hash, State *state, const LoadTile &loadTile, uint16_t width, uint16_t height, uint32_t tlut) {
            const std::string baseName = orderedDumpBaseName(sequence, hash);
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error) {
                return false;
            }

            const std::filesystem::path pngPath = directory / (baseName + ".png");
            if (std::filesystem::exists(pngPath)) {
                return false;
            }

            std::filesystem::directory_iterator iterator(directory, error);
            const std::filesystem::directory_iterator end;
            while (!error && (iterator != end)) {
                if (iterator->is_regular_file(error) && !error) {
                    const std::filesystem::path candidatePath = iterator->path();
                    if ((candidatePath.extension() == ".png") && hasHashInFilename(candidatePath.filename().u8string(), hash)) {
                        std::filesystem::rename(candidatePath, pngPath, error);
                        if (!error) {
                            return false;
                        }

                        error.clear();
                    }
                }

                iterator.increment(error);
            }

            const uint8_t *tmem = reinterpret_cast<const uint8_t *>(state->rdp->TMEM);
            std::vector<uint8_t> rgba;
            if (!decodeTextureToRGBA(tmem, loadTile, width, height, tlut, rgba)) {
                return false;
            }

            const std::string pngPathUtf8 = pngPath.u8string();
            return stbi_write_png(pngPathUtf8.c_str(), width, height, 4, rgba.data(), int(width) * 4) != 0;
        }

    }

    // TextureManager

    void TextureManager::uploadEmpty(State *state, TextureCache *textureCache, uint64_t creationFrame, uint16_t width, uint16_t height, uint64_t replacementHash) {
        if (hashSet.find(replacementHash) == hashSet.end()) {
            hashSet.insert(replacementHash);
            textureCache->queueGPUUploadTMEM(replacementHash, creationFrame, nullptr, 0, width, height, 0, LoadTile(), false);
        }
    }

    uint64_t TextureManager::uploadTMEM(State *state, const LoadTile &loadTile, TextureCache *textureCache, uint64_t creationFrame, uint16_t byteOffset, uint16_t byteCount, uint16_t width, uint16_t height, uint32_t tlut) {
        XXH3_state_t xxh3;
        XXH3_64bits_reset(&xxh3);
        const uint8_t *TMEM = reinterpret_cast<const uint8_t *>(state->rdp->TMEM);
        XXH3_64bits_update(&xxh3, &TMEM[byteOffset], byteCount);
        XXH3_64bits_update(&xxh3, &byteOffset, sizeof(byteOffset));
        XXH3_64bits_update(&xxh3, &byteCount, sizeof(byteCount));
        const uint64_t hash = XXH3_64bits_digest(&xxh3);
        if (hashSet.find(hash) == hashSet.end()) {
            hashSet.insert(hash);
            textureCache->queueGPUUploadTMEM(hash, creationFrame, TMEM, RDP_TMEM_BYTES, width, height, 0, LoadTile(), false);
        }
        
        // Dump memory contents into a file if the process is active.
        if (!state->dumpingTexturesDirectory.empty()) {
            // Since width and height are not exactly guaranteed to be sane values when using raw TMEM, ensure we only dump textures when the values make sense.
            const bool validTextureCheck = (width > 0x0) && (height > 0x0);
            const bool bigTextureCheck = (width > 0x1000) || (height > 0x1000);
            if (validTextureCheck && !bigTextureCheck) {
                dumpTexture(hash, state, loadTile, width, height, tlut);
            }
        }

        return hash;
    }

    uint64_t TextureManager::uploadTexture(State *state, const LoadTile &loadTile, TextureCache *textureCache, uint64_t creationFrame, uint16_t width, uint16_t height, uint32_t tlut) {
        const uint8_t *TMEM = reinterpret_cast<const uint8_t *>(state->rdp->TMEM);
        uint64_t hash = TMEMHasher::hash(TMEM, loadTile, width, height, tlut, TMEMHasher::CurrentHashVersion);
        if (hashSet.find(hash) == hashSet.end()) {
            hashSet.insert(hash);
            textureCache->queueGPUUploadTMEM(hash, creationFrame, TMEM, RDP_TMEM_BYTES, width, height, tlut, loadTile, true);
        }

        // Dump memory contents into a file if the process is active.
        if (!state->dumpingTexturesDirectory.empty()) {
            dumpTexture(hash, state, loadTile, width, height, tlut);
        }

        return hash;
    }

    void TextureManager::seedDumpedTexturesFromDirectory(const std::filesystem::path &directory) {
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error) || error) {
            return;
        }

        uint32_t maxSequence = dumpSequence;
        std::filesystem::directory_iterator iterator(
            directory,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::directory_iterator end;
        while (!error && (iterator != end)) {
            if (iterator->is_regular_file(error) && !error && iterator->path().extension() == ".png") {
                uint32_t parsedSequence = 0;
                uint64_t parsedHash = 0;
                if (parseDumpFilename(iterator->path(), parsedSequence, parsedHash)) {
                    dumpedSet.insert(parsedHash);
                    maxSequence = std::max(maxSequence, parsedSequence);
                }
            }

            iterator.increment(error);
        }

        dumpSequence = maxSequence;
        dumpWrittenCount = 0;
    }

    void TextureManager::dumpTexture(uint64_t hash, State *state, const LoadTile &loadTile, uint16_t width, uint16_t height, uint32_t tlut) {
        if (dumpedSet.find(hash) != dumpedSet.end()) {
            return;
        }

        // Insert into set regardless of whether the dump is successful or not.
        dumpedSet.insert(hash);
        const uint32_t sequence = ++dumpSequence;
        
        // Dump the entirety of TMEM.
        if (dumpTexturePNG(state->dumpingTexturesDirectory, sequence, hash, state, loadTile, width, height, tlut)) {
            dumpWrittenCount++;
        }
    }

    void TextureManager::removeHashes(const std::vector<uint64_t> &hashes) {
        for (uint64_t hash : hashes) {
            hashSet.erase(hash);
        }
    }
};
