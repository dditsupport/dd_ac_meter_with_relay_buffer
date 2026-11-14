package com.dangeedums.acmeter.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import androidx.lifecycle.viewModelScope
import com.dangeedums.acmeter.AcMeterApp
import com.dangeedums.acmeter.cloud.CloudClient
import com.dangeedums.acmeter.cloud.CloudDevice
import com.dangeedums.acmeter.cloud.CloudSignIn
import com.dangeedums.acmeter.cloud.DevicesResponse
import com.dangeedums.acmeter.cloud.ReadingPoint
import com.dangeedums.acmeter.data.BlePinStore
import com.dangeedums.acmeter.data.BleUnlockRegistry
import com.dangeedums.acmeter.data.CloudCredentialStore
import com.dangeedums.acmeter.data.CloudSessionStore
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

enum class Range(val label: String, val aggregate: String) {
    Today  ("Today",   "hourly"),
    Last24h("24 h",    "hourly"),
    Last7d ("7 days",  "daily"),
    Last30d("30 days", "daily"),
    Last12m("12 mo",   "monthly"),
}

data class CloudUi(
    val loggedIn: Boolean = false,
    val username: String = "",
    val baseUrl: String = "https://ac.aromen.biz",
    val devices: List<CloudDevice> = emptyList(),
    val selectedDeviceId: String? = null,
    /** 1-based meter being charted. A multi-meter device is viewed one meter at
     *  a time — its meters are separate cumulative counters measuring different
     *  loads, so combining them into one chart would be misleading. */
    val selectedChannel: Int = 1,
    val range: Range = Range.Today,
    val points: List<ReadingPoint> = emptyList(),
    /** Server's whole-window total (see ReadingsResponse.total_kwh). */
    val periodKwh: Double? = null,
    val loading: Boolean = false,
    val error: String? = null,
)

