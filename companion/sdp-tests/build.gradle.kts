plugins {
    kotlin("jvm")
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation(project(":sdp-core"))
    testImplementation(kotlin("test"))
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
