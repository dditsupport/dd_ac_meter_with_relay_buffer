package com.dangeedums.acmeter.ble

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * JSON shape exposed by the firmware on the Device Info characteristic.
 * Keep in sync with build_device_info_json() in firmware/ble_service.cpp.
 */
@Serializable
data class DeviceInfoBle(
    @SerialName("device_id")          val deviceId: String,
    @SerialName("fw")                 val fw: String,
    @SerialName("unsynced_count")     val unsyncedCount: Int = 0,
    @SerialName("current_boot_id")    val currentBootId: Int = 0,
    @SerialName("uptime_sec")         val uptimeSec: Long = 0,
    @SerialName("last_seq")           val lastSeq: Long = 0,
    @SerialName("expected_row_count") val expectedRowCount: Int = 0,
    @SerialName("wall_clock_known")   val wallClockKnown: Boolean = false,
    @SerialName("rtc_ok")             val rtcOk: Boolean = false,
    @SerialName("ingest_host")        val ingestHost: String = "",
    @SerialName("ingest_path")        val ingestPath: String = "",
    @SerialName("log_interval_sec")   val logIntervalSec: Int = 900,
    @SerialName("total_kwh")          val totalKwh: Double? = null,
    // How many PZEM meters and relays the connected firmware has. The device
    // tells us, so one screen serves both the 1-relay and 2-relay builds.
    @SerialName("channel_count")      val channelCount: Int = 1,
    @SerialName("relay_count")        val relayCount: Int = 1,
)

@Serializable
data class BootRecord(
    @SerialName("boot_id")      val bootId: Int,
    @SerialName("duration_sec") val durationSec: Int,
)

/** Wi-Fi Scan characteristic shape: [{"s":"SSID","r":-65,"e":1}, ...] */
@Serializable
data class WifiScanResult(
    @SerialName("s") val ssid: String,
    @SerialName("r") val rssi: Int,
    @SerialName("e") val encrypted: Int = 0,
) {
    val isEncrypted: Boolean get() = encrypted != 0
}

/**
 * One relay's state. Every firmware build — 1-relay or 2-relay — reports an
 * ARRAY of these, so the UI just renders one control set per entry.
 *
 * `energized` is the raw coil state and means **AC CUT**, because the relay is
 * wired fail-safe NC. The appliance is powered when [energized] is false; use
 * [applianceOn] rather than reading the flag directly.
 *
 * Keep in sync with relay::status_json().
 */
@Serializable
data class RelayState(
    val ch: Int = 1,
    val mode: String = "auto",
    val energized: Boolean = false,
    val sm: String = "",
    @SerialName("latest_w")      val latestW: Int = -1,
    @SerialName("compressor_w")  val compressorW: Int = 0,
    @SerialName("grace_min")     val graceMin: Int = 0,
    @SerialName("sched_version") val schedVersion: Int = 0,
) {
    /** True when the appliance has power (relay de-energized). */
    val applianceOn: Boolean get() = !energized
}

/** Relay characteristic shape: {"relays":[{...},{...}]} — one entry per relay. */
@Serializable
data class RelayStatus(
    val relays: List<RelayState> = emptyList(),
)

/**
 * Wi-Fi Status characteristic shape:
 *   {"status":"connected","ssid":"...","saved_ssid":"...","tx_power_dbm":13.0}
 * `ssid` is the live association (present only when connected); `savedSsid` is
 * the credential stored on the device, shown even when offline. `txPowerDbm` is
 * the device's current Wi-Fi TX power.
 */
@Serializable
data class WifiStatus(
    val status: String,
    val ssid: String? = null,
    val ip: String? = null,
    val detail: String? = null,
    val next: String? = null,
    @SerialName("saved_ssid")   val savedSsid: String? = null,
    @SerialName("tx_power_dbm") val txPowerDbm: Double? = null,
)
