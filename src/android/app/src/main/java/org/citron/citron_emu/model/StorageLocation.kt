// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.model

import androidx.annotation.StringRes

data class StorageLocation(
    @StringRes val titleId: Int,
    val path: String,
    @StringRes val descriptionId: Int,
    val isDirectPath: Boolean
)
