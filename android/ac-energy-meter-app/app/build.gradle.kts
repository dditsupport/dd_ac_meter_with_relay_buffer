plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

android {
    namespace = "com.dangeedums.acmeter"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.dangeedums.acmeter"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        // Static preshared key for the BLE access handshake. MUST match
        // BLE_PSK in the firmware's config.h. Never sent over BLE (only the
        // HMAC of a per-connection nonce is). Kept here rather than in source
        // so it is trivially rotatable; note it is still recoverable from the
        // built APK, so this is deterrence against generic BLE tools, not
        // strong secrecy. Ideally injected from an untracked gradle property.
        buildConfigField(
            "String",
            "BLE_PSK",
            "\"49705412b4105495af9b3d25974605ebde7bd3fe0525d510f15c15ea75baef3c\"",
        )
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.navigation.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.ui.tooling.preview)
    implementation(libs.androidx.material3)
    implementation(libs.androidx.material.icons.extended)
    implementation(libs.androidx.datastore.preferences)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.kotlinx.coroutines.android)

    // BLE (Kotlin-first, Flow-based GATT)
    implementation(libs.kable.core)

    // HTTP client for /meter/api/*
    implementation(libs.ktor.client.core)
    implementation(libs.ktor.client.cio)
    implementation(libs.ktor.client.content.negotiation)
    implementation(libs.ktor.client.logging)
    implementation(libs.ktor.serialization.kotlinx.json)

    // Charts (Compose-native)
    implementation(libs.vico.compose.m3)
    implementation(libs.vico.core)

    debugImplementation(libs.androidx.ui.tooling)
}
