// Modal dialog for naming a sort destination and choosing its folder.
#pragma once
#include "Settings.h"

enum class DestinationDialogAction { Cancel, Save, Remove };

struct DestinationDialogResult {
    DestinationDialogAction action = DestinationDialogAction::Cancel;
    Destination value;
};

DestinationDialogResult ShowDestinationDialog(HWND owner, int slot, const Destination& current,
                                              const std::wstring& folderBeingSorted);
