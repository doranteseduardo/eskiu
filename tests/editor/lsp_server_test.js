#!/usr/bin/env node
// Exercises editor/vscode/server.js over stdio (run from tests/run.sh when node is
// available): full-document sync, serverInfo.version read from package.json, and an
// unsaved buffer checked next to its file so a relative import still resolves, with
// only this document's own diagnostics published.
'use strict';
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

const root = path.resolve(__dirname, '..', '..');
const server = spawn(process.execPath, [path.join(root, 'editor', 'vscode', 'server.js')]);
const pkg = JSON.parse(fs.readFileSync(path.join(root, 'editor', 'vscode', 'package.json'), 'utf8'));
const docPath = path.join(__dirname, 'unsaved_main.esk');
const uri = 'file://' + docPath;
const text = [
    'import "helper.esk";',
    'int main() {',
    '    int x = "oops";',
    '    return helper_value();',
    '}',
    '',
].join('\n');

function send(obj) {
    const body = JSON.stringify(obj);
    server.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`);
}

let buf = Buffer.alloc(0);
const failures = [];
let gotInit = false;
const timer = setTimeout(() => { failures.push('timeout'); finish(); }, 20000);

function finish() {
    clearTimeout(timer);
    server.kill();
    if (failures.length) { console.log('FAIL ' + failures.join('; ')); process.exit(1); }
    console.log('ok');
    process.exit(0);
}

server.stdout.on('data', (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    while (true) {
        const he = buf.indexOf('\r\n\r\n');
        if (he < 0) return;
        const len = parseInt(/Content-Length:\s*(\d+)/i.exec(buf.slice(0, he).toString())[1], 10);
        if (buf.length < he + 4 + len) return;
        const msg = JSON.parse(buf.slice(he + 4, he + 4 + len).toString());
        buf = buf.slice(he + 4 + len);
        if (msg.id === 1) {
            gotInit = true;
            const r = msg.result;
            if (r.capabilities.textDocumentSync !== 1) failures.push('textDocumentSync is not full (1)');
            if (r.serverInfo.version !== pkg.version)
                failures.push(`serverInfo.version ${r.serverInfo.version} != package.json ${pkg.version}`);
            send({ jsonrpc: '2.0', method: 'textDocument/didChange', params: {
                textDocument: { uri, version: 2 }, contentChanges: [{ text }] } });
        } else if (msg.method === 'textDocument/publishDiagnostics') {
            const d = msg.params.diagnostics;
            if (d.length !== 1) failures.push(`expected 1 diagnostic, got ${d.length}: ${JSON.stringify(d)}`);
            else if (d[0].range.start.line !== 2) failures.push(`diagnostic on line ${d[0].range.start.line + 1}, expected 3`);
            if (fs.readdirSync(__dirname).some((f) => f.startsWith('.eskiu_lsp_')))
                failures.push('temp file left behind');
            finish();
        }
    }
});

send({ jsonrpc: '2.0', id: 1, method: 'initialize', params: {} });
