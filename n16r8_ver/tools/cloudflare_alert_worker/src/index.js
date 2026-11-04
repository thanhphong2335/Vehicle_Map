/**
 * MotoMap Emergency Alert Gateway - Cloudflare Worker
 *
 * Core Features:
 * 1. Hybrid Auto-Resolver: Transparently resolves Vietnamese Phone Numbers to Zalo Chat IDs via Cloudflare KV (PHONE_KV),
 *    while preserving direct Chat IDs and Group IDs (e.g. zgr-...).
 * 2. Zalo Webhook Integration: Automatically handles user registration via syntax "DK <phone>", shared phone contact, or raw 10-digit phone.
 * 3. Diagnostic & Auto-Setup Endpoints:
 *    - GET /api/status: Check bot info, webhook info, KV database status, and debug logs.
 *    - GET /api/set-webhook: Automatically registers https://<worker-domain>/webhook with Zalo Bot Platform.
 * 4. CAD / eCall message format: Professional, clean, and zero emojis/junk icons.
 * 5. Zero external dependencies: Uses Cloudflare Native V8 Runtime and Fetch API.
 */

const ZALO_API_URL = "https://bot-api.zaloplatforms.com";

// Normalize Vietnamese phone numbers into standard 10-digit format (e.g., "0901234567")
function normalizePhoneNumber(rawPhone) {
  if (!rawPhone || typeof rawPhone !== "string") return null;
  let digits = rawPhone.replace(/[^\d+]/g, "").trim();
  if (digits.startsWith("+84")) {
    digits = "0" + digits.slice(3);
  } else if (digits.startsWith("84") && digits.length === 11) {
    digits = "0" + digits.slice(2);
  }
  // Standard Vietnam mobile format: 10 digits starting with 0[3|5|7|8|9]
  if (/^0[35789]\d{8}$/.test(digits)) {
    return digits;
  }
  return null;
}

function isPhoneNumber(str) {
  return normalizePhoneNumber(str) !== null;
}

function formatTimestamp(isoString) {
  try {
    const d = isoString ? new Date(isoString) : new Date();
    const options = {
      timeZone: "Asia/Ho_Chi_Minh",
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
      hour12: false
    };
    return new Intl.DateTimeFormat("vi-VN", options).format(d);
  } catch {
    return isoString || "N/A";
  }
}

function formatEmergencyMessage(data) {
  const timeStr = formatTimestamp(data.timestamp);
  const lat = Number(data.latitude);
  const lon = Number(data.longitude);
  const latStr = !isNaN(lat) ? lat.toFixed(6) : "Không xác định";
  const lonStr = !isNaN(lon) ? lon.toFixed(6) : "Không xác định";
  const speedStr = data.speedKmh !== undefined && !isNaN(Number(data.speedKmh))
    ? `${Number(data.speedKmh).toFixed(1)} km/h`
    : "Không xác định";
  const course = Number(data.courseDeg);
  const headingStr = !isNaN(course) ? `${course.toFixed(0)}°` : "Không xác định";
  const mapUrl = !isNaN(lat) && !isNaN(lon)
    ? `https://maps.google.com/?q=${latStr},${lonStr}`
    : "Chưa có vị trí GPS";

  let eventVi = data.event || "Phát hiện sự cố / Té ngã";
  if (eventVi.includes("Fall Detected")) {
    eventVi = eventVi.replace("Fall Detected", "Phát hiện xe bị ngã");
  } else if (eventVi.includes("System Alert Test")) {
    eventVi = "Kiểm tra hệ thống cảnh báo (Thử nghiệm)";
  }

  let statusVi = data.status || "Tự động kích hoạt";
  if (statusVi.includes("Auto-Triggered")) {
    statusVi = "Tự động kích hoạt bởi cảm biến IMU";
  } else if (statusVi.includes("Manually triggered")) {
    statusVi = "Kích hoạt thủ công từ giao diện Web";
  }

  return [
    "[CẢNH BÁO KHẨN CẤP - MOTOMAP]",
    `Sự cố: ${eventVi}`,
    `Thời gian: ${timeStr}`,
    `Tọa độ: ${latStr}, ${lonStr}`,
    `Bản đồ Google Maps: ${mapUrl}`,
    `Vận tốc: ${speedStr}`,
    `Hướng di chuyển: ${headingStr}`,
    `Thiết bị: ${data.deviceId || "MotoMap-S3"}`,
    `Trạng thái: ${statusVi}`
  ].join("\n");
}

