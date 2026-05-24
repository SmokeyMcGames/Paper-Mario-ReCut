//
// RT64
//

#include "rt64_rdp_tmem.h"

#include <cassert>
#include <cinttypes>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
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

        std::string replacementBaseName(uint64_t hash) {
            char baseName[64];
            snprintf(baseName, sizeof(baseName), "%016" PRIx64 ".v%u", hash, TMEMHasher::CurrentHashVersion);
            return std::string(baseName);
        }

        std::string textureFormatName(const LoadTile &loadTile, uint32_t tlut) {
            const char *fmtName = "unknown";
            switch (loadTile.fmt) {
            case G_IM_FMT_RGBA:
                fmtName = "rgba";
                break;
            case G_IM_FMT_YUV:
                fmtName = "yuv";
                break;
            case G_IM_FMT_CI:
                fmtName = "ci";
                break;
            case G_IM_FMT_IA:
                fmtName = "ia";
                break;
            case G_IM_FMT_I:
                fmtName = "i";
                break;
            case G_IM_FMT_DEPTH:
                fmtName = "depth";
                break;
            }

            const char *sizName = "unknown";
            switch (loadTile.siz) {
            case G_IM_SIZ_4b:
                sizName = "4b";
                break;
            case G_IM_SIZ_8b:
                sizName = "8b";
                break;
            case G_IM_SIZ_16b:
                sizName = "16b";
                break;
            case G_IM_SIZ_32b:
                sizName = "32b";
                break;
            }

            std::string name = std::string(fmtName) + "_" + sizName;
            if (tlut == G_TT_RGBA16) {
                name += "_tlut_rgba16";
            }
            else if (tlut == G_TT_IA16) {
                name += "_tlut_ia16";
            }

            return name;
        }

        std::filesystem::path replacementTextureFolder(const LoadTile &loadTile, uint32_t tlut) {
            if ((tlut > 0) || (loadTile.fmt == G_IM_FMT_CI)) {
                return std::filesystem::path("sprites") / "unassigned";
            }

            switch (loadTile.fmt) {
            case G_IM_FMT_RGBA:
                return std::filesystem::path("models") / "unassigned";
            case G_IM_FMT_IA:
            case G_IM_FMT_I:
                return std::filesystem::path("masks") / "unassigned";
            case G_IM_FMT_YUV:
            case G_IM_FMT_DEPTH:
            default:
                return std::filesystem::path("misc") / "unassigned";
            }
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

        void dumpTextureMetadata(const std::filesystem::path &metadataPath, uint64_t hash, const LoadTile &loadTile, uint16_t width, uint16_t height, uint32_t tlut) {
            if (std::filesystem::exists(metadataPath)) {
                return;
            }

            std::ofstream metadataStream(metadataPath);
            if (!metadataStream.is_open()) {
                return;
            }

            json jroot;
            const std::filesystem::path folder = replacementTextureFolder(loadTile, tlut);
            const auto category = folder.begin();
            jroot["hash"] = ReplacementDatabase::hashToString(hash);
            jroot["hashVersion"] = TMEMHasher::CurrentHashVersion;
            jroot["category"] = (category != folder.end()) ? category->u8string() : "misc";
            jroot["object"] = "unassigned";
            jroot["format"] = textureFormatName(loadTile, tlut);
            jroot["width"] = width;
            jroot["height"] = height;
            jroot["tile"] = loadTile;
            if (tlut == G_TT_RGBA16) {
                jroot["tlut"] = LoadTLUT::RGBA16;
            }
            else if (tlut == G_TT_IA16) {
                jroot["tlut"] = LoadTLUT::IA16;
            }
            else {
                jroot["tlut"] = LoadTLUT::None;
            }

            metadataStream << std::setw(4) << jroot << std::endl;
        }

        void dumpTexturePNG(const std::filesystem::path &directory, uint64_t hash, State *state, const LoadTile &loadTile, uint16_t width, uint16_t height, uint32_t tlut) {
            const std::string baseName = replacementBaseName(hash);
            const std::filesystem::path textureFolder = directory / replacementTextureFolder(loadTile, tlut);
            std::error_code error;
            std::filesystem::create_directories(textureFolder, error);
            if (error) {
                return;
            }

            const std::filesystem::path pngPath = textureFolder / (baseName + ".png");
            const std::filesystem::path metadataPath = textureFolder / (baseName + ".texture.json");
            dumpTextureMetadata(metadataPath, hash, loadTile, width, height, tlut);
            if (std::filesystem::exists(pngPath)) {
                return;
            }

            const uint8_t *tmem = reinterpret_cast<const uint8_t *>(state->rdp->TMEM);
            std::vector<uint8_t> rgba;
            if (!decodeTextureToRGBA(tmem, loadTile, width, height, tlut, rgba)) {
                return;
            }

            const std::string pngPathUtf8 = pngPath.u8string();
            stbi_write_png(pngPathUtf8.c_str(), width, height, 4, rgba.data(), int(width) * 4);
        }

        void ensureReplacementDatabaseEntry(const std::filesystem::path &directory, uint64_t hash) {
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error) {
                return;
            }

            ReplacementDatabase database;
            database.config.autoPath = ReplacementAutoPath::RT64;
            database.config.defaultOperation = ReplacementOperation::Stream;
            database.config.defaultShift = ReplacementShift::Half;
            database.config.hashVersion = TMEMHasher::CurrentHashVersion;

            const std::filesystem::path databasePath = directory / ReplacementDatabaseFilename;
            if (std::filesystem::exists(databasePath)) {
                std::ifstream databaseStream(databasePath);
                if (databaseStream.is_open()) {
                    try {
                        json parsed;
                        databaseStream >> parsed;
                        database = parsed;
                    }
                    catch (const nlohmann::detail::exception &) {
                        database = ReplacementDatabase();
                    }
                }
            }

            database.config.autoPath = ReplacementAutoPath::RT64;
            database.config.hashVersion = TMEMHasher::CurrentHashVersion;
            database.buildHashMaps();

            if (!database.getReplacement(hash).isEmpty()) {
                return;
            }

            ReplacementTexture texture;
            texture.hashes.rt64 = ReplacementDatabase::hashToString(hash);
            database.addReplacement(texture);

            std::ofstream databaseStream(databasePath);
            if (databaseStream.is_open()) {
                json serialized = database;
                databaseStream << std::setw(4) << serialized << std::endl;
            }
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

    void TextureManager::dumpTexture(uint64_t hash, State *state, const LoadTile &loadTile, uint16_t width, uint16_t height, uint32_t tlut) {
        if (dumpedSet.find(hash) != dumpedSet.end()) {
            return;
        }

        // Insert into set regardless of whether the dump is successful or not.
        dumpedSet.insert(hash);
        
        // Dump the entirety of TMEM.
        const std::string baseName = replacementBaseName(hash);
        ensureReplacementDatabaseEntry(state->dumpingTexturesDirectory, hash);
        dumpTexturePNG(state->dumpingTexturesDirectory, hash, state, loadTile, width, height, tlut);

        const std::filesystem::path rawDumpDirectory = state->dumpingTexturesDirectory / "_debug" / "raw";
        std::error_code rawDumpError;
        std::filesystem::create_directories(rawDumpDirectory, rawDumpError);
        const std::filesystem::path &dumpDirectory = rawDumpError ? state->dumpingTexturesDirectory : rawDumpDirectory;

        std::filesystem::path dumpTmemPath = dumpDirectory / (baseName + ".tmem");
        std::ofstream dumpTmemStream(dumpTmemPath, std::ios::binary);
        if (dumpTmemStream.is_open()) {
            const char *TMEM = reinterpret_cast<const char *>(state->rdp->TMEM);
            dumpTmemStream.write(TMEM, RDP_TMEM_BYTES);
            dumpTmemStream.close();
        }

        // Dump the RDRAM last loaded into the TMEM address pointed to by the tile. Required for generating hashes used by Rice.
        const LoadOperation &loadOp = state->rdp->rice.lastLoadOpByTMEM[loadTile.tmem];
        uint32_t rdramStart = loadOp.texture.address;
        uint32_t rdramCount = 0;
        uint32_t commonBytesOffset = (loadOp.tile.uls >> 2) << loadOp.texture.siz >> 1;
        uint32_t commonBytesPerRow = loadOp.texture.width << loadOp.texture.siz >> 1;
        if (loadOp.type == LoadOperation::Type::Block) {
            uint32_t wordCount = ((loadOp.tile.lrs - loadOp.tile.uls) >> (4 - loadOp.tile.siz)) + 1;
            rdramStart = loadOp.texture.address + commonBytesOffset + commonBytesPerRow * loadOp.tile.ult;
            rdramCount = (wordCount << 3);

            // Increase the amount of RDRAM dumped by textures that require padding when using load block.
            commonBytesPerRow = std::max(commonBytesPerRow, uint32_t(loadTile.line) << 3U);
        }
        else if (loadOp.type == LoadOperation::Type::Tile) {
            uint32_t rowCount = 1 + ((loadOp.tile.lrt >> 2) - (loadOp.tile.ult >> 2));
            uint32_t tileWidth = ((loadOp.tile.lrs >> 2) - (loadOp.tile.uls >> 2));
            uint32_t wordsPerRow = (tileWidth >> (4 - loadOp.tile.siz)) + 1;
            rdramStart = loadOp.texture.address + commonBytesOffset + commonBytesPerRow * (loadOp.tile.ult >> 2);
            rdramCount = rowCount * commonBytesPerRow;
        }
        
        // Dump more RDRAM if necessary if it doesn't cover what the tile could possibly sample.
        uint32_t loadTileBpr = width << loadTile.siz >> 1;
        rdramCount = std::max(rdramCount, std::max(loadTileBpr, commonBytesPerRow) * height);

        if (rdramCount > 0) {
            std::filesystem::path dumpRdramPath = dumpDirectory / (baseName + ".rice.rdram");
            std::ofstream dumpRdramStream(dumpRdramPath, std::ios::binary);
            if (dumpRdramStream.is_open()) {
                const char *RDRAM = reinterpret_cast<const char *>(state->RDRAM);
                dumpRdramStream.write(&RDRAM[rdramStart], rdramCount);
                dumpRdramStream.close();
            }

            std::filesystem::path dumpRdramInfoPath = dumpDirectory / (baseName + ".rice.json");
            std::ofstream dumpRdramInfoStream(dumpRdramInfoPath);
            if (dumpRdramInfoStream.is_open()) {
                json jroot;
                jroot["tile"] = loadOp.tile;
                jroot["type"] = loadOp.type;
                jroot["texture"] = loadOp.texture;
                dumpRdramInfoStream << std::setw(4) << jroot << std::endl;
                dumpRdramInfoStream.close();
            }
        }
        
        // Repeat a similar process for dumping the palette.
        if (tlut > 0) {
            const bool CI4 = (loadTile.siz == G_IM_SIZ_4b);
            const int32_t paletteTMEM = (RDP_TMEM_WORDS >> 1) + (CI4 ? (loadTile.palette << 4) : 0);
            const LoadOperation &paletteLoadOp = state->rdp->rice.lastLoadOpByTMEM[paletteTMEM];
            uint32_t paletteBytesOffset = (paletteLoadOp.tile.uls >> 2) << paletteLoadOp.texture.siz >> 1;
            uint32_t paletteBytesPerRow = paletteLoadOp.texture.width << paletteLoadOp.texture.siz >> 1;
            const uint32_t rowCount = 1 + ((paletteLoadOp.tile.lrt >> 2) - (paletteLoadOp.tile.ult >> 2));
            const uint32_t wordsPerRow = ((paletteLoadOp.tile.lrs >> 2) - (paletteLoadOp.tile.uls >> 2)) + 1;
            uint32_t paletteRdramStart = paletteLoadOp.texture.address + paletteBytesOffset + paletteBytesPerRow * (paletteLoadOp.tile.ult >> 2);
            uint32_t paletteRdramCount = (rowCount - 1) * paletteBytesPerRow + (wordsPerRow << 3);
            if (paletteRdramCount > 0) {
                std::filesystem::path dumpPaletteRdramPath = dumpDirectory / (baseName + ".rice.palette.rdram");
                std::ofstream dumpPaletteRdramStream(dumpPaletteRdramPath, std::ios::binary);
                if (dumpPaletteRdramStream.is_open()) {
                    const char *RDRAM = reinterpret_cast<const char *>(state->RDRAM);
                    dumpPaletteRdramStream.write(&RDRAM[paletteRdramStart], paletteRdramCount);
                    dumpPaletteRdramStream.close();
                }
            }

            std::filesystem::path dumpPaletteRdramInfoPath = dumpDirectory / (baseName + ".rice.palette.json");
            std::ofstream dumpPaletteRdramInfoStream(dumpPaletteRdramInfoPath);
            if (dumpPaletteRdramInfoStream.is_open()) {
                json jroot;
                jroot["tile"] = paletteLoadOp.tile;
                jroot["type"] = paletteLoadOp.type;
                jroot["texture"] = paletteLoadOp.texture;
                dumpPaletteRdramInfoStream << std::setw(4) << jroot << std::endl;
                dumpPaletteRdramInfoStream.close();
            }
        }

        // Dump the parameters of the tile into a JSON file.
        std::filesystem::path dumpTilePath = dumpDirectory / (baseName + ".tile.json");
        std::ofstream dumpTileStream(dumpTilePath);
        if (dumpTileStream.is_open()) {
            json jroot;
            jroot["tile"] = loadTile;
            jroot["width"] = width;
            jroot["height"] = height;

            // Serialize the TLUT into an enum instead.
            if (tlut == G_TT_RGBA16) {
                jroot["tlut"] = LoadTLUT::RGBA16;
            }
            else if (tlut == G_TT_IA16) {
                jroot["tlut"] = LoadTLUT::IA16;
            }
            else {
                jroot["tlut"] = LoadTLUT::None;
            }

            dumpTileStream << std::setw(4) << jroot << std::endl;
            dumpTileStream.close();
        }
    }

    void TextureManager::removeHashes(const std::vector<uint64_t> &hashes) {
        for (uint64_t hash : hashes) {
            hashSet.erase(hash);
        }
    }
};
