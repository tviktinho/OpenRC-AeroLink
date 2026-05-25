#include "control_task.h"
#include "radio_task.h"
#include "../config.h"
#include "../flight_data.h"

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <PID_v1.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace ControlTask {
namespace {

Servo s_esc, s_srvL, s_srvR;

double s_kp_r = 1.5, s_ki_r = 0.1, s_kd_r = 0.05;
double s_kp_p = 1.5, s_ki_p = 0.1, s_kd_p = 0.05;

double s_setpoint_roll, s_input_roll, s_output_roll;
double s_setpoint_pitch, s_input_pitch, s_output_pitch;

PID s_pid_roll(&s_input_roll, &s_output_roll, &s_setpoint_roll,
               s_kp_r, s_ki_r, s_kd_r, DIRECT);
PID s_pid_pitch(&s_input_pitch, &s_output_pitch, &s_setpoint_pitch,
                s_kp_p, s_ki_p, s_kd_p, DIRECT);

void loadPidsFromNvs() {
    Preferences prefs;
    prefs.begin("fc-config", true);
    s_kp_r = prefs.getDouble("kp_roll",  1.5);
    s_ki_r = prefs.getDouble("ki_roll",  0.1);
    s_kd_r = prefs.getDouble("kd_roll",  0.05);
    s_kp_p = prefs.getDouble("kp_pitch", 1.5);
    s_ki_p = prefs.getDouble("ki_pitch", 0.1);
    s_kd_p = prefs.getDouble("kd_pitch", 0.05);
    prefs.end();
}

void armEscSafety() {
    for (int i = 0; i < 100; ++i) {
        s_esc.writeMicroseconds(PWM_IDLE);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void task(void*) {
    // Init PWM
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    s_esc.setPeriodHertz(50);
    s_srvL.setPeriodHertz(50);
    s_srvR.setPeriodHertz(50);
    s_esc.attach(PIN_ESC, PWM_MIN, PWM_MAX);
    s_srvL.attach(PIN_SERVO_L, PWM_MIN, PWM_MAX);
    s_srvR.attach(PIN_SERVO_R, PWM_MIN, PWM_MAX);
    armEscSafety();

    loadPidsFromNvs();
    s_pid_roll.SetOutputLimits(-400, 400);
    s_pid_pitch.SetOutputLimits(-400, 400);
    s_pid_roll.SetTunings(s_kp_r, s_ki_r, s_kd_r);
    s_pid_pitch.SetTunings(s_kp_p, s_ki_p, s_kd_p);
    s_pid_roll.SetMode(AUTOMATIC);
    s_pid_pitch.SetMode(AUTOMATIC);

    auto applyDiff = [](float x) { if (x < 0) x *= (1.0f - DIFF); return x; };

    const TickType_t period = pdMS_TO_TICKS(1000 / LOOP_FREQ_HZ);
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        // Snapshot atômico do FlightData
        FlightData::lock();
        bool failsafe     = g_fd.failsafe_active;
        bool manual       = g_fd.manual_mode;
        int  throttle_us  = g_fd.throttle_us;
        float cmd_roll    = g_fd.cmd_roll;
        float cmd_pitch   = g_fd.cmd_pitch;
        float roll_rate   = g_fd.roll_rate_dps;
        float pitch_rate  = g_fd.pitch_rate_dps;
        FlightData::unlock();

        if (failsafe) {
            s_esc.writeMicroseconds(PWM_IDLE);
            s_srvL.writeMicroseconds(1500 + REFLEX_US);
            s_srvR.writeMicroseconds(1500 + REFLEX_US);
            vTaskDelayUntil(&lastWake, period);
            continue;
        }

        float outL, outR;
        if (manual) {
            outL = (-cmd_pitch * MANUAL_GAIN) + (cmd_roll * MANUAL_GAIN);
            outR = (-cmd_pitch * MANUAL_GAIN) - (cmd_roll * MANUAL_GAIN);
        } else {
            s_input_roll    = roll_rate;
            s_input_pitch   = pitch_rate;
            s_setpoint_roll  = cmd_roll  * MAX_RATE_DPS;
            s_setpoint_pitch = cmd_pitch * MAX_RATE_DPS;
            s_pid_roll.Compute();
            s_pid_pitch.Compute();
            outL = (-s_output_pitch * G_E) + (s_output_roll * G_A);
            outR = (-s_output_pitch * G_E) - (s_output_roll * G_A);
        }

        int trimL = RadioTask::trimL();
        int trimR = RadioTask::trimR();
        s_srvL.writeMicroseconds(constrain(1500 + (int)applyDiff(outL) + REFLEX_US + trimL,
                                           PWM_MIN, PWM_MAX));
        s_srvR.writeMicroseconds(constrain(1500 + (int)applyDiff(outR) + REFLEX_US + trimR,
                                           PWM_MIN, PWM_MAX));
        s_esc.writeMicroseconds(throttle_us);

        vTaskDelayUntil(&lastWake, period);
    }
}

}  // anon

void start() {
    xTaskCreatePinnedToCore(task, "ControlTask", 4096, nullptr, 5, nullptr, 1);
}

}  // namespace ControlTask
