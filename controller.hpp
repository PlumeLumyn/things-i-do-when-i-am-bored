#pragma once

#include <atomic>
#include <thread>
class Controller {
private:
  std::thread thread;
  std::atomic<bool> active{false};

public:
  virtual void loop() = 0;
  bool isAlive() { return active.load(); }
  void Start() {
    active.store(true);
    thread = std::thread([this]() {
      while (isAlive())
        loop();
    });
  }
  void Stop() {
    active.store(false);
    if (thread.joinable()) {
      thread.join();
    }
  }
};
