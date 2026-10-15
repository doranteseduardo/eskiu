package xyz.reactvision.eskiu

import com.intellij.openapi.application.PathManager
import java.nio.file.Path
import org.jetbrains.plugins.textmate.api.TextMateBundleProvider

/**
 * Registers the bundled Eskiu TextMate grammar with the platform's TextMate engine,
 * so installing the plugin enables `.esk` highlighting with no manual bundle import.
 *
 * The build copies `src/main/resources/bundles/eskiu/` (a copy of the canonical
 * `editor/vscode/syntaxes/eskiu.tmLanguage.json`, refreshed by `make -C editor
 * sync-grammar`) into the plugin's own directory, where TextMate can read it as files.
 */
class EskiuBundleProvider : TextMateBundleProvider {
    override fun getBundles(): List<TextMateBundleProvider.PluginBundle> {
        // The plugin directory holds lib/<our jar> and bundles/eskiu.
        val jar = PathManager.getJarPathForClass(EskiuBundleProvider::class.java) ?: return emptyList()
        val dir = Path.of(jar).parent?.parent ?: return emptyList()
        return listOf(TextMateBundleProvider.PluginBundle("Eskiu", dir.resolve("bundles/eskiu")))
    }
}
