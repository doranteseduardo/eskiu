// Eskiu IntelliJ plugin — wraps the TextMate grammar as an installable plugin.
// Build: ./gradlew buildPlugin  → build/distributions/eskiu-intellij-<version>.zip
//
// Uses the IntelliJ Platform Gradle Plugin (2.x). Requires JDK 17+.

plugins {
    kotlin("jvm") version "1.9.24"
    id("org.jetbrains.intellij.platform") version "2.0.1"
}

group = providers.gradleProperty("pluginGroup").get()
version = providers.gradleProperty("pluginVersion").get()

repositories {
    mavenCentral()
    intellijPlatform { defaultRepositories() }
}

dependencies {
    intellijPlatform {
        create(
            providers.gradleProperty("platformType").get(),
            providers.gradleProperty("platformVersion").get(),
        )
        // Contribute our bundle to the platform's built-in TextMate engine.
        bundledPlugin(providers.gradleProperty("platformBundledPlugins").get())
    }
}

kotlin { jvmToolchain(17) }
