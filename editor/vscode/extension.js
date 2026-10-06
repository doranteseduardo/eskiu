'use strict';
/**
 * extension.js: VS Code extension entry point for the Eskiu language.
 *
 * Every feature shells out to the compiler: diagnostics from `--test-typechecker`,
 * hover from `--hover-at`, go-to-definition from `--definition-at`, formatting from
 * `eskiuc fmt`, and the Run command from `eskiuc run`. An unsaved document is checked
 * through a hidden sibling file, so relative imports still resolve.
 */

const vscode = require('vscode');
const { execFile } = require('child_process');
const path = require('path');
const fs   = require('fs');
const os   = require('os');

let diagnosticCollection;
const pendingChecks = new Map();

function config() {
    return vscode.workspace.getConfiguration('eskiu');
}

// Extra flags configured by user, e.g. ["--freestanding"]
function getCompilerFlags() {
    const flags = config().get('compilerFlags');
    if (Array.isArray(flags)) return flags.filter(Boolean);
    if (typeof flags === 'string' && flags.trim()) return flags.trim().split(/\s+/);
    return [];
}

// The compiler: the `eskiu.compilerPath` setting, else `build22/eskiuc` or `build/eskiuc`
// in workspace folders or above hintPath, else `eskiuc` on PATH.
function findEskiuc(hintPath) {
    const configured = (config().get('compilerPath') || '').trim();
    if (configured) return configured;
    if (process.env.ESKIUC) {
        try { fs.accessSync(process.env.ESKIUC, fs.constants.X_OK); return process.env.ESKIUC; } catch {}
    }

    const buildNames = ['build22', 'build', 'build-debug', 'build-release'];

    // 1. Check workspace folders
    const wfs = vscode.workspace.workspaceFolders || [];
    for (const wf of wfs) {
        for (const b of buildNames) {
            const c = path.join(wf.uri.fsPath, b, 'eskiuc');
            try { fs.accessSync(c, fs.constants.X_OK); return c; } catch {}
        }
    }

    // 2. Check hintPath walking up directory tree
    if (hintPath) {
        let cur = path.dirname(path.resolve(hintPath));
        for (let i = 0; i < 6; i++) {
            for (const b of buildNames) {
                const c = path.join(cur, b, 'eskiuc');
                try { fs.accessSync(c, fs.constants.X_OK); return c; } catch {}
            }
            const parent = path.dirname(cur);
            if (parent === cur) break;
            cur = parent;
        }
    }

    // 3. Check relative to extension installation / checkout
    const relCandidates = [
        path.join(__dirname, '..', '..', 'build22', 'eskiuc'),
        path.join(__dirname, '..', '..', 'build', 'eskiuc'),
        path.join(__dirname, '..', '..', '..', 'build22', 'eskiuc'),
        path.join(__dirname, '..', '..', '..', 'build', 'eskiuc'),
    ];
    for (const c of relCandidates) {
        try { fs.accessSync(c, fs.constants.X_OK); return c; } catch {}
    }

    return 'eskiuc';
}

function runCompiler(args, timeout, cwd, hintPath) {
    const compiler = findEskiuc(hintPath);
    return new Promise((resolve) => {
        execFile(compiler, args, { timeout, cwd }, (err, stdout, stderr) => {
            if (err && err.code === 'ENOENT') {
                reportMissingCompiler(compiler);
                resolve({ ok: false, stdout: '', stderr: '' });
                return;
            }
            resolve({ ok: !err, stdout: stdout || '', stderr: stderr || '' });
        });
    });
}

let missingReported = false;
function reportMissingCompiler(name) {
    if (missingReported) return;
    missingReported = true;
    vscode.window.showWarningMessage(
        `Eskiu: could not run '${name || 'eskiuc'}'. Install eskiuc or set "eskiu.compilerPath".`,
        'Open Settings'
    ).then((choice) => {
        if (choice) vscode.commands.executeCommand('workbench.action.openSettings', 'eskiu.compilerPath');
    });
}

