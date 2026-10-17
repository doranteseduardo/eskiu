package xyz.reactvision.eskiu

import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.PersistentStateComponent
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.State
import com.intellij.openapi.components.Storage

/** Settings → Tools → Eskiu: where to find `eskiuc` and `node` (empty = look them up), and extra compiler flags. */
@Service(Service.Level.APP)
@State(name = "EskiuSettings", storages = [Storage("eskiu.xml")])
class EskiuSettings : PersistentStateComponent<EskiuSettings.Paths> {
    class Paths {
        var compilerPath: String = ""
        var nodePath: String = ""
        var compilerFlags: String = ""
    }

    private var paths = Paths()

    override fun getState(): Paths = paths

    override fun loadState(state: Paths) {
        paths = state
    }

    companion object {
        fun get(): EskiuSettings = ApplicationManager.getApplication().getService(EskiuSettings::class.java)
    }
}
