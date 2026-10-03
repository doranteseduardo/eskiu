// Eskiu IntelliJ plugin: wraps the TextMate grammar as an installable plugin.
// Build: ./gradlew buildPlugin  -> build/distributions/eskiu-intellij-<version>.zip
//
// Uses the IntelliJ Platform Gradle Plugin (2.x) and JDK 17+. It builds against 2024.1,
// the first platform with the TextMateBundleProvider API, and sets no upper bound, so
// the same zip installs in every JetBrains IDE from 2024.1 on.

plugins {
    kotlin("jvm") version "2.4.20"
    id("org.jetbrains.intellij.platform") version "2.19.0"
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

intellijPlatform {
    pluginConfiguration {
        ideaVersion {
            sinceBuild = "241"
            untilBuild = provider { null }
        }
    }
    buildSearchableOptions = false
}

// TextMate reads a bundle from disk, so ship it as files in the plugin directory.
tasks.prepareSandbox {
    from(layout.projectDirectory.dir("src/main/resources/bundles")) {
        into(intellijPlatform.projectName.map { "$it/bundles" })
    }
}

kotlin { jvmToolchain(17) }
