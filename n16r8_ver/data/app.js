(() => {
  "use strict";

  const NOMINATIM_URL = "https://nominatim.openstreetmap.org/search";
  const SEARCH_DELAY_MS = 1100;
  const ANIMATION_FPS = 12;
  const ANIMATION_SECONDS = 5;
  const ANIMATION_FRAMES = ANIMATION_FPS * ANIMATION_SECONDS;
  const DEFAULT_DISPLAY_WIDTH = 320;
  const DEFAULT_DISPLAY_HEIGHT = 240;
  // 40 KB/frame keeps frames sharp while fitting within PSRAM and LittleFS
  const MAX_FRAME_BYTES = 40 * 1024;
  // A still image is sent only once, so it can retain more JPEG detail than a video frame.
  // The crop editor needs a re-encode, therefore reserve enough headroom to avoid
  // visibly washing out detailed photos while still fitting safely in ESP32 RAM.
  const MAX_TEST_IMAGE_BYTES = 128 * 1024;
  const DEFAULT_TFT_TONE = 55;

  const $ = (id) => document.getElementById(id);
  const els = {
    deviceStatus: $("device-status"), connectionDetail: $("connection-detail"), stationWebLink: $("station-web-link"), refreshStatus: $("refresh-status"),
    wifiForm: $("wifi-form"), wifiSsid: $("wifi-ssid"), wifiPassword: $("wifi-password"), saveWifi: $("save-wifi"), wifiMessage: $("wifi-message"),
    searchForm: $("search-form"), searchProvider: $("search-provider"), addressInput: $("address-input"), coordinateForm: $("coordinate-form"),
    coordinateInput: $("coordinate-input"), vehicleSelect: $("vehicle-select"), searchMessage: $("search-message"),
    searchResults: $("search-results"), locationQuality: $("location-quality"), destinationDetail: $("destination-detail"), mapStyle: $("map-style"), mapStatus: $("map-status"),
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
    animationProgress: $("animation-progress"), animationStatus: $("animation-status"), mediaMessage: $("media-message"),
    downloadRouteMap: $("download-route-map"), deleteOfflineMap: $("delete-offline-map"),
    offlineMapProgress: $("offline-map-progress"), offlineMapMessage: $("offline-map-message"), offlineMapStats: $("offline-map-stats"),
    alertForm: $("alert-form"), alertEnabled: $("alert-enabled"), alertChatIds: $("alert-chat-ids"),
    alertGatewayUrl: $("alert-gateway-url"), alertAuthKey: $("alert-auth-key"),
    saveAlertConfig: $("save-alert-config"), testAlertBtn: $("test-alert-btn"),
    alertMessage: $("alert-message"), alertStatusPill: $("alert-status-pill"),
    imuStatusPill: $("imu-status-pill"), fallAlertBanner: $("fall-alert-banner"),
    fallReason: $("fall-reason"), clearFallAlertBtn: $("clear-fall-alert-btn"),
    imuRoll: $("imu-roll"), imuPitch: $("imu-pitch"),
    rollBike: $("rollBike"), pitchScene: $("pitchScene"),
    imuAccel: $("imu-accel"), imuGyro: $("imu-gyro"), imuTemp: $("imu-temp")
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

  let currentTileLayer = null;

  function getMapTileLayer(styleKey) {
    const mapboxKey = getMapboxApiKey();
    if (styleKey === "mapbox" && mapboxKey && mapboxKey !== "DIEN_MAPBOX_TOKEN_VAO_DAY") {
      return {
        layer: window.L.tileLayer(
          "https://api.mapbox.com/styles/v1/mapbox/streets-v12/tiles/512/{z}/{x}/{y}@2x?access_token=" + mapboxKey,
          {
            tileSize: 512,
            zoomOffset: -1,
            maxZoom: 22,
            attribution: "© Mapbox"
          }
        ),
        name: "Mapbox HD",
        status: "Mapbox Streets v12 Online"
      };
    }

    if (styleKey === "google") {
      return {
        layer: window.L.tileLayer(
          "https://mt{s}.google.com/vt/lyrs=m&x={x}&y={y}&z={z}&hl=vi",
          {
            subdomains: ["0", "1", "2", "3"],
            maxZoom: 20,
            attribution: "© Google Maps"
          }
        ),
        name: "Google Maps",
        status: "Google Road Tiles Online"
      };
    }

    return {
      layer: window.L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {
        maxZoom: 19,
        attribution: "© OpenStreetMap contributors"
      }),
      name: "OSM",
      status: "OSM Standard Online"
    };
  }

  function applyMapLayer(styleKey) {
    if (!mapReady()) return;
    const tileConfig = getMapTileLayer(styleKey);
    if (currentTileLayer) {
      map.removeLayer(currentTileLayer);
    }
    currentTileLayer = tileConfig.layer;
    currentTileLayer.addTo(map);
    setTag(els.mapStatus, tileConfig.status, "success");
  }

  function initialiseOsmMap() {
    if (mapReady() || !window.L) return mapReady();
    const mapElement = $("map");
    if (!mapElement) return false;

    mapElement.replaceChildren();
    leafletAvailable = true;
    const defaultLat = Number(window.MOTOMAP_ENV?.DEFAULT_LAT) || 10.880539600217121;
    const defaultLon = Number(window.MOTOMAP_ENV?.DEFAULT_LON) || 106.80541333694616;
    map = window.L.map("map", { zoomControl: true }).setView([defaultLat, defaultLon], 16);

    const initialStyle = els.mapStyle ? els.mapStyle.value : "mapbox";
    applyMapLayer(initialStyle);

    if (els.mapStyle) {
      els.mapStyle.addEventListener("change", () => {
        applyMapLayer(els.mapStyle.value);
      });
    }

    map.on("click", (event) => {
      setDestination(event.latlng.lat, event.latlng.lng, "Điểm ghim thủ công", true);
      setMessage(els.searchMessage, "Đã đặt điểm đến trên bản đồ.", "success");
    });
    if (state.destination) renderDestinationMarker();
    if (state.gps?.valid) updateGpsMarker(state.gps);
    window.requestAnimationFrame(() => map.invalidateSize());
    return true;
  }

  function loadOpenStreetMap() {
    if (initialiseOsmMap() || mapLoadFailed) return;
    showMapUnavailable("Đang tải dữ liệu bản đồ…");
    setTag(els.mapStatus, "Đang tải bản đồ…");

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
      showMapUnavailable("Không tải được bản đồ. Hãy mở web khi điện thoại có Internet rồi làm mới trang.");
      setTag(els.mapStatus, "Cần kết nối Internet", "warning");
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
      if (els.downloadRouteMap) els.downloadRouteMap.disabled = true;
      return;
    }
    els.destinationDetail.textContent = `${state.destination.label} · ${state.destination.lat.toFixed(6)}, ${state.destination.lon.toFixed(6)}`;
    els.startNavigation.disabled = false;
    if (els.downloadRouteMap) els.downloadRouteMap.disabled = false;
  }

  function getLocationIqApiKey() {
    return (window.MOTOMAP_ENV && window.MOTOMAP_ENV.LOCATIONIQ_API_KEY)
      ? String(window.MOTOMAP_ENV.LOCATIONIQ_API_KEY).trim()
      : "";
  }

  function getMapboxApiKey() {
    return (window.MOTOMAP_ENV && window.MOTOMAP_ENV.MAPBOX_API_KEY)
      ? String(window.MOTOMAP_ENV.MAPBOX_API_KEY).trim()
      : "";
  }

  function getReferenceCoords() {
    if (state.gps?.valid) {
      return { lat: Number(state.gps.lat), lon: Number(state.gps.lon) };
    }
    return {
      lat: Number(window.MOTOMAP_ENV?.DEFAULT_LAT) || 10.880539600217121,
      lon: Number(window.MOTOMAP_ENV?.DEFAULT_LON) || 106.80541333694616
    };
  }

  async function searchAddressWithMapbox(query, apiKey) {
    const ref = getReferenceCoords();
    // 1. Ưu tiên Mapbox Search Box API v1 (hỗ trợ POI, tòa nhà, trường học, quán xá và số nhà tại VN cực chuẩn)
    const sbParams = new URLSearchParams({
      q: query,
      access_token: apiKey,
      country: "VN",
      language: "vi",
      limit: "5"
    });
    // Không ép proximity cố định nếu người dùng tìm địa chỉ chứa dấu phẩy (tỉnh/thành cụ thể)
    if (!query.includes(",")) {
      sbParams.set("proximity", ref.lon + "," + ref.lat);
    }
    try {
      const sbUrl = "https://api.mapbox.com/search/searchbox/v1/forward?" + sbParams.toString();
      const sbRes = await fetch(sbUrl, { cache: "no-store" });
      if (sbRes.ok) {
        const sbData = await sbRes.json();
        if (Array.isArray(sbData.features) && sbData.features.length > 0) {
          return sbData.features.map((f) => {
            const props = f.properties || {};
            const coords = f.geometry?.coordinates || [0, 0];
            const name = props.name || "";
            const fullAddr = props.full_address || props.place_formatted || name;
            return {
              name: name || fullAddr.split(",")[0].trim(),
              display_name: fullAddr.includes(name) ? fullAddr : (name ? name + " — " + fullAddr : fullAddr),
              lat: Number(coords[1]),
              lon: Number(coords[0]),
              address: {
                house_number: props.Context?.address?.address_number || (props.feature_type === "address" || props.feature_type === "poi" ? "yes" : "")
              }
            };
          });
        }
      }
    } catch (_) {}

    // 2. Dự phòng sang Mapbox Geocoding API v5
    try {
      const v5Params = new URLSearchParams({
        access_token: apiKey,
        country: "vn",
        language: "vi",
        limit: "5",
        autocomplete: "true"
      });
      if (!query.includes(",")) {
        v5Params.set("proximity", ref.lon + "," + ref.lat);
      }
      const v5Url = "https://api.mapbox.com/geocoding/v5/mapbox.places/" + encodeURIComponent(query) + ".json?" + v5Params.toString();
      const response = await fetch(v5Url, { cache: "no-store" });
      if (response.ok) {
        const data = await response.json();
        return (data.features || []).map((f) => ({
          name: f.text_vi || f.text || (f.place_name ? f.place_name.split(",")[0].trim() : ""),
          display_name: f.place_name_vi || f.place_name || "",
          lat: Array.isArray(f.center) ? f.center[1] : 0,
          lon: Array.isArray(f.center) ? f.center[0] : 0,
          address: {
            house_number: f.address || (Array.isArray(f.place_type) && f.place_type.includes("address") ? "yes" : "")
          }
        }));
      }
    } catch (_) {}

    return [];
  }

  async function searchAddressWithLocationIq(query, apiKey) {
    const ref = getReferenceCoords();
    // 1. Thử gọi Autocomplete
    const autoParams = new URLSearchParams({
      key: apiKey,
      q: query,
      countrycodes: "vn",
      format: "json",
      limit: "5",
      "accept-language": "vi"
    });
    if (!query.includes(",")) {
      autoParams.set("lat", String(ref.lat));
      autoParams.set("lon", String(ref.lon));
    }
    
    let results = [];
    try {
      const res = await fetch("https://us1.locationiq.com/v1/autocomplete?" + autoParams.toString(), { cache: "no-store" });
      if (res.ok) {
        const data = await res.json();
        if (Array.isArray(data)) results = data;
      }
    } catch (_) {}

    // 2. Nếu Autocomplete ít kết quả hoặc tìm địa danh tỉnh thành cụ thể, bổ sung thêm Search API v1
    if (results.length < 3) {
      try {
        const searchParams = new URLSearchParams({
          key: apiKey,
          q: query,
          countrycodes: "vn",
          format: "json",
          limit: "5",
          "accept-language": "vi"
        });
        const res2 = await fetch("https://us1.locationiq.com/v1/search?" + searchParams.toString(), { cache: "no-store" });
        if (res2.ok) {
          const data2 = await res2.json();
          if (Array.isArray(data2)) {
            data2.forEach((item) => {
              if (!results.some((r) => r.place_id === item.place_id)) {
                results.push(item);
              }
            });
          }
        }
      } catch (_) {}
    }

    return results.map((item) => {
      const disp = item.display_name || "";
      const primary = item.name || (disp ? disp.split(",")[0].trim() : "");
      return {
        ...item,
        name: primary,
        display_name: disp
      };
    });
  }

  async function searchAddress(address) {
    const wait = SEARCH_DELAY_MS - (Date.now() - state.lastSearchAt);
    if (wait > 0) await new Promise((resolve) => setTimeout(resolve, wait));
    state.lastSearchAt = Date.now();
    const params = new URLSearchParams({
      q: address,
      format: "jsonv2",
      addressdetails: "1",
      limit: "5",
      countrycodes: "vn",
      "accept-language": "vi"
    });
    try {
      const response = await fetch(NOMINATIM_URL + "?" + params.toString(), { headers: { Accept: "application/json" }, cache: "no-store" });
      if (!response.ok) return [];
      const data = await response.json();
      if (!Array.isArray(data)) return [];
      return data.map((item) => {
        const disp = item.display_name || "";
        const primary = item.name || (item.address ? (item.address.name || item.address.road || disp.split(",")[0]) : disp.split(",")[0]);
        return {
          ...item,
          name: (primary || "").trim(),
          display_name: disp
        };
      });
    } catch (_) {
      return [];
    }
  }

  function approxDistanceMeters(lat1, lon1, lat2, lon2) {
    const R = 6371000;
    const dLat = (lat2 - lat1) * Math.PI / 180;
    const dLon = (lon2 - lon1) * Math.PI / 180;
    const a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
              Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
              Math.sin(dLon / 2) * Math.sin(dLon / 2);
    return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  }

  function cleanVietnameseText(str) {
    return String(str || "")
      .toLowerCase()
      .normalize("NFD")
      .replace(/[\u0300-\u036f]/g, "")
      .replace(/đ/g, "d")
      .replace(/[^a-z0-9\s]/g, " ")
      .replace(/\s+/g, " ")
      .trim();
  }

  const GENERIC_VIET_WORDS = new Set([
    "truong", "tieu", "hoc", "trung", "pho", "thong", "dai", "mam", "non",
    "duong", "ngo", "hem", "ngach", "so", "nha", "khu", "ap",
    "quan", "huyen", "thi", "xa", "thanh", "tinh", "phuong"
  ]);

  // Chấm điểm độ khớp giữa từ khóa tìm kiếm và kết quả
  function calculateRelevanceScore(item, rawQuery) {
    const normQuery = cleanVietnameseText(rawQuery);
    const allTokens = normQuery.split(" ").filter((t) => t.length > 1);
    if (allTokens.length === 0) return 100;

    // Lọc loại trừ các từ chỉ loại hình/hành chính chung chung (trường, tiểu học, đường, quận...)
    const keyTokens = allTokens.filter((t) => !GENERIC_VIET_WORDS.has(t));
    const effectiveTokens = keyTokens.length > 0 ? keyTokens : allTokens;

    const primaryName = item.name || (item.display_name ? item.display_name.split(",")[0].trim() : "");
    const normName = cleanVietnameseText(primaryName);
    const normFull = cleanVietnameseText(item.display_name || "");
    const combined = normName + " " + normFull;

    // Kiểm tra từng cụm con được phân tách bằng dấu phẩy (vd: "trường tiểu học hùng vương", "vĩnh long")
    const subPhrases = rawQuery
      .toLowerCase()
      .split(",")
      .map((s) => cleanVietnameseText(s))
      .filter((s) => s.length > 2);

    let phraseMatches = 0;
    subPhrases.forEach((phrase) => {
      if (combined.includes(phrase)) {
        phraseMatches++;
      }
    });

    // Đếm từ khóa cốt lõi xuất hiện trong kết quả
    let matchedKeyTokens = 0;
    effectiveTokens.forEach((t) => {
      if (combined.includes(t)) matchedKeyTokens++;
    });

    const keyRatio = matchedKeyTokens / effectiveTokens.length;

    // Nếu người dùng nhập có địa danh/tỉnh thành (có dấu phẩy, >= 2 vế):
    // Phải khớp ít nhất 1 cụm hoặc tối thiểu 60% từ khóa cốt lõi, nếu không là kết quả nhận vơ khác địa phương!
    if (subPhrases.length >= 2 && phraseMatches === 0 && keyRatio < 0.6) {
      return 0;
    }

    let score = (keyRatio * 60) + (phraseMatches * 30);
    // Thưởng thêm nếu vế đầu (tên địa điểm) xuất hiện trực tiếp trong tên chính
    if (subPhrases[0] && normName.includes(subPhrases[0])) {
      score += 20;
    }

    return score;
  }

  function mergeMultiSourceResults(mapboxList, locationIqList, osmList, rawQuery, maxTotal = 10) {
    const tagged = [];
    (mapboxList || []).forEach((item) => tagged.push({ ...item, _source: "mapbox" }));
    (locationIqList || []).forEach((item) => tagged.push({ ...item, _source: "locationiq" }));
    (osmList || []).forEach((item) => tagged.push({ ...item, _source: "osm" }));

    // 1. Tính điểm liên quan cho từng kết quả
    tagged.forEach((item) => {
      item._score = calculateRelevanceScore(item, rawQuery);
    });

    // 2. Lọc bỏ các kết quả có điểm = 0 (bị loại hoàn toàn vì lệch địa phương/không khớp tên cốt lõi)
    let filtered = tagged.filter((item) => item._score > 0);

    // Nếu lọc hết (do không có kết quả chuẩn tuyệt đối), dùng lại danh sách gốc để người dùng tự xem
    if (filtered.length === 0) {
      filtered = tagged;
    }

    // 3. Sắp xếp kết quả: Điểm liên quan cao nhất lên đầu
    filtered.sort((a, b) => b._score - a._score);

    // 4. Khử trùng lặp theo tọa độ (< 35m) và giới hạn trong lãnh thổ Việt Nam
    const deduplicated = [];
    for (const item of filtered) {
      const lat = Number(item.lat);
      const lon = Number(item.lon);
      if (!Number.isFinite(lat) || !Number.isFinite(lon)) continue;
      // Khóa phạm vi lãnh thổ Việt Nam (Vĩ độ: 8.0 - 23.6, Kinh độ: 102.0 - 110.0)
      if (lat < 8.0 || lat > 23.6 || lon < 102.0 || lon > 110.0) continue;

      const isDuplicate = deduplicated.some((existing) => {
        const dist = approxDistanceMeters(lat, lon, Number(existing.lat), Number(existing.lon));
        return dist < 35;
      });

      if (!isDuplicate) {
        deduplicated.push(item);
        if (deduplicated.length >= maxTotal) break;
      }
    }
    return deduplicated;
  }

  function renderSearchResults(results, defaultSource = "osm", summaryText = "") {
    els.searchResults.replaceChildren();
    const defaultLabel = defaultSource === "mapbox" ? "Mapbox" : (defaultSource === "locationiq" ? "LocationIQ" : "OSM");
    if (!results || !results.length) {
      setMessage(els.searchMessage, (summaryText || "[" + defaultLabel + "]") + " Không tìm thấy kết quả phù hợp. Hãy thử từ khóa khác hoặc tự ghim bản đồ.", "error");
      return;
    }
    setMessage(
      els.searchMessage,
      summaryText || ("Tìm thấy " + results.length + " kết quả từ " + defaultLabel + ". Bấm vào địa chỉ để ghim lên bản đồ."),
      "success"
    );

    results.forEach((result) => {
      const itemSource = result._source || defaultSource;
      const sourceLabel = itemSource === "mapbox" ? "Mapbox" : (itemSource === "locationiq" ? "LocationIQ" : "OSM");

      const button = document.createElement("button");
      button.type = "button";
      button.className = "search-result";
      const title = document.createElement("strong");

      const displayName = result.display_name || "";
      const primaryName = result.name || (result.address ? (result.address.name || result.address.road || displayName.split(",")[0]) : displayName.split(",")[0]);
      title.textContent = "[" + sourceLabel + "] " + primaryName.trim();

      const detail = document.createElement("span");
      detail.textContent = displayName;
      button.append(title, detail);

      button.addEventListener("click", () => {
        const hasHouseNumber = Boolean(result.address?.house_number);
        setDestination(Number(result.lat), Number(result.lon), displayName, true);
        if (itemSource === "mapbox") {
          setTag(els.locationQuality, hasHouseNumber ? "Số nhà Mapbox" : "Địa chỉ Mapbox", "success");
        } else if (itemSource === "locationiq") {
          setTag(els.locationQuality, hasHouseNumber ? "Số nhà LocationIQ" : "Địa chỉ LocationIQ", "success");
        } else {
          setTag(els.locationQuality, hasHouseNumber ? "Có số nhà OSM" : "Vị trí gần đúng", hasHouseNumber ? "success" : "warning");
        }
        setMessage(els.searchMessage, "Đã ghim điểm đến (" + sourceLabel + ")! Có thể kéo ghim trên bản đồ để tinh chỉnh.", "success");
      });
      els.searchResults.appendChild(button);
    });
  }

  async function executeAddressSearch() {
    const address = els.addressInput.value.trim();
    if (!address) return;
    els.searchResults.replaceChildren();

    const mode = els.searchProvider ? els.searchProvider.value : "combined";
    const mapboxKey = getMapboxApiKey();
    const locationIqKey = getLocationIqApiKey();
    const hasMapbox = Boolean(mapboxKey && mapboxKey !== "DIEN_MAPBOX_TOKEN_VAO_DAY");
    const hasLocationIq = Boolean(locationIqKey && locationIqKey !== "DIEN_LOCATIONIQ_TOKEN_VAO_DAY");

    try {
      if (mode === "combined") {
        setMessage(els.searchMessage, "Đang tìm kiếm đồng thời trên cả 3 API (Mapbox + LocationIQ + OSM)…");
        const tasks = [
          hasMapbox ? searchAddressWithMapbox(address, mapboxKey) : Promise.resolve([]),
          hasLocationIq ? searchAddressWithLocationIq(address, locationIqKey) : Promise.resolve([]),
          searchAddress(address)
        ];
        const [mbxSettled, liqSettled, osmSettled] = await Promise.allSettled(tasks);
        const mbxList = mbxSettled.status === "fulfilled" && Array.isArray(mbxSettled.value) ? mbxSettled.value : [];
        const liqList = liqSettled.status === "fulfilled" && Array.isArray(liqSettled.value) ? liqSettled.value : [];
        const osmList = osmSettled.status === "fulfilled" && Array.isArray(osmSettled.value) ? osmSettled.value : [];

        const merged = mergeMultiSourceResults(mbxList, liqList, osmList, address, 10);
        const summary = `Gộp chuẩn ${merged.length} kết quả (Mapbox: ${mbxList.length}, LocationIQ: ${liqList.length}, OSM: ${osmList.length})`;
        renderSearchResults(merged, "combined", summary);
        return;
      }

      if (mode === "mapbox") {
        if (!hasMapbox) {
          setMessage(els.searchMessage, "Chưa có MAPBOX_API_KEY trong env.js!", "error");
          return;
        }
        setMessage(els.searchMessage, "Đang tìm kiếm qua Mapbox API…");
        const results = await searchAddressWithMapbox(address, mapboxKey);
        const scored = (results || []).map((r) => ({ ...r, _score: calculateRelevanceScore(r, address) }));
        scored.sort((a, b) => b._score - a._score);
        renderSearchResults(scored, "mapbox");
        return;
      }

      if (mode === "locationiq") {
        if (!hasLocationIq) {
          setMessage(els.searchMessage, "Chưa có LOCATIONIQ_API_KEY trong env.js!", "error");
          return;
        }
        setMessage(els.searchMessage, "Đang tìm kiếm qua LocationIQ API…");
        const results = await searchAddressWithLocationIq(address, locationIqKey);
        const scored = (results || []).map((r) => ({ ...r, _score: calculateRelevanceScore(r, address) }));
        scored.sort((a, b) => b._score - a._score);
        renderSearchResults(scored, "locationiq");
        return;
      }

      if (mode === "osm") {
        setMessage(els.searchMessage, "Đang tìm trên OpenStreetMap (Nominatim)…");
        const results = await searchAddress(address);
        renderSearchResults(results, "osm");
        return;
      }

      // Chế độ Tự động tuần tự: Mapbox -> LocationIQ -> OSM
      if (hasMapbox) {
        setMessage(els.searchMessage, "Đang tìm kiếm qua Mapbox…");
        try {
          const results = await searchAddressWithMapbox(address, mapboxKey);
          if (results && results.length > 0) {
            const scored = results.map((r) => ({ ...r, _score: calculateRelevanceScore(r, address) }));
            scored.sort((a, b) => b._score - a._score);
            if (scored[0]._score >= 30) {
              renderSearchResults(scored, "mapbox");
              return;
            }
          }
        } catch (err) {
          console.warn("Mapbox error, falling back to LocationIQ:", err);
        }
      }

      if (hasLocationIq) {
        setMessage(els.searchMessage, "Đang tìm kiếm qua LocationIQ…");
        try {
          const results = await searchAddressWithLocationIq(address, locationIqKey);
          if (results && results.length > 0) {
            renderSearchResults(results, "locationiq");
            return;
          }
        } catch (err) {
          console.warn("LocationIQ error, falling back to OSM:", err);
        }
      }

      setMessage(els.searchMessage, "Đang tìm trên OpenStreetMap…");
      renderSearchResults(await searchAddress(address), "osm");
    } catch (error) {
      setMessage(els.searchMessage, "Không tìm được địa chỉ: " + error.message, "error");
    }
  }

  els.searchForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    await executeAddressSearch();
  });

  if (els.searchProvider) {
    els.searchProvider.addEventListener("change", () => {
      if (els.addressInput.value.trim()) {
        executeAddressSearch();
      }
    });
  }

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
    const srcW = width || source.videoWidth || source.naturalWidth || state.displayWidth;
    const srcH = height || source.videoHeight || source.naturalHeight || state.displayHeight;
    const canvas = els.mediaCanvas;
    const context = canvas.getContext("2d", { alpha: false });
    canvas.width = state.displayWidth;
    canvas.height = state.displayHeight;
    context.fillStyle = "#000";
    context.fillRect(0, 0, canvas.width, canvas.height);

    const scale = Math.max(canvas.width / srcW, canvas.height / srcH);
    const drawWidth = Math.round(srcW * scale);
    const drawHeight = Math.round(srcH * scale);
    const drawX = Math.round((canvas.width - drawWidth) / 2);
    const drawY = Math.round((canvas.height - drawHeight) / 2);

    context.imageSmoothingEnabled = true;
    context.imageSmoothingQuality = "high";
    context.drawImage(source, 0, 0, srcW, srcH, drawX, drawY, drawWidth, drawHeight);

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
    if (!state.mediaActive || !state.mediaFile || state.frameInFlight || state.animationUploading) return;
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

  function isVideoFile(file) {
    if (!file) return false;
    if (file.type && file.type.startsWith("video/")) return true;
    const name = (file.name || "").toLowerCase();
    return /\.(mp4|m4v|mov|webm|mkv|avi|3gp|ts)$/i.test(name);
  }

  els.mediaInput.addEventListener("change", () => {
    const file = els.mediaInput.files?.[0];
    if (!file) return;
    if (state.mediaObjectUrl) URL.revokeObjectURL(state.mediaObjectUrl);
    state.mediaFile = file;
    state.mediaObjectUrl = URL.createObjectURL(file);
    const isVideo = isVideoFile(file);
    const isImage = !isVideo;
    els.imagePreview.classList.add("hidden");
    els.videoPreview.classList.toggle("hidden", isImage);
    els.cropEditor.classList.toggle("hidden", !isImage);
    state.crop.active = isImage;
    if (isImage) {
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
    els.startMedia.disabled = isImage;
    els.saveAnimation.disabled = isImage;
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
    if (!isVideoFile(state.mediaFile)) return;
    try {
      await postJson("/mode/media", {});
      state.mediaActive = true;
      els.startMedia.disabled = true;
      els.stopMedia.disabled = false;
      await els.videoPreview.play();
      state.mediaTimer = window.setInterval(sendMediaFrame, 125);
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
    if (video.readyState >= 1 && Number.isFinite(video.duration) && video.duration > 0) {
      return Promise.resolve();
    }
    return new Promise((resolve, reject) => {
      const onDone = () => { cleanup(); resolve(); };
      const onError = () => { cleanup(); reject(new Error("Không đọc được metadata video")); };
      const timeout = setTimeout(() => { cleanup(); resolve(); }, 3500);
      function cleanup() {
        clearTimeout(timeout);
        video.removeEventListener("loadedmetadata", onDone);
        video.removeEventListener("canplay", onDone);
        video.removeEventListener("error", onError);
      }
      video.addEventListener("loadedmetadata", onDone, { once: true });
      video.addEventListener("canplay", onDone, { once: true });
      video.addEventListener("error", onError, { once: true });
    });
  }

  function seekVideo(video, time) {
    return new Promise((resolve) => {
      let settled = false;
      const finish = () => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        video.removeEventListener("seeked", onSeeked);
        requestAnimationFrame(() => setTimeout(resolve, 20));
      };

      const timer = setTimeout(finish, 600);
      const onSeeked = () => finish();

      video.addEventListener("seeked", onSeeked, { once: true });
      try {
        video.currentTime = Math.min(time, Math.max(0, (video.duration || 1) - 0.03));
      } catch (_) {
        finish();
      }
    });
  }

  els.saveAnimation.addEventListener("click", async () => {
    if (!state.mediaFile) {
      setMessage(els.mediaMessage, "Vui lòng chọn file video trước.", "warning");
      return;
    }
    if (state.animationUploading) return;
    if (!isVideoFile(state.mediaFile)) {
      setMessage(els.mediaMessage, "File đã chọn không phải video (.mp4, .mov, .webm...).", "error");
      return;
    }

    setMessage(els.mediaMessage, "Đang chuẩn bị lưu video…");

    // Tắt ngay chế độ stream live nếu đang chạy để không làm nghẽn WebServer ESP32
    state.mediaActive = false;
    if (state.mediaTimer !== null) {
      window.clearInterval(state.mediaTimer);
      state.mediaTimer = null;
    }
    els.startMedia.disabled = true;
    els.stopMedia.disabled = true;

    state.animationUploading = true;
    els.saveAnimation.disabled = true;
    els.animationProgress.classList.remove("hidden");
    els.animationProgress.max = 100;
    els.animationProgress.value = 0;

    // Đợi frame stream đang dở kết thúc hoàn toàn
    while (state.frameInFlight) {
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    await new Promise(resolve => setTimeout(resolve, 200));

    try {
      await waitForVideoMetadata(els.videoPreview);
      els.videoPreview.muted = true;
      els.videoPreview.play().then(() => els.videoPreview.pause()).catch(() => {});

      const duration = Math.min(10.0, Math.max(0.2, Number(els.videoPreview.duration) || 1.0));
      // Tốc độ 16 FPS chuẩn xác theo thời gian thực (đảm bảo ESP32-S3 giải mã mượt mà 1.0x, không bị chậm thành 4s)
      const fps = 16;
      const totalFrames = Math.min(160, Math.max(2, Math.round(duration * fps)));

      // Ngân sách PSRAM cho animation: 3.5 MB (~22 KB/frame ở 160 frames)
      const targetRamBudget = 3500000;
      const maxFrameBytes = Math.min(MAX_FRAME_BYTES, Math.max(12000, Math.floor(targetRamBudget / totalFrames)));

      setMessage(els.mediaMessage, `Đang khởi tạo ${totalFrames} frames (${fps} fps, thời lượng ${duration.toFixed(1)}s)…`);
      const begin = await postJson("/api/media/animation/begin", { frames: totalFrames, fps: fps });
      const uploadId = begin.upload_id;

      for (let index = 0; index < totalFrames; index += 1) {
        setMessage(els.mediaMessage, `Đang lưu frame ${index + 1}/${totalFrames}…`);
        const frameTime = totalFrames > 1
          ? (index / (totalFrames - 1)) * Math.max(0, duration - 0.03)
          : 0;
        await seekVideo(els.videoPreview, frameTime);
        drawMediaSource(els.videoPreview, els.videoPreview.videoWidth, els.videoPreview.videoHeight);
        const blob = await compressedFrameBlob(maxFrameBytes);
        const form = new FormData();
        form.append("frame", blob, `f${String(index).padStart(3, "0")}.jpg`);
        await api(`/api/media/animation/frame?upload_id=${encodeURIComponent(uploadId)}&index=${index}`, { method: "POST", body: form });
        els.animationProgress.value = Math.round(((index + 1) / totalFrames) * 100);
        setMessage(els.mediaMessage, `Đã lưu frame ${index + 1}/${totalFrames} (${Math.round((blob.size / 1024) * 10) / 10} KB)…`);
      }

      await postJson("/api/media/animation/commit", { upload_id: uploadId });
      els.animationProgress.value = 100;
      setMessage(els.mediaMessage, `Đã lưu animation (${totalFrames} frames @ ${fps} fps). Đang phát trên màn hình!`, "success");

      // Cưỡng chế ESP32 chuyển sang Media mode ngay
      try {
        await postJson("/mode/media", {});
      } catch (_) {}
      await refreshStatus();
    } catch (error) {
      setMessage(els.mediaMessage, `Không lưu được animation: ${error.message}`, "error");
    } finally {
      state.animationUploading = false;
      els.saveAnimation.disabled = !isVideoFile(state.mediaFile);
      window.setTimeout(() => els.animationProgress.classList.add("hidden"), 1500);
    }
  });

  els.deleteAnimation.addEventListener("click", async () => {
    try {
      await api("/api/media/animation", { method: "DELETE" });
      setMessage(els.mediaMessage, "Đã xóa animation.", "success");
      await refreshStatus();
    } catch (error) { setMessage(els.mediaMessage, error.message, "error"); }
  });

  async function refreshOfflineMapStats() {
    try {
      const stats = await api("/api/map/offline/stats");
      if (els.offlineMapStats) {
        els.offlineMapStats.textContent = `${stats.tiles_count || 0} tiles (${stats.total_kb || 0} KB)`;
      }
    } catch (_) {}
  }

  function lon2tile(lon, zoom) {
    return Math.floor((lon + 180) / 360 * Math.pow(2, zoom));
  }
  function lat2tile(lat, zoom) {
    return Math.floor((1 - Math.log(Math.tan(lat * Math.PI / 180) + 1 / Math.cos(lat * Math.PI / 180)) / Math.PI) / 2 * Math.pow(2, zoom));
  }

  els.downloadRouteMap.addEventListener("click", async () => {
    if (!state.destination) {
      setMessage(els.offlineMapMessage, "Hãy chọn điểm đến trước.", "warning");
      return;
    }
    const originLat = state.gps?.valid ? state.gps.lat : (map ? map.getCenter().lat : state.destination.lat);
    const originLon = state.gps?.valid ? state.gps.lon : (map ? map.getCenter().lng : state.destination.lon);
    const destLat = state.destination.lat;
    const destLon = state.destination.lon;

    els.downloadRouteMap.disabled = true;
    els.offlineMapProgress.classList.remove("hidden");
    els.offlineMapProgress.max = 100;
    els.offlineMapProgress.value = 0;
    setMessage(els.offlineMapMessage, "Đang tính toán các ô bản đồ trên lộ trình…");

    try {
      // 1. Fetch route geometry from OSRM
      const osrmUrl = `https://router.project-osrm.org/route/v1/driving/${originLon},${originLat};${destLon},${destLat}?overview=full&geometries=geojson`;
      const routeRes = await fetch(osrmUrl);
      if (!routeRes.ok) throw new Error("Không lấy được đường đi từ OSRM");
      const routeData = await routeRes.json();
      const coords = routeData.routes?.[0]?.geometry?.coordinates;
      if (!coords || !coords.length) throw new Error("Lộ trình không có toạ độ");

      // 2. Collect unique zoom 16 tiles along the route
      const zoom = 16;
      const tileMap = new Map();
      coords.forEach(([lon, lat]) => {
        const tx = lon2tile(lon, zoom);
        const ty = lat2tile(lat, zoom);
        for (let dx = -1; dx <= 1; dx++) {
          for (let dy = -1; dy <= 1; dy++) {
            const key = `${zoom}_${tx + dx}_${ty + dy}`;
            tileMap.set(key, { z: zoom, x: tx + dx, y: ty + dy });
          }
        }
      });

      const tiles = Array.from(tileMap.values());
      const maxTiles = 300;
      const downloadTiles = tiles.slice(0, maxTiles);
      const total = downloadTiles.length;

      setMessage(els.offlineMapMessage, `Tìm thấy ${total} ô bản đồ dọc tuyến đường. Bắt đầu tải vào ESP32…`);

      let saved = 0;
      for (let i = 0; i < total; i++) {
        const { z, x, y } = downloadTiles[i];
        const sub = ["a", "b", "c"][(x + y) % 3];
        const tileUrl = `https://${sub}.basemaps.cartocdn.com/rastertiles/voyager/${z}/${x}/${y}.png`;
        try {
          const tileRes = await fetch(tileUrl);
          if (tileRes.ok) {
            const blob = await tileRes.blob();
            const form = new FormData();
            form.append("tile", blob, `t_${z}_${x}_${y}.png`);
            await api(`/api/map/tile?z=${z}&x=${x}&y=${y}`, { method: "POST", body: form });
            saved++;
          }
        } catch (_) {}
        els.offlineMapProgress.value = Math.round(((i + 1) / total) * 100);
        setMessage(els.offlineMapMessage, `Đang tải map offline: ${i + 1}/${total} (${saved} thành công)…`);
      }

      setMessage(els.offlineMapMessage, `Đã lưu thành công ${saved} ô bản đồ vào ESP32! Sẵn sàng đi offline không cần mạng!`, "success");
      await refreshOfflineMapStats();
    } catch (err) {
      setMessage(els.offlineMapMessage, `Lỗi tải map offline: ${err.message}`, "error");
    } finally {
      els.downloadRouteMap.disabled = false;
      window.setTimeout(() => els.offlineMapProgress.classList.add("hidden"), 3000);
    }
  });

  els.deleteOfflineMap.addEventListener("click", async () => {
    if (!confirm("Bạn có chắc chắn muốn xóa toàn bộ bản đồ offline đã lưu trong ESP32?")) return;
    try {
      await api("/api/map/offline", { method: "DELETE" });
      setMessage(els.offlineMapMessage, "Đã xóa toàn bộ bản đồ offline.", "success");
      await refreshOfflineMapStats();
    } catch (err) {
      setMessage(els.offlineMapMessage, `Lỗi xóa: ${err.message}`, "error");
    }
  });

    async function loadAlertConfig() {
    try {
      const data = await api("/api/alert-config");
      if (els.alertEnabled) els.alertEnabled.checked = data.enabled !== false;
      if (els.alertChatIds) els.alertChatIds.value = data.chat_ids || "";
      if (els.alertGatewayUrl) els.alertGatewayUrl.value = data.gateway_url || "";
      if (els.alertAuthKey && data.auth_key) els.alertAuthKey.value = data.auth_key;
      if (els.alertStatusPill && data.last_status) {
        els.alertStatusPill.textContent = data.busy ? "Đang gửi..." : data.last_status;
      }
    } catch (_) {}
  }

  async function handleAlertConfigSave(e) {
    if (e) e.preventDefault();
    if (els.alertMessage) setMessage(els.alertMessage, "Đang lưu cấu hình...");
    try {
      const payload = {
        enabled: els.alertEnabled ? els.alertEnabled.checked : true,
        chat_ids: els.alertChatIds ? els.alertChatIds.value.trim() : "",
        gateway_url: els.alertGatewayUrl ? els.alertGatewayUrl.value.trim() : "",
        auth_key: els.alertAuthKey ? els.alertAuthKey.value.trim() : ""
      };
      const res = await api("/api/alert-config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      if (res && res.ok) {
        if (els.alertMessage) setMessage(els.alertMessage, "Đã lưu cấu hình cảnh báo thành công!", "success");
      } else {
        throw new Error(res.error || "Lỗi lưu cấu hình");
      }
    } catch (err) {
      if (els.alertMessage) setMessage(els.alertMessage, `Lỗi: ${err.message}`, "error");
    }
  }

  async function handleAlertTest() {
    if (els.alertMessage) setMessage(els.alertMessage, "Đang lưu cấu hình và gửi cảnh báo…");
    if (els.testAlertBtn) els.testAlertBtn.disabled = true;
    await handleAlertConfigSave();
    try {
      const res = await api("/api/alert-test", { method: "POST" });
      if (res && res.ok) {
        if (els.alertMessage) setMessage(els.alertMessage, "Đã gửi lệnh phát cảnh báo thử nghiệm thành công! Vui lòng kiểm tra Zalo.", "success");
      } else {
        throw new Error(res.error || "Không thể kích hoạt test");
      }
    } catch (err) {
      if (els.alertMessage) setMessage(els.alertMessage, `Lỗi gửi thử nghiệm: ${err.message}`, "error");
    } finally {
      if (els.testAlertBtn) els.testAlertBtn.disabled = false;
      setTimeout(loadAlertConfig, 2500);
    }
  }

  if (els.alertForm) els.alertForm.addEventListener("submit", handleAlertConfigSave);
  if (els.testAlertBtn) els.testAlertBtn.addEventListener("click", handleAlertTest);

  async function loadImuData() {
    try {
      const data = await api("/api/imu");
      if (!data) return;
      if (els.imuStatusPill) {
        els.imuStatusPill.textContent = data.connected ? "Hoạt động" : "Chưa kết nối";
        els.imuStatusPill.className = "status-pill " + (data.connected ? "status-online" : "status-offline");
      }
      if (data.valid) {
        const roll = Number(data.roll || 0);
        const pitch = Number(data.pitch || 0);
        if (els.imuRoll) els.imuRoll.textContent = roll.toFixed(1);
        if (els.imuPitch) els.imuPitch.textContent = pitch.toFixed(1);
        if (els.rollBike) els.rollBike.setAttribute("transform", `rotate(${roll.toFixed(1)} 160 95)`);
        if (els.pitchScene) els.pitchScene.setAttribute("transform", `rotate(${pitch.toFixed(1)} 160 115)`);

        const ax = Number(data.ax || 0), ay = Number(data.ay || 0), az = Number(data.az || 0);
        const totalAccel = Math.hypot(ax, ay, az);
        if (els.imuAccel) els.imuAccel.textContent = totalAccel.toFixed(1);

        const gx = Number(data.gx || 0), gy = Number(data.gy || 0), gz = Number(data.gz || 0);
        const totalGyro = Math.hypot(gx, gy, gz);
        if (els.imuGyro) els.imuGyro.textContent = totalGyro.toFixed(2);

        if (els.imuTemp) els.imuTemp.textContent = Number(data.temperature || 0).toFixed(1);

        if (els.fallAlertBanner) {
          if (data.fall_detected) {
            els.fallAlertBanner.classList.remove("hidden");
            if (els.fallReason) els.fallReason.textContent = `(Roll: ${roll.toFixed(0)}°, Pitch: ${pitch.toFixed(0)}°)`;
          } else {
            els.fallAlertBanner.classList.add("hidden");
          }
        }
      }
    } catch (_) {}
  }

  async function handleClearFallAlert() {
    try {
      if (els.fallAlertBanner) els.fallAlertBanner.classList.add("hidden");
      await api("/api/imu/clear-alert", { method: "POST" });
    } catch (_) {}
  }

  if (els.clearFallAlertBtn) els.clearFallAlertBtn.addEventListener("click", handleClearFallAlert);

  els.refreshStatus.addEventListener("click", refreshStatus);
  loadAlertConfig();
  loadImuData();
  loadOpenStreetMap();
  updateDestination();
  loadConfig();
  refreshStatus();
  refreshOfflineMapStats();
  window.setInterval(refreshStatus, 2000);
  window.setInterval(loadImuData, 500);
})();