// Find an entry point (e.g. kernel.esk or main.esk) that imports docPath,
// so multi-file symbols are resolved without spurious 'undefined variable/function' errors.
function findEntryPoint(docPath) {
    if (!docPath) return null;
    const configured = (config().get('entryPoint') || '').trim();
    if (configured) {
        if (path.isAbsolute(configured) && fs.existsSync(configured)) return configured;
        const wfs = vscode.workspace.workspaceFolders || [];
        for (const wf of wfs) {
            const p = path.join(wf.uri.fsPath, configured);
            if (fs.existsSync(p)) return p;
        }
        const rel = path.join(path.dirname(docPath), configured);
        if (fs.existsSync(rel)) return rel;
    }

    const docDir = path.dirname(docPath);
    const docBase = path.basename(docPath);

    // Common root names to prioritize
    const rootNames = ['kernel.esk', 'main.esk', 'app.esk', 'index.esk', 'root.esk'];
    for (const r of rootNames) {
        const rootPath = path.join(docDir, r);
        if (rootPath !== docPath && fs.existsSync(rootPath)) {
            try {
                const text = fs.readFileSync(rootPath, 'utf8');
                if (text.includes(`"${docBase}"`) || text.includes(`<${docBase}>`)) {
                    return rootPath;
                }
            } catch {}
        }
    }

    // Check sibling .esk files in the same directory
    try {
        const files = fs.readdirSync(docDir);
        for (const f of files) {
            if (!f.endsWith('.esk') || f.startsWith('.')) continue;
            const full = path.join(docDir, f);
            if (full === docPath) continue;
            try {
                const text = fs.readFileSync(full, 'utf8');
                if (text.includes(`"${docBase}"`)) {
                    return full;
                }
            } catch {}
        }
    } catch {}

    return null;
}

// Run `fn(filePath, cwd)` on the document's current text.
// When standalone is false, multi-file entry points (e.g. kernel.esk, main.esk)
// are used to resolve imports and prevent false errors.
async function withCurrentText(document, fn, standalone = false) {
    const origPath = document.uri.fsPath;
    const entryPoint = standalone ? null : findEntryPoint(origPath);
    const dir = document.isUntitled ? os.tmpdir() : path.dirname(origPath);
    const base = document.isUntitled ? 'untitled.esk' : path.basename(origPath);

    if (!document.isDirty && !document.isUntitled) {
        if (entryPoint && fs.existsSync(entryPoint)) {
            const res = await fn(entryPoint, path.dirname(entryPoint));
            return { result: res, checked: origPath, orig: origPath };
        }
        const res = await fn(origPath, dir);
        return { result: res, checked: origPath, orig: origPath };
    }

    const tmp = path.join(dir, `.${base}.${process.pid}.${Date.now()}.eskiu-check.esk`);
    try {
        fs.writeFileSync(tmp, document.getText());
    } catch {
        const fallbackTarget = entryPoint || origPath;
        const res = await fn(fallbackTarget, path.dirname(fallbackTarget));
        return { result: res, checked: origPath, orig: origPath };
    }

    let tmpEntry = null;
    try {
        if (entryPoint && fs.existsSync(entryPoint)) {
            try {
                const entryContent = fs.readFileSync(entryPoint, 'utf8');
                const relTmp = path.basename(tmp);
                const replaced = entryContent.replace(
                    new RegExp(`(import\\s+["'][^"']*?)\\b${base.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}(["'])`, 'g'),
                    `$1${relTmp}$2`
                );
                if (replaced !== entryContent) {
                    tmpEntry = path.join(path.dirname(entryPoint), `.${path.basename(entryPoint)}.${process.pid}.${Date.now()}.eskiu-check.esk`);
                    fs.writeFileSync(tmpEntry, replaced);
                }
            } catch {}
        }

        const targetFile = tmpEntry || (entryPoint ? null : tmp);
        if (targetFile) {
            const res = await fn(targetFile, path.dirname(targetFile));
            return { result: res, checked: tmp, orig: origPath };
        } else {
            const res = await fn(entryPoint || tmp, dir);
            return { result: res, checked: tmp, orig: origPath };
        }
    } finally {
        fs.rm(tmp, { force: true }, () => {});
        if (tmpEntry) {
            fs.rm(tmpEntry, { force: true }, () => {});
        }
    }
}

