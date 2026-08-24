# FRP public deployment

These templates expose only the meeting TCP service. Do not expose MySQL (`3306`),
the internal server port (`8000`), or an FRP dashboard to the public Internet.

## 1. Database account

Replace the sample password in `database-hardening.sql` and
`meeting-server.env.example` with the same long random value. Run the SQL once,
then install the environment file with owner `root:root` and mode `600`:

```bash
sudo install -o root -g root -m 600 meeting-server.env.example /etc/meeting-server.env
```

The server no longer contains the old `root/123456` credential and will refuse to
start unless `MEETING_DB_USER` and `MEETING_DB_PASSWORD` are present.

## 2. Build and run the server

Build inside the Linux VM that has `g++`, `make`, MySQL client headers, and the
MySQL client library installed:

```bash
cd serverDemo/src
make clean
make -j"$(nproc)"
sudo useradd --system --home /nonexistent --shell /usr/sbin/nologin meeting 2>/dev/null || true
sudo install -d -o root -g root -m 755 /opt/meeting
sudo install -o root -g root -m 755 server /opt/meeting/server
sudo install -o root -g root -m 644 /path/to/Meeting/deploy/meeting-server.service /etc/systemd/system/meeting-server.service
sudo systemctl daemon-reload
sudo systemctl enable --now meeting-server
```

Keep TCP `8000` blocked on the VM firewall except for loopback. `frpc` connects to
it through `127.0.0.1`.

## 3. Configure FRP

Use the same frp 0.68.x release for `frps` and `frpc`. Generate a random token and
put it in `/etc/frp/server_token` on the VPS and `/etc/frp/client_token` on the VM;
both files should be mode `600`. Fill `FRP_SERVER_ADDR` before starting `frpc`.

Validate before restart:

```bash
frps verify -c /etc/frp/frps.toml
FRP_SERVER_ADDR=YOUR_VPS_IP frpc verify -c /etc/frp/frpc.toml
```

On the public VPS firewall, allow TCP `7000` only from the VM's egress IP when
that address is stable, and allow TCP `18000` from client networks. No FRP web
dashboard is enabled by these templates.

## 4. Configure clients

Place `config.ini` next to `Meeting02.exe` and set its `host` to the VPS IP or DNS
name and `port` to `18000`. The new client supports both DNS names and IPv4.

FRP TLS protects the `frpc` to `frps` tunnel, but the public client to VPS segment
still uses the application's current plaintext TCP protocol. Session tokens now
prevent blind user-ID spoofing, but end-to-end confidentiality requires a later
application TLS upgrade and a DNS name/certificate.
