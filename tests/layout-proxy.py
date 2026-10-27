"""Test-only JSON relay with a controlled serverinfo LED layout.

Only the configured TV may connect. Receiver settings remain unchanged.
The control file can select count/shift, an invalid layout or passthrough.
"""
import argparse
import json
from pathlib import Path
import socket
import socketserver

parser = argparse.ArgumentParser()
parser.add_argument('--bind', required=True)
parser.add_argument('--port', type=int, default=19445)
parser.add_argument('--upstream', required=True)
parser.add_argument('--upstream-port', type=int, default=19444)
parser.add_argument('--control-file', required=True)
args = parser.parse_args()


class Relay(socketserver.StreamRequestHandler):
    def handle(self):
        if self.client_address[0] != args.upstream:
            return
        try:
            with socket.create_connection((args.upstream, args.upstream_port), timeout=3) as remote:
                incoming = remote.makefile('rb')
                while line := self.rfile.readline(2 * 1024 * 1024):
                    command = json.loads(line)
                    remote.sendall(line)
                    reply = incoming.readline(2 * 1024 * 1024)
                    if not reply:
                        return
                    if command.get('command') == 'serverinfo':
                        control = json.loads(Path(args.control_file).read_text(encoding='utf-8'))
                        response = json.loads(reply)
                        if control.get('invalid'):
                            response['info']['leds'] = []
                        elif 'count' in control:
                            count = control['count']
                            if not isinstance(count, int) or not 1 <= count <= 16384:
                                return
                            shift = control.get('shift', 0)
                            response['info']['leds'] = [
                                {'hmin': (i + shift) / count, 'hmax': (i + 1) / count,
                                 'vmin': 0, 'vmax': .05}
                                for i in range(count)
                            ]
                        reply = json.dumps(response).encode() + b'\n'
                        print(json.dumps({'serverinfo': control}), flush=True)
                    self.request.sendall(reply[:10])
                    self.request.sendall(reply[10:])
        except (OSError, ValueError, KeyError):
            pass


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True


with Server((args.bind, args.port), Relay) as server:
    print('Layout relay ready', flush=True)
    server.serve_forever()
