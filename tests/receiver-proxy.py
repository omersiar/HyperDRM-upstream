"""Test-only TCP relay: synthetic token login and fragmented JSON replies.

Forward subsequent commands to a real receiver without changing its settings.
The fixed token is a test fixture, never a real receiver credential.
"""
import argparse
import json
import socket
import socketserver
import time

parser = argparse.ArgumentParser()
parser.add_argument('--bind', required=True)
parser.add_argument('--port', type=int, default=19445)
parser.add_argument('--upstream', required=True)
parser.add_argument('--upstream-port', type=int, default=19444)
args = parser.parse_args()


class Relay(socketserver.StreamRequestHandler):
    def handle(self):
        if self.client_address[0] != args.upstream:
            return
        try:
            with socket.create_connection((args.upstream, args.upstream_port), timeout=3) as remote:
                incoming = remote.makefile('rb')
                authorized = False
                while line := self.rfile.readline(2 * 1024 * 1024):
                    command = json.loads(line)
                    if command.get('command') == 'authorize':
                        authorized = command.get('token') == 'fixture-token'
                        reply = json.dumps({'command': 'authorize-login', 'success': authorized}).encode() + b'\n'
                    elif not authorized:
                        reply = b'{"success":false,"error":"No Authorization"}\n'
                    else:
                        remote.sendall(line)
                        reply = incoming.readline(2 * 1024 * 1024)
                        if not reply:
                            break
                        if command.get('command') != 'color':
                            print(json.dumps({'command': command.get('command'), 'bytes': len(reply)}), flush=True)
                    self.request.sendall(reply[:10])
                    time.sleep(0.005)
                    self.request.sendall(reply[10:])
        except (OSError, ValueError):
            pass


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True


with Server((args.bind, args.port), Relay) as server:
    print('Receiver relay ready', flush=True)
    server.serve_forever()
