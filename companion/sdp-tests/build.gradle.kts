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
    implementation(project(":sdp-core"))
    testImplementation(kotlin("test"))
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
    // sdp-core depends on org.json as `implementation`, so it does not reach
    // here transitively. Tests that assert on an adapter payload need it.
    testImplementation("org.json:json:20240303")
}

tasks.test {
    useJUnitPlatform()
}

/**
 * Render the OSM map to PNGs under sdp-tests/build/map-preview.
 *
 * The watch is sealed, so without this every map tweak costs a build, an
 * install and a photograph before anyone can say whether it looks right.
 */
tasks.register<JavaExec>("mapPreview") {
    group = "verification"
    description = "Render the map sub-app's display lists to PNG using the real pipeline"
    dependsOn("compileTestKotlin")
    classpath = sourceSets["test"].runtimeClasspath
    mainClass.set("slate.map.MapPreviewGeneratorKt")
}

tasks.register<JavaExec>("mapTiming") {
    group = "verification"
    description = "Measure reproject-and-render cost; it runs on the location callback thread"
    dependsOn("compileTestKotlin")
    classpath = sourceSets["test"].runtimeClasspath
    mainClass.set("slate.map.MapTimingProbeKt")
}

tasks.register<JavaExec>("generateGoldens") {
    group = "verification"
    description = "Refresh golden PNG/BIN files under src/test/resources/golden"
    dependsOn("compileTestKotlin")
    classpath = sourceSets["test"].runtimeClasspath
    mainClass.set("slate.golden.GoldenGeneratorKt")
    workingDir = rootProject.projectDir
}
