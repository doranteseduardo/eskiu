#!/usr/bin/env node
/**
 * server.js — Eskiu Language Server
 *
 * Minimal LSP server that runs eskiuc --test-typechecker on each file
 * change and converts the output to VS Code diagnostics.
 *
 * Protocol: JSON-RPC over stdin/stdout (standard LSP transport).
 *
 * Requires: Node.js 16+. No npm dependencies — pure Node.js stdlib.
 *
 * Install (already done if you used the symlink method):
 *   ln -s /path/to/eskiu/editor/vscode ~/.vscode/extensions/eskiu-language
 *   # Then restart VS Code.
 */

'use strict';

const { execFile } = require('child_process');
const path = require('path');
const fs   = require('fs');

// ── LSP message framing ───────────────────────────────────────────────────────

let buffer = Buffer.alloc(0);

process.stdin.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd === -1) break;
        const headers = buffer.slice(0, headerEnd).toString();
        const lenMatch = headers.match(/Content-Length:\s*(\d+)/i);
        if (!lenMatch) { buffer = buffer.slice(headerEnd + 4); continue; }
        const len = parseInt(lenMatch[1], 10);
        if (buffer.length < headerEnd + 4 + len) break;
        const body = buffer.slice(headerEnd + 4, headerEnd + 4 + len).toString();
        buffer = buffer.slice(headerEnd + 4 + len);
        handleMessage(JSON.parse(body));
    }
});

function send(obj) {
    const body = JSON.stringify(obj);
    process.stdout.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
}

// ── Message handler ───────────────────────────────────────────────────────────

// Path to eskiuc: $ESKIUC, else build22/build walking upwards or next to this file, else PATH
function findEskiuc(hintPath) {
    if (process.env.ESKIUC) {
        try { fs.accessSync(process.env.ESKIUC, fs.constants.X_OK); return process.env.ESKIUC; } catch {}
    }

    const buildNames = ['build22', 'build', 'build-debug', 'build-release'];

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

    const candidates = [
        path.join(__dirname, '..', '..', 'build22', 'eskiuc'),
        path.join(__dirname, '..', '..', 'build', 'eskiuc'),
        path.join(__dirname, '..', '..', '..', 'build22', 'eskiuc'),
        path.join(__dirname, '..', '..', '..', 'build', 'eskiuc'),
    ];
    for (const c of candidates) {
        try { fs.accessSync(c, fs.constants.X_OK); return c; } catch {}
    }
    return 'eskiuc'; // rely on PATH
}

