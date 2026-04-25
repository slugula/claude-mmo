
Three things need to change to go from localhost to a remote player:

**1. Expose the server port**  
Right now `:8080` only accepts connections from `localhost`. You need to either:

- Run it on a machine with a public IP and open port 8080 in the firewall/router
- Use a tunneling tool like **ngrok** (`ngrok tcp 8080`) to get a public URL instantly — easiest for a quick test with one friend, no server setup required

**2. Update the client's WebSocket URL**  
In `src/engine/NetworkClient.ts`, the connect call is hardcoded:

```
this.network.connect('ws://localhost:8080');
```

Change this to your public IP or ngrok URL (e.g. `ws://3.14.159.26:8080` or `wss://abc123.ngrok.io`). Long-term this should be an environment variable via Vite's `import.meta.env`.

**3. Serve the client publicly**  
`npm run dev` is Vite's local dev server — it also only listens on localhost by default. Options:

- `npm run build` then serve the `dist/` folder from any static host (Netlify, Vercel, GitHub Pages — all free)
- Or `vite --host` to expose the dev server on your local network IP (LAN only)

**For a quick friend test today**: ngrok for the server + `vite --host` for the client on the same machine is the fastest path. For anything more serious, deploy the built client to Vercel and run the Node server on a cheap VPS or fly.io instance.