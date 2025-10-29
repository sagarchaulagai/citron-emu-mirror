// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citron.citron_emu.adapters

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import org.citron.citron_emu.databinding.CardSimpleOutlinedBinding
import org.citron.citron_emu.model.StorageLocation

class StorageLocationAdapter(
    private val locations: List<StorageLocation>,
    private val onLocationSelected: (StorageLocation) -> Unit
) : RecyclerView.Adapter<StorageLocationAdapter.StorageLocationViewHolder>() {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): StorageLocationViewHolder {
        val binding = CardSimpleOutlinedBinding.inflate(
            LayoutInflater.from(parent.context),
            parent,
            false
        )
        return StorageLocationViewHolder(binding)
    }

    override fun onBindViewHolder(holder: StorageLocationViewHolder, position: Int) {
        holder.bind(locations[position])
    }

    override fun getItemCount(): Int = locations.size

    inner class StorageLocationViewHolder(private val binding: CardSimpleOutlinedBinding) :
        RecyclerView.ViewHolder(binding.root) {

        fun bind(location: StorageLocation) {
            binding.title.setText(location.titleId)
            binding.description.setText(location.descriptionId)

            binding.root.setOnClickListener {
                onLocationSelected(location)
            }
        }
    }
}
