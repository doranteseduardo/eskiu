package xyz.reactvision.eskiu

import com.intellij.openapi.options.Configurable
import com.intellij.ui.components.JBTextField
import com.intellij.util.ui.FormBuilder
import javax.swing.JComponent
import javax.swing.JPanel

class EskiuConfigurable : Configurable {
    private val compiler = JBTextField()
    private val node = JBTextField()

    override fun getDisplayName(): String = "Eskiu"

    override fun createComponent(): JComponent {
        compiler.emptyText.text = "build22/eskiuc or build/eskiuc in the project, ~/.eskiu/bin/eskiuc, else PATH"
        node.emptyText.text = "node on PATH, Homebrew, fnm, nvm or Volta"
        val panel: JPanel = FormBuilder.createFormBuilder()
            .addLabeledComponent("eskiuc path:", compiler)
            .addLabeledComponent("Node.js path:", node)
            .addComponentFillVertically(JPanel(), 0)
            .panel
        reset()
        return panel
    }

    override fun isModified(): Boolean {
        val s = EskiuSettings.get().state
        return compiler.text.trim() != s.compilerPath || node.text.trim() != s.nodePath
    }

    override fun apply() {
        val s = EskiuSettings.get().state
        s.compilerPath = compiler.text.trim()
        s.nodePath = node.text.trim()
    }

    override fun reset() {
        val s = EskiuSettings.get().state
        compiler.text = s.compilerPath
        node.text = s.nodePath
    }
}