async function sendZaloMessage(botToken, chatId, text) {
  try {
    const response = await fetch(`${ZALO_API_URL}/bot${botToken}/sendMessage`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        chat_id: String(chatId),
        text: text
      })
    });
    const data = await response.json().catch(() => ({}));
    return {
      ok: response.ok && (data.error === 0 || data.ok === true),
      status: response.status,
      response: data
    };
  } catch (err) {
    return {
      ok: false,
      status: 500,
      error: err.message
    };
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // Support CORS preflight
    if (request.method === "OPTIONS") {
      return new Response(null, {
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type, Authorization"
        }
      });
    }

    // =========================================================================
    // ROUTE: AUTO SET WEBHOOK (GET /api/set-webhook or GET /set-webhook)
    // Automatically registers the current Worker URL as the Zalo Bot webhook!
    // =========================================================================
    // =========================================================================
    // ROUTE: POLL UPDATES (GET /api/poll-updates)
    // Deletes webhook and polls Zalo directly via getUpdates (bypassing WAF completely!)
    // =========================================================================
    if (url.pathname === "/api/poll-updates") {
      const botToken = env.ZALO_BOT_TOKEN;
      try {
        // 1. Delete webhook if active
        const delRes = await fetch(`${ZALO_API_URL}/bot${botToken}/deleteWebhook`, { method: "POST" });
        const delData = await delRes.json().catch(() => ({}));
        // 2. Call getUpdates
        const updRes = await fetch(`${ZALO_API_URL}/bot${botToken}/getUpdates`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ timeout: 0 })
        });
        const updData = await updRes.json().catch(() => ({}));
        return new Response(JSON.stringify({
          deleteWebhook: delData,
          getUpdates: updData
        }, null, 2), {
          headers: { "Content-Type": "application/json" }
        });
      } catch (err) {
        return new Response(JSON.stringify({ error: err.message }), { status: 500 });
      }
    }

    if (url.pathname === "/api/test-webhook" || url.pathname === "/test-webhook") {
      const botToken = env.ZALO_BOT_TOKEN;
      try {
        const testRes = await fetch(`${ZALO_API_URL}/bot${botToken}/testWebhook`, {
          method: "POST"
        });
        const testData = await testRes.json().catch(() => ({}));
        return new Response(JSON.stringify(testData, null, 2), {
          headers: { "Content-Type": "application/json" }
        });
      } catch (err) {
        return new Response(JSON.stringify({ error: err.message }), { status: 500 });
      }
    }

    if (url.pathname === "/api/set-webhook" || url.pathname === "/set-webhook") {
      const botToken = env.ZALO_BOT_TOKEN;
      if (!botToken) {
        return new Response(JSON.stringify({ error: "ZALO_BOT_TOKEN secret not configured" }), {
          status: 500,
          headers: { "Content-Type": "application/json" }
        });
      }
      const targetWebhookUrl = url.searchParams.get("url") || `${url.origin}/webhook`;
      const secretToken = env.GATEWAY_AUTH_KEY || "MotoMapSecureKey2026";
      try {
        const setRes = await fetch(`${ZALO_API_URL}/bot${botToken}/setWebhook`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            url: targetWebhookUrl,
            secret_token: secretToken
          })
        });
        const setResult = await setRes.json().catch(() => ({}));
        return new Response(JSON.stringify({
          action: "setWebhook",
          webhookUrl: targetWebhookUrl,
          zaloResponse: setResult,
          timestamp: new Date().toISOString()
        }, null, 2), {
          status: 200,
          headers: { "Content-Type": "application/json" }
        });
      } catch (err) {
        return new Response(JSON.stringify({ error: err.message }), { status: 500 });
      }
    }

    // =========================================================================
    // ROUTE: MANUAL LINK / REGISTER (GET /register or GET /link)
    // Allows user or friend to easily pair phone number <-> Chat ID via web browser
    // =========================================================================
    if (url.pathname === "/register" || url.pathname === "/link") {
      const phone = url.searchParams.get("phone");
      const chatId = url.searchParams.get("chatId");
      if (phone && chatId) {
        const normalizedPhone = normalizePhoneNumber(phone);
        if (!normalizedPhone) {
          return new Response("Số điện thoại không hợp lệ (cần 10 chữ số).", { status: 400 });
        }
        if (env.PHONE_KV) {
          await env.PHONE_KV.put(normalizedPhone, String(chatId).trim());
          await env.PHONE_KV.put(`chat:${String(chatId).trim()}`, normalizedPhone);
        }
        return new Response(`[MOTOMAP] Liên kết thành công: SĐT ${normalizedPhone} <-> Chat ID ${chatId}! Bạn có thể dùng SĐT này trên MotoMap Navigator.`, {
          headers: { "Content-Type": "text/plain; charset=utf-8" }
        });
      }
      return new Response(`<!DOCTYPE html><html><head><meta charset="utf-8"><title>MotoMap - Liên kết Zalo SOS</title><meta name="viewport" content="width=device-width, initial-scale=1"><style>body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;max-width:450px;margin:30px auto;padding:20px;background:#f5f7fb;color:#333}h2{color:#0068ff}label{font-weight:600;display:block;margin-top:12px}input{width:100%;box-sizing:border-box;padding:10px;margin-top:6px;border:1px solid #ccc;border-radius:6px;font-size:16px}button{width:100%;margin-top:20px;padding:12px;background:#0068ff;color:#fff;border:none;border-radius:6px;font-size:16px;font-weight:700;cursor:pointer}.card{background:#fff;padding:24px;border-radius:10px;box-shadow:0 2px 8px rgba(0,0,0,0.08)}</style></head><body><div class="card"><h2>Liên kết Zalo Bot</h2><p style="color:#666;font-size:14px">Nhập Số điện thoại và Chat ID để MotoMap Navigator gửi thông báo cứu hộ tới tài khoản Zalo này.</p><form method="GET"><label>Số điện thoại người nhận:<input name="phone" placeholder="Ví dụ: 0987654321" required></label><label>Zalo Chat ID:<input name="chatId" placeholder="Ví dụ: 3de1b93d92687b362279 hoặc zgr-..." required></label><button type="submit">Lưu liên kết</button></form></div></body></html>`, {
        headers: { "Content-Type": "text/html; charset=utf-8" }
      });
    }

    // =========================================================================
    // ROUTE: STATUS & DIAGNOSTICS (GET / or GET /api/status)
    // =========================================================================
    if (request.method === "GET") {
      const botToken = env.ZALO_BOT_TOKEN;
      let botInfo = null;
      let webhookInfo = null;
      let lastWebhookDebug = null;
      let lastSendDebug = null;

      if (botToken) {
        try {
          const [meRes, whRes] = await Promise.all([
            fetch(`${ZALO_API_URL}/bot${botToken}/getMe`).then(r => r.json()).catch(() => null),
            fetch(`${ZALO_API_URL}/bot${botToken}/getWebhookInfo`).then(r => r.json()).catch(() => null)
          ]);
          botInfo = meRes;
          webhookInfo = whRes;
        } catch {}
      }

      if (env.PHONE_KV) {
        try {
          lastWebhookDebug = await env.PHONE_KV.get("DEBUG_LAST_WEBHOOK", "json");
          lastSendDebug = await env.PHONE_KV.get("DEBUG_LAST_SEND", "json");
        } catch {}
      }

      return new Response(JSON.stringify({
        status: "online",
        service: "MotoMap Zalo Emergency Gateway",
        kv_storage: env.PHONE_KV ? "connected" : "not_bound",
        bot_configured: Boolean(botToken),
        bot_info: botInfo,
        webhook_info: webhookInfo,
        default_chat_id: env.DEFAULT_ZALO_CHAT_IDS || env.ZALO_DEFAULT_CHAT_ID || null,
        debug_last_webhook: lastWebhookDebug,
        debug_last_send: lastSendDebug,
        timestamp: new Date().toISOString()
      }, null, 2), {
        headers: { "Content-Type": "application/json" }
      });
    }

    if (request.method !== "POST") {
      return new Response(JSON.stringify({ error: "Method not allowed. Use POST." }), {
        status: 405,
        headers: { "Content-Type": "application/json" }
      });
    }

    let payload;
    try {
      payload = await request.json();
    } catch {
      return new Response(JSON.stringify({ error: "Invalid JSON payload" }), {
        status: 400,
        headers: { "Content-Type": "application/json" }
      });
    }

    // =========================================================================
    // ROUTE 1: ZALO WEBHOOK HANDLER (User registration & bot interaction)
    // Matches if requested at /webhook or if payload has Zalo event fields
    // =========================================================================
    const isZaloWebhook = url.pathname.includes("webhook") ||
                          payload.event_name !== undefined ||
                          payload.chat !== undefined ||
                          payload.result !== undefined ||
                          payload.sender !== undefined;

    if (isZaloWebhook) {
      // Save debug snapshot to KV
      if (env.PHONE_KV) {
        await env.PHONE_KV.put("DEBUG_LAST_WEBHOOK", JSON.stringify({
          receivedAt: new Date().toISOString(),
          pathname: url.pathname,
          payload
        })).catch(() => {});
      }

      // Unwrap possible nested structures (payload or payload.result or payload.data)
      const root = payload.result || payload.data || payload;

      const chatId = root.chat?.id ||
                     root.message?.chat?.id ||
                     root.message?.from?.id ||
                     root.chat_id ||
                     root.sender?.id ||
                     root.from?.id ||
                     root.user_id_by_app ||
                     root.fromUser?.id ||
                     payload.chat?.id ||
                     payload.message?.chat?.id ||
                     payload.chat_id ||
                     payload.sender?.id ||
                     payload.from?.id;

      const text = (
        root.message?.text ||
        root.text ||
        payload.message?.text ||
        payload.text ||
        ""
      ).trim();

      if (!chatId) {
        return new Response(JSON.stringify({ ok: true, notice: "No chat ID found in webhook payload", payload }), {
          status: 200,
          headers: { "Content-Type": "application/json" }
        });
      }

      const botToken = env.ZALO_BOT_TOKEN;
      if (!botToken) {
        return new Response(JSON.stringify({ error: "ZALO_BOT_TOKEN secret not configured" }), { status: 500 });
      }

      // Look for phone number in:
      // 1. Direct message text: "DK 0901355901", "0901355901", "090 135 5901", etc.
      // 2. Shared contact object: root.message.phone_number or attachments
      let candidatePhone = null;

      if (root.message?.phone_number) {
        candidatePhone = root.message.phone_number;
      } else if (root.message?.contact?.phone_number) {
        candidatePhone = root.message.contact.phone_number;
      } else if (Array.isArray(root.message?.attachments)) {
        for (const att of root.message.attachments) {
          if (att.payload?.phone_number) {
            candidatePhone = att.payload.phone_number;
            break;
          }
        }
      }

      if (!candidatePhone && text) {
        const phoneMatch = text.match(/(?:(?:DK|DANGKY|LIENKET|PHONE)\s*)?([+0-9.\-\s]{9,15})/i);
        if (phoneMatch) {
          candidatePhone = phoneMatch[1];
        } else {
          candidatePhone = text;
        }
      }

      const normalizedPhone = normalizePhoneNumber(candidatePhone);

      if (normalizedPhone) {
        // Save phone mapping into Cloudflare KV
        if (env.PHONE_KV) {
          // Key: normalized phone (e.g. "0901355901") -> Value: chatId
          await env.PHONE_KV.put(normalizedPhone, String(chatId));
          // Reverse lookup: "chat:<chatId>" -> normalized phone
          await env.PHONE_KV.put(`chat:${chatId}`, normalizedPhone);
        }

        const replyMsg = [
          "[MOTOMAP - ĐĂNG KÝ THÀNH CÔNG]",
          `Số điện thoại: ${normalizedPhone}`,
          `Chat ID: ${chatId}`,
          "Trạng thái: Đã liên kết thành công với thiết bị dẫn đường MotoMap.",
          "Tài khoản Zalo này sẽ tự động nhận được tin nhắn định vị cứu hộ khi xe máy xảy ra sự cố hoặc té ngã."
        ].join("\n");

        const sendResult = await sendZaloMessage(botToken, chatId, replyMsg);

        if (env.PHONE_KV) {
          await env.PHONE_KV.put("DEBUG_LAST_SEND", JSON.stringify({
            sentAt: new Date().toISOString(),
            chatId,
            sendResult
          })).catch(() => {});
        }

        return new Response(JSON.stringify({
          ok: true,
          action: "registered",
          phone: normalizedPhone,
          chatId,
          sendResult
        }), {
          status: 200,
          headers: { "Content-Type": "application/json" }
        });
      }

      // Handle unregister command "HUY"
      if (/^(HUY|XOA|UNREGISTER)$/i.test(text)) {
        if (env.PHONE_KV) {
          const linkedPhone = await env.PHONE_KV.get(`chat:${chatId}`);
          if (linkedPhone) {
            await env.PHONE_KV.delete(linkedPhone);
            await env.PHONE_KV.delete(`chat:${chatId}`);
            await sendZaloMessage(botToken, chatId, `[MOTOMAP] Đã hủy đăng ký thành công số điện thoại: ${linkedPhone}.`);
            return new Response(JSON.stringify({ ok: true, action: "unregistered" }), { status: 200 });
          }
        }
        await sendZaloMessage(botToken, chatId, "[MOTOMAP] Tài khoản của bạn chưa đăng ký số điện thoại nào.");
        return new Response(JSON.stringify({ ok: true }), { status: 200 });
      }

      // Default Help / Welcome response
      let existingPhone = null;
      if (env.PHONE_KV) {
        existingPhone = await env.PHONE_KV.get(`chat:${chatId}`);
      }

      const helpMsg = [
        "[MOTOMAP SOS BOT - TRỢ LÝ CỨU HỘ]",
        "Xin chào! Đây là hệ thống tiếp nhận cảnh báo khẩn cấp tự động từ thiết bị MotoMap Navigator.",
        existingPhone
          ? `Số điện thoại đã liên kết: ${existingPhone}`
          : "Chưa có số điện thoại nào được liên kết.",
        "",
        "Hướng dẫn sử dụng:",
        "- Để liên kết nhận cảnh báo: Nhắn tin cú pháp: DK <Số_điện_thoại>",
        "  (Ví dụ: DK 0901355901)",
        "- Để hủy liên kết: Nhắn tin: HUY",
        "- Lưu ý: Chỉ cần đăng ký 1 lần duy nhất, hệ thống sẽ tự động lưu lại vĩnh viễn."
      ].join("\n");

      const helpResult = await sendZaloMessage(botToken, chatId, helpMsg);
      if (env.PHONE_KV) {
        await env.PHONE_KV.put("DEBUG_LAST_SEND", JSON.stringify({
          sentAt: new Date().toISOString(),
          chatId,
          helpResult
        })).catch(() => {});
      }

      return new Response(JSON.stringify({ ok: true, action: "help_sent", chatId, helpResult }), {
        status: 200,
        headers: { "Content-Type": "application/json" }
      });
    }

    // =========================================================================
    // ROUTE 2: ESP32 EMERGENCY ALERT DISPATCHER
    // =========================================================================

    // 1. Verify ESP32 request bearer token
    const authHeader = request.headers.get("Authorization");
    const expectedKey = env.GATEWAY_AUTH_KEY;
    if (expectedKey && (!authHeader || authHeader !== `Bearer ${expectedKey}`)) {
      return new Response(JSON.stringify({ error: "Unauthorized access" }), {
        status: 401,
        headers: { "Content-Type": "application/json" }
      });
    }

    // 2. Resolve raw target list from payload
    let rawTargets = [];
    if (Array.isArray(payload.chatIds) && payload.chatIds.length > 0) {
      rawTargets = payload.chatIds.map(id => String(id).trim()).filter(Boolean);
    } else if (payload.chatId) {
      rawTargets = [String(payload.chatId).trim()];
    } else if (env.DEFAULT_ZALO_CHAT_IDS) {
      rawTargets = env.DEFAULT_ZALO_CHAT_IDS.split(",").map(id => id.trim()).filter(Boolean);
    } else if (env.ZALO_DEFAULT_CHAT_ID) {
      rawTargets = env.ZALO_DEFAULT_CHAT_ID.split(",").map(id => id.trim()).filter(Boolean);
    }

    if (rawTargets.length === 0) {
      return new Response(JSON.stringify({ error: "No recipient phone numbers or Chat IDs provided" }), {
        status: 400,
        headers: { "Content-Type": "application/json" }
      });
    }

    const botToken = env.ZALO_BOT_TOKEN;
    if (!botToken) {
      return new Response(JSON.stringify({ error: "ZALO_BOT_TOKEN secret is not configured" }), {
        status: 500,
        headers: { "Content-Type": "application/json" }
      });
    }

    // 3. Hybrid Resolver: Auto-resolve Phone Numbers to Chat IDs via Cloudflare KV
    const targetChatIds = new Set();
    const unresolvedPhones = [];

    for (const target of rawTargets) {
      const normalizedPhone = normalizePhoneNumber(target);
      if (normalizedPhone) {
        let mappedChatId = null;
        if (env.PHONE_KV) {
          mappedChatId = await env.PHONE_KV.get(normalizedPhone);
        }
        if (mappedChatId) {
          targetChatIds.add(mappedChatId.trim());
        } else {
          unresolvedPhones.push(normalizedPhone);
        }
      } else {
        // Direct Chat ID or Group ID (e.g. "zgr-...", hex IDs)
        targetChatIds.add(target);
      }
    }

    // If all phone numbers were unmapped and no direct chat IDs were found, fallback
    if (targetChatIds.size === 0) {
      const defaultFallback = env.DEFAULT_ZALO_CHAT_IDS || env.ZALO_DEFAULT_CHAT_ID;
      if (defaultFallback) {
        defaultFallback.split(",").map(id => id.trim()).filter(Boolean).forEach(id => targetChatIds.add(id));
      } else {
        return new Response(JSON.stringify({
          error: "Unregistered phone numbers",
          unresolvedPhones,
          message: `Cac so dien thoai chua dang ky Zalo Bot: ${unresolvedPhones.join(", ")}. Vui long mo Bot va nhan 'DK <SDT>' de kich hoat.`
        }), {
          status: 400,
          headers: { "Content-Type": "application/json" }
        });
      }
    }

    // 4. Format CAD / eCall emergency message
    const messageText = formatEmergencyMessage(payload);

    // 5. Broadcast message to all target Chat IDs
    const results = [];
    for (const chatId of targetChatIds) {
      try {
        const sendResult = await sendZaloMessage(botToken, chatId, messageText);
        results.push({
          chatId,
          ok: sendResult.ok,
          status: sendResult.status,
          response: sendResult.response
        });
      } catch (err) {
        results.push({
          chatId,
          ok: false,
          error: err.message
        });
      }
    }

    const deliveredCount = results.filter(r => r.ok).length;
    const allSuccessful = deliveredCount > 0 && deliveredCount === targetChatIds.size;

    return new Response(JSON.stringify({
      ok: deliveredCount > 0,
      deliveredCount,
      totalTargets: targetChatIds.size,
      unresolvedPhones,
      details: results
    }), {
      status: allSuccessful ? 200 : (deliveredCount > 0 ? 207 : 502),
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*"
      }
    });
  }
};
