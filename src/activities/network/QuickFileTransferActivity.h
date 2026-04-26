#pragma once

#include <memory>
#include <string>

#include "../Activity.h"
#include "MappedInputManager.h"
#include "network/CrossPointWebServer.h"

enum class QuickTransferState {
  CONNECTING_WIFI,  // WiFi selection / auto-connect in progress
  SERVER_RUNNING,   // Server active, waiting for upload
  OPENING_BOOK      // File received, transitioning to reader
};

class QuickFileTransferActivity : public Activity {
 public:
  QuickFileTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("QuickFileTransfer", renderer, mappedInput) {}
  ~QuickFileTransferActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return state == QuickTransferState::SERVER_RUNNING; }
  bool blocksQuickFileTransferHotkey() const override { return true; }

 private:
  QuickTransferState state = QuickTransferState::CONNECTING_WIFI;
  std::unique_ptr<CrossPointWebServer> webServer;
  std::string connectedIP;
  std::string connectedSSID;
  unsigned long lastHandleClientTime = 0;
  std::string uploadedFilePath;
  bool fileReceived = false;

  void startWebServer();
  void stopWebServer();
  void onFileUploaded(const std::string& path);
  void onWifiSelectionFinished(const ActivityResult& result);
};
