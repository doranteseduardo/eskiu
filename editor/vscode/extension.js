'use strict';
/**
 * extension.js — VS Code extension entry point for Eskiu Language
 *
 * Launches server.js as a child process and connects to it via
 * vscode-languageclient (bundled with VS Code).
 *
 * If vscode-languageclient is not available (e.g. first install), the extension
 * falls back to direct diagnostic generation using execFile.
 */

const vscode = require('vscode');
const { execFile } = require('child_process');
const path = require('path');
const fs   = require('fs');

let diagnosticCollection;

function findEskiuc() {
    const candidates = [
        path.join(__dirname, '..', '..', 'build', 'eskiuc'),
        path.join(__dirname, '..', '..', '..', 'build', 'eskiuc'),
    ];
    for (const c of candidates) {
        try { fs.accessSync(c, fs.constants.X_OK); return c; } catch {}
    }
    return 'eskiuc';
}

function parseErrors(text, uri) {
    const diagnostics = [];
    const re = /^(.+?):(\d+):(\d+):\s*(.+)$/gm;
    let m;
    while ((m = re.exec(text)) !== null) {
        const [, , line, col, msg] = m;
        const ln = Math.max(0, parseInt(line, 10) - 1);
        const ch = Math.max(0, parseInt(col,  10) - 1);
        const severity = msg.startsWith('warning')
            ? vscode.DiagnosticSeverity.Warning
            : vscode.DiagnosticSeverity.Error;
        const range = new vscode.Range(ln, ch, ln, ch + 1);
        const d = new vscode.Diagnostic(range,
            msg.replace(/^(error|warning):\s*/, ''), severity);
        d.source = 'eskiuc';
        diagnostics.push(d);
    }
    return diagnostics;
}

function validate(document) {
    if (document.languageId !== 'eskiu') return;
    const eskiuc = findEskiuc();
    const filePath = document.uri.fsPath;
    execFile(eskiuc, [filePath, '--test-typechecker'], { timeout: 10000 },
        (err, stdout, stderr) => {
            const output = (stdout || '') + (stderr || '');
            diagnosticCollection.set(document.uri, parseErrors(output, document.uri));
        }
    );
}

function runEskiuc(filePath, args) {
    return new Promise((resolve) => {
        const eskiuc = findEskiuc();
        execFile(eskiuc, [filePath, ...args], { timeout: 5000 }, (err, stdout, stderr) => {
            resolve((stdout || '').trim());
        });
    });
}

function activate(context) {
    diagnosticCollection = vscode.languages.createDiagnosticCollection('eskiu');
    context.subscriptions.push(diagnosticCollection);

    // Validate on open and save
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(validate),
        vscode.workspace.onDidSaveTextDocument(validate),
    );

    // Validate already-open .esk files
    vscode.workspace.textDocuments.forEach(validate);

    // Clear diagnostics when file is closed
    context.subscriptions.push(
        vscode.workspace.onDidCloseTextDocument((doc) => {
            diagnosticCollection.delete(doc.uri);
        })
    );

    // Hover: show inferred type under cursor
    context.subscriptions.push(
        vscode.languages.registerHoverProvider('eskiu', {
            async provideHover(document, position) {
                const line = position.line + 1;
                const col  = position.character + 1;
                const type = await runEskiuc(document.uri.fsPath,
                    [`--hover-at`, `${line}:${col}`]);
                if (!type || type.startsWith('(')) return null;
                return new vscode.Hover(
                    new vscode.MarkdownString(`\`\`\`eskiu\n${type}\n\`\`\``)
                );
            }
        })
    );

    // Go-to-definition
    context.subscriptions.push(
        vscode.languages.registerDefinitionProvider('eskiu', {
            async provideDefinition(document, position) {
                const line = position.line + 1;
                const col  = position.character + 1;
                const loc = await runEskiuc(document.uri.fsPath,
                    [`--definition-at`, `${line}:${col}`]);
                // Format: file:line:col
                const m = loc.match(/^(.+):(\d+):(\d+)$/);
                if (!m) return null;
                const [, file, defLine, defCol] = m;
                const uri = vscode.Uri.file(file);
                const pos = new vscode.Position(
                    Math.max(0, parseInt(defLine, 10) - 1),
                    Math.max(0, parseInt(defCol,  10) - 1)
                );
                return new vscode.Location(uri, pos);
            }
        })
    );
}

function deactivate() {
    if (diagnosticCollection) diagnosticCollection.dispose();
}

module.exports = { activate, deactivate };
