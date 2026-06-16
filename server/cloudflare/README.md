# ComsPlus Cloudflare Relay

This Worker serves the ComsPlus main menu chat at:

```text
https://hexasystems.xyz/comsplus
```

It supports:

- `GET /comsplus/health`
- `GET /comsplus/poll?room=main&since=0`
- `POST /comsplus/send?room=main`

## Deploy

```bash
npm install
npm run deploy
```

The route is already set in `wrangler.jsonc`:

```text
hexasystems.xyz/comsplus*
```
