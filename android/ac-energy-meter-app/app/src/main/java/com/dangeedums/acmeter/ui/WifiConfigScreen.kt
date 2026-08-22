package com.dangeedums.acmeter.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.dangeedums.acmeter.ble.WifiScanResult

@Composable
fun WifiConfigScreen(vm: WifiConfigViewModel, onBack: () -> Unit) {
    val ui by vm.ui.collectAsStateWithLifecycle()

    // The whole screen scrolls as one surface. It used to be a fixed Column whose
    // only scrollable part was the network LazyColumn, so the list was confined to
    // whatever vertical space was left over — once the TX-power section was added
    // above it, that was a ~100dp strip and everything else was unreachable.
    Column(modifier = Modifier.fillMaxSize().padding(16.dp)
                              .verticalScroll(rememberScrollState())) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
            }
            Text("Configure Wi-Fi", style = MaterialTheme.typography.titleLarge,
                 modifier = Modifier.weight(1f))
            IconButton(onClick = { vm.requestScan() }) {
                if (ui.scanning) CircularProgressIndicator(modifier = Modifier.padding(4.dp))
                else             Icon(Icons.Default.Refresh, contentDescription = "Scan")
            }
        }

        ui.status?.let { st ->
            Spacer(Modifier.height(4.dp))
            Card(elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
                 modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Text("Device status: ${st.status}", fontWeight = FontWeight.Medium)
                    if (ui.savedSsid.isNotBlank()) {
                        Text("Saved network: ${ui.savedSsid}",
                             style = MaterialTheme.typography.bodySmall)
                    }
                    st.ssid?.let   { Text("Connected to: $it", style = MaterialTheme.typography.bodySmall) }
                    st.next?.let   { Text("Next: $it",   style = MaterialTheme.typography.bodySmall) }
                    st.detail?.let { Text("Detail: $it", style = MaterialTheme.typography.bodySmall,
                                          color = MaterialTheme.colorScheme.error) }
                }
            }
        }

        Spacer(Modifier.height(12.dp))

        // Always-visible manual entry. The scan list below is a convenience.
        OutlinedTextField(
            value = ui.selected,
            onValueChange = vm::selectSsid,
            label = { Text("Network name (SSID)") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(8.dp))
        OutlinedTextField(
            value = ui.password,
            onValueChange = vm::setPassword,
            label = { Text("Password (leave blank for open network)") },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(8.dp))
        Button(
            onClick = { vm.saveCredentials() },
            enabled = ui.selected.isNotBlank() && !ui.saving,
            modifier = Modifier.fillMaxWidth(),
        ) {
            if (ui.saving) {
                CircularProgressIndicator(
                    modifier = Modifier.height(18.dp).padding(end = 8.dp),
                    strokeWidth = 2.dp,
                )
            }
            Text(if (ui.saving) "Saving…" else "Save & connect")
        }

        if (ui.message.isNotBlank()) {
            Spacer(Modifier.height(8.dp))
            val isError = ui.message.contains("Couldn't", ignoreCase = true) ||
                          ui.message.contains("Check the password", ignoreCase = true)
            Text(
                ui.message,
                color = if (isError) MaterialTheme.colorScheme.error
                        else MaterialTheme.colorScheme.primary,
                style = MaterialTheme.typography.bodyMedium,
            )
        }

        // ---- Wi-Fi transmit power ----
        Spacer(Modifier.height(20.dp))
        Text("Wi-Fi transmit power", style = MaterialTheme.typography.titleMedium)
        Text(
            "Lower power can be steadier on this board once the RTC/meter are wired. " +
            "Current: " + (ui.txPowerDbm?.let { "%.1f dBm".format(it) } ?: "—"),
            style = MaterialTheme.typography.bodySmall,
        )
        Spacer(Modifier.height(4.dp))
        var txMenuOpen by remember { mutableStateOf(false) }
        Box {
            OutlinedButton(onClick = { txMenuOpen = true }) {
                Text(ui.txPowerDbm?.let { "Set TX power (%.1f dBm)".format(it) } ?: "Set TX power")
            }
            DropdownMenu(expanded = txMenuOpen, onDismissRequest = { txMenuOpen = false }) {
                listOf(8.5, 11.0, 13.0, 15.0, 17.0, 19.5).forEach { dbm ->
                    DropdownMenuItem(
                        text = { Text("%.1f dBm".format(dbm)) },
                        onClick = { txMenuOpen = false; vm.setTxPower(dbm) },
                    )
                }
            }
        }

        Spacer(Modifier.height(20.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Nearby networks", style = MaterialTheme.typography.titleMedium,
                 modifier = Modifier.weight(1f))
            TextButton(onClick = { vm.requestScan() }, enabled = !ui.scanning) {
                Text(if (ui.scanning) "Scanning…" else "Re-scan")
            }
        }
        Spacer(Modifier.height(4.dp))

        if (ui.networks.isEmpty()) {
            Text(
                if (ui.scanning) "Asking the device to scan…"
                else "No networks reported by the device yet. Enter the SSID above and tap Save & connect.",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyMedium,
            )
        } else {
            // Not a LazyColumn: a lazy list inside a scrolling parent is measured
            // with unbounded height and throws. It doesn't need to be lazy either
            // — the firmware emits at most MAX_SCAN_RESULTS (12) APs, and the
            // view model then keeps one row per SSID.
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                ui.networks.forEach { net ->
                    NetworkRow(
                        net = net,
                        selected = net.ssid == ui.selected,
                        onClick = { vm.selectSsid(net.ssid) },
                    )
                }
            }
        }
    }
}

@Composable
private fun NetworkRow(net: WifiScanResult, selected: Boolean, onClick: () -> Unit) {
    Card(
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
        modifier  = Modifier.fillMaxWidth(),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (net.isEncrypted) {
                Icon(Icons.Default.Lock, contentDescription = "Encrypted",
                     modifier = Modifier.padding(end = 8.dp))
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(net.ssid, fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal)
                Text("RSSI ${net.rssi} dBm",
                     style = MaterialTheme.typography.bodySmall,
                     color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            Button(onClick = onClick) {
                Text(if (selected) "Selected" else "Use")
            }
        }
    }
}
