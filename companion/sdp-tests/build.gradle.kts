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
}

tasks.test {
    useJUnitPlatform()
}

tasks.register<JavaExec>("generateGoldens") {
    group = "verification"
    description = "Refresh golden PNG/BIN files under src/test/resources/golden"
    dependsOn("compileTestKotlin")
    classpath = sourceSets["test"].runtimeClasspath
    mainClass.set("slate.golden.GoldenGeneratorKt")
    workingDir = rootProject.projectDir
}
