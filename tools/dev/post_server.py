#!/usr/bin/env python3
"""Echo server for testing POST/fetch/XHR from the Willows browser: python3 tools/dev/post_server.py  (port 8002).
From the guest it is http://10.0.2.2:8002/ (QEMU's gateway = the host). /api returns JSON, any other path an HTML page, /redir answers 303."""
import http.server, json
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _reply(self):
        n = int(self.headers.get('Content-Length', 0) or 0); body = self.rfile.read(n).decode('utf-8', 'replace') if n else ''
        if self.path.startswith('/redir'):
            self.send_response(303); self.send_header('Location', '/landed'); self.send_header('Content-Length', '0'); self.end_headers(); return
        if self.path.startswith('/file/'):                     # /file/<name> serves build/dev/dl/<name> (real installers fetched for testing; git-ignored)
            import os; p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build', 'dev', 'dl', os.path.basename(self.path[6:]))
            data = open(p, 'rb').read()
            self.send_response(200); self.send_header('Content-Type', 'application/octet-stream'); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data); return
        if self.path.startswith('/dl/'):                     # download tests: /dl/cd (named by Content-Disposition), /dl/windows (no extension, no header)
            kb = 3
            if self.path.startswith('/dl/big/'): kb = int(self.path.split('/')[3])              # /dl/big/4000 serves a file of that many KB
            data = b'MZ' + b'\0' * (kb * 1000)
            self.send_response(200); self.send_header('Content-Type', 'application/octet-stream'); self.send_header('Content-Length', str(len(data)))
            if self.path.startswith('/dl/cd'): self.send_header('Content-Disposition', 'attachment; filename="Setup Tool (v2).exe"')
            self.end_headers(); self.wfile.write(data); return
        out = json.dumps({'method': self.command, 'path': self.path, 'ctype': self.headers.get('Content-Type'), 'len': n, 'body': body, 'x': self.headers.get('X-Test')})
        ct = 'application/json' if self.path.startswith('/api') else 'text/html'
        data = (out if ct == 'application/json' else '<html><head><title>POST result</title></head><body><h1>Server saw a %s</h1><pre>%s</pre></body></html>' % (self.command, out.replace('&', '&amp;').replace('<', '&lt;'))).encode()
        self.send_response(200); self.send_header('Content-Type', ct); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
    do_GET = do_POST = do_PUT = _reply
if __name__ == '__main__':
    http.server.ThreadingHTTPServer(('0.0.0.0', 8002), H).serve_forever()
