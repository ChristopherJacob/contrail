#pragma once
// The touch panel's link to the settings registry (config spec §5): what the panel shows, and a
// queue that carries its edits from the render thread to the web task, which saves them (an NVS
// write can take tens of milliseconds, too long for a frame).
#include "screens/SettingsScreen.h"
#include "services/Lock.h"
#include "services/Settings.h"

namespace app {

scr::PanelValues panelValues(const svc::Settings& s);
// Saves one edit; false if the registry refused it.
bool applyEdit(svc::Settings& s, const scr::SettingEdit& e);

class EditQueue {
   public:
    static constexpr int kMax = 8;
    bool push(const scr::SettingEdit& e);  // false when full
    bool pop(scr::SettingEdit& out);       // oldest first

   private:
    svc::Mutex mu_;
    scr::SettingEdit q_[kMax];
    int n_ = 0;
};

}  // namespace app