// Diagnostics located in the checked file or original file belong to this document.
function parseErrors(text, checkedPath, origPath, document) {
    const diagnostics = [];
    const re = /^(?:error:\s*)?(.+?):(\d+):(\d+):\s*(.+)$/gm;
    const wantChecked = path.resolve(checkedPath);
    const wantOrig = origPath ? path.resolve(origPath) : null;
    let m;
    while ((m = re.exec(text)) !== null) {
        const [, file, line, col, msg] = m;
        const resFile = path.resolve(file);
        const isMatch = (resFile === wantChecked) ||
                        (wantOrig && resFile === wantOrig) ||
                        (path.basename(resFile) === path.basename(wantChecked)) ||
                        (wantOrig && path.basename(resFile) === path.basename(wantOrig));
        if (!isMatch) continue;

        const ln = Math.max(0, parseInt(line, 10) - 1);
        const ch = Math.max(0, parseInt(col,  10) - 1);

        // Find token end for a precise range squiggle
        let endCh = ch + 1;
        if (document && ln < document.lineCount) {
            const lineText = document.lineAt(ln).text;
            if (ch < lineText.length) {
                while (endCh < lineText.length && /[a-zA-Z0-9_]/.test(lineText[endCh])) {
                    endCh++;
                }
            }
        }

        const severity = msg.startsWith('warning')
            ? vscode.DiagnosticSeverity.Warning
            : vscode.DiagnosticSeverity.Error;
        const cleanMsg = msg.replace(/^(error|warning):\s*/, '');
        const d = new vscode.Diagnostic(new vscode.Range(ln, ch, ln, endCh), cleanMsg, severity);
        d.source = 'eskiuc';
        diagnostics.push(d);
    }
    return diagnostics;
}

async function validate(document) {
    if (document.languageId !== 'eskiu') return;
    const version = document.version;
    const extraFlags = getCompilerFlags();
    const { result, checked, orig } = await withCurrentText(document, (file, cwd) =>
        runCompiler([file, ...extraFlags, '--test-typechecker'], 10000, cwd, document.uri.fsPath));
    if (document.isClosed || document.version !== version) return;
    diagnosticCollection.set(document.uri, parseErrors(result.stdout + result.stderr, checked, orig, document));
}

function scheduleValidate(document) {
    if (document.languageId !== 'eskiu' || !config().get('checkOnType')) return;
    const key = document.uri.toString();
    clearTimeout(pendingChecks.get(key));
    pendingChecks.set(key, setTimeout(() => {
        pendingChecks.delete(key);
        validate(document);
    }, Math.max(100, config().get('checkDelay') || 400)));
}

async function hover(document, position) {
    const at = `${position.line + 1}:${position.character + 1}`;
    const extraFlags = getCompilerFlags();
    const { result } = await withCurrentText(document, (file, cwd) =>
        runCompiler([file, ...extraFlags, '--hover-at', at], 5000, cwd, document.uri.fsPath), true);
    const lines = result.stdout.trim().split('\n').map((l) => l.trim()).filter(Boolean);
    const lastLine = lines[lines.length - 1] || '';
    if (!lastLine || lastLine.startsWith('(') || lastLine.startsWith('error:')) return null;
    return new vscode.Hover(new vscode.MarkdownString(`\`\`\`eskiu\n${lastLine}\n\`\`\``));
}

