package xyz.reactvision.eskiu

import com.intellij.ide.plugins.PluginManagerCore
import com.intellij.openapi.extensions.PluginId
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
        val plugin = PluginManagerCore.getPlugin(PluginId.getId("xyz.reactvision.eskiu"))
            ?: return emptyList()
        return listOf(TextMateBundleProvider.PluginBundle("Eskiu", plugin.pluginPath.resolve("bundles/eskiu")))
    }
}
