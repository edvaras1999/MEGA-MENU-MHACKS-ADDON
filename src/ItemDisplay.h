#pragma once
#include "skse/GameMenus.h"
#include "Settings.h"

namespace SPII::ItemDisplay {
void Update(IMenu* menu, bool magicMenu, const Settings& settings);
void Reset(IMenu* closingMenu=nullptr);
}
