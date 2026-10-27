"""Test-only FlatBuffer TCP relay; accepts only the configured TV address."""
import argparse
import select
import socket
import socketserver

parser = argparse.ArgumentParser()
parser.add_argument('--bind', required=True)
parser.add_argument('--port', type=int, default=19446)
parser.add_argument('--upstream', required=True)
parser.add_argument('--upstream-port', type=int, default=19400)
args = parser.parse_args()


class Relay(socketserver.BaseRequestHandler):
    def handle(self):
        if self.client_address[0] != args.upstream:
            return
        transferred = 0
        try:
            with socket.create_connection((args.upstream, args.upstream_port), timeout=3) as remote:
                self.request.settimeout(3)
                while True:
                    readable, _, _ = select.select([self.request, remote], [], [], 3)
                    if not readable:
                        break
                    for source in readable:
                        data = source.recv(65536)
                        if not data:
                            return
                        destination = remote if source is self.request else self.request
                        destination.sendall(data)
                        if source is self.request:
                            if not transferred:
                                print('PicCap FlatBuffer data received', flush=True)
                            transferred += len(data)
        except OSError:
            pass
        finally:
            print(f'FlatBuffer connection closed: {transferred} bytes from TV', flush=True)


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True


with Server((args.bind, args.port), Relay) as server:
    print('FlatBuffer relay ready', flush=True)
    server.serve_forever()
