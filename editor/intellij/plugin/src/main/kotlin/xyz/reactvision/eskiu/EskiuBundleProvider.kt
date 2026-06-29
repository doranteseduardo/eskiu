package xyz.reactvision.eskiu

import com.intellij.openapi.application.PluginPathManager
import org.jetbrains.plugins.textmate.api.TextMateBundleProvider
import kotlin.io.path.Path

/**
 * Registers the bundled Eskiu TextMate grammar with the platform's TextMate engine,
 * so installing the plugin enables `.esk` highlighting with no manual bundle import.
 *
 * The grammar lives at `resources/bundles/eskiu/` (a copy of the canonical
 * `editor/vscode/syntaxes/eskiu.tmLanguage.json`); keep them in sync.
 */
class EskiuBundleProvider : TextMateBundleProvider {
    override fun getBundles(): List<TextMateBundleProvider.PluginBundle> {
        val bundlePath = PluginPathManager
            .getPluginResource(javaClass, "bundles/eskiu")
            ?.toPath()
            ?: Path(javaClass.classLoader.getResource("bundles/eskiu")!!.toURI())
        return listOf(TextMateBundleProvider.PluginBundle("Eskiu", bundlePath))
    }
}
