(() => {
  "use strict";

  const NOMINATIM_URL = "https://nominatim.openstreetmap.org/search";
  const SEARCH_DELAY_MS = 1100;
  const ANIMATION_FPS = 12;
  const ANIMATION_SECONDS = 5;
  const ANIMATION_FRAMES = ANIMATION_FPS * ANIMATION_SECONDS;
  const DEFAULT_DISPLAY_WIDTH = 320;
  const DEFAULT_DISPLAY_HEIGHT = 172;
  // 23 KB/frame keeps all 60 frames below the firmware's 1.45 MB transaction limit.
  const MAX_FRAME_BYTES = 23 * 1024;
  // A still image is sent only once, so it can retain more JPEG detail than a video frame.
  // The crop editor needs a re-encode, therefore reserve enough headroom to avoid
  // visibly washing out detailed photos while still fitting safely in ESP32 RAM.
  const MAX_TEST_IMAGE_BYTES = 128 * 1024;
  const DEFAULT_TFT_TONE = 55;

  const $ = (id) => document.getElementById(id);
  const els = {
    deviceStatus: $("device-status"), connectionDetail: $("connection-detail"), stationWebLink: $("station-web-link"), refreshStatus: $("refresh-status"),
    wifiForm: $("wifi-form"), wifiSsid: $("wifi-ssid"), wifiPassword: $("wifi-password"), saveWifi: $("save-wifi"), wifiMessage: $("wifi-message"),
    searchForm: $("search-form"), addressInput: $("address-input"), coordinateForm: $("coordinate-form"),
    coordinateInput: $("coordinate-input"), vehicleSelect: $("vehicle-select"), searchMessage: $("search-message"),
    searchResults: $("search-results"), locationQuality: $("location-quality"), destinationDetail: $("destination-detail"), mapStatus: $("map-status"),
    centerGps: $("center-gps"), clearDestination: $("clear-destination"), gpsStatus: $("gps-status"),
    routeState: $("route-state"), instructionDistance: $("instruction-distance"), instructionRoad: $("instruction-road"),
    routeDetail: $("route-detail"), startNavigation: $("start-navigation"), stopNavigation: $("stop-navigation"),
    rerouteNavigation: $("reroute-navigation"), navigationMessage: $("navigation-message"),
    mediaInput: $("media-input"), videoPreview: $("video-preview"), imagePreview: $("image-preview"),
    cropEditor: $("crop-editor"), cropStage: $("crop-stage"), cropImage: $("crop-image"),
    cropZoom: $("crop-zoom"), tftTone: $("tft-tone"), tftToneInfo: $("tft-tone-info"),
    rotateCrop: $("rotate-crop"), resetCrop: $("reset-crop"), cropInfo: $("crop-info"),
    mediaCanvas: $("media-canvas"), mediaTitle: $("media-title"), showTestImage: $("show-test-image"), clearTestImage: $("clear-test-image"),
    startMedia: $("start-media"), stopMedia: $("stop-media"),
    saveAnimation: $("save-animation"), deleteAnimation: $("delete-animation"),
    animationProgress: $("animation-progress"), animationStatus: $("animation-status"), mediaMessage: $("media-message")
  };

  const state = {
    destination: null,
    destinationMarker: null,
    currentMarker: null,
    gps: null,
    routeState: "IDLE",
    configuredSsid: "",
    wifiConfigured: false,
    lastSearchAt: 0,
    mediaFile: null,
    mediaObjectUrl: null,
    mediaTimer: null,
    mediaActive: false,
    frameInFlight: false,
    animationUploading: false,
    testImageActive: false,
    displayWidth: DEFAULT_DISPLAY_WIDTH,
    displayHeight: DEFAULT_DISPLAY_HEIGHT,
    crop: { active: false, zoom: 1, rotation: 0, x: 0, y: 0, tftTone: DEFAULT_TFT_TONE }
  };

  const fallbackMap = {
    setView() { return this; }, getZoom() { return 13; }, on() {}, removeLayer() {}
  };
  let map = fallbackMap;
  let leafletAvailable = false;
  let mapLoadFailed = false;

  function setMessage(element, message, kind = "") {
    element.textContent = message || "";
    element.className = `message${kind ? ` ${kind}` : ""}`;
  }

  function setTag(element, text, kind = "") {
    element.textContent = text;
    element.className = `tag${kind ? ` ${kind}` : ""}`;
  }

  function mapReady() {
    return leafletAvailable && map !== fallbackMap;
  }

  function showMapUnavailable(message) {
    const mapElement = $("map");
    if (mapElement && !mapReady()) mapElement.textContent = message;
  }

  function initialiseOsmMap() {
    if (mapReady() || !window.L) return mapReady();
    const mapElement = $("map");
    if (!mapElement) return false;

    mapElement.replaceChildren();
    leafletAvailable = true;
    map = window.L.map("map", { zoomControl: true }).setView([10.8231, 106.6297], 13);
    window.L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {
      maxZoom: 19,
      attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
    }).addTo(map);
    map.on("click", (event) => {
      setDestination(event.latlng.lat, event.latlng.lng, "Điểm ghim thủ công", true);
      setMessage(els.searchMessage, "Đã đặt điểm đến trên bản đồ.", "success");
    });
    if (state.destination) renderDestinationMarker();
    if (state.gps?.valid) updateGpsMarker(state.gps);
    window.requestAnimationFrame(() => map.invalidateSize());
    setTag(els.mapStatus, "OSM online", "success");
    return true;
  }

  function loadOpenStreetMap() {
    if (initialiseOsmMap() || mapLoadFailed) return;
    showMapUnavailable("Đang tải bản đồ OpenStreetMap…");
    setTag(els.mapStatus, "Đang tải OSM…");

    const style = document.createElement("link");
    style.rel = "stylesheet";
    style.href = "https://unpkg.com/leaflet@1.9.4/dist/leaflet.css";
    document.head.appendChild(style);

    const script = document.createElement("script");
    script.src = "https://unpkg.com/leaflet@1.9.4/dist/leaflet.js";
    script.async = true;
    let finished = false;
    const failed = () => {
      if (finished) return;
      finished = true;
      mapLoadFailed = true;
      showMapUnavailable("Không tải được OSM. Hãy mở web khi điện thoại có Internet rồi làm mới trang.");
      setTag(els.mapStatus, "OSM cần Internet", "warning");
    };
    script.onload = () => {
      if (finished) return;
      if (initialiseOsmMap()) finished = true;
      else failed();
    };
    script.onerror = failed;
    window.setTimeout(failed, 10000);
    document.head.appendChild(script);
  }

  function updateDisplayGeometry(width, height) {
    if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1) return;
    state.displayWidth = width;
    state.displayHeight = height;
    els.mediaCanvas.width = width;
    els.mediaCanvas.height = height;
    if (els.mediaTitle) els.mediaTitle.textContent = `Ảnh / video ${width}×${height}`;
    els.cropStage.style.aspectRatio = `${width} / ${height}`;
    [els.imagePreview, els.videoPreview].forEach((preview) => {
      preview.style.aspectRatio = `${width} / ${height}`;
      preview.style.maxHeight = `${height}px`;
    });
    if (state.crop.active) window.requestAnimationFrame(renderCropEditor);
  }

  function validCoordinate(lat, lon) {
    return Number.isFinite(lat) && Number.isFinite(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
  }

  function parseCoordinates(value) {
    const match = String(value || "").trim().match(/^\s*(-?\d+(?:\.\d+)?)\s*[,;\s]\s*(-?\d+(?:\.\d+)?)\s*$/);
    if (!match) return null;
    const lat = Number(match[1]);
    const lon = Number(match[2]);
    return validCoordinate(lat, lon) ? { lat, lon } : null;
  }

  async function api(path, options = {}) {
    const response = await fetch(path, { cache: "no-store", ...options });
    let body = null;
    try { body = await response.json(); } catch (_) { body = null; }
    if (!response.ok) throw new Error(body?.error || `ESP32 HTTP ${response.status}`);
    return body;
  }

  async function postJson(path, body) {
    return api(path, {
      method: "POST",
      headers: { "Content-Type": "application/json", Accept: "application/json" },
      body: JSON.stringify(body)
    });
  }

  function renderDestinationMarker() {
    if (!mapReady() || !state.destination) return;
    if (state.destinationMarker) map.removeLayer(state.destinationMarker);
    state.destinationMarker = window.L.marker([state.destination.lat, state.destination.lon], { draggable: true }).addTo(map)
      .bindPopup("Điểm đến — kéo để chỉnh").openPopup();
    state.destinationMarker.on("dragend", () => {
      const position = state.destinationMarker.getLatLng();
      state.destination = { lat: position.lat, lon: position.lng, label: "Điểm ghim đã chỉnh" };
      setTag(els.locationQuality, "Đã chỉnh ghim", "success");
      updateDestination();
    });
  }

  function setDestination(lat, lon, label, manuallyConfirmed) {
    if (!validCoordinate(lat, lon)) return;
    state.destination = { lat, lon, label: label || "Điểm đã ghim" };
    renderDestinationMarker();
    map.setView([lat, lon], Math.max(map.getZoom(), 16));
    setTag(els.locationQuality, manuallyConfirmed ? "Đã ghim thủ công" : "Kiểm tra lại ghim",
      manuallyConfirmed ? "success" : "warning");
    updateDestination();
  }

  function updateDestination() {
    if (!state.destination) {
      els.destinationDetail.textContent = "Chưa có điểm đến.";
      els.startNavigation.disabled = true;
      return;
    }
    els.destinationDetail.textContent = `${state.destination.label} · ${state.destination.lat.toFixed(6)}, ${state.destination.lon.toFixed(6)}`;
    els.startNavigation.disabled = false;
  }

  async function searchAddress(address) {
    const wait = SEARCH_DELAY_MS - (Date.now() - state.lastSearchAt);
    if (wait > 0) await new Promise((resolve) => setTimeout(resolve, wait));
    state.lastSearchAt = Date.now();
    const params = new URLSearchParams({ q: address, format: "jsonv2", addressdetails: "1", limit: "5", "accept-language": "vi" });
    const response = await fetch(`${NOMINATIM_URL}?${params}`, { headers: { Accept: "application/json" }, cache: "no-store" });
    if (!response.ok) throw new Error(`Nominatim HTTP ${response.status}`);
    return response.json();
  }

  function renderSearchResults(results) {
    els.searchResults.replaceChildren();
    if (!results.length) {
      setMessage(els.searchMessage, "Không tìm thấy. Hãy thử địa chỉ ngắn hơn hoặc tự ghim bản đồ.", "error");
      return;
    }
    results.forEach((result) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "search-result";
      const title = document.createElement("strong");
      title.textContent = result.name || result.display_name.split(",")[0];
      const detail = document.createElement("span");
      detail.textContent = result.display_name;
      button.append(title, detail);
      button.addEventListener("click", () => {
        const houseNumber = Boolean(result.address?.house_number);
        setDestination(Number(result.lat), Number(result.lon), result.display_name, false);
        setTag(els.locationQuality, houseNumber ? "Có số nhà OSM" : "Vị trí gần đúng", houseNumber ? "success" : "warning");
        setMessage(els.searchMessage, houseNumber
          ? "Đã chọn kết quả. Có thể kéo ghim để kiểm tra."
          : "OSM chưa xác nhận số nhà; hãy kéo ghim tới vị trí chính xác.", houseNumber ? "success" : "");
      });
      els.searchResults.appendChild(button);
    });
  }

  els.searchForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    const address = els.addressInput.value.trim();
    if (!address) return;
    setMessage(els.searchMessage, "Đang tìm trên OpenStreetMap…");
    els.searchResults.replaceChildren();
    try { renderSearchResults(await searchAddress(address)); }
    catch (error) { setMessage(els.searchMessage, `Không tìm được địa chỉ: ${error.message}`, "error"); }
  });

  els.coordinateForm.addEventListener("submit", (event) => {
    event.preventDefault();
    const coordinates = parseCoordinates(els.coordinateInput.value);
    if (!coordinates) {
      setMessage(els.searchMessage, "Tọa độ phải có dạng lat, lon.", "error");
      return;
    }
    setDestination(coordinates.lat, coordinates.lon, "Tọa độ nhập thủ công", true);
  });

  els.clearDestination.addEventListener("click", () => {
    if (state.destinationMarker && mapReady()) map.removeLayer(state.destinationMarker);
    state.destinationMarker = null;
    state.destination = null;
    setTag(els.locationQuality, "Chưa chọn");
    updateDestination();
  });

  els.centerGps.addEventListener("click", () => {
    if (!state.gps?.valid) {
      setMessage(els.navigationMessage, "ESP32 chưa có GPS fix.", "error");
      return;
    }
    if (!mapReady()) {
      setMessage(els.navigationMessage, "OSM chưa tải được; điện thoại cần Internet để xem bản đồ.", "error");
      return;
    }
    map.setView([state.gps.lat, state.gps.lon], 17);
  });

  async function loadConfig() {
    try {
      const config = await api("/api/config");
      state.wifiConfigured = Boolean(config.wifi_configured);
      state.configuredSsid = config.wifi_ssid || "";
      els.wifiSsid.value = state.configuredSsid;
      setMessage(els.wifiMessage, state.wifiConfigured ? "Hotspot đã được lưu trong ESP32." : "Nhập hotspot iPhone lần đầu.", state.wifiConfigured ? "success" : "");
    } catch (error) {
      setMessage(els.wifiMessage, `Không đọc được cấu hình: ${error.message}`, "error");
    }
  }

  async function saveWifiIfNeeded() {
    const ssid = els.wifiSsid.value.trim();
    const password = els.wifiPassword.value;
    const changed = !state.wifiConfigured || ssid !== state.configuredSsid || password.length > 0;
    if (!changed) return;
    if (!ssid) throw new Error("Chưa nhập tên hotspot iPhone");
    if ((!state.wifiConfigured || ssid !== state.configuredSsid) && password.length < 8) {
      throw new Error("Khi đổi hotspot, hãy nhập mật khẩu ít nhất 8 ký tự");
    }
    await postJson("/api/config/wifi", { ssid, password, connect_now: false });
    state.wifiConfigured = true;
    state.configuredSsid = ssid;
    els.wifiPassword.value = "";
  }

  els.wifiForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    const ssid = els.wifiSsid.value.trim();
    const password = els.wifiPassword.value;
    if (!ssid) {
      setMessage(els.wifiMessage, "Hãy nhập tên Wi-Fi hotspot trước.", "error");
      return;
    }
    if (password.length < 8 && !state.wifiConfigured) {
      setMessage(els.wifiMessage, "Mật khẩu hotspot phải có ít nhất 8 ký tự.", "error");
      return;
    }
    els.saveWifi.disabled = true;
    setMessage(els.wifiMessage, "Đã gửi cấu hình. ESP32 sẽ chuyển sang kết nối hotspot sau 5 giây…");
    try {
      await postJson("/api/config/wifi", { ssid, password, connect_now: true });
      state.wifiConfigured = true;
      state.configuredSsid = ssid;
      els.wifiPassword.value = "";
      setMessage(els.wifiMessage, "ESP32 đã lưu Wi-Fi và đang kết nối. AP MotoMap sẽ tắt; hãy xem IP mới trên Serial Monitor.", "success");
    } catch (error) {
      setMessage(els.wifiMessage, `Không lưu được Wi-Fi: ${error.message}`, "error");
      els.saveWifi.disabled = false;
    }
  });

  els.startNavigation.addEventListener("click", async () => {
    if (!state.destination) return;
    els.startNavigation.disabled = true;
    setMessage(els.navigationMessage, "Đang lưu hotspot và điểm đến…");
    try {
      await saveWifiIfNeeded();
      const result = await postJson("/api/destination", {
        lat: state.destination.lat,
        lon: state.destination.lon,
        label: state.destination.label,
        vehicle: els.vehicleSelect.value,
        start: true
      });
      const switching = Number(result?.switching_to_hotspot_in_ms || 0) > 0;
      setMessage(els.navigationMessage, switching
        ? "Đã lưu. ESP32 sẽ chuyển sang hotspot sau 5 giây. Nếu dùng GPS điện thoại, hãy giữ Safari mở."
        : "Đã bắt đầu. Nếu dùng GPS điện thoại, hãy giữ Safari mở và cấp quyền Vị trí.", "success");
    } catch (error) {
      setMessage(els.navigationMessage, error.message, "error");
      els.startNavigation.disabled = false;
    }
  });

  els.stopNavigation.addEventListener("click", async () => {
    try {
      await postJson("/api/navigation/stop", {});
      setMessage(els.navigationMessage, "Đã dừng navigation.", "success");
    } catch (error) { setMessage(els.navigationMessage, error.message, "error"); }
  });

  els.rerouteNavigation.addEventListener("click", async () => {
    try {
      await postJson("/api/navigation/reroute", {});
      setMessage(els.navigationMessage, "ESP32 đang tính lại route.", "success");
    } catch (error) { setMessage(els.navigationMessage, error.message, "error"); }
  });

  function formatDistance(meters) {
    const value = Number(meters || 0);
    return value >= 1000 ? `${(value / 1000).toFixed(1)} km` : `${Math.round(value)} m`;
  }

  function updateGpsMarker(gps) {
    if (!gps?.valid) return;
    if (!state.currentMarker) {
      if (mapReady()) {
        state.currentMarker = window.L.circleMarker([gps.lat, gps.lon], { radius: 7, color: "#2d9cff", fillColor: "#70b7ff", fillOpacity: 0.9, weight: 2 })
          .addTo(map).bindTooltip("GPS của ESP32");
      }
    } else state.currentMarker.setLatLng([gps.lat, gps.lon]);
  }

  async function refreshStatus() {
    try {
      const status = await api("/api/status");
      updateDisplayGeometry(Number(status.display_width), Number(status.display_height));
      state.routeState = status.route_state || "IDLE";
      state.gps = status.gps || null;
      els.deviceStatus.textContent = status.internet_connected ? "HOTSPOT ONLINE" : status.network_state;
      els.deviceStatus.className = `status-pill ${status.internet_connected ? "status-online" : ""}`;
      els.connectionDetail.textContent = `${status.web_url || location.href} · ${status.network_state} · heap ${Math.round(Number(status.heap_free || 0) / 1024)}KB`;
      const hotspotUrl = status.internet_connected && String(status.web_url || "").startsWith("http://")
        ? String(status.web_url) : "";
      els.stationWebLink.classList.toggle("hidden", !hotspotUrl);
      if (hotspotUrl) {
        els.stationWebLink.href = hotspotUrl;
        els.stationWebLink.textContent = `Mở web hotspot: ${hotspotUrl}`;
      }

      if (state.gps?.valid) {
        setTag(els.gpsStatus, `GPS NEO-6M · ${state.gps.satellites} vệ tinh · HDOP ${Number(state.gps.hdop).toFixed(1)}`, "success");
        updateGpsMarker(state.gps);
      } else {
        setTag(els.gpsStatus, "Đang chờ GPS NEO-6M", "warning");
      }
      const navigation = status.navigation || {};
      els.routeState.textContent = state.routeState;
      els.instructionDistance.textContent = navigation.distance_to_turn_m ? formatDistance(navigation.distance_to_turn_m) : "—";
      els.instructionRoad.textContent = navigation.road_name || "—";
      els.routeDetail.textContent = navigation.status
        ? `${navigation.status} · còn ${formatDistance(navigation.remaining_distance_m)} · ETA ${Math.round(Number(navigation.eta_seconds || 0) / 60)} phút`
        : "ESP32 sẽ tự tính route sau khi có GPS và hotspot.";
      const animation = status.animation || {};
      state.testImageActive = Boolean(status.test_image_active);
      els.clearTestImage.disabled = !state.testImageActive;
      setTag(els.animationStatus, animation.valid ? `${animation.frames} frame · ${animation.fps} fps` : "Chưa có animation", animation.valid ? "success" : "");
      els.startNavigation.disabled = !state.destination;
    } catch (_) {
      els.deviceStatus.textContent = "MẤT KẾT NỐI";
      els.deviceStatus.className = "status-pill status-offline";
      els.connectionDetail.textContent = "ESP32 có thể đang chuyển từ AP sang hotspot. Xem IP trên màn hình ESP32.";
    }
  }

  function applySharpen(context, amount = 0.40) {
    if (amount <= 0) return;
    const canvas = els.mediaCanvas;
    const w = canvas.width;
    const h = canvas.height;
    const imgData = context.getImageData(0, 0, w, h);
    const src = imgData.data;
    const output = context.createImageData(w, h);
    const dst = output.data;

    const c = 1 + 4 * amount;
    const n = -amount;

    for (let y = 0; y < h; y++) {
      const yPrev = (y > 0 ? y - 1 : 0) * w * 4;
      const yCurr = y * w * 4;
      const yNext = (y < h - 1 ? y + 1 : h - 1) * w * 4;

      for (let x = 0; x < w; x++) {
        const xPrev = (x > 0 ? x - 1 : 0) * 4;
        const xCurr = x * 4;
        const xNext = (x < w - 1 ? x + 1 : w - 1) * 4;

        const idx = yCurr + xCurr;

        for (let channel = 0; channel < 3; channel++) {
          const val = src[idx + channel] * c +
                      (src[yPrev + xCurr + channel] +
                       src[yNext + xCurr + channel] +
                       src[yCurr + xPrev + channel] +
                       src[yCurr + xNext + channel]) * n;
          dst[idx + channel] = val < 0 ? 0 : (val > 255 ? 255 : val);
        }
        dst[idx + 3] = 255;
      }
    }
    context.putImageData(output, 0, 0);
  }

  function drawMediaSource(source, width, height) {
    const canvas = els.mediaCanvas;
    const context = canvas.getContext("2d", { alpha: false });
    canvas.width = state.displayWidth;
    canvas.height = state.displayHeight;
    context.fillStyle = "#000";
    context.fillRect(0, 0, canvas.width, canvas.height);

    const scale = Math.max(canvas.width / width, canvas.height / height);
    const drawWidth = Math.round(width * scale);
    const drawHeight = Math.round(height * scale);
    const drawX = Math.round((canvas.width - drawWidth) / 2);
    const drawY = Math.round((canvas.height - drawHeight) / 2);

    context.imageSmoothingEnabled = true;
    context.imageSmoothingQuality = "high";
    context.drawImage(source, 0, 0, width, height, drawX, drawY, drawWidth, drawHeight);

    applySharpen(context, 0.40);
    applyTftTone(context);
  }

  function cropMetrics() {
    const originalWidth = els.cropImage.naturalWidth;
    const originalHeight = els.cropImage.naturalHeight;
    const stage = els.cropStage.getBoundingClientRect();
    if (!state.crop.active || !originalWidth || !originalHeight || stage.width < 1 || stage.height < 1) return null;
    const sideways = state.crop.rotation % 180 !== 0;
    const sourceWidth = sideways ? originalHeight : originalWidth;
    const sourceHeight = sideways ? originalWidth : originalHeight;
    const baseScale = Math.max(stage.width / sourceWidth, stage.height / sourceHeight);
    const scale = baseScale * state.crop.zoom;
    return {
      stageWidth: stage.width,
      stageHeight: stage.height,
      originalWidth,
      originalHeight,
      sourceWidth,
      sourceHeight,
      imageWidth: sourceWidth * scale,
      imageHeight: sourceHeight * scale
    };
  }

  function constrainCrop(metrics) {
    state.crop.x = Math.min(0, Math.max(metrics.stageWidth - metrics.imageWidth, state.crop.x));
    state.crop.y = Math.min(0, Math.max(metrics.stageHeight - metrics.imageHeight, state.crop.y));
  }

  function renderCropEditor() {
    const metrics = cropMetrics();
    if (!metrics) return;
    constrainCrop(metrics);
    const sideways = state.crop.rotation % 180 !== 0;
    const elementWidth = sideways ? metrics.imageHeight : metrics.imageWidth;
    const elementHeight = sideways ? metrics.imageWidth : metrics.imageHeight;
    els.cropImage.style.left = `${state.crop.x + (metrics.imageWidth - elementWidth) / 2}px`;
    els.cropImage.style.top = `${state.crop.y + (metrics.imageHeight - elementHeight) / 2}px`;
    els.cropImage.style.width = `${elementWidth}px`;
    els.cropImage.style.height = `${elementHeight}px`;
    els.cropImage.style.transform = `rotate(${state.crop.rotation}deg)`;
    const sourceWidth = Math.round(metrics.stageWidth / metrics.imageWidth * metrics.sourceWidth);
    const sourceHeight = Math.round(metrics.stageHeight / metrics.imageHeight * metrics.sourceHeight);
    els.cropInfo.textContent = `${sourceWidth}×${sourceHeight}`;
  }

  function resetCrop() {
    state.crop.zoom = 1;
    els.cropZoom.value = "1";
    const metrics = cropMetrics();
    if (!metrics) return;
    state.crop.x = (metrics.stageWidth - metrics.imageWidth) / 2;
    state.crop.y = (metrics.stageHeight - metrics.imageHeight) / 2;
    renderCropEditor();
  }

  function setCropZoom(zoom) {
    const before = cropMetrics();
    const focusX = before ? (before.stageWidth / 2 - state.crop.x) / before.imageWidth : 0.5;
    const focusY = before ? (before.stageHeight / 2 - state.crop.y) / before.imageHeight : 0.5;
    state.crop.zoom = Math.min(3, Math.max(1, zoom));
    const after = cropMetrics();
    if (after) {
      state.crop.x = after.stageWidth / 2 - focusX * after.imageWidth;
      state.crop.y = after.stageHeight / 2 - focusY * after.imageHeight;
    }
    renderCropEditor();
  }

  function rotateCrop() {
    if (!state.crop.active) return;
    state.crop.rotation = (state.crop.rotation + 90) % 360;
    resetCrop();
  }

  function selectedSourceRect() {
    const metrics = cropMetrics();
    if (!metrics) return null;
    constrainCrop(metrics);
    const x = Math.max(0, -state.crop.x / metrics.imageWidth * metrics.sourceWidth);
    const y = Math.max(0, -state.crop.y / metrics.imageHeight * metrics.sourceHeight);
    const width = Math.min(metrics.sourceWidth - x, metrics.stageWidth / metrics.imageWidth * metrics.sourceWidth);
    const height = Math.min(metrics.sourceHeight - y, metrics.stageHeight / metrics.imageHeight * metrics.sourceHeight);
    return { x, y, width, height };
  }

  function drawCropSelection() {
    const selection = selectedSourceRect();
    if (!selection) {
      drawMediaSource(els.imagePreview, els.imagePreview.naturalWidth, els.imagePreview.naturalHeight);
      return;
    }
    const canvas = els.mediaCanvas;
    const context = canvas.getContext("2d", { alpha: false });
    canvas.width = state.displayWidth;
    canvas.height = state.displayHeight;

    // Normal crop needs only one resize. The previous editor path first resized
    // into a 4x intermediate canvas and then resized again, which softened fine
    // detail and slightly changed colours on some iPhone photos.
    if (state.crop.rotation === 0) {
      context.imageSmoothingEnabled = true;
      context.imageSmoothingQuality = "high";
      context.drawImage(
        els.imagePreview,
        selection.x,
        selection.y,
        selection.width,
        selection.height,
        0,
        0,
        canvas.width,
        canvas.height
      );
      applySharpen(context, 0.40);
      applyTftTone(context);
      return;
    }

    const intermediate = document.createElement("canvas");
    intermediate.width = Math.max(canvas.width, Math.min(Math.ceil(selection.width), canvas.width * 4));
    intermediate.height = Math.max(canvas.height, Math.min(Math.ceil(selection.height), canvas.height * 4));
    const intermediateContext = intermediate.getContext("2d", { alpha: false });
    intermediateContext.imageSmoothingEnabled = true;
    intermediateContext.imageSmoothingQuality = "high";
    const scaleX = intermediate.width / selection.width;
    const scaleY = intermediate.height / selection.height;
    intermediateContext.save();
    intermediateContext.scale(scaleX, scaleY);
    intermediateContext.translate(-selection.x, -selection.y);
    switch (state.crop.rotation) {
      case 90:
        intermediateContext.translate(els.imagePreview.naturalHeight, 0);
        intermediateContext.rotate(Math.PI / 2);
        break;
      case 180:
        intermediateContext.translate(els.imagePreview.naturalWidth, els.imagePreview.naturalHeight);
        intermediateContext.rotate(Math.PI);
        break;
      case 270:
        intermediateContext.translate(0, els.imagePreview.naturalWidth);
        intermediateContext.rotate(-Math.PI / 2);
        break;
      default:
        break;
    }
    intermediateContext.drawImage(els.imagePreview, 0, 0);
    intermediateContext.restore();

    context.imageSmoothingEnabled = true;
    context.imageSmoothingQuality = "high";
    context.drawImage(intermediate, 0, 0, intermediate.width, intermediate.height, 0, 0, canvas.width, canvas.height);
    applySharpen(context, 0.40);
    applyTftTone(context);
  }

  function applyTftTone(context) {
    // Safari converts iPhone HDR/P3 images through its canvas pipeline before
    // JPEG export. Dark zones can become grey on a small TFT, so compensate only
    // after the crop has been rendered at the final 320x240 resolution.
    const amount = state.crop.tftTone / 100;
    if (amount <= 0) return;
    const canvas = els.mediaCanvas;
    const image = context.getImageData(0, 0, canvas.width, canvas.height);
    const pixels = image.data;
    const blackPoint = 18 * amount;
    const inverseRange = 255 / (255 - blackPoint);
    const gamma = 1 + 0.10 * amount;
    const contrast = 1 + 0.25 * amount;
    const saturation = 1 + 0.38 * amount;
    for (let index = 0; index < pixels.length; index += 4) {
      let red = Math.pow(Math.max(0, (pixels[index] - blackPoint) * inverseRange) / 255, gamma) * 255;
      let green = Math.pow(Math.max(0, (pixels[index + 1] - blackPoint) * inverseRange) / 255, gamma) * 255;
      let blue = Math.pow(Math.max(0, (pixels[index + 2] - blackPoint) * inverseRange) / 255, gamma) * 255;
      red = (red - 127.5) * contrast + 127.5;
      green = (green - 127.5) * contrast + 127.5;
      blue = (blue - 127.5) * contrast + 127.5;
      const luminance = red * 0.2126 + green * 0.7152 + blue * 0.0722;
      pixels[index] = Math.max(0, Math.min(255, luminance + (red - luminance) * saturation));
      pixels[index + 1] = Math.max(0, Math.min(255, luminance + (green - luminance) * saturation));
      pixels[index + 2] = Math.max(0, Math.min(255, luminance + (blue - luminance) * saturation));
    }
    context.putImageData(image, 0, 0);
  }

  let cropPointer = null;
  els.cropStage.addEventListener("pointerdown", (event) => {
    if (!state.crop.active) return;
    cropPointer = { id: event.pointerId, x: event.clientX, y: event.clientY, cropX: state.crop.x, cropY: state.crop.y };
    els.cropStage.setPointerCapture(event.pointerId);
    event.preventDefault();
  });
  els.cropStage.addEventListener("pointermove", (event) => {
    if (!cropPointer || cropPointer.id !== event.pointerId) return;
    state.crop.x = cropPointer.cropX + event.clientX - cropPointer.x;
    state.crop.y = cropPointer.cropY + event.clientY - cropPointer.y;
    renderCropEditor();
  });
  const stopCropDrag = (event) => {
    if (cropPointer?.id === event.pointerId) cropPointer = null;
  };
  els.cropStage.addEventListener("pointerup", stopCropDrag);
  els.cropStage.addEventListener("pointercancel", stopCropDrag);
  els.cropZoom.addEventListener("input", () => setCropZoom(Number(els.cropZoom.value)));
  els.tftTone.addEventListener("input", () => {
    state.crop.tftTone = Number(els.tftTone.value);
    els.tftToneInfo.textContent = `${state.crop.tftTone}%`;
  });
  els.rotateCrop.addEventListener("click", rotateCrop);
  els.resetCrop.addEventListener("click", resetCrop);

  function canvasBlob(quality = 0.9) {
    return new Promise((resolve, reject) => {
      els.mediaCanvas.toBlob((blob) => blob ? resolve(blob) : reject(new Error("Không tạo được JPEG")), "image/jpeg", quality);
    });
  }

  async function compressedFrameBlob(maxBytes = MAX_FRAME_BYTES) {
    for (let quality = 0.88; quality >= 0.45; quality -= 0.04) {
      const blob = await canvasBlob(quality);
      if (blob.size <= maxBytes) return blob;
    }
    return canvasBlob(0.40);
  }

  async function compressedTestImageBlob() {
    for (let quality = 0.98; quality >= 0.30; quality -= 0.02) {
      const blob = await canvasBlob(quality);
      if (blob.size <= MAX_TEST_IMAGE_BYTES) return blob;
    }
    throw new Error("Image is too detailed to fit in 128KB");
  }

  async function bestTestImageBlob() {
    if (!state.mediaFile?.type.startsWith("image/")) {
      throw new Error("Choose an image file first");
    }
    if (!els.imagePreview.complete) await els.imagePreview.decode();

    // Keep a correctly prepared JPEG byte-for-byte. Re-encoding it would soften
    // fine details even though it is already exactly the display's native size.
    if (state.mediaFile.type === "image/jpeg" &&
        els.imagePreview.naturalWidth === state.displayWidth &&
        els.imagePreview.naturalHeight === state.displayHeight &&
        state.crop.zoom === 1 &&
        state.crop.rotation === 0 &&
        state.crop.tftTone === DEFAULT_TFT_TONE &&
        state.mediaFile.size <= MAX_TEST_IMAGE_BYTES) {
      return state.mediaFile;
    }

    drawCropSelection();
    return compressedTestImageBlob();
  }

  els.showTestImage.addEventListener("click", async () => {
    if (!state.mediaFile?.type.startsWith("image/") || state.frameInFlight) return;
    state.frameInFlight = true;
    els.showTestImage.disabled = true;
    try {
      const blob = await bestTestImageBlob();
      const form = new FormData();
      form.append("image", blob, "test.jpg");
      await api("/api/media/test-image", { method: "POST", body: form });
      state.testImageActive = true;
      els.clearTestImage.disabled = false;
      setMessage(els.mediaMessage, "Test image is now on the ESP32 display.", "success");
      await refreshStatus();
    } catch (error) {
      setMessage(els.mediaMessage, `Could not show image: ${error.message}`, "error");
    } finally {
      state.frameInFlight = false;
      els.showTestImage.disabled = !state.mediaFile?.type.startsWith("image/");
    }
  });

  els.clearTestImage.addEventListener("click", async () => {
    try {
      await api("/api/media/test-image", { method: "DELETE" });
      state.testImageActive = false;
      els.clearTestImage.disabled = true;
      setMessage(els.mediaMessage, "Test image cleared.", "success");
      await refreshStatus();
    } catch (error) { setMessage(els.mediaMessage, error.message, "error"); }
  });

  async function sendMediaFrame() {
    if (!state.mediaFile || state.frameInFlight || state.animationUploading) return;
    state.frameInFlight = true;
    try {
      if (!els.videoPreview.videoWidth) return;
      drawMediaSource(els.videoPreview, els.videoPreview.videoWidth, els.videoPreview.videoHeight);
      const blob = await canvasBlob(0.9);
      const response = await fetch("/media/frame", { method: "POST", headers: { "Content-Type": "image/jpeg" }, body: blob, cache: "no-store" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      setMessage(els.mediaMessage, "Đang stream tới ESP32.", "success");
    } catch (error) { setMessage(els.mediaMessage, `Stream lỗi: ${error.message}`, "error"); }
    finally { state.frameInFlight = false; }
  }

  els.mediaInput.addEventListener("change", () => {
    const file = els.mediaInput.files?.[0];
    if (!file) return;
    if (state.mediaObjectUrl) URL.revokeObjectURL(state.mediaObjectUrl);
    state.mediaFile = file;
    state.mediaObjectUrl = URL.createObjectURL(file);
    const image = file.type.startsWith("image/");
    els.imagePreview.classList.add("hidden");
    els.videoPreview.classList.toggle("hidden", image);
    els.cropEditor.classList.toggle("hidden", !image);
    state.crop.active = image;
    if (image) {
      state.crop.rotation = 0;
      state.crop.tftTone = DEFAULT_TFT_TONE;
      els.tftTone.value = String(DEFAULT_TFT_TONE);
      els.tftToneInfo.textContent = `${DEFAULT_TFT_TONE}%`;
      els.imagePreview.src = state.mediaObjectUrl;
      els.cropImage.src = state.mediaObjectUrl;
      els.showTestImage.disabled = true;
    } else {
      els.videoPreview.src = state.mediaObjectUrl;
      els.showTestImage.disabled = true;
    }
    els.startMedia.disabled = image;
    els.saveAnimation.disabled = image;
    setMessage(els.mediaMessage, `${file.name} đã sẵn sàng.`);
  });

  els.cropImage.addEventListener("load", () => {
    if (!state.crop.active) return;
    window.requestAnimationFrame(() => {
      resetCrop();
      els.showTestImage.disabled = false;
      setMessage(els.mediaMessage, "Kéo ảnh trong khung để chọn phần muốn hiện.", "success");
    });
  });

  els.startMedia.addEventListener("click", async () => {
    if (!state.mediaFile?.type.startsWith("video/")) return;
    try {
      await postJson("/mode/media", {});
      state.mediaActive = true;
      els.startMedia.disabled = true;
      els.stopMedia.disabled = false;
      if (state.mediaFile.type.startsWith("image/")) await sendMediaFrame();
      else {
        await els.videoPreview.play();
        state.mediaTimer = window.setInterval(sendMediaFrame, 125);
      }
    } catch (error) { setMessage(els.mediaMessage, error.message, "error"); }
  });

  els.stopMedia.addEventListener("click", async () => {
    state.mediaActive = false;
    if (state.mediaTimer !== null) window.clearInterval(state.mediaTimer);
    state.mediaTimer = null;
    els.videoPreview.pause();
    els.startMedia.disabled = !state.mediaFile;
    els.stopMedia.disabled = true;
    try { await postJson("/mode/idle", {}); } catch (_) {}
  });

  function waitForVideoMetadata(video) {
    if (video.readyState >= 1 && Number.isFinite(video.duration)) return Promise.resolve();
    return new Promise((resolve, reject) => {
      video.addEventListener("loadedmetadata", resolve, { once: true });
      video.addEventListener("error", () => reject(new Error("Không đọc được video")), { once: true });
    });
  }

  function seekVideo(video, time) {
    return new Promise((resolve) => {
      if (Math.abs(video.currentTime - time) < 0.04 && video.readyState >= 2) {
        resolve();
        return;
      }
      let settled = false;
      const done = () => {
        if (settled) return;
        settled = true;
        window.clearTimeout(timeout);
        video.removeEventListener("seeked", done);
        video.removeEventListener("timeupdate", done);
        resolve();
      };
      const timeout = window.setTimeout(done, 1200);
      video.addEventListener("seeked", done, { once: true });
      video.addEventListener("timeupdate", done, { once: true });
      try {
        video.currentTime = time;
      } catch (_) {
        done();
      }
    });
  }

  els.saveAnimation.addEventListener("click", async () => {
    if (!state.mediaFile?.type.startsWith("video/") || state.animationUploading) return;
    state.animationUploading = true;
    els.saveAnimation.disabled = true;
    els.animationProgress.classList.remove("hidden");
    els.animationProgress.value = 0;
    els.videoPreview.pause();
    try {
      await waitForVideoMetadata(els.videoPreview);
      const duration = Math.max(0.1, Number(els.videoPreview.duration));
      // Tối ưu tốc độ phát 20 - 24 FPS để chuyển động nhanh, dứt khoát như trên điện thoại
      let fps = 24;
      let totalFrames = 24;
      if (duration <= 1.6) {
        fps = 24;
        totalFrames = Math.min(26, Math.max(16, Math.round(duration * fps)));
      } else if (duration <= 3.0) {
        fps = 20;
        totalFrames = Math.min(36, Math.max(20, Math.round(duration * fps)));
      } else {
        fps = 16;
        totalFrames = Math.min(48, Math.max(24, Math.round(Math.min(duration, 5) * fps)));
      }

      // Ngân sách RAM cho animation: 160 KB (ESP32 có ~260 KB free heap)
      const targetRamBudget = 160000;
      const maxFrameBytes = Math.min(MAX_FRAME_BYTES, Math.max(5000, Math.floor(targetRamBudget / totalFrames)));

      const begin = await postJson("/api/media/animation/begin", { frames: totalFrames, fps: fps });
      const uploadId = begin.upload_id;
      for (let index = 0; index < totalFrames; index += 1) {
        const frameTime = (index / (totalFrames - 1)) * Math.max(0, duration - 0.03);
        await seekVideo(els.videoPreview, frameTime);
        drawMediaSource(els.videoPreview, els.videoPreview.videoWidth, els.videoPreview.videoHeight);
        const blob = await compressedFrameBlob(maxFrameBytes);
        const form = new FormData();
        form.append("frame", blob, `f${String(index).padStart(3, "0")}.jpg`);
        await api(`/api/media/animation/frame?upload_id=${encodeURIComponent(uploadId)}&index=${index}`, { method: "POST", body: form });
        els.animationProgress.value = Math.round(((index + 1) / totalFrames) * 100);
        setMessage(els.mediaMessage, `Đang lưu frame ${index + 1}/${totalFrames} (chất lượng cao)…`);
      }
      await postJson("/api/media/animation/commit", { upload_id: uploadId });
      setMessage(els.mediaMessage, `Đã lưu animation (${totalFrames} frames @ ${fps} fps). Đang nạp RAM và phát siêu mượt & sắc nét.`, "success");
      await refreshStatus();
    } catch (error) {
      setMessage(els.mediaMessage, `Không lưu được animation: ${error.message}`, "error");
    } finally {
      state.animationUploading = false;
      els.saveAnimation.disabled = !state.mediaFile?.type.startsWith("video/");
      window.setTimeout(() => els.animationProgress.classList.add("hidden"), 1200);
    }
  });

  els.deleteAnimation.addEventListener("click", async () => {
    try {
      await api("/api/media/animation", { method: "DELETE" });
      setMessage(els.mediaMessage, "Đã xóa animation.", "success");
      await refreshStatus();
    } catch (error) { setMessage(els.mediaMessage, error.message, "error"); }
  });

  els.refreshStatus.addEventListener("click", refreshStatus);
  loadOpenStreetMap();
  updateDestination();
  loadConfig();
  refreshStatus();
  window.setInterval(refreshStatus, 2000);
})();
