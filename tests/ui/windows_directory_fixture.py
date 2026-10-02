"""Loopback-only WinUI interaction fixture; never a substitute for production E2E.

Run with Python, then launch Catro with CATRO_SERVICE_URL=http://127.0.0.1:18765
and CATRO_ALLOW_INSECURE_RTC=1 in that process's environment only. No dependencies,
credentials on disk, external requests, or RTC transport. /_test/status reports
request counts for foreground/background/minimized pacing checks.
"""

import json
import time
from collections import Counter
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlsplit


class Fixture(BaseHTTPRequestHandler):
    user = {}
    server = {}
    messages = []
    counts = Counter()

    def log_message(self, *_):
        pass  # Never log authorization headers or the registration credential.

    def reply(self, value, status=200):
        body = json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/_test/status":
            self.reply({"requests": dict(self.counts), "message_count": len(self.messages)})
            return
        self.counts["GET " + path] += 1
        if path == "/v1/servers":
            self.reply({"servers": [self.server]})
        elif path == "/v1/members":
            self.reply({"members": [
                {**self.user, "role": "owner"},
                {"user_id": "ab" * 16, "display_name": "Mira", "role": "member"},
            ]})
        elif path == "/v1/join-requests":
            self.reply({"requests": []})
        elif path == "/v1/messages":
            after = int(parse_qs(urlsplit(self.path).query).get("after", ["0"])[0])
            rows = [row for row in self.messages if row["sequence"] > after][:100]
            self.reply({"messages": rows, "next_after": rows[-1]["sequence"] if rows else after})
        else:
            self.reply({"error": "This action is not provided by the local UI fixture."}, 404)

    def do_POST(self):
        path = urlsplit(self.path).path
        self.counts["POST " + path] += 1
        length = int(self.headers.get("Content-Length", 0))
        if length > 8192:
            self.reply({"error": "Fixture request too large"}, 413)
            return
        body = json.loads(self.rfile.read(length))
        if path == "/v1/users/register":
            Fixture.user = {"user_id": body["user_id"], "display_name": body["display_name"]}
            self.reply({"access_token": "local-ui-fixture-not-a-production-token"})
        elif path == "/v1/servers/sync":
            Fixture.server = {
                **body, "id": body["server_id"], "owner_id": self.user["user_id"],
                "role": "owner", "member_count": 2,
                "public_code": "CAT-1234-5678-ABCD-EF01-2345",
            }
            self.reply(self.server)
        elif path == "/v1/messages":
            if body["content"] == "fixture:fail":
                self.reply({"error": "Local test failure. Your draft is safe; retry when ready."}, 503)
                return
            sequence = len(self.messages) + 1
            message = {
                **body, "id": f"{sequence:032x}", "sequence": sequence,
                "author_id": self.user["user_id"],
                "author_display_name": self.user["display_name"],
                "created_at": int(time.time() * 1000),
            }
            self.messages.append(message)
            self.reply(message)
        elif path == "/v1/invites":
            self.reply({"code": "LOCAL-UI-INVITE", "expires": int(time.time()) + 900,
                        "server": self.server})
        else:
            self.reply({"error": "Local fixture: RTC and membership changes are intentionally disabled."}, 503)


if __name__ == "__main__":
    print("Local UI fixture: 127.0.0.1:18765 (no production access)", flush=True)
    HTTPServer(("127.0.0.1", 18765), Fixture).serve_forever()
