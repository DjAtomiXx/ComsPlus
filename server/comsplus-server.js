"use strict";

const http = require("node:http");

const port = Number.parseInt(process.env.PORT || "8787", 10);
const maxBodyBytes = 4096;
const maxMessagesPerRoom = 250;
const rooms = new Map();

function roomName(value) {
  const clean = String(value || "main").replace(/[^a-zA-Z0-9_-]/g, "").slice(0, 32);
  return clean || "main";
}

function getRoom(name) {
  const key = roomName(name);
  let room = rooms.get(key);
  if (!room) {
    room = { lastId: 0, messages: [] };
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
    const lines = [String(room.lastId)];
    for (const message of room.messages) {
      if (message.id > since) {
        lines.push(message.payload);
      }
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

    const room = getRoom(url.searchParams.get("room"));
    room.lastId += 1;
    room.messages.push({ id: room.lastId, payload });
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
