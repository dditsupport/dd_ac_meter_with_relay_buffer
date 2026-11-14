package com.dangeedums.acmeter.cloud

import com.dangeedums.acmeter.data.CloudCredentialStore
import com.dangeedums.acmeter.data.CloudSessionStore
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * Keeps the app signed in to the cloud. The server session expires after a
 * period of inactivity (SESSION_LIFETIME), which used to drop the user back to
 * the login screen and make a BLE sync fail at the upload. When the session is
 * dead and the user ticked "Remember me", this signs in again with the saved
 * username + password before the caller gives up.
 *
 * One mutex for the whole app, so the Cloud tab and a BLE sync that both find
 * the session dead at the same moment log in once, not twice.
 */
class CloudSignIn(
    private val client: CloudClient,
    private val credentials: CloudCredentialStore,
    private val session: CloudSessionStore,
) {
    enum class Status { SignedIn, NeedsSignIn, Unreachable }

    private val mutex = Mutex()

    /** Probe the session; if the server rejects it, renew it with the saved sign-in. */
    suspend fun ensureSignedIn(): Status = mutex.withLock {
        val probe = try {
            client.devices()
        } catch (ce: CancellationException) {
            throw ce
        } catch (_: Exception) {
            return Status.Unreachable
        }
        if (probe.ok) {
            // A process restart wipes the in-memory CSRF token the session needs.
            if (client.csrf.isBlank()) client.refreshCsrf()
            return Status.SignedIn
        }
        renewLocked()
    }

    /**
     * Sign in again with the saved credentials, without probing first. For
     * callers that just got "login_required" / "unauthorized" back and so
     * already know the session is dead.
     */
    suspend fun renew(): Status = mutex.withLock { renewLocked() }

    private suspend fun renewLocked(): Status {
        val saved = credentials.load() ?: return Status.NeedsSignIn
        val login = try {
            client.login(saved.username, saved.password)
        } catch (ce: CancellationException) {
            throw ce
        } catch (_: Exception) {
            return Status.Unreachable
        }
        return when {
            login.ok -> {
                session.setLoggedIn(true)
                Status.SignedIn
            }
            login.error == "invalid_credentials" -> {
                // Password changed on the server: stop retrying it on every call.
                credentials.clear()
                Status.NeedsSignIn
            }
            else -> Status.NeedsSignIn
        }
    }

    companion object {
        /** Server errors that mean "the session is gone", as against a real refusal. */
        fun isSessionError(error: String?): Boolean =
            error == "login_required" || error == "unauthorized" || error == "bad_csrf"
    }
}
