# ComsPlus Server

Small relay for the ComsPlus main menu chat.

Recommended public endpoint:

```text
https://hexasystems.xyz/comsplus
```

Use `cloudflare/` when the domain is routed through Cloudflare. Use this Node server when you want to run the relay on a VPS and reverse-proxy `/comsplus/*` to it.

## Run

```bash
npm start
```

Optional public port:

```bash
PORT=8787 node comsplus-server.js
```

For a persistent Linux server, run it behind HTTPS with nginx/Caddy and keep it alive with PM2:

```bash
pm2 start comsplus-server.js --name comsplus --update-env
pm2 save
```

This release always connects to:

```text
https://hexasystems.xyz/comsplus
```