class CloudViewModel(
    application: Application,
    private val client: CloudClient,
    private val session: CloudSessionStore,
    private val blePinStore: BlePinStore,
    private val unlockRegistry: BleUnlockRegistry,
    private val signIn: CloudSignIn,
    private val credentials: CloudCredentialStore,
) : AndroidViewModel(application) {

    private val _ui = MutableStateFlow(CloudUi())
    val ui: StateFlow<CloudUi> = _ui.asStateFlow()

    /** Cache the authorised BLE PINs locally so the BLE access gate can
     *  validate entered PINs offline. Called whenever we fetch the device list
     *  while logged in. */
    private suspend fun cachePins(devices: List<CloudDevice>) {
        val pins = devices.mapNotNull { d ->
            d.ble_pin?.takeIf { it.isNotBlank() }?.let { d.device_id.lowercase() to it }
        }.toMap()
        blePinStore.setAll(pins)
        session.setLoggedIn(true)
    }

    init {
        viewModelScope.launch {
            val s = session.settings.first()
            client.setBaseUrl(s.baseUrl)
            // Optimistically restore the logged-in UI from the persisted flag so
            // a cold start (the process was swiped away or killed for memory)
            // lands on the dashboard, not the login screen. tryAutoFetch() then
            // validates the persisted session cookie in the background and only
            // drops us to login on an explicit server "unauthorized" — never on
            // a transient network error at launch.
            val wasLoggedIn = session.isLoggedIn()
            _ui.value = _ui.value.copy(
                baseUrl = s.baseUrl,
                username = s.username,
                loggedIn = wasLoggedIn,
            )
            tryAutoFetch(wasLoggedIn)
        }
    }

    private suspend fun tryAutoFetch(wasLoggedIn: Boolean) {
        // devicesRenewingSession() signs in again with the saved password when
        // the server has expired the session, so a remembered user opens
        // straight on the dashboard instead of the login screen.
        runCatching { devicesRenewingSession() }
            .onSuccess { resp ->
                if (resp.ok) {
                    // Session is alive but CSRF was wiped at process restart — refresh it.
                    client.refreshCsrf()
                    cachePins(resp.devices)
                    _ui.value = _ui.value.copy(
                        loggedIn = true,
                        devices = resp.devices,
                        selectedDeviceId = resp.devices.firstOrNull()?.device_id,
                    )
                    refreshChart()
                } else {
                    // Server explicitly rejected the session (expired / invalid).
                    // A real logout — clear the persisted flag so we stop
                    // optimistically restoring a dead session on later launches.
                    session.setLoggedIn(false)
                    _ui.value = _ui.value.copy(loggedIn = false)
                }
            }
            .onFailure {
                // Network/transient failure at launch (offline, or the request
                // raced device connectivity). Keep whatever logged-in state we
                // optimistically restored — a hiccup must not sign the user out.
                // If we were never logged in, stay on the login screen.
                if (!wasLoggedIn) _ui.value = _ui.value.copy(loggedIn = false)
            }
    }

    /**
     * GET /devices; if the server says the session is gone, renew it with the
     * saved sign-in ("Remember me") and ask once more.
     */
    private suspend fun devicesRenewingSession(): DevicesResponse {
        val first = client.devices()
        if (first.ok || !CloudSignIn.isSessionError(first.error)) return first
        if (signIn.renew() != CloudSignIn.Status.SignedIn) return first
        return client.devices()
    }

    /** The session is gone and could not be renewed: back to the login screen. */
    private suspend fun dropToLogin(error: String?) {
        session.setLoggedIn(false)
        _ui.value = CloudUi(
            baseUrl = _ui.value.baseUrl,
            username = _ui.value.username,
            error = error ?: "Session expired, please sign in again.",
        )
    }

    fun setBaseUrl(url: String) {
        _ui.value = _ui.value.copy(baseUrl = url)
        client.setBaseUrl(url)
        viewModelScope.launch { session.update(baseUrl = url) }
    }

    /**
     * @param remember keep the sign-in (password encrypted with a Keystore key)
     *   so an expired session is renewed without asking again.
     */
    fun login(username: String, password: String, remember: Boolean) {
        _ui.value = _ui.value.copy(loading = true, error = null)
        viewModelScope.launch {
            runCatching { client.login(username, password) }
                .onSuccess { resp ->
                    if (resp.ok) {
                        session.update(username = username)
                        if (remember) credentials.save(username, password) else credentials.clear()
                        _ui.value = _ui.value.copy(loggedIn = true, username = username, loading = false)
                        refreshDevices()
                    } else {
                        _ui.value = _ui.value.copy(loading = false, error = resp.error ?: "login failed")
                    }
                }
                .onFailure { _ui.value = _ui.value.copy(loading = false, error = it.message ?: "network error") }
        }
    }

    fun logout() {
        viewModelScope.launch {
            // Signing out also forgets the saved password, so nothing signs
            // back in behind the user's back.
            runCatching { credentials.clear() }
            runCatching { client.logout() }
            // Drop cached BLE PINs + unlocks so registered devices re-lock.
            runCatching { blePinStore.clear() }
            unlockRegistry.clear()
            session.setLoggedIn(false)
            _ui.value = CloudUi(baseUrl = _ui.value.baseUrl)
        }
    }

    fun refreshDevices() {
        _ui.value = _ui.value.copy(loading = true, error = null)
        viewModelScope.launch {
            runCatching { devicesRenewingSession() }
                .onSuccess { resp ->
                    if (!resp.ok) {
                        // Valid JSON but refused: without this an expired session
                        // looked like "you have no devices" (and wiped the PINs).
                        if (CloudSignIn.isSessionError(resp.error)) dropToLogin(null)
                        else _ui.value = _ui.value.copy(loading = false, error = resp.error ?: "fetch failed")
                        return@onSuccess
                    }
                    cachePins(resp.devices)
                    val selected = _ui.value.selectedDeviceId
                        ?: resp.devices.firstOrNull()?.device_id
                    _ui.value = _ui.value.copy(
                        devices = resp.devices,
                        selectedDeviceId = selected,
                        loading = false,
                    )
                    refreshChart()
                }
                .onFailure { _ui.value = _ui.value.copy(loading = false, error = it.message ?: "fetch failed") }
        }
    }

    fun selectDevice(id: String) {
        // Reset to meter 1: the previously-selected channel may not exist on the
        // newly-picked device.
        _ui.value = _ui.value.copy(selectedDeviceId = id, selectedChannel = 1)
        refreshChart()
    }

    fun selectRange(r: Range) {
        _ui.value = _ui.value.copy(range = r)
        refreshChart()
    }

    /** Switch which meter the chart shows (1-based). */
    fun selectChannel(ch: Int) {
        if (ch == _ui.value.selectedChannel) return
        _ui.value = _ui.value.copy(selectedChannel = ch)
        refreshChart()
    }

    fun refreshChart() {
        val dev = _ui.value.selectedDeviceId ?: return
        val ch  = _ui.value.selectedChannel
        val r   = _ui.value.range
        val fromIso = when (r) {
            Range.Today   -> LocalDate.now().atStartOfDay().format(LocalIso)
            Range.Last24h -> LocalDateTime.now().minusHours(24).format(LocalIso)
            Range.Last7d  -> LocalDateTime.now().minusDays(7).format(LocalIso)
            Range.Last30d -> LocalDateTime.now().minusDays(30).format(LocalIso)
            Range.Last12m -> LocalDateTime.now().minusMonths(12).format(LocalIso)
        }
        _ui.value = _ui.value.copy(loading = true, error = null)
        viewModelScope.launch {
            runCatching {
                val first = client.readings(dev, r.aggregate, fromIso = fromIso, channel = ch)
                // Session expired since the last call: renew it and ask once more.
                if (!first.ok && CloudSignIn.isSessionError(first.error) &&
                    signIn.renew() == CloudSignIn.Status.SignedIn) {
                    client.readings(dev, r.aggregate, fromIso = fromIso, channel = ch)
                } else first
            }
                .onSuccess { resp ->
                    if (!resp.ok && CloudSignIn.isSessionError(resp.error)) {
                        dropToLogin(null)
                        return@onSuccess
                    }
                    _ui.value = if (resp.ok) {
                        _ui.value.copy(loading = false, points = resp.points, periodKwh = resp.total_kwh)
                    } else {
                        // e.g. 403 no_such_device: say so instead of an empty chart.
                        _ui.value.copy(loading = false, points = emptyList(), periodKwh = null,
                                       error = resp.error ?: "server rejected the request")
                    }
                }
                .onFailure { _ui.value = _ui.value.copy(loading = false, error = it.message ?: "fetch failed") }
        }
    }

    companion object {
        private val LocalIso: DateTimeFormatter = DateTimeFormatter.ISO_LOCAL_DATE_TIME

        fun factory(application: Application): ViewModelProvider.Factory = viewModelFactory {
            initializer {
                val app = application as AcMeterApp
                CloudViewModel(
                    application, app.cloudClient, app.cloudSessionStore,
                    app.blePinStore, app.bleUnlockRegistry,
                    app.cloudSignIn, app.cloudCredentialStore,
                )
            }
        }
    }
}
