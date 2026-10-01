#pragma once

#include <string>

#include "app/RunLoop.h"
#include "ui/Tx12View.h"

namespace pilotfly {

class MainWindow {
public:
    MainWindow(RunLoop& runLoop, const Tx12Layout& layout, std::string stopKeyName);

    void draw(double nowSeconds);

private:
    RunLoop& runLoop_;
    Tx12View view_;
    std::string stopKeyName_;
};

}
