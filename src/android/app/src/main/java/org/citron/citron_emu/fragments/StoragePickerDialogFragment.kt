// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.fragments

import android.app.Dialog
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.contract.ActivityResultContracts
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.activityViewModels
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citron.citron_emu.R
import org.citron.citron_emu.CitronApplication
import org.citron.citron_emu.adapters.StorageLocationAdapter
import org.citron.citron_emu.databinding.DialogStoragePickerBinding
import org.citron.citron_emu.features.settings.model.Settings
import org.citron.citron_emu.model.HomeViewModel
import org.citron.citron_emu.model.StorageLocation
import org.citron.citron_emu.utils.DirectoryInitialization
import java.io.File

class StoragePickerDialogFragment : DialogFragment() {
    private var _binding: DialogStoragePickerBinding? = null
    private val binding get() = _binding!!

    private val homeViewModel: HomeViewModel by activityViewModels()

    private val storageLocations = mutableListOf<StorageLocation>()

    private val getStorageDirectory =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { result ->
            if (result != null) {
                selectStoragePath(result)
            }
        }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        _binding = DialogStoragePickerBinding.inflate(layoutInflater)

        setupStorageLocations()

        binding.storageLocationList.apply {
            layoutManager = LinearLayoutManager(requireContext())
            adapter = StorageLocationAdapter(storageLocations) { location ->
                onStorageLocationSelected(location)
            }
        }

        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.select_storage_location)
            .setView(binding.root)
            .setNegativeButton(android.R.string.cancel, null)
            .create()
    }

    private fun setupStorageLocations() {
        storageLocations.clear()

        // Get all available external storage locations
        val externalStoragePaths = CitronApplication.appContext.getExternalFilesDirs(null)

        // Default app-specific storage (primary external storage)
        // Create a citron subdirectory to make it more accessible
        if (externalStoragePaths.isNotEmpty() && externalStoragePaths[0] != null) {
            val defaultPath = File(externalStoragePaths[0], "citron")
            storageLocations.add(
                StorageLocation(
                    R.string.storage_default,
                    defaultPath.canonicalPath,
                    R.string.storage_default_description,
                    true
                )
            )
        }

        // Add external SD card if available (secondary external storage)
        if (externalStoragePaths.size > 1) {
            externalStoragePaths.drop(1).forEach { path ->
                if (path != null && path.exists()) {
                    val sdPath = File(path, "citron")
                    storageLocations.add(
                        StorageLocation(
                            R.string.storage_external_sd,
                            sdPath.canonicalPath,
                            R.string.storage_external_sd_description,
                            true
                        )
                    )
                }
            }
        }

        // Custom location (user selects via SAF)
        // Note: SAF content:// URIs cannot be used directly with native code
        // This option is available but will require additional handling
        storageLocations.add(
            StorageLocation(
                R.string.storage_custom,
                "CUSTOM",
                R.string.storage_custom_description,
                false
            )
        )
    }

    private fun onStorageLocationSelected(location: StorageLocation) {
        when (location.path) {
            "CUSTOM" -> {
                // Open folder picker for custom location
                // Note: SAF-selected folders will need special handling as they provide content:// URIs
                // which are not directly accessible as file paths by native code
                getStorageDirectory.launch(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).data)
            }
            else -> {
                // Direct path selection (app-specific or SD card storage)
                setStoragePath(location.path)
                dismiss()
            }
        }
    }

    private fun selectStoragePath(uri: Uri) {
        try {
            requireActivity().contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            )

            val documentFile = DocumentFile.fromTreeUri(requireContext(), uri)
            if (documentFile != null) {
                // For SAF URIs, store the content URI
                // Note: This requires special handling in DirectoryInitialization
                // as native code cannot directly access content:// URIs
                setStoragePath(uri.toString())
                dismiss()
            }
        } catch (e: Exception) {
            // Handle permission errors
            android.widget.Toast.makeText(
                requireContext(),
                "Failed to access selected location: ${e.message}",
                android.widget.Toast.LENGTH_LONG
            ).show()
        }
    }

    private fun setStoragePath(path: String) {
        val preferences = PreferenceManager.getDefaultSharedPreferences(CitronApplication.appContext)
        val oldPath = preferences.getString(Settings.PREF_CUSTOM_STORAGE_PATH, null)

        android.util.Log.d("StoragePicker", "Setting storage path: $path")
        android.util.Log.d("StoragePicker", "Old path was: $oldPath")

        // Determine the current data directory
        val currentDataDir = if (!oldPath.isNullOrEmpty() && !oldPath.startsWith("content://")) {
            File(oldPath)
        } else {
            File(CitronApplication.appContext.getExternalFilesDir(null)!!.canonicalPath)
        }

        val newDataDir = File(path)

        // Check if we need to migrate data
        if (currentDataDir.exists() && currentDataDir.canonicalPath != newDataDir.canonicalPath) {
            val hasData = currentDataDir.listFiles()?.isNotEmpty() == true

            if (hasData) {
                // Show migration dialog
                migrateData(currentDataDir, newDataDir, path)
            } else {
                // No data to migrate, just set the path
                savePath(path, oldPath)
            }
        } else {
            // Same path or no existing data
            savePath(path, oldPath)
        }
    }

    private fun savePath(path: String, oldPath: String?) {
        val preferences = PreferenceManager.getDefaultSharedPreferences(CitronApplication.appContext)

        // Save the new path - use commit() to ensure it's saved immediately
        val success = preferences.edit()
            .putString(Settings.PREF_CUSTOM_STORAGE_PATH, path)
            .commit()

        android.util.Log.d("StoragePicker", "Preference save success: $success")

        // Verify it was saved
        val savedPath = preferences.getString(Settings.PREF_CUSTOM_STORAGE_PATH, null)
        android.util.Log.d("StoragePicker", "Verified saved path: $savedPath")

        // Show confirmation with the selected path
        android.widget.Toast.makeText(
            requireContext(),
            "Storage location set to:\n$path",
            android.widget.Toast.LENGTH_LONG
        ).show()

        // Only trigger changed event if the path actually changed
        if (oldPath != path) {
            homeViewModel.setStorageLocationChanged(true)
        }
    }

    private fun migrateData(sourceDir: File, destDir: File, newPath: String) {
        ProgressDialogFragment.newInstance(
            requireActivity(),
            R.string.migrating_data,
            true
        ) { progressCallback, _ ->
            try {
                android.util.Log.i("StoragePicker", "Starting data migration from ${sourceDir.path} to ${destDir.path}")

                // Create destination directory
                if (!destDir.exists()) {
                    destDir.mkdirs()
                }

                // Get list of files/directories to copy
                val items = sourceDir.listFiles() ?: arrayOf()
                val totalItems = items.size

                if (totalItems == 0) {
                    android.util.Log.i("StoragePicker", "No items to migrate")
                    return@newInstance getString(R.string.migration_complete)
                }

                android.util.Log.i("StoragePicker", "Found $totalItems items to migrate")

                // Copy each item
                items.forEachIndexed { index, item ->
                    // Check if cancelled
                    if (progressCallback(index, totalItems)) {
                        android.util.Log.w("StoragePicker", "Migration cancelled by user")
                        return@newInstance getString(R.string.migration_cancelled)
                    }

                    val dest = File(destDir, item.name)
                    android.util.Log.d("StoragePicker", "Copying ${item.name}")

                    if (item.isDirectory) {
                        item.copyRecursively(dest, overwrite = true)
                    } else {
                        item.copyTo(dest, overwrite = true)
                    }
                }

                android.util.Log.i("StoragePicker", "Migration completed successfully")

                // Save the new path after successful migration
                savePath(newPath, DirectoryInitialization.userDirectory)

                getString(R.string.migration_complete)
            } catch (e: Exception) {
                android.util.Log.e("StoragePicker", "Migration failed", e)
                MessageDialogFragment.newInstance(
                    requireActivity(),
                    titleId = R.string.migration_failed,
                    descriptionString = getString(R.string.migration_failed_description, e.message)
                )
            }
        }.show(parentFragmentManager, ProgressDialogFragment.TAG)
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    companion object {
        const val TAG = "StoragePickerDialogFragment"

        fun newInstance(): StoragePickerDialogFragment {
            return StoragePickerDialogFragment()
        }
    }
}
