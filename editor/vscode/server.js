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

// Path to eskiuc — try next to this file, then PATH
function findEskiuc() {
    const candidates = [
        path.join(__dirname, '..', '..', 'build', 'eskiuc'),
        path.join(__dirname, '..', '..', '..', 'build', 'eskiuc'),
        'eskiuc',
    ];
    for (const c of candidates) {
        try { fs.accessSync(c, fs.constants.X_OK); return c; } catch {}
    }
    return 'eskiuc'; // rely on PATH
}

const ESKIUC = findEskiuc();

// Parse "file.esk:8:22: message" → LSP Diagnostic
function parseErrors(text, fileUri) {
    const diagnostics = [];
    const re = /^(.+?):(\d+):(\d+):\s*(.+)$/gm;
    let m;
    while ((m = re.exec(text)) !== null) {
        const [, , line, col, msg] = m;
        const ln  = Math.max(0, parseInt(line, 10) - 1);
        const ch  = Math.max(0, parseInt(col,  10) - 1);
        diagnostics.push({
            range: { start: { line: ln, character: ch },
                     end:   { line: ln, character: ch + 1 } },
            severity: msg.startsWith('warning') ? 2 : 1,
            source:   'eskiuc',
            message:  msg.replace(/^error:\s*/, '').replace(/^warning:\s*/, ''),
        });
    }
    return diagnostics;
}

// Run eskiuc and publish diagnostics for a document
function validate(uri, filePath) {
    execFile(ESKIUC, [filePath, '--test-typechecker'], { timeout: 10000 },
        (err, stdout, stderr) => {
            const output = (stdout || '') + (stderr || '');
            const diagnostics = parseErrors(output, uri);
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
                textDocumentSync: 2,          // incremental
                diagnosticProvider: { interFileDependencies: false,
                                      workspaceDiagnostics: false },
            },
            serverInfo: { name: 'eskiu-lsp', version: '0.0.11' },
        }});
        return;
    }

    if (method === 'initialized') return;

    if (method === 'shutdown') { send({ jsonrpc: '2.0', id, result: null }); return; }
    if (method === 'exit')     { process.exit(0); }

    if (method === 'textDocument/didOpen') {
        const { uri } = params.textDocument;
        if (uri.endsWith('.esk')) validate(uri, uriToPath(uri));
        return;
    }

    if (method === 'textDocument/didSave') {
        const { uri } = params.textDocument;
        if (uri.endsWith('.esk')) validate(uri, uriToPath(uri));
        return;
    }

    if (method === 'textDocument/didChange') {
        // Write content to a temp file and validate
        const { uri, contentChanges } = params.textDocument
            ? params : { textDocument: params.textDocument, contentChanges: params.contentChanges };
        if (!uri || !uri.endsWith('.esk')) return;
        const content = contentChanges?.[contentChanges.length - 1]?.text;
        if (content == null) return;
        const tmp = path.join(require('os').tmpdir(), `eskiu_lsp_${Date.now()}.esk`);
        fs.writeFileSync(tmp, content);
        execFile(ESKIUC, [tmp, '--test-typechecker'], { timeout: 10000 },
            (err, stdout, stderr) => {
                fs.unlinkSync(tmp);
                // Remap temp file path back to original URI
                const output = ((stdout || '') + (stderr || '')).replace(
                    new RegExp(tmp.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'g'),
                    uriToPath(uri)
                );
                send({ jsonrpc: '2.0', method: 'textDocument/publishDiagnostics',
                       params: { uri, diagnostics: parseErrors(output, uri) } });
            }
        );
        return;
    }

    // Respond to unknown requests with null to avoid timeouts
    if (id !== undefined) send({ jsonrpc: '2.0', id, result: null });
}

process.on('uncaughtException', () => {});
