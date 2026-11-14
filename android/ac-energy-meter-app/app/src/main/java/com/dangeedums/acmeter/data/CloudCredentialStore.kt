package com.dangeedums.acmeter.data

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import kotlinx.coroutines.flow.first
import java.security.KeyStore
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * The cloud sign-in remembered for automatic re-login before a sync.
 *
 * The password is encrypted with an AES-GCM key that lives in the Android
 * Keystore and never leaves it; only the ciphertext is stored, in the
 * cloud_session DataStore (excluded from backup and device transfer, see
 * res/xml/data_extraction_rules.xml). If the key is gone (app data cleared,
 * restored to another phone) the saved password simply can't be read and the
 * user signs in again.
 */
class CloudCredentialStore(private val store: DataStore<Preferences>) {

    data class Credentials(val username: String, val password: String)

    private val keyUsername = stringPreferencesKey("saved_login_username")
    private val keyPassword = stringPreferencesKey("saved_login_password_enc")

    suspend fun save(username: String, password: String) {
        val sealed = encrypt(password)
        store.edit {
            it[keyUsername] = username
            it[keyPassword] = sealed
        }
    }

    /** The saved sign-in, or null when none is saved or it can't be decrypted. */
    suspend fun load(): Credentials? {
        val prefs = store.data.first()
        val user = prefs[keyUsername]?.takeIf { it.isNotBlank() } ?: return null
        val sealed = prefs[keyPassword] ?: return null
        val password = runCatching { decrypt(sealed) }.getOrNull()
        if (password == null) {
            clear()   // unreadable for good (key lost); don't keep trying it
            return null
        }
        return Credentials(user, password)
    }

    suspend fun clear() {
        store.edit {
            it.remove(keyUsername)
            it.remove(keyPassword)
        }
    }

    private fun key(): SecretKey {
        val ks = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        (ks.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
        gen.init(
            KeyGenParameterSpec.Builder(
                KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build()
        )
        return gen.generateKey()
    }

    /** "<iv>:<ciphertext>", both Base64. */
    private fun encrypt(plain: String): String {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, key())
        val ct = cipher.doFinal(plain.toByteArray(Charsets.UTF_8))
        val b64 = Base64.getEncoder()
        return b64.encodeToString(cipher.iv) + ":" + b64.encodeToString(ct)
    }

    private fun decrypt(sealed: String): String {
        val (iv, ct) = sealed.split(':', limit = 2).map { Base64.getDecoder().decode(it) }
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, iv))
        return String(cipher.doFinal(ct), Charsets.UTF_8)
    }

    private companion object {
        const val KEYSTORE = "AndroidKeyStore"
        const val KEY_ALIAS = "acmeter_cloud_login"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
    }
}
