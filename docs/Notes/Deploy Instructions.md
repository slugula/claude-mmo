# Deploy Instructions

## Infrastructure

- **Client** — served by Nginx from `~/app/dist` on the Lightsail instance
- **Server** — Node.js WebSocket server running via PM2 on the Lightsail instance
- **URL** — http://34.204.12.71
- **SSH** — Lightsail dashboard → instance → Connect using SSH (browser console)

## Security Notes

- Port 8080 is **closed** on both IPv4 and IPv6 firewalls — players connect via Nginx on port 80 only
- PM2 is configured to **auto-restart on reboot** (`pm2 startup` + `pm2 save` already run)
- No HTTPS yet — needed before going public (requires domain + Let's Encrypt)
- No player authentication — anyone with the URL can join (fine for friend testing)

---

## Deploy Workflow

### When you change client code (anything in `src/`, `index.html`)

Run these on your **Windows machine**:

```bash
npm run build
git add dist/ src/
git commit -m "your message"
git push
```

Then in the **Lightsail SSH console**:

```bash
cd ~/app && git pull
sudo systemctl reload nginx
```

### When you change server code (anything in `server/`)

Run these on your **Windows machine**:

```bash
git add server/
git commit -m "your message"
git push
```

Then in the **Lightsail SSH console**:

```bash
cd ~/app && git pull
pm2 restart game-server
```

### When you change both at once

**Windows machine**:

```bash
npm run build
git add dist/ src/ server/
git commit -m "your message"
git push
```

**Lightsail SSH console**:

```bash
cd ~/app && git pull
sudo systemctl reload nginx
pm2 restart game-server
```

---

## Useful Server Commands

| Command                        | What it does                    |
| ------------------------------ | ------------------------------- |
| `pm2 status`                   | Check if game server is running |
| `pm2 logs game-server`         | View live server logs           |
| `pm2 restart game-server`      | Restart the game server         |
| `sudo systemctl reload nginx`  | Reload Nginx (no downtime)      |
| `sudo systemctl restart nginx` | Full Nginx restart              |
| `sudo nginx -t`                | Test Nginx config for errors    |
