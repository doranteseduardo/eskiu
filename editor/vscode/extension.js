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
}

function deactivate() {
    if (diagnosticCollection) diagnosticCollection.dispose();
}

module.exports = { activate, deactivate };
