#pragma once

namespace RadioTask {
    void start();
    // trims efetivos (rc_trim[AIL] + servo_trim_l/r) lidos pelo control_task
    int trimL();
    int trimR();
}