async function definition(document, position) {
    const at = `${position.line + 1}:${position.character + 1}`;
    const extraFlags = getCompilerFlags();
    const { result, checked } = await withCurrentText(document, (file, cwd) =>
        runCompiler([file, ...extraFlags, '--definition-at', at], 5000, cwd, document.uri.fsPath), true);
    const m = result.stdout.trim().match(/^(.+):(\d+):(\d+)$/m);
    if (!m) return null;
    const [, file, line, col] = m;
    const target = path.resolve(file) === path.resolve(checked) ||
                   path.basename(file) === path.basename(document.uri.fsPath)
        ? document.uri
        : vscode.Uri.file(path.resolve(path.dirname(checked), file));
    return new vscode.Location(target, new vscode.Position(
        Math.max(0, parseInt(line, 10) - 1), Math.max(0, parseInt(col, 10) - 1)));
}

// `eskiuc fmt` rewrites a file in place, so format a temporary copy and replace the
// whole document with the result.
async function format(document) {
    const tmp = path.join(os.tmpdir(), `eskiu-fmt-${process.pid}-${Date.now()}.esk`);
    try {
        fs.writeFileSync(tmp, document.getText());
        const r = await runCompiler(['fmt', tmp], 10000, undefined, document.uri.fsPath);
        if (!r.ok) return [];
        const formatted = fs.readFileSync(tmp, 'utf8');
        if (formatted === document.getText()) return [];
        const all = new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length));
        return [vscode.TextEdit.replace(all, formatted)];
    } catch {
        return [];
    } finally {
        fs.rm(tmp, { force: true }, () => {});
    }
}

function quote(s) {
    return process.platform === 'win32' ? `"${s}"` : `'${s.replace(/'/g, `'\\''`)}'`;
}

async function runFile(uri) {
    const editor = vscode.window.activeTextEditor;
    const document = uri
        ? await vscode.workspace.openTextDocument(uri)
        : editor && editor.document;
    if (!document || document.languageId !== 'eskiu') {
        vscode.window.showInformationMessage('Eskiu: open a .esk file to run it.');
        return;
    }
    if (document.isUntitled) {
        vscode.window.showInformationMessage('Eskiu: save the file before running it.');
        return;
    }
    if (document.isDirty) await document.save();
    let terminal = vscode.window.terminals.find((t) => t.name === 'Eskiu');
    if (!terminal) terminal = vscode.window.createTerminal('Eskiu');
    terminal.show(true);
    const compiler = findEskiuc(document.uri.fsPath);
    terminal.sendText(`${quote(compiler)} run ${quote(document.uri.fsPath)}`);
}

function activate(context) {
    diagnosticCollection = vscode.languages.createDiagnosticCollection('eskiu');
    context.subscriptions.push(diagnosticCollection);

    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(validate),
        vscode.workspace.onDidSaveTextDocument(validate),
        vscode.workspace.onDidChangeTextDocument((e) => scheduleValidate(e.document)),
        vscode.workspace.onDidCloseTextDocument((doc) => {
            clearTimeout(pendingChecks.get(doc.uri.toString()));
            pendingChecks.delete(doc.uri.toString());
            diagnosticCollection.delete(doc.uri);
        }),
        vscode.workspace.onDidChangeConfiguration((e) => {
            if (e.affectsConfiguration('eskiu')) {
                missingReported = false;
                vscode.workspace.textDocuments.forEach(validate);
            }
        }),
        vscode.languages.registerHoverProvider('eskiu', { provideHover: hover }),
        vscode.languages.registerDefinitionProvider('eskiu', { provideDefinition: definition }),
        vscode.languages.registerDocumentFormattingEditProvider('eskiu', { provideDocumentFormattingEdits: format }),
        vscode.commands.registerCommand('eskiu.runFile', runFile),
    );

    vscode.workspace.textDocuments.forEach(validate);
}

function deactivate() {
    for (const t of pendingChecks.values()) clearTimeout(t);
    if (diagnosticCollection) diagnosticCollection.dispose();
}

module.exports = { activate, deactivate };
