plugins {
    kotlin("jvm")
    kotlin("plugin.compose")
    id("org.jetbrains.compose")
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation(project(":sdp-core"))
    implementation(compose.desktop.currentOs)
}

compose.desktop {
    application {
        mainClass = "slate.emulator.MainKt"
    }
}
