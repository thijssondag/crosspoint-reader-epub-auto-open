#include "QuickFileTransferActivity.h"

#include <variant>

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <WiFi.h>

#include "../ActivityManager.h"
#include "../ActivityResult.h"
#include "WifiSelectionActivity.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"

namespace {
constexpr const char* MDNS_HOSTNAME = "crosspoint";
constexpr int QR_CODE_SIZE = 198;
}  // namespace

void QuickFileTransferActivity::onEnter() {
  Activity::onEnter();

  LOG_DBG("QUICKXFER", "Quick File Transfer starting");
  LOG_DBG("QUICKXFER", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());

  state = QuickTransferState::CONNECTING_WIFI;
  fileReceived = false;
  uploadedFilePath.clear();
  connectedIP.clear();
  connectedSSID.clear();

  requestUpdate();

  // Join remembered WiFi (auto-connect) or fall back to network list (WifiSelectionActivity default behavior)
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, true),
                         [this](const ActivityResult& result) { onWifiSelectionFinished(result); });
}

void QuickFileTransferActivity::onWifiSelectionFinished(const ActivityResult& result) {
  if (result.isCancelled || !std::holds_alternative<WifiResult>(result.data)) {
    LOG_DBG("QUICKXFER", "WiFi selection cancelled or invalid result");
    activityManager.goHome();
    return;
  }

  const auto& wifi = std::get<WifiResult>(result.data);
  if (!wifi.connected || wifi.ip.empty()) {
    LOG_DBG("QUICKXFER", "WiFi not connected");
    activityManager.goHome();
    return;
  }

  connectedIP = wifi.ip;
  connectedSSID = wifi.ssid;

  if (MDNS.begin(MDNS_HOSTNAME)) {
    LOG_DBG("QUICKXFER", "mDNS started: http://%s.local/", MDNS_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }

  startWebServer();
}

void QuickFileTransferActivity::onExit() {
  Activity::onExit();

  LOG_DBG("QUICKXFER", "Quick File Transfer exiting");

  stopWebServer();

  MDNS.end();

  delay(50);

  WiFi.disconnect(false);
  delay(30);

  WiFi.mode(WIFI_OFF);
  delay(30);

  LOG_DBG("QUICKXFER", "Free heap at onExit: %d bytes", ESP.getFreeHeap());
}

void QuickFileTransferActivity::startWebServer() {
  LOG_DBG("QUICKXFER", "Starting web server...");

  webServer = std::make_unique<CrossPointWebServer>();

  webServer->setUploadCallback([this](const std::string& path) { this->onFileUploaded(path); });

  webServer->begin();

  if (webServer->isRunning()) {
    LOG_DBG("QUICKXFER", "Web server started on port %d", webServer->getPort());
    state = QuickTransferState::SERVER_RUNNING;
    requestUpdate();
  } else {
    LOG_ERR("QUICKXFER", "Failed to start web server");
    activityManager.goHome();
  }
}

void QuickFileTransferActivity::stopWebServer() {
  if (webServer && webServer->isRunning()) {
    LOG_DBG("QUICKXFER", "Stopping web server...");
    webServer->stop();
    webServer.reset();
  }
}

void QuickFileTransferActivity::onFileUploaded(const std::string& path) {
  LOG_DBG("QUICKXFER", "File uploaded: %s", path.c_str());

  if (!fileReceived) {
    fileReceived = true;
    uploadedFilePath = path;
    state = QuickTransferState::OPENING_BOOK;
    requestUpdate();
  }
}

void QuickFileTransferActivity::loop() {
  Activity::loop();

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    LOG_DBG("QUICKXFER", "Back button pressed, cancelling");
    activityManager.goHome();
    return;
  }

  if (state == QuickTransferState::SERVER_RUNNING) {
    if (webServer && webServer->isRunning()) {
      const unsigned long now = millis();
      if (now - lastHandleClientTime >= 1) {
        webServer->handleClient();
        lastHandleClientTime = now;
      }
    }

    if (fileReceived) {
      LOG_DBG("QUICKXFER", "Opening received file: %s", uploadedFilePath.c_str());
      stopWebServer();
      activityManager.goToReader(uploadedFilePath);
    }
  }
}

void QuickFileTransferActivity::render(RenderLock&& lock) {
  Activity::render(std::move(lock));

  // Full white clear so previous activity (e.g. home) does not show through on e-ink
  renderer.clearScreen(0xFF);

  const int padding = 20;
  int y = padding + 40;

  if (state == QuickTransferState::CONNECTING_WIFI) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, "Connecting to WiFi...");
  } else if (state == QuickTransferState::SERVER_RUNNING) {
    std::string statusText = "Connected to: ";
    statusText += connectedSSID;
    renderer.drawCenteredText(UI_10_FONT_ID, y, statusText.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 10;

    renderer.drawCenteredText(UI_10_FONT_ID, y, "Upload a book to open it automatically");
    y += renderer.getLineHeight(UI_10_FONT_ID) + 20;

    std::string qrUrl = "http://";
    qrUrl += connectedIP;
    qrUrl += "/";

    const int pageWidth = renderer.getScreenWidth();
    const Rect qrBounds((pageWidth - QR_CODE_SIZE) / 2, y, QR_CODE_SIZE, QR_CODE_SIZE);
    QrUtils::drawQrCode(renderer, qrBounds, qrUrl);

    y += QR_CODE_SIZE + 20;

    renderer.drawCenteredText(UI_10_FONT_ID, y, qrUrl.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 20;

    renderer.drawCenteredText(SMALL_FONT_ID, y, "Press Back to cancel");

  } else if (state == QuickTransferState::OPENING_BOOK) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, "Opening book...");
  }

  renderer.displayBuffer();
}
