plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "slate.app"
    compileSdk = 35

    defaultConfig {
        applicationId = "slate.app"
        minSdk = 30
        targetSdk = 35
        // BUMP versionCode ON EVERY BUILD THAT IS INSTALLED.
        // It sat at 1 through roughly fifteen installs, which made the version
        // display useless for the one thing it exists for â€” telling two builds
        // apart. versionName tracks features; versionCode tracks the install.
        versionCode = 75
        versionName = "0.8.2-p74"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
        debug {
            applicationIdSuffix = ".debug"
        }
    }

    buildFeatures {
        compose = true
        // Version strings for the in-app log header and the main screen.
        buildConfig = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation(project(":sdp-core"))
    implementation("androidx.activity:activity-ktx:1.9.3")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    // 1.1.0 stable wants compileSdk 36 + AGP 8.9; stay on alpha that fits SDK 35.
    implementation("androidx.health.connect:connect-client:1.1.0-alpha10")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.camera:camera-core:1.4.0")
    implementation("androidx.camera:camera-camera2:1.4.0")
    implementation("androidx.camera:camera-lifecycle:1.4.0")
    implementation("androidx.javascriptengine:javascriptengine:1.0.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    // OsmAnd turn-by-turn AIDL (turn type + metres to next maneuver).
    implementation("net.osmand:android-aidl-lib:master-snapshot@aar")
    testImplementation(kotlin("test"))
    testImplementation("org.json:json:20240303")
    val composeBom = platform("androidx.compose:compose-bom:2024.10.01")
    implementation(composeBom)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui-tooling-preview")
    debugImplementation("androidx.compose.ui:ui-tooling")
}
