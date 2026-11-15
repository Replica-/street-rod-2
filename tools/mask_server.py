"""Mask editor server: serves tools/mask_editor.html and takes its "Send to Claude" submissions.

  python tools/mask_server.py            (then open http://localhost:8765/?img=work/fooocus/hedge_zoom.png)

A submission is written to work/edits/<date-time>_<picture>/: picture.png (the picture as edited), mask_<n>.png (white =
change, one per touch-up), marked.png (the picture with every touch-up in its colour) and request.txt (each touch-up's
description). Claude watches work/edits for new folders. Pictures are opened from the repository only (?img=path).
Standard library only; listens on localhost.
"""
import base64
import json
import os
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EDITS = os.path.join(ROOT, 'work', 'edits')
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765


def data_url_bytes(url):
    return base64.b64decode(url.split(',', 1)[1])


class Handler(BaseHTTPRequestHandler):
    def send(self, code, body, ctype='text/plain; charset=utf-8'):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path in ('/', '/index.html'):
            with open(os.path.join(ROOT, 'tools', 'mask_editor.html'), 'rb') as f:
                return self.send(200, f.read(), 'text/html; charset=utf-8')
        if u.path == '/file':                       # a picture from the repository
            rel = parse_qs(u.query).get('path', [''])[0]
            full = os.path.normpath(os.path.join(ROOT, rel))
            if not full.startswith(ROOT + os.sep) or not os.path.isfile(full):
                return self.send(404, b'not found')
            ext = os.path.splitext(full)[1].lower()
            ctype = {'.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.bmp': 'image/bmp',
                     '.gif': 'image/gif', '.webp': 'image/webp'}.get(ext, 'application/octet-stream')
            with open(full, 'rb') as f:
                return self.send(200, f.read(), ctype)
        return self.send(404, b'not found')

    def do_POST(self):
        if urlparse(self.path).path != '/submit':
            return self.send(404, b'not found')
        n = int(self.headers.get('Content-Length', 0))
        req = json.loads(self.rfile.read(n))
        name = re.sub(r'[^A-Za-z0-9_.-]+', '_', req.get('name', 'picture'))[:60] or 'picture'
        out = os.path.join(EDITS, time.strftime('%Y-%m-%d_%H-%M-%S') + '_' + name)
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, 'picture.png'), 'wb') as f:
            f.write(data_url_bytes(req['picture']))
        with open(os.path.join(out, 'marked.png'), 'wb') as f:
            f.write(data_url_bytes(req['marked']))
        lines = [f"Picture: {req.get('name')} ({req.get('source') or 'opened from disk'}), "
                 f"{req.get('width')} x {req.get('height')}", '']
        for k, t in enumerate(req['touchups'], 1):
            with open(os.path.join(out, f'mask_{k}.png'), 'wb') as f:
                f.write(data_url_bytes(t['mask']))
            lines.append(f"{k}. {t['title']} ({t['color']}, mask_{k}.png): {t['desc'].strip() or '(no description)'}")
        if req.get('note', '').strip():
            lines += ['', 'Note: ' + req['note'].strip()]
        with open(os.path.join(out, 'request.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        print('submission:', out, flush=True)
        self.send(200, json.dumps({'ok': True, 'folder': os.path.relpath(out, ROOT)}).encode(), 'application/json')

    def log_message(self, fmt, *args):
        pass


if __name__ == '__main__':
    os.makedirs(EDITS, exist_ok=True)
    print(f'Mask editor: http://localhost:{PORT}/  (submissions -> {EDITS})', flush=True)
    ThreadingHTTPServer(('127.0.0.1', PORT), Handler).serve_forever()
