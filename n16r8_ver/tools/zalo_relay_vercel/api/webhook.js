/**
 * MotoMap Zalo Webhook Relay (Vercel Serverless Function)
 *
 * Problem Solved:
 * Zalo Bot Platform servers send webhooks with `User-Agent: Java/<version>`.
 * Cloudflare's multi-tenant *.workers.dev edge blocks Java user agents with HTTP 403.
 *
 * Solution:
 * Vercel Serverless Functions natively accept Java clients without any 403 WAF blocks.
 * This relay receives Zalo's webhook, adds a browser User-Agent, and forwards it to
 * the Cloudflare Worker (https://motomap-zalo-gateway.nguyenxphuz.workers.dev/webhook).
 *
 * Result:
 * Friends simply type "DK <phone>" on Zalo -> Zalo hits Vercel -> Vercel hits Cloudflare -> Cloudflare saves to KV and replies "Registered successfully!"
 */

const CLOUDFLARE_WORKER_URL = "https://motomap-zalo-gateway.nguyenxphuz.workers.dev/webhook";

export default async function handler(req, res) {
  // Support CORS
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type, X-Bot-Api-Secret-Token");

  if (req.method === "OPTIONS") {
    return res.status(200).end();
  }

  // Health check endpoint
  if (req.method === "GET") {
    return res.status(200).json({
      status: "online",
      service: "MotoMap Zalo Webhook Relay on Vercel",
      target: CLOUDFLARE_WORKER_URL,
      timestamp: new Date().toISOString()
    });
  }

  if (req.method !== "POST") {
    return res.status(405).json({ error: "Method not allowed. Use POST." });
  }

  try {
    const payload = req.body || {};

    // Forward to Cloudflare Worker with a standard Desktop Browser User-Agent
    const forwardResponse = await fetch(CLOUDFLARE_WORKER_URL, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"
      },
      body: JSON.stringify(payload)
    });

    const forwardData = await forwardResponse.json().catch(() => ({ ok: true }));

    // Respond back to Zalo with HTTP 200 OK
    return res.status(200).json(forwardData);
  } catch (error) {
    console.error("[RELAY ERROR]", error);
    // Always return HTTP 200 to Zalo so it doesn't enter retry spam loop
    return res.status(200).json({ ok: false, error: error.message });
  }
}
