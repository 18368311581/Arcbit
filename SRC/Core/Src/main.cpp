#include "app.hpp"
#include "basic.h"
#include "power.h"
int main(void){
    Hold_power_early();
    HAL_Init();
    Clock_init();
    App_run();
    App_handle_unexpected_return();
}
