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

// The compiler: the `eskiu.compilerPath` setting, else a `build/eskiuc` next to this
// extension when it runs from a checkout of the repository, else `eskiuc` on PATH.
function findEskiuc() {
    const configured = (config().get('compilerPath') || '').trim();
    if (configured) return configured;
    const candidates = [
        path.join(__dirname, '..', '..', 'build', 'eskiuc'),
        path.join(__dirname, '..', '..', '..', 'build', 'eskiuc'),
    ];
    for (const c of candidates) {
        try { fs.accessSync(c, fs.constants.X_OK); return c; } catch {}
    }
    return 'eskiuc';
}

function runCompiler(args, timeout) {
    return new Promise((resolve) => {
        execFile(findEskiuc(), args, { timeout }, (err, stdout, stderr) => {
            if (err && err.code === 'ENOENT') {
                reportMissingCompiler();
                resolve({ ok: false, stdout: '', stderr: '' });
                return;
            }
            resolve({ ok: !err, stdout: stdout || '', stderr: stderr || '' });
        });
    });
}

let missingReported = false;
function reportMissingCompiler() {
    if (missingReported) return;
    missingReported = true;
    vscode.window.showWarningMessage(
        `Eskiu: could not run '${findEskiuc()}'. Install eskiuc or set "eskiu.compilerPath".`,
        'Open Settings'
    ).then((choice) => {
        if (choice) vscode.commands.executeCommand('workbench.action.openSettings', 'eskiu.compilerPath');
    });
}

// Run `fn(filePath)` on the document's current text. A saved document is used as is;
// an unsaved one is written to a hidden file next to it (same directory, so relative
// imports resolve) that is removed afterwards. Returns fn's result and the path used.
async function withCurrentText(document, fn) {
    if (!document.isDirty && !document.isUntitled) {
        return { result: await fn(document.uri.fsPath), checked: document.uri.fsPath };
    }
    const dir = document.isUntitled ? os.tmpdir() : path.dirname(document.uri.fsPath);
    const base = document.isUntitled ? 'untitled.esk' : path.basename(document.uri.fsPath);
    const tmp = path.join(dir, `.${base}.${process.pid}.eskiu-check.esk`);
    try {
        fs.writeFileSync(tmp, document.getText());
    } catch {
        return { result: await fn(document.uri.fsPath), checked: document.uri.fsPath };
    }
    try {
        return { result: await fn(tmp), checked: tmp };
    } finally {
        fs.rm(tmp, { force: true }, () => {});
    }
}

// Diagnostics located in the checked file belong to this document (an error inside an
// imported module names that module's file).
function parseErrors(text, checkedPath) {
    const diagnostics = [];
    const re = /^(?:error:\s*)?(.+?):(\d+):(\d+):\s*(.+)$/gm;
    const want = path.resolve(checkedPath);
    let m;
    while ((m = re.exec(text)) !== null) {
        const [, file, line, col, msg] = m;
        if (path.resolve(file) !== want) continue;
        const ln = Math.max(0, parseInt(line, 10) - 1);
        const ch = Math.max(0, parseInt(col,  10) - 1);
        const severity = msg.startsWith('warning')
            ? vscode.DiagnosticSeverity.Warning
            : vscode.DiagnosticSeverity.Error;
        const d = new vscode.Diagnostic(new vscode.Range(ln, ch, ln, ch + 1),
            msg.replace(/^(error|warning):\s*/, ''), severity);
        d.source = 'eskiuc';
        diagnostics.push(d);
    }
    return diagnostics;
}

async function validate(document) {
    if (document.languageId !== 'eskiu') return;
    const version = document.version;
    const { result, checked } = await withCurrentText(document, (file) =>
        runCompiler([file, '--test-typechecker'], 10000));
    if (document.isClosed || document.version !== version) return;   // a newer check follows
    diagnosticCollection.set(document.uri, parseErrors(result.stdout + result.stderr, checked));
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
    const { result } = await withCurrentText(document, (file) =>
        runCompiler([file, '--hover-at', at], 5000));
    const type = result.stdout.trim();
    if (!type || type.startsWith('(')) return null;
    return new vscode.Hover(new vscode.MarkdownString(`\`\`\`eskiu\n${type}\n\`\`\``));
}

async function definition(document, position) {
    const at = `${position.line + 1}:${position.character + 1}`;
    const { result, checked } = await withCurrentText(document, (file) =>
        runCompiler([file, '--definition-at', at], 5000));
    const m = result.stdout.trim().match(/^(.+):(\d+):(\d+)$/);
    if (!m) return null;
    const [, file, line, col] = m;
    const target = path.resolve(file) === path.resolve(checked) ? document.uri : vscode.Uri.file(file);
    return new vscode.Location(target, new vscode.Position(
        Math.max(0, parseInt(line, 10) - 1), Math.max(0, parseInt(col, 10) - 1)));
}

// `eskiuc fmt` rewrites a file in place, so format a temporary copy and replace the
// whole document with the result.
async function format(document) {
    const tmp = path.join(os.tmpdir(), `eskiu-fmt-${process.pid}-${Date.now()}.esk`);
    try {
        fs.writeFileSync(tmp, document.getText());
        const r = await runCompiler(['fmt', tmp], 10000);
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
    terminal.sendText(`${quote(findEskiuc())} run ${quote(document.uri.fsPath)}`);
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
            if (e.affectsConfiguration('eskiu.compilerPath')) {
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
