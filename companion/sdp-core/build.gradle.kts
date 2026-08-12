plugins {
    kotlin("jvm")
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation("org.json:json:20240303")
    implementation("org.mozilla:rhino:1.7.15")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
    testImplementation(kotlin("test"))
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
}

tasks.processResources {
    from("$rootDir/shared-js") {
        into("slate/script")
    }
    from("$rootDir/examples/timer") {
        into("slate/subapps/timer")
    }
    from("$rootDir/examples/navigation") {
        into("slate/subapps/navigation")
    }
    from("$rootDir/examples/camera") {
        into("slate/subapps/camera")
    }
    from("$rootDir/examples/vibrate") {
        into("slate/subapps/vibrate")
    }
    from("$rootDir/examples/location") {
        into("slate/subapps/location")
    }
    from("$rootDir/examples/map") {
        into("slate/subapps/map")
    }
    from("$rootDir/examples/news") {
        into("slate/subapps/news")
    }
    from("$rootDir/examples/media") {
        into("slate/subapps/media")
    }
    from("$rootDir/examples/weather") {
        into("slate/subapps/weather")
    }
    from("$rootDir/examples/httpdemo") {
        into("slate/subapps/httpdemo")
    }
    from("$rootDir/examples/calendar") {
        into("slate/subapps/calendar")
    }
    from("$rootDir/examples/alarms") {
        into("slate/subapps/alarms")
    }
    from("$rootDir/examples/home") {
        into("slate/subapps/home")
    }
    from("$rootDir/examples/health") {
        into("slate/subapps/health")
    }
}

tasks.test {
    useJUnitPlatform()
}