// Find an entry point (e.g. kernel.esk or main.esk) that imports filePath,
// so multi-file symbols are resolved without spurious 'undefined variable/function' errors.
function findEntryPoint(filePath) {
    if (!filePath) return null;
    const docDir = path.dirname(filePath);
    const docBase = path.basename(filePath);

    const rootNames = ['kernel.esk', 'main.esk', 'app.esk', 'index.esk', 'root.esk'];
    for (const r of rootNames) {
        const rootPath = path.join(docDir, r);
        if (rootPath !== filePath && fs.existsSync(rootPath)) {
            try {
                const text = fs.readFileSync(rootPath, 'utf8');
                if (text.includes(`"${docBase}"`) || text.includes(`<${docBase}>`)) {
                    return rootPath;
                }
            } catch {}
        }
    }

    try {
        const files = fs.readdirSync(docDir);
        for (const f of files) {
            if (!f.endsWith('.esk') || f.startsWith('.')) continue;
            const full = path.join(docDir, f);
            if (full === filePath) continue;
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

// The extension's own version (serverInfo), read from package.json next to us.
const VERSION = (() => {
    try { return JSON.parse(fs.readFileSync(path.join(__dirname, 'package.json'), 'utf8')).version; }
    catch { return 'unknown'; }
})();

// Parse "file.esk:8:22: message" → LSP Diagnostic.
function parseErrors(text, checkedPath, origPath, fileContent) {
    const diagnostics = [];
    const re = /^(?:error:\s*)?(.+?):(\d+):(\d+):\s*(.+)$/gm;
    const wantChecked = path.resolve(checkedPath);
    const wantOrig = origPath ? path.resolve(origPath) : null;
    const lines = fileContent ? fileContent.split('\n') : null;
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

        let endCh = ch + 1;
        if (lines && ln < lines.length) {
            const lineText = lines[ln];
            while (endCh < lineText.length && /[a-zA-Z0-9_]/.test(lineText[endCh])) {
                endCh++;
            }
        }

        diagnostics.push({
            range: { start: { line: ln, character: ch },
                     end:   { line: ln, character: endCh } },
            severity: msg.startsWith('warning') ? 2 : 1,
            source:   'eskiuc',
            message:  msg.replace(/^error:\s*/, '').replace(/^warning:\s*/, ''),
        });
    }
    return diagnostics;
}

// Run eskiuc and publish diagnostics for a document
function validate(uri, filePath, content) {
    const compiler = findEskiuc(filePath);
    const entryPoint = findEntryPoint(filePath);
    const target = entryPoint || filePath;
    let fileText = content;
    if (fileText == null) {
        try { fileText = fs.readFileSync(filePath, 'utf8'); } catch {}
    }

    execFile(compiler, [target, '--test-typechecker'], { timeout: 10000, cwd: path.dirname(target) },
        (err, stdout, stderr) => {
            const output = (stdout || '') + (stderr || '');
            const diagnostics = parseErrors(output, target, filePath, fileText);
            send({ jsonrpc: '2.0', method: 'textDocument/publishDiagnostics',
                   params: { uri, diagnostics } });
        }
    );
}

// URI → filesystem path
function uriToPath(uri) {
    return decodeURIComponent(uri.replace(/^file:\/\//, ''));
}

function handleMessage(msg) {
    const { id, method, params } = msg;

    if (method === 'initialize') {
        send({ jsonrpc: '2.0', id, result: {
            capabilities: {
                textDocumentSync: 1,          // full: every change carries the whole text
                diagnosticProvider: { interFileDependencies: false,
                                      workspaceDiagnostics: false },
            },
            serverInfo: { name: 'eskiu-lsp', version: VERSION },
        }});
        return;
    }

    if (method === 'initialized') return;

    if (method === 'shutdown') { send({ jsonrpc: '2.0', id, result: null }); return; }
    if (method === 'exit')     { process.exit(0); }

    if (method === 'textDocument/didOpen') {
        const { uri, text } = params.textDocument;
        if (uri.endsWith('.esk')) validate(uri, uriToPath(uri), text);
        return;
    }

    if (method === 'textDocument/didSave') {
        const { uri } = params.textDocument;
        if (uri.endsWith('.esk')) validate(uri, uriToPath(uri));
        return;
    }

    if (method === 'textDocument/didChange') {
        const uri = params.textDocument && params.textDocument.uri;
        const contentChanges = params.contentChanges;
        if (!uri || !uri.endsWith('.esk')) return;
        const content = contentChanges?.[contentChanges.length - 1]?.text;
        if (content == null) return;

        const orig = uriToPath(uri);
        const compiler = findEskiuc(orig);
        const entryPoint = findEntryPoint(orig);
        const dir = path.dirname(orig);
        const base = path.basename(orig);

        let tmp = path.join(dir, `.eskiu_lsp_${process.pid}_${Date.now()}.esk`);
        try { fs.writeFileSync(tmp, content); }
        catch {
            tmp = path.join(require('os').tmpdir(), `.eskiu_lsp_${process.pid}_${Date.now()}.esk`);
            fs.writeFileSync(tmp, content);
        }

        let tmpEntry = null;
        if (entryPoint && fs.existsSync(entryPoint)) {
            try {
                const entryContent = fs.readFileSync(entryPoint, 'utf8');
                const relTmp = path.basename(tmp);
                const replaced = entryContent.replace(
                    new RegExp(`(import\\s+["'][^"']*?)\\b${base.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}(["'])`, 'g'),
                    `$1${relTmp}$2`
                );
                if (replaced !== entryContent) {
                    tmpEntry = path.join(path.dirname(entryPoint), `.eskiu_lsp_entry_${process.pid}_${Date.now()}.esk`);
                    fs.writeFileSync(tmpEntry, replaced);
                }
            } catch {}
        }

        const runTarget = tmpEntry || (entryPoint ? null : tmp) || tmp;

        execFile(compiler, [runTarget, '--test-typechecker'], { timeout: 10000, cwd: path.dirname(runTarget) },
            (err, stdout, stderr) => {
                try { fs.unlinkSync(tmp); } catch {}
                if (tmpEntry) { try { fs.unlinkSync(tmpEntry); } catch {} }

                const output = (stdout || '') + (stderr || '');
                const diagnostics = parseErrors(output, tmp, orig, content);
                send({ jsonrpc: '2.0', method: 'textDocument/publishDiagnostics',
                       params: { uri, diagnostics } });
            }
        );
        return;
    }

    // Respond to unknown requests with null to avoid timeouts
    if (id !== undefined) send({ jsonrpc: '2.0', id, result: null });
}

process.on('uncaughtException', () => {});
