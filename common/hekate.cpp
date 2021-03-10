/*
 * Copyright (c) 2020-2021 Studious Pancake
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "hekate.hpp"

#include "ini.h"
#include "reboot_to_payload.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <span>

namespace Hekate {

    namespace {

        int BootConfigHandler(void *user, char const *section, char const *name, char const *value) {
            auto list = reinterpret_cast<BootConfigList *>(user);

            /* ignore pre-config and global config entries. */
            if (section[0] == '\0' || std::strcmp(section, "config") == 0) {
                return 1;
            }

            /* Find existing entry. */
            auto it = std::find_if(list->begin(), list->end(), [section](BootConfig &cfg) {
                return cfg.name == section;
            });

            /* Create config entry if not existant. */
            BootConfig &config = (it != list->end()) ? *it : list->emplace_back(section, list->size() + 1);

            /* TODO: parse more information and display that. */
            (void)config;

            (void)name;
            (void)value;

            return 1;
        }

        constexpr char const *const PayloadPaths[] = {
            "sdmc:/atmosphere/reboot_payload.bin",
            "sdmc:/bootloader/update.bin",
            "sdmc:/bootloader/payloads/hekate.bin",
            "sdmc:/sept/payload.bin",
        };

        bool LoadPayload() {
            for (auto path : PayloadPaths) {
                /* Clear payload buffer. */
                std::memset(g_reboot_payload, 0xFF, sizeof(g_reboot_payload));

                /* Open potential hekate payload. */
                auto file = fopen(path, "r");
                if (file == nullptr)
                    continue;

                /* Read payload to buffer. */
                std::size_t ret = fread(g_reboot_payload, 1, sizeof(g_reboot_payload), file);

                /* Close file. */
                fclose(file);

                /* Verify hekate payload loaded successfully. */
                if (ret == 0 || *(u32 *)(g_reboot_payload + Hekate::MagicOffset) != Hekate::Magic)
                    continue;

                return true;
            }

            return false;
        }

    }

    BootConfigList LoadBootConfigList() {
        BootConfigList configs;
        ini_parse("sdmc:/bootloader/hekate_ipl.ini", BootConfigHandler, &configs);
        return configs;
    }

    BootConfigList LoadIniConfigList() {
        BootConfigList configs;

        if (chdir("sdmc:/bootloader/ini") != 0)
            return configs;

        /* Open ini folder */
        auto dirp = opendir(".");
        if (dirp == nullptr)
            return configs;

        u32 count=0;
        char dir_entries[8][0x100];

        /* Get entries */
        while (auto dent = readdir(dirp)) {
            if (dent->d_type != DT_REG)
                continue;

            std::strcpy(dir_entries[count++], dent->d_name);

            if (count == std::size(dir_entries))
                break;
        }

        /* Reorder ini files by ASCII ordering. */
        char temp[0x100];
        for (size_t i = 0; i < count - 1 ; i++) {
            for (size_t j = i + 1; j < count; j++) {
                if (std::strcmp(dir_entries[i], dir_entries[j]) > 0) {
                    std::strcpy(temp, dir_entries[i]);
                    std::strcpy(dir_entries[i], dir_entries[j]);
                    std::strcpy(dir_entries[j], temp);
                }
            }
        }

        /* parse config */
        for (auto &entry : std::span(dir_entries, count))
            ini_parse(entry, BootConfigHandler, &configs);

        closedir(dirp);

        chdir("sdmc:/");

        return configs;
    }

    bool RebootDefault() {
        /* Load payload. */
        if (!LoadPayload())
            return false;

        /* Get boot storage pointer. */
        auto storage = reinterpret_cast<BootStorage *>(g_reboot_payload + BootStorageOffset);

        /* Clear boot storage. */
        std::memset(storage, 0, sizeof(BootStorage));

        /* Reboot */
        reboot_to_payload();

        return true;
    }

    bool RebootToConfig(BootConfig const &config, bool autoboot_list) {
        /* Load payload. */
        if (!LoadPayload())
            return false;

        /* Get boot storage pointer. */
        auto storage = reinterpret_cast<BootStorage *>(g_reboot_payload + BootStorageOffset);

        /* Clear boot storage. */
        std::memset(storage, 0, sizeof(BootStorage));

        /* Force autoboot and set boot id. */
        storage->boot_cfg      = BootCfg_ForceAutoBoot;
        storage->autoboot      = config.index;
        storage->autoboot_list = autoboot_list;

        /* Reboot */
        reboot_to_payload();

        return true;
    }

    bool RebootToUMS(UmsTarget const target) {
        /* Load payload. */
        if (!LoadPayload())
            return false;

        /* Get boot storage pointer. */
        auto storage = reinterpret_cast<BootStorage *>(g_reboot_payload + BootStorageOffset);

        /* Clear boot storage. */
        std::memset(storage, 0, sizeof(BootStorage));

        /* Force boot to menu, target UMS and select target. */
        storage->boot_cfg  = BootCfg_ForceAutoBoot;
        storage->extra_cfg = ExtraCfg_NyxUms;
        storage->autoboot  = 0;
        storage->ums       = target;

        /* Reboot */
        reboot_to_payload();

        return true;
    }

}