#include "builtin_texture_pack.h"

#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace paper_mario {
    namespace {
        constexpr char BuiltinTextureDatabase[] =
            "{\n"
            "    \"configuration\": {\n"
            "        \"configurationVersion\": 3,\n"
            "        \"autoPath\": \"rt64\",\n"
            "        \"defaultOperation\": \"stream\",\n"
            "        \"defaultShift\": \"half\",\n"
            "        \"hashVersion\": 5\n"
            "    },\n"
            "    \"textures\": [],\n"
            "    \"operationFilters\": [],\n"
            "    \"shiftFilters\": [],\n"
            "    \"extraFiles\": []\n"
            "}\n";

        struct BuiltinTextureResource {
            const wchar_t* resource_name;
            const char* relative_path;
        };

        constexpr BuiltinTextureResource BuiltinTextures[] = {
#include "builtin_texture_pack_entries.inc"
        };

        bool file_matches_bytes(const std::filesystem::path& path, const void* data, size_t size) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error || (std::filesystem::file_size(path, error) != size) || error) {
                return false;
            }

            std::ifstream input(path, std::ios::binary);
            if (!input.good()) {
                return false;
            }

            std::vector<char> bytes(size);
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            return input.good() && (std::memcmp(bytes.data(), data, size) == 0);
        }

        bool write_file_if_different(const std::filesystem::path& path, const void* data, size_t size) {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) {
                return false;
            }

            if (file_matches_bytes(path, data, size)) {
                return true;
            }

            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output.good()) {
                return false;
            }

            output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            return output.good();
        }
    }

    bool ensure_builtin_texture_pack(const std::filesystem::path& directory) {
#ifdef _WIN32
        if (directory.empty()) {
            return false;
        }

        if (!write_file_if_different(directory / "rt64.json", BuiltinTextureDatabase, sizeof(BuiltinTextureDatabase) - 1)) {
            return false;
        }

        HMODULE module = GetModuleHandleW(nullptr);
        for (const BuiltinTextureResource& texture : BuiltinTextures) {
            HRSRC resource = FindResourceW(module, texture.resource_name, MAKEINTRESOURCEW(10));
            if (resource == nullptr) {
                return false;
            }

            HGLOBAL loaded_resource = LoadResource(module, resource);
            const DWORD resource_size = SizeofResource(module, resource);
            const void* resource_data = LockResource(loaded_resource);
            if ((loaded_resource == nullptr) || (resource_data == nullptr) || (resource_size == 0)) {
                return false;
            }

            if (!write_file_if_different(directory / std::filesystem::u8path(texture.relative_path), resource_data, resource_size)) {
                return false;
            }
        }

        return true;
#else
        (void)directory;
        return true;
#endif
    }
}
