"use strict";

const http = require("node:http");

const port = Number.parseInt(process.env.PORT || "8787", 10);
const maxBodyBytes = 4096;
const maxMessagesPerRoom = 250;
const maxActivityRows = 80;
const presenceTtlMs = 45_000;
const rooms = new Map();

function roomName(value) {
  const clean = String(value || "main").replace(/[^a-zA-Z0-9_-]/g, "").slice(0, 32);
  return clean || "main";
}

function getRoom(name) {
  const key = roomName(name);
  let room = rooms.get(key);
  if (!room) {
    room = { lastId: 0, messages: [], presence: new Map(), activity: [] };
    rooms.set(key, room);
  }
  return room;
}

function write(res, status, body, contentType = "text/plain; charset=utf-8") {
  res.writeHead(status, {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET,POST,OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type",
    "Cache-Control": "no-store",
    "Content-Type": contentType
  });
  res.end(body);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let body = "";
    req.setEncoding("utf8");
    req.on("data", chunk => {
      body += chunk;
      if (Buffer.byteLength(body, "utf8") > maxBodyBytes) {
        reject(new Error("body too large"));
        req.destroy();
      }
    });
    req.on("end", () => resolve(body));
    req.on("error", reject);
  });
}

function validPayload(payload) {
  if (!payload || payload.length > maxBodyBytes) return false;
  try {
    const parsed = JSON.parse(payload);
    return parsed && parsed.v === 1 && typeof parsed.id === "string" && typeof parsed.name === "string";
  } catch {
    return false;
  }
}

function sanitizeField(value, fallback, maxLength) {
  const clean = String(value || fallback).replace(/[\t\r\n]/g, " ").trim().slice(0, maxLength);
  return clean || fallback;
}

function parsePayload(payload) {
  if (!validPayload(payload)) return null;
  try {
    return JSON.parse(payload);
  } catch {
    return null;
  }
}

function presenceKey(accountId, name) {
  return accountId > 0 ? `a:${accountId}` : `n:${String(name).toLowerCase()}`;
}

function prune(room, now) {
  for (const [key, value] of room.presence) {
    if (value.lastSeen < now - presenceTtlMs) {
      room.presence.delete(key);
    }
  }
}

function addActivity(room, now, value) {
  room.activity.push({ createdAt: now, text: sanitizeField(value, "activity", 96) });
  if (room.activity.length > maxActivityRows) {
    room.activity.splice(0, room.activity.length - maxActivityRows);
  }
}

function heartbeat(room, nameValue, accountId, iconValue, now = Date.now()) {
  prune(room, now);
  const name = sanitizeField(nameValue, "Player", 24);
  const icon = sanitizeField(iconValue, "", 64);
  const key = presenceKey(accountId, name);
  const existing = room.presence.get(key);
  const rejoined = !existing || existing.lastSeen < now - presenceTtlMs;
  room.presence.set(key, {
    name,
    accountId,
    icon,
    joinedAt: rejoined ? now : existing.joinedAt,
    lastSeen: now,
    messageCount: existing ? existing.messageCount : 0
  });
  if (rejoined) {
    addActivity(room, now, `${name} joined main chat`);
  }
}

function routePath(pathname) {
  return pathname.startsWith("/comsplus/") ? pathname.slice("/comsplus".length) : pathname;
}

const server = http.createServer(async (req, res) => {
  if (req.method === "OPTIONS") {
    write(res, 204, "");
    return;
  }

  const url = new URL(req.url || "/", `http://${req.headers.host || "localhost"}`);

  const path = routePath(url.pathname);

  if (req.method === "GET" && path === "/health") {
    write(res, 200, "ok\n");
    return;
  }

  if (req.method === "GET" && path === "/poll") {
    const room = getRoom(url.searchParams.get("room"));
    const since = Number.parseInt(url.searchParams.get("since") || "0", 10) || 0;
    const accountId = Number.parseInt(url.searchParams.get("aid") || "0", 10) || 0;
    heartbeat(room, url.searchParams.get("name") || "Player", accountId, url.searchParams.get("icon") || "");
    const lines = [String(room.lastId)];
    for (const message of room.messages) {
      if (message.id > since) {
        lines.push(message.payload);
      }
    }
    write(res, 200, `${lines.join("\n")}\n`);
    return;
  }

  if (req.method === "GET" && path === "/presence") {
    const room = getRoom(url.searchParams.get("room"));
    prune(room, Date.now());
    const players = [...room.presence.values()].sort((a, b) => b.lastSeen - a.lastSeen).slice(0, 80);
    const lines = [String(players.length)];
    for (const player of players) {
      lines.push([
        player.accountId,
        sanitizeField(player.name, "Player", 24),
        sanitizeField(player.icon, "", 64),
        player.joinedAt,
        player.lastSeen,
        player.messageCount
      ].join("\t"));
    }
    write(res, 200, `${lines.join("\n")}\n`);
    return;
  }

  if (req.method === "GET" && path === "/activity") {
    const room = getRoom(url.searchParams.get("room"));
    const rows = room.activity.slice(-60).reverse();
    const lines = [String(rows.length)];
    for (const row of rows) {
      lines.push(`${row.createdAt}\t${sanitizeField(row.text, "activity", 96)}`);
    }
    write(res, 200, `${lines.join("\n")}\n`);
    return;
  }

  if (req.method === "POST" && path === "/send") {
    let payload = "";
    try {
      payload = await readBody(req);
    } catch {
      write(res, 413, "payload too large\n");
      return;
    }

    if (!validPayload(payload)) {
      write(res, 400, "invalid payload\n");
      return;
    }

    const parsed = parsePayload(payload);
    if (!parsed) {
      write(res, 400, "invalid payload\n");
      return;
    }

    const room = getRoom(url.searchParams.get("room"));
    const name = sanitizeField(parsed.name, "Player", 24);
    const accountId = Number.isFinite(parsed.aid) ? Math.trunc(parsed.aid) : 0;
    heartbeat(room, name, accountId, sanitizeField(parsed.icon, "", 64));
    room.lastId += 1;
    room.messages.push({ id: room.lastId, payload });
    const key = presenceKey(accountId, name);
    const entry = room.presence.get(key);
    if (entry) {
      entry.messageCount += 1;
      entry.lastSeen = Date.now();
    }
    const kind = typeof parsed.kind === "string" ? parsed.kind : "user";
    const activityText = kind === "report" ?
      `${name} sent a report` :
      kind === "mod" ? `${name} used moderation` : `${name} sent a message`;
    addActivity(room, Date.now(), activityText);
    if (room.messages.length > maxMessagesPerRoom) {
      room.messages.splice(0, room.messages.length - maxMessagesPerRoom);
    }
    write(res, 200, `${room.lastId}\n`);
    return;
  }

  write(res, 404, "not found\n");
});

server.listen(port, "0.0.0.0", () => {
  console.log(`ComsPlus server listening on 0.0.0.0:${port}`);
});
