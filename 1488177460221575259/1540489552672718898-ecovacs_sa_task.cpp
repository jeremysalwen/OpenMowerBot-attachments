#if ENABLE_ECOVACS

#include "hub_interface.h"
#include "config.h"
#include "ecovacs_dock_contact.h"
#include "sa_protocol.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <variant>

static const char* TAG = "ecovacs";

// ---------------------------------------------------------------------------
// UART helpers
// ---------------------------------------------------------------------------

static void uart_init(void)
{
    uart_config_t cfg = {};
    cfg.baud_rate  = UART_LL_BAUD;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(
        UART_LL_NUM, UART_LL_BUF_SIZE * 2, UART_LL_BUF_SIZE, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_LL_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(
        UART_LL_NUM, UART0_TX_PIN, UART0_RX_PIN, -1, -1));
}

static void uart_send(const std::vector<uint8_t>& data)
{
    uart_write_bytes(UART_LL_NUM, data.data(), data.size());
}

// ---------------------------------------------------------------------------
// Tracking state  (local to this task, no mutex needed)
// ---------------------------------------------------------------------------

struct MotorEntry { uint8_t id; uint16_t current_ma; };
struct SpeedEntry { uint8_t id; int16_t  speed; };
struct CodeEntry  { uint8_t motor_id; uint8_t code; };

struct MotorAggregate {
    MotorEntry motors[ECOVACS_MAX_MOTOR_ENTRIES];
    uint8_t    num_motors = 0;
    SpeedEntry speeds[ECOVACS_MAX_MOTOR_ENTRIES];
    uint8_t    num_speeds = 0;
    CodeEntry  faults[ECOVACS_MAX_FAULT_ENTRIES];
    uint8_t    num_faults = 0;
    CodeEntry  warnings[ECOVACS_MAX_FAULT_ENTRIES];
    uint8_t    num_warnings = 0;
    uint8_t    last_done_motor_id = 0;
    uint8_t    last_done_result = 0;
};

struct SensorAggregate {
    uint8_t types[ECOVACS_MAX_SENSOR_ENTRIES];
    uint8_t values[ECOVACS_MAX_SENSOR_ENTRIES];
    uint8_t num_entries = 0;

    bool bump_left = false, bump_right = false, bump_lds = false;
    bool fall_left = false, fall_right = false;
    bool downin_left = false, downin_front = false, downin_front_right = false;
    bool downin_right = false, downin_left_back = false, downin_right_back = false;
    bool dirtbox_present = false, carpet_detected = false, mop_attached = false;
    bool sensor_error_flag = false;
};

struct EcovacsState {
    ecovacs::SAProtocol protocol;

    // Keepalive timing
    uint32_t last_wa_tick = 0;
    uint32_t last_ha_tick = 0;
    // Cut motor command refresh + release gate (see cut_motor_refresh_tick)
    uint16_t cut_target_speed = 0;
    uint32_t last_cut_tick    = 0;
    bool     cut_gate_open    = false;
    // Last tick the measured wheel speed (faster wheel) was above
    // BLADE_GATE_WHEEL_MMS; 0 = never. Updated from WD frames.
    uint32_t last_wheel_motion_tick = 0;
    uint64_t wa_count = 0;
    uint64_t ha_count = 0;
    uint64_t rx_count = 0;
    uint64_t tx_count = 0;

    // Tick-differentiation state (Twist computation)
    uint32_t last_wd_tick     = 0;
    int32_t  last_left_ticks  = 0;
    int32_t  last_right_ticks = 0;
    bool     first_wheel_data = true;

    // Motor and safety aggregates
    MotorAggregate  motors;
    SensorAggregate sensors;

    // Dock contact latch (CO edges + CC cross-check)
    DockContactTracker dock;

    // Top-level safety flags
    bool wheel_protection = false;
    bool left_torque = false;
    bool right_torque = false;
    bool estop = false;
    bool estop_external = false;          // last latched reason
    char estop_description[ECOVACS_MAX_DESCRIPTION] = "Normal operation";
    bool robot_invalid = false;
    bool motors_armed = false;
    bool rain_detected = false;

    // Last trigger ticks for the event-only fault messages (WF/WH/ME/MF).
    // fault_ttl_tick() expires the latches when the LL board stops
    // re-reporting; 0 = no latch pending.
    uint32_t last_wf_tick      = 0;
    uint32_t last_torque_tick  = 0;
    uint32_t last_fault_tick   = 0;
    uint32_t last_warning_tick = 0;

    // E-Stop clear state (DO [0x00, 0x01] retry)
    bool     estop_clear_pending  = true;   // set at boot, cleared on DO response
    bool     estop_clear_acked    = false;  // MCU acknowledged DO command
    uint32_t last_do_tick         = 0;      // last DO send tick
    uint8_t  do_retry_count       = 0;      // retry counter for logging

    // Periodic snapshot timing
    uint32_t last_motor_status_pub_tick     = 0;
    uint32_t last_emergency_status_pub_tick = 0;
    uint32_t last_sensor_status_pub_tick    = 0;
    uint32_t last_dock_status_pub_tick      = 0;
};

// ---------------------------------------------------------------------------
// Aggregate update helpers
// ---------------------------------------------------------------------------

static void upsert_motor_current(MotorAggregate& m, uint8_t id, uint16_t cur)
{
    for (uint8_t i = 0; i < m.num_motors; ++i) {
        if (m.motors[i].id == id) { m.motors[i].current_ma = cur; return; }
    }
    if (m.num_motors < ECOVACS_MAX_MOTOR_ENTRIES) {
        m.motors[m.num_motors++] = { id, cur };
    }
}

static void upsert_motor_speed(MotorAggregate& m, uint8_t id, int16_t sp)
{
    for (uint8_t i = 0; i < m.num_speeds; ++i) {
        if (m.speeds[i].id == id) { m.speeds[i].speed = sp; return; }
    }
    if (m.num_speeds < ECOVACS_MAX_MOTOR_ENTRIES) {
        m.speeds[m.num_speeds++] = { id, sp };
    }
}

static void upsert_code(CodeEntry* arr, uint8_t& count, uint8_t id, uint8_t code)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (arr[i].motor_id == id) { arr[i].code = code; return; }
    }
    if (count < ECOVACS_MAX_FAULT_ENTRIES) {
        arr[count++] = { id, code };
    }
}

static void upsert_sensor(SensorAggregate& s, uint8_t type, uint8_t value)
{
    for (uint8_t i = 0; i < s.num_entries; ++i) {
        if (s.types[i] == type && s.values[i] == value) return;
    }
    if (s.num_entries < ECOVACS_MAX_SENSOR_ENTRIES) {
        s.types[s.num_entries]  = type;
        s.values[s.num_entries] = value;
        s.num_entries++;
    }
}

static bool is_escape_command(int32_t left, int32_t right)
{
    return static_cast<int64_t>(left) + static_cast<int64_t>(right) <= 0;
}

static uint8_t calculate_emergency_level(const EcovacsState& s)
{
    if (s.estop || s.wheel_protection) return 3;            // CRITICAL
    if (s.motors.num_faults > 0)        return 2;            // FAULT
    if (s.rain_detected || s.motors.num_warnings > 0) return 1;  // WARNING
    return 0;                                                // OK
}

static bool gd32_rain_active()
{
#if ENABLE_GD32
    uint32_t last = g_health.gd32_rain_last_tick.load();
    if (last == 0) return false;
    uint32_t age_ms = (xTaskGetTickCount() - last) * portTICK_PERIOD_MS;
    if (age_ms > GD32_RAIN_STALE_MS) return false;
    return g_health.gd32_rain_detected.load();
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// OEM boot sequence  (must complete before motors respond)
// ---------------------------------------------------------------------------

static void initialize_mcu(EcovacsState& s)
{
    ESP_LOGI(TAG, "OEM boot sequence starting...");

    // RA (RTC Init)
    {
        std::vector<uint8_t> payload(8, 0x00);
        uart_send(s.protocol.build_frame("RA", payload));
        s.tx_count++;
    }

    // HA (Heartbeat, timeout=2)
    {
        auto payload = ecovacs::CommandBuilder::heartbeat(SA_HEARTBEAT_TIMEOUT);
        uart_send(s.protocol.build_frame("HA", payload));
        s.tx_count++;
        s.ha_count++;
        s.last_ha_tick = xTaskGetTickCount();
    }

    // GC (Gyro Calibration, non-persistent like OEM)
    // IMPORTANT: Do NOT use mode=2 (flash save) during boot! The second gyro
    // sensor may not have finished initializing yet, causing corrupted
    // calibration data to be written to flash.
    {
        auto payload = ecovacs::CommandBuilder::gyro_calibration(false);
        uart_send(s.protocol.build_frame("GC", payload));
        s.tx_count++;
        ESP_LOGI(TAG, "GC sent (mode=0, session only, OEM default)");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // WA #1 (wheel watchdog feed)
    {
        auto payload = ecovacs::CommandBuilder::stop_wheels();
        uart_send(s.protocol.build_frame("WA", payload));
        s.tx_count++;
        s.wa_count++;
        s.last_wa_tick = xTaskGetTickCount();
    }

    // LA (Light Control - OEM payload)
    {
        std::vector<uint8_t> la = {
            0x00,0x00,0x01, 0x01,0x00,0x00, 0x02,0x01,0x0f,
            0x05,0x00,0x00, 0x09,0x00,0x00
        };
        uart_send(s.protocol.build_frame("LA", la));
        s.tx_count++;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // WA #2
    {
        auto payload = ecovacs::CommandBuilder::stop_wheels();
        uart_send(s.protocol.build_frame("WA", payload));
        s.tx_count++;
        s.wa_count++;
        s.last_wa_tick = xTaskGetTickCount();
    }

    // UC (Clock Sync)
    {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        std::vector<uint8_t> uc = {
            0x00,
            static_cast<uint8_t>(now_ms & 0xFF),
            static_cast<uint8_t>((now_ms >> 8) & 0xFF),
            static_cast<uint8_t>((now_ms >> 16) & 0xFF),
            static_cast<uint8_t>((now_ms >> 24) & 0xFF)
        };
        uart_send(s.protocol.build_frame("UC", uc));
        s.tx_count++;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // WA #3 (arm)
    {
        auto payload = ecovacs::CommandBuilder::stop_wheels();
        uart_send(s.protocol.build_frame("WA", payload));
        s.tx_count++;
        s.wa_count++;
        s.last_wa_tick = xTaskGetTickCount();
        ESP_LOGI(TAG, "WA #3 (arm)");
    }

    // Final HA
    {
        auto payload = ecovacs::CommandBuilder::heartbeat(SA_HEARTBEAT_TIMEOUT);
        uart_send(s.protocol.build_frame("HA", payload));
        s.tx_count++;
        s.ha_count++;
        s.last_ha_tick = xTaskGetTickCount();
    }

    ESP_LOGI(TAG, "OEM boot sequence complete");

    // Let the MCU settle before sending clear commands
    vTaskDelay(pdMS_TO_TICKS(2000));

    // GC mode=1: clear gyro calibration flag (suppresses SENSOR_ERROR)
    {
        std::vector<uint8_t> gc_clear = {0x00, 0x01};
        uart_send(s.protocol.build_frame("GC", gc_clear));
        s.tx_count++;
        ESP_LOGI(TAG, "GC sent (mode=1, flag reset only)");
    }

    // DO [0x00, 0x01]: OEM E-Stop clear command (first attempt).
    // Retry logic in keepalive_tick resends every 500ms until MCU ACKs.
    {
        std::vector<uint8_t> do_clear = {0x00, 0x01};
        uart_send(s.protocol.build_frame("DO", do_clear));
        s.tx_count++;
        s.last_do_tick = xTaskGetTickCount();
        s.do_retry_count = 0;
        s.estop_clear_pending = true;
        s.estop_clear_acked = false;
        ESP_LOGI(TAG, "DO sent (estop clear, retry enabled)");
    }

    g_health.ecovacs_state.store(PeripheralState::RUNNING);
    g_health.ecovacs_last_tick.store(xTaskGetTickCount());
}

// ---------------------------------------------------------------------------
// Message processing  (SA RX -> queue to hub_link)
// ---------------------------------------------------------------------------

static void emit_dock_status(const EcovacsState& s);

static void process_message(const ecovacs::SAMessage& msg, EcovacsState& s)
{
    // Update health tick on every valid message from the LL Board
    g_health.ecovacs_last_tick.store(xTaskGetTickCount());
    if (g_health.ecovacs_state.load() == PeripheralState::DATA_TIMEOUT) {
        ESP_LOGI(TAG, "LL Board data resumed");
        g_health.ecovacs_state.store(PeripheralState::RUNNING);
    }

    // MCU debug text (TB messages) - forward to log for diagnostics.
    // The MCU sends emergency state changes and WA processing status via
    // log_printf which produces TB messages over SA protocol.
    if (msg.msg_id() == "TB") {
        const auto& p = msg.payload();
        std::string text(p.begin(), p.end());
        // Strip trailing \r\n
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
            text.pop_back();
        if (!text.empty()) {
            ESP_LOGD(TAG, "MCU: %s", text.c_str());
        }
        return;
    }

    // DO response: the MCU echoes every DO (sa_handle_DO_estop_control,
    // mcu_gkr 1.5.6) whether or not the bit was cleared. The ACK ends the
    // boot handshake; the runtime retry keys on the reported e-stop.
    if (msg.msg_id() == "DO") {
        const auto& p = msg.payload();
        if (p.size() >= 2) {
            ESP_LOGD(TAG, "DO response: status=%u result=%u", p[0], p[1]);
        } else {
            ESP_LOGD(TAG, "DO response: %zu bytes", p.size());
        }
        s.estop_clear_acked = true;
        s.estop_clear_pending = false;
        return;
    }

    // Raw BC payload hex dump (debug-only, very high frequency)
    if (msg.msg_id() == "BC") {
        const auto& p = msg.payload();
        char hex[128];
        int pos = 0;
        for (size_t i = 0; i < p.size() && pos < 120; ++i) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", p[i]);
        }
        ESP_LOGD(TAG, "BC raw[%zu]: %s", p.size(), hex);
    }

    auto decoded = msg.decode();

    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, ecovacs::WheelData>) {
            if (!arg.is_valid()) return;

            // Push raw ticks for diagnostics / downstream bridges
            WheelTicksQueueMsg ticks_msg{};
            ticks_msg.ts_us       = static_cast<uint64_t>(esp_timer_get_time());
            ticks_msg.left_ticks  = arg.left_ticks;
            ticks_msg.right_ticks = arg.right_ticks;
            ticks_msg.valid       = true;  // invalid frames already dropped via is_valid() above
            xQueueOverwrite(g_queues.wheel_ticks, &ticks_msg);

            // Tick differentiation -> measured Twist (replaces ecovacs_xbot_bridge).
            uint32_t now_tick = xTaskGetTickCount();
            if (s.first_wheel_data) {
                s.last_left_ticks  = arg.left_ticks;
                s.last_right_ticks = arg.right_ticks;
                s.last_wd_tick     = now_tick;
                s.first_wheel_data = false;
            } else {
                uint32_t dt_ticks = now_tick - s.last_wd_tick;
                double dt_sec = dt_ticks * (portTICK_PERIOD_MS * 1e-3);
                if (dt_sec >= 0.001) {
                    int32_t dleft  = arg.left_ticks  - s.last_left_ticks;
                    int32_t dright = arg.right_ticks - s.last_right_ticks;
                    double v_left  = (dleft  / TICKS_PER_M) / dt_sec;
                    double v_right = (dright / TICKS_PER_M) / dt_sec;

                    // Blade release gate input: measured proof the wheels
                    // really turn (faster wheel magnitude, mm/s).
                    const double abs_l = (v_left  < 0) ? -v_left  : v_left;
                    const double abs_r = (v_right < 0) ? -v_right : v_right;
                    if ((abs_l > abs_r ? abs_l : abs_r) * 1000.0 >=
                        (double)BLADE_GATE_WHEEL_MMS) {
                        s.last_wheel_motion_tick = now_tick;
                    }

                    EcovacsTwistQueueMsg twist{};
                    twist.ts_us     = static_cast<uint64_t>(esp_timer_get_time());
                    twist.linear_x  = static_cast<float>((v_left + v_right) * 0.5);
                    twist.angular_z = static_cast<float>((v_right - v_left) / WHEEL_BASE_M);
                    xQueueOverwrite(g_queues.measured_twist, &twist);
                    g_health.ecovacs_speed_mms.store(
                        static_cast<int16_t>(twist.linear_x * 1000.0f));

                    s.last_left_ticks  = arg.left_ticks;
                    s.last_right_ticks = arg.right_ticks;
                    s.last_wd_tick     = now_tick;
                }
            }
        }
        else if constexpr (std::is_same_v<T, ecovacs::GyroData>) {
            if (!arg.is_valid()) return;
            EcovacsImuQueueMsg m{};
            m.ts_us = static_cast<uint64_t>(esp_timer_get_time());
            // Gyroscope: actual rad/s values from IIM-42652 (bytes 15-20)
            m.gx = static_cast<float>(arg.gyro_x_rad());
            m.gy = static_cast<float>(arg.gyro_y_rad());
            m.gz = static_cast<float>(arg.gyro_z_rad());
            // Accelerometer: IIM-42652 bias-compensated values (bytes 9-14)
            m.ax = static_cast<float>(arg.accel_x_ms2());
            m.ay = static_cast<float>(arg.accel_y_ms2());
            m.az = static_cast<float>(arg.accel_z_ms2());
            // Lossless stream: never overwrite queued samples.
            xQueueSend(g_queues.ecovacs_imu, &m, 0);
        }
        else if constexpr (std::is_same_v<T, ecovacs::GeomagneticInfo>) {
            // GI counts are MILLIGAUSS, not uT: field probe 2026-08-08 read
            // |B| = 423..435 counts against a ~48 uT earth field - exactly
            // the 10x of mG (1 mG = 0.1 uT = 1e-7 T). With the old 1e-6 the
            // topic overstated the field 10x; direction-only consumers are
            // scale-invariant and were unaffected.
            constexpr double MAG_SCALE = 1e-7;
            EcovacsMagQueueMsg m{};
            m.ts_us = static_cast<uint64_t>(esp_timer_get_time());
            m.mx = static_cast<float>(arg.mag_x * MAG_SCALE);
            m.my = static_cast<float>(arg.mag_y * MAG_SCALE);
            m.mz = static_cast<float>(arg.mag_z * MAG_SCALE);
            xQueueOverwrite(g_queues.ecovacs_mag, &m);
        }
        else if constexpr (std::is_same_v<T, ecovacs::GeomagneticHardware>) {
            // The board's own fused compass heading. Forwarded in raw counts
            // - unit and zero direction are the host's business, so a wrong
            // guess never costs a firmware flash. status/level travel with
            // it so consumers can judge the sample instead of trusting it.
            EcovacsCompassQueueMsg m{};
            m.ts_us            = static_cast<uint64_t>(esp_timer_get_time());
            m.raw_compass      = arg.raw_compass;
            m.combined_compass = arg.combined_compass;
            m.level            = arg.compass_level;
            m.status           = arg.status;
            xQueueOverwrite(g_queues.ecovacs_compass, &m);
        }
        else if constexpr (std::is_same_v<T, ecovacs::ChargeController>) {
            BatteryQueueMsg m{};
            m.percentage  = arg.battery_percent / 100.0f;
            m.voltage     = arg.voltage_mv / 1000.0f;
            m.current     = arg.current_ma / 1000.0f;
            m.temperature = static_cast<float>(arg.temperature_c);
            m.charge_state = arg.charge_state;
            xQueueOverwrite(g_queues.battery, &m);

            // Update cached atomics for WebSocket/REST status push
            g_health.ecovacs_battery_pct.store(
                static_cast<uint8_t>(arg.battery_percent));
            g_health.ecovacs_battery_mv.store(arg.voltage_mv);
            g_health.ecovacs_battery_ma.store(
                static_cast<int16_t>(arg.current_ma));
            g_health.ecovacs_battery_temp_c.store(arg.temperature_c);
            g_health.ecovacs_charge_state.store(arg.charge_state);

            // Cross-check the dock-contact latch against the debounced
            // charge state (recovers lost CO edges in both directions).
            if (s.dock.on_charge_state(arg.charge_state, xTaskGetTickCount(),
                                       static_cast<uint64_t>(esp_timer_get_time()))) {
                emit_dock_status(s);
            }
        }
        else if constexpr (std::is_same_v<T, ecovacs::ChargeOverview>) {
            // Event-driven charger appear/disappear edge; the periodic
            // snapshot refresh covers datagram loss towards the host.
            s.dock.on_charge_overview(
                arg, static_cast<uint64_t>(esp_timer_get_time()));
            emit_dock_status(s);
        }
        else if constexpr (std::is_same_v<T, ecovacs::WheelProtection>) {
            s.wheel_protection = arg.triggered;
            g_health.ecovacs_wheel_protection.store(arg.triggered);
            if (arg.triggered) {
                s.last_wf_tick = xTaskGetTickCount();
                ESP_LOGW(TAG, "Wheel protection triggered");
                g_cmd_vel.estop_active.store(true);
                std::strncpy(s.estop_description,
                             "Wheel protection triggered",
                             sizeof(s.estop_description) - 1);
            } else {
                s.last_wf_tick = 0;
                // Auto-clear estop when wheel protection releases,
                // unless a real E-STOP (BC index 14) is still active.
                if (!s.estop) {
                    g_cmd_vel.estop_active.store(false);
                    ESP_LOGI(TAG, "Wheel protection released, estop cleared");
                }
            }
        }
        else if constexpr (std::is_same_v<T, ecovacs::WheelHubTorque>) {
            if (arg.wheel_index == 0) s.left_torque = arg.torque_triggered;
            else                       s.right_torque = arg.torque_triggered;
            s.last_torque_tick = (s.left_torque || s.right_torque)
                                 ? xTaskGetTickCount() : 0;
        }
        else if constexpr (std::is_same_v<T, ecovacs::OnOffSensorInfo>) {
            s.sensors.sensor_error_flag = arg.sensor_error;

            static bool was_sensor_error = false;
            if (arg.sensor_error) {
                if (!was_sensor_error) {
                    ESP_LOGE(TAG, "SENSOR_ERROR active");
                }
            } else if (was_sensor_error) {
                ESP_LOGI(TAG, "SENSOR_ERROR cleared");
            }
            was_sensor_error = arg.sensor_error;

            // Reset decoded bools, repopulate from this BC frame
            s.sensors = SensorAggregate{};
            s.sensors.sensor_error_flag = arg.sensor_error;

            for (const auto& entry : arg.sensors) {
                upsert_sensor(s.sensors, entry.type, entry.value);
                if (entry.type == ecovacs::ONOFF_TYPE_BUMP) {
                    if (entry.value == ecovacs::BUMP_LEFT)  s.sensors.bump_left  = true;
                    else if (entry.value == ecovacs::BUMP_RIGHT) s.sensors.bump_right = true;
                    else if (entry.value == ecovacs::BUMP_LDS)   s.sensors.bump_lds   = true;
                } else if (entry.type == ecovacs::ONOFF_TYPE_FALL) {
                    if (entry.value == ecovacs::FALL_LEFT)  s.sensors.fall_left  = true;
                    else if (entry.value == ecovacs::FALL_RIGHT) s.sensors.fall_right = true;
                } else if (entry.type == ecovacs::ONOFF_TYPE_DOWNIN) {
                    switch (entry.value) {
                        case ecovacs::DOWNIN_LEFT:        s.sensors.downin_left        = true; break;
                        case ecovacs::DOWNIN_FRONT:       s.sensors.downin_front       = true; break;
                        case ecovacs::DOWNIN_FRONT_RIGHT: s.sensors.downin_front_right = true; break;
                        case ecovacs::DOWNIN_RIGHT:       s.sensors.downin_right       = true; break;
                        case ecovacs::DOWNIN_LEFT_BACK:   s.sensors.downin_left_back   = true; break;
                        case ecovacs::DOWNIN_RIGHT_BACK:  s.sensors.downin_right_back  = true; break;
                    }
                } else if (entry.type == ecovacs::ONOFF_TYPE_DIRTBOX) {
                    s.sensors.dirtbox_present = (entry.value != 0);
                } else if (entry.type == ecovacs::ONOFF_TYPE_CARPET) {
                    s.sensors.carpet_detected = (entry.value != 0);
                } else if (entry.type == ecovacs::ONOFF_TYPE_MOP) {
                    s.sensors.mop_attached = (entry.value != 0);
                }
            }

            bool was_estop = s.estop;
            s.estop = arg.has_estop && (arg.estop_state != 0);
            if (s.estop && !was_estop) {
                ESP_LOGW(TAG, "E-STOP detected via BC message (state=%u)",
                         arg.estop_state);
                g_cmd_vel.estop_active.store(true);
                std::snprintf(s.estop_description, sizeof(s.estop_description),
                              "Emergency stop (BC state=%u)", arg.estop_state);
                // Trigger DO clear retry to unlock the MCU
                s.estop_clear_pending = true;
                s.estop_clear_acked = false;
                s.do_retry_count = 0;
                s.last_do_tick = xTaskGetTickCount();
            } else if (!s.estop && was_estop) {
                // E-STOP cleared, re-enable wheel commands unless
                // wheel protection is still active.
                if (!s.wheel_protection) {
                    g_cmd_vel.estop_active.store(false);
                    ESP_LOGI(TAG, "E-STOP cleared, estop released");
                }
            }

            // Update cached atomics for WebSocket/REST status push
            g_health.ecovacs_bumper_left.store(s.sensors.bump_left);
            g_health.ecovacs_bumper_right.store(s.sensors.bump_right);
            g_health.ecovacs_estop.store(s.estop);
            g_health.ecovacs_fall_left.store(s.sensors.fall_left);
            g_health.ecovacs_fall_right.store(s.sensors.fall_right);
            g_health.ecovacs_sensor_error.store(s.sensors.sensor_error_flag);
        }
        else if constexpr (std::is_same_v<T, ecovacs::RobotInvalidState>) {
            s.robot_invalid = arg.is_not_on_plane;
            if (arg.is_not_on_plane) {
                ESP_LOGW(TAG, "Robot lifted/tilted");
            }
            g_health.ecovacs_lifted.store(arg.is_not_on_plane);
        }
        else if constexpr (std::is_same_v<T, ecovacs::FullBump>) {
            ESP_LOGW(TAG, "Full bump: type=%d angle=%d", arg.type, arg.angle);
            FullBumpQueueMsg fb{};
            fb.bump_type = arg.type;
            fb.angle     = arg.angle;
            uint8_t n = (arg.raw_data.size() <= ECOVACS_MAX_BUMP_RAW_BYTES)
                       ? static_cast<uint8_t>(arg.raw_data.size())
                       : static_cast<uint8_t>(ECOVACS_MAX_BUMP_RAW_BYTES);
            std::memcpy(fb.raw_data, arg.raw_data.data(), n);
            fb.raw_size = n;
            xQueueOverwrite(g_queues.full_bump, &fb);
        }
        else if constexpr (std::is_same_v<T, ecovacs::MotorBattery>) {
            for (const auto& e : arg.motors) {
                upsert_motor_current(s.motors, e.index, e.current_ma);
                // Internal index 0 = cut motor (blade) current
                if (e.index == 0) {
                    g_health.ecovacs_blade_current_ma.store(e.current_ma);
                }
            }
        }
        else if constexpr (std::is_same_v<T, ecovacs::MotorSpeed>) {
            upsert_motor_speed(s.motors, arg.motor_index, arg.speed);
            // Motor index 10 = cut motor (blade) RPM
            if (arg.motor_index == ecovacs::MOTOR_CUT) {
                g_health.ecovacs_blade_speed.store(arg.speed);
                g_health.ecovacs_blade_running.store(arg.speed != 0);
            }
        }
        else if constexpr (std::is_same_v<T, ecovacs::MotorDone>) {
            s.motors.last_done_motor_id = arg.motor_index;
            s.motors.last_done_result   = arg.result;
        }
        else if constexpr (std::is_same_v<T, ecovacs::MotorError>) {
            ESP_LOGE(TAG, "Motor error: raw=%d mapped=%d",
                     arg.raw_code, arg.mapped_code);
            // No motor index in this payload -> use 0xFF as placeholder id
            upsert_code(s.motors.faults, s.motors.num_faults,
                        0xFF, arg.mapped_code);
            s.last_fault_tick = xTaskGetTickCount();
        }
        else if constexpr (std::is_same_v<T, ecovacs::MotorWarning>) {
            upsert_code(s.motors.warnings, s.motors.num_warnings,
                        0xFF, arg.warning_code);
            s.last_warning_tick = xTaskGetTickCount();
        }
        else if constexpr (std::is_same_v<T, ecovacs::Heartbeat>) {
            g_health.ecovacs_heartbeat_ok.store(true);
        }
        else if constexpr (std::is_same_v<T, ecovacs::WheelActionAck>) {
            // WA ACK received (normal operation, no log needed)
        }
        else if constexpr (std::is_same_v<T, ecovacs::GyroStatus>) {
            static uint8_t last_gc_status = 0xFF;
            if (arg.status != last_gc_status) {
                ESP_LOGI(TAG, "Gyro status changed: ready=%d status=%u",
                         arg.ready ? 1 : 0, arg.status);
                last_gc_status = arg.status;
            }
            // Deferred motor arming logic removed - the OEM boot sequence
            // already arms motors via WA(STOP). The GS handler only tracks
            // status for diagnostics, not for gating wheel commands.
        }
        else if constexpr (std::is_same_v<T, ecovacs::MotorAcknowledge>) {
            for (const auto& m : arg.motors) {
                ESP_LOGD(TAG, "Motor ACK: id=%u dir=%u spd=%u result=%u",
                         m.motor_id, m.direction, m.speed_lsb, m.result);
            }
        }
        else if constexpr (std::is_same_v<T, ecovacs::RawPayload>) {
            ESP_LOGD(TAG, "Unhandled msg [%s] payload(%u bytes)",
                     msg.msg_id().c_str(), (unsigned)arg.data.size());
        }
    }, decoded);
}

// ---------------------------------------------------------------------------
// Snapshot publishers (push aggregates into queues for hub_link_task to drain)
// ---------------------------------------------------------------------------

static void emit_motor_status(const EcovacsState& s)
{
    MotorStatusQueueMsg m{};
    m.num_motors = s.motors.num_motors;
    for (uint8_t i = 0; i < m.num_motors; ++i) {
        m.motor_ids[i]   = s.motors.motors[i].id;
        m.currents_ma[i] = s.motors.motors[i].current_ma;
    }
    m.num_speeds = s.motors.num_speeds;
    for (uint8_t i = 0; i < m.num_speeds; ++i) {
        m.speed_motor_ids[i] = s.motors.speeds[i].id;
        m.speeds[i]          = s.motors.speeds[i].speed;
    }
    m.left_torque_triggered  = s.left_torque ? 1 : 0;
    m.right_torque_triggered = s.right_torque ? 1 : 0;
    m.last_done_motor_id     = s.motors.last_done_motor_id;
    m.last_done_result       = s.motors.last_done_result;

    m.num_faults = s.motors.num_faults;
    for (uint8_t i = 0; i < m.num_faults; ++i) {
        m.fault_motor_ids[i] = s.motors.faults[i].motor_id;
        m.fault_codes[i]     = s.motors.faults[i].code;
    }
    m.num_warnings = s.motors.num_warnings;
    for (uint8_t i = 0; i < m.num_warnings; ++i) {
        m.warning_motor_ids[i] = s.motors.warnings[i].motor_id;
        m.warning_codes[i]     = s.motors.warnings[i].code;
    }
    xQueueOverwrite(g_queues.motor_status, &m);
}

static void emit_emergency_status(const EcovacsState& s)
{
    EmergencyStatusQueueMsg m{};
    m.estop_state = s.estop ? 1 : calculate_emergency_level(s);
    std::strncpy(m.estop_description, s.estop_description,
                 sizeof(m.estop_description) - 1);

    m.robot_invalid_state        = s.robot_invalid;
    m.rain_detected              = s.rain_detected;
    m.wheel_protection_triggered = s.wheel_protection;
    m.left_torque_protection     = s.left_torque ? 1 : 0;
    m.right_torque_protection    = s.right_torque ? 1 : 0;

    m.num_faults = s.motors.num_faults;
    for (uint8_t i = 0; i < m.num_faults; ++i) {
        m.fault_motor_ids[i] = s.motors.faults[i].motor_id;
        m.fault_codes[i]     = s.motors.faults[i].code;
    }
    m.num_warnings = s.motors.num_warnings;
    for (uint8_t i = 0; i < m.num_warnings; ++i) {
        m.warning_motor_ids[i] = s.motors.warnings[i].motor_id;
        m.warning_codes[i]     = s.motors.warnings[i].code;
    }

    m.emergency_level = calculate_emergency_level(s);
    xQueueOverwrite(g_queues.emergency_status, &m);

    // Mirror the aggregate for /api/status and the web UI (the emergency
    // picture must never again be invisible while missions get cancelled).
    g_health.ecovacs_emergency_level.store(m.emergency_level);
    g_health.ecovacs_num_faults.store(s.motors.num_faults);
    g_health.ecovacs_num_warnings.store(s.motors.num_warnings);
}

static void emit_sensor_status(const EcovacsState& s)
{
    SensorStatusQueueMsg m{};
    m.num_sensors = s.sensors.num_entries;
    for (uint8_t i = 0; i < m.num_sensors; ++i) {
        m.sensor_types[i]  = s.sensors.types[i];
        m.sensor_values[i] = s.sensors.values[i];
    }

    m.bump_left   = s.sensors.bump_left;
    m.bump_right  = s.sensors.bump_right;
    m.bump_lds    = s.sensors.bump_lds;
    m.fall_left   = s.sensors.fall_left;
    m.fall_right  = s.sensors.fall_right;
    m.downin_left        = s.sensors.downin_left;
    m.downin_front       = s.sensors.downin_front;
    m.downin_front_right = s.sensors.downin_front_right;
    m.downin_right       = s.sensors.downin_right;
    m.downin_left_back   = s.sensors.downin_left_back;
    m.downin_right_back  = s.sensors.downin_right_back;
    m.dirtbox_present    = s.sensors.dirtbox_present;
    m.carpet_detected    = s.sensors.carpet_detected;
    m.mop_attached       = s.sensors.mop_attached;
    m.sensor_error       = s.sensors.sensor_error_flag;

    if (s.sensors.sensor_error_flag) {
        std::strncpy(m.description, "Sensor error reported by MCU",
                     sizeof(m.description) - 1);
    } else {
        m.description[0] = '\0';
    }
    xQueueOverwrite(g_queues.sensor_status, &m);
}

static void emit_dock_status(const EcovacsState& s)
{
    DockStatusQueueMsg m{};
    s.dock.fill(m);
    xQueueOverwrite(g_queues.dock_status, &m);

    g_health.ecovacs_dock_contact.store(m.dock_contact);
    g_health.ecovacs_contact_mv.store(m.contact_mv);
}

static void status_snapshots_tick(EcovacsState& s)
{
    // Push aggregate snapshots at 10 Hz so hub_link_task can publish them.
    constexpr uint32_t STATUS_INTERVAL_MS = 100;

    uint32_t now = xTaskGetTickCount();
    if ((now - s.last_motor_status_pub_tick) * portTICK_PERIOD_MS >= STATUS_INTERVAL_MS) {
        emit_motor_status(s);
        s.last_motor_status_pub_tick = now;
    }
    if ((now - s.last_emergency_status_pub_tick) * portTICK_PERIOD_MS >= STATUS_INTERVAL_MS) {
        // Refresh rain from the GD32 top-board sensor before emitting so
        // both the emergency level and the rain_detected flag reflect it.
        s.rain_detected = gd32_rain_active();
        emit_emergency_status(s);
        s.last_emergency_status_pub_tick = now;
    }
    if ((now - s.last_sensor_status_pub_tick) * portTICK_PERIOD_MS >= STATUS_INTERVAL_MS) {
        emit_sensor_status(s);
        s.last_sensor_status_pub_tick = now;
    }

    // Dock contact is latched from rare CO edges: a slow refresh covers
    // datagram loss and hosts that (re)connect while the state is stable.
    constexpr uint32_t DOCK_STATUS_INTERVAL_MS = 1000;
    if ((now - s.last_dock_status_pub_tick) * portTICK_PERIOD_MS >= DOCK_STATUS_INTERVAL_MS) {
        emit_dock_status(s);
        s.last_dock_status_pub_tick = now;
    }
}

// ---------------------------------------------------------------------------
// OEM-style keepalive  (called from main loop every SA_KEEPALIVE_TICK_MS)
// ---------------------------------------------------------------------------

static void keepalive_tick(EcovacsState& s)
{
    uint32_t now_tick = xTaskGetTickCount();

    // E-Stop clear retry: resend DO [0x00, 0x01] while the LL board still
    // reports the e-stop. The MCU echoes every DO regardless of effect, so
    // the response cannot end the retry.
    const bool do_needed =
        s.estop || (s.estop_clear_pending && !s.estop_clear_acked);
    if (do_needed) {
        uint32_t ms_since_do = (now_tick - s.last_do_tick) * portTICK_PERIOD_MS;
        uint32_t interval = (s.do_retry_count < 20) ? 500 : 5000;
        if (ms_since_do >= interval) {
            if (s.do_retry_count < 255) s.do_retry_count++;
            std::vector<uint8_t> do_clear = {0x00, 0x01};
            uart_send(s.protocol.build_frame("DO", do_clear));
            s.tx_count++;
            s.last_do_tick = now_tick;
            if (s.do_retry_count <= 20) {
                ESP_LOGD(TAG, "DO retry #%u (estop clear)", s.do_retry_count);
            } else if (s.do_retry_count == 21) {
                ESP_LOGD(TAG, "DO estop clear: switching to slow retry (5s)");
            }
        }
    } else {
        s.do_retry_count = 0;
    }

    uint32_t ms_since_cmd = (now_tick - g_cmd_vel.last_cmd_vel_tick.load())
                            * portTICK_PERIOD_MS;
    const int32_t left  = g_cmd_vel.target_left_speed.load();
    const int32_t right = g_cmd_vel.target_right_speed.load();
    const bool gated  = g_cmd_vel.estop_active.load();
    const bool escape = gated && is_escape_command(left, right);
    bool moving = g_cmd_vel.movement_active.load() &&
                  (ms_since_cmd < SA_CMD_VEL_TIMEOUT_MS) &&
                  (!gated || escape);

    // Periodic diagnostic (every 2 seconds, debug-only)
    static uint32_t last_dbg_tick = 0;
    if ((now_tick - last_dbg_tick) * portTICK_PERIOD_MS >= 2000) {
        ESP_LOGD(TAG, "keepalive: active=%d since_cmd=%lu estop=%d escape=%d "
                      "moving=%d L=%ld R=%ld",
                 (int)g_cmd_vel.movement_active.load(),
                 (unsigned long)ms_since_cmd,
                 (int)gated,
                 (int)escape,
                 (int)moving,
                 (long)left, (long)right);
        last_dbg_tick = now_tick;
    }

    if (moving) {
        // Movement mode: WA dominant, HA sparse
        uint32_t ms_since_wa = (now_tick - s.last_wa_tick) * portTICK_PERIOD_MS;
        if (ms_since_wa >= SA_WA_MIN_INTERVAL_MS) {
            if (escape) {
                static uint32_t last_escape_log_tick = 0;
                if (last_escape_log_tick == 0 ||
                    (now_tick - last_escape_log_tick) * portTICK_PERIOD_MS >= 1000)
                {
                    ESP_LOGW(TAG, "Escape drive while gated (%s): L=%ld R=%ld",
                             s.estop_description, (long)left, (long)right);
                    last_escape_log_tick = now_tick;
                }
            }
            auto payload = ecovacs::CommandBuilder::wheel_action(left, right);
            uart_send(s.protocol.build_frame("WA", payload));
            ESP_LOGD(TAG, "WA SENT: L=%ld R=%ld", (long)left, (long)right);
            s.tx_count++;
            s.wa_count++;
            s.last_wa_tick = now_tick;
        }

        uint32_t ms_since_ha = (now_tick - s.last_ha_tick) * portTICK_PERIOD_MS;
        if (ms_since_ha >= SA_HA_MOVE_INTERVAL_MS) {
            auto payload = ecovacs::CommandBuilder::heartbeat(SA_HEARTBEAT_TIMEOUT);
            uart_send(s.protocol.build_frame("HA", payload));
            s.tx_count++;
            s.ha_count++;
            s.last_ha_tick = now_tick;
        }
    } else {
        // Idle mode: HA only (OEM sends NO WA during idle)
        uint32_t ms_since_ha = (now_tick - s.last_ha_tick) * portTICK_PERIOD_MS;
        if (ms_since_ha >= SA_HA_IDLE_INTERVAL_MS) {
            auto payload = ecovacs::CommandBuilder::heartbeat(SA_HEARTBEAT_TIMEOUT);
            uart_send(s.protocol.build_frame("HA", payload));
            s.tx_count++;
            s.ha_count++;
            s.last_ha_tick = now_tick;
        }
    }
}

// ---------------------------------------------------------------------------
// Motor command processing  (from subscriber queue)
// ---------------------------------------------------------------------------

static void process_motor_commands(EcovacsState& s)
{
    MotorCmdQueueMsg cmd;
    while (xQueueReceive(g_queues.motor_cmd, &cmd, 0) == pdTRUE) {
        if (g_cmd_vel.estop_active.load() && cmd.speed != 0) continue;

        if (cmd.motor_id == ecovacs::MOTOR_CUT) {
            // No GC frame here. "GC" is Gyro Calibration, not a grass-cutting
            // mode: the protocol has no such command, and the prerequisite
            // the cut motor really has is the one GC of the boot sequence,
            // which run_boot_sequence() already sends.
            uint16_t abs_speed = (cmd.speed > 0)
                ? static_cast<uint16_t>(cmd.speed) : 0;
            if (abs_speed == 0) {
                // A stop always goes out immediately (kill-switch semantics,
                // never gated).
                uart_send(s.protocol.build_frame(
                    "MA", ecovacs::CommandBuilder::cut_motor_speed(0)));
                s.tx_count++;
                if (s.cut_target_speed != 0) {
                    ESP_LOGI(TAG, "Cut motor OFF (commanded)");
                }
                s.cut_target_speed = 0;
                s.cut_gate_open    = false;
                g_health.ecovacs_cut_gated.store(false);
            } else {
                // A start only latches the request. Emission is owned by
                // cut_motor_refresh_tick, which gates on measured wheel
                // motion - the LL board would drop a standing-still MA
                // anyway, and dither-flickered wheel detection must not
                // find a fresh spin-up command to act on.
                s.cut_target_speed = abs_speed;
                s.last_cut_tick    = 0;   // emit on the first open-gate tick
                ESP_LOGI(TAG, "Cut motor request latched (speed %u), "
                              "engages when the wheels turn", abs_speed);
            }
        } else {
            auto payload = ecovacs::CommandBuilder::motor_control_signed(
                cmd.motor_id, cmd.speed);
            uart_send(s.protocol.build_frame("MA", payload));
            s.tx_count++;
        }
    }
}

// ---------------------------------------------------------------------------
// Fault clear  (HubLink kOpClearFaults, REST /api/faults/clear)
// ---------------------------------------------------------------------------
// WF / WH / ME / MF are event messages: the LL board reports the trigger, not
// the release. Without this reset a single wheel-protection event pins
// emergency_level at 3 until the hub reboots, and the coverage orchestrator
// cancels every mission - undock included - milliseconds after the start.
// Mirrors clear_faults_callback() of the pre-hub ecovacs_comms node.

static void process_fault_clear(EcovacsState& s)
{
    if (!g_health.ecovacs_clear_faults_request.exchange(false)) return;

    s.wheel_protection    = false;
    s.left_torque         = false;
    s.right_torque        = false;
    s.motors.num_faults   = 0;
    s.motors.num_warnings = 0;
    s.last_wf_tick        = 0;
    s.last_torque_tick    = 0;
    s.last_fault_tick     = 0;
    s.last_warning_tick   = 0;
    g_health.ecovacs_wheel_protection.store(false);

    // s.estop mirrors every BC frame at ~10 Hz, so clearing it here would only
    // hold until the next frame. Release the command gate for the latched
    // reasons only, and keep it closed while a real e-stop stands.
    if (!s.estop) {
        g_cmd_vel.estop_active.store(false);
        std::strncpy(s.estop_description, "Normal operation",
                     sizeof(s.estop_description) - 1);
    }

    // Reboot parity: booting recovers a stuck MCU-side e-stop because the
    // boot sequence sends DO. Give the runtime clear the same power --
    // restart the DO retry cycle so a standing BC e-stop gets a fresh
    // release attempt instead of requiring a hub reboot.
    s.estop_clear_pending = true;
    s.estop_clear_acked   = false;
    s.do_retry_count      = 0;
    s.last_do_tick        = xTaskGetTickCount();
    {
        std::vector<uint8_t> do_clear = {0x00, 0x01};
        uart_send(s.protocol.build_frame("DO", do_clear));
        s.tx_count++;
    }

    ESP_LOGW(TAG, "Fault state cleared (wheel protection, torque, motor "
                  "faults) + DO estop clear restarted");

    emit_emergency_status(s);
    emit_motor_status(s);
}

// ---------------------------------------------------------------------------
// Fault latch expiry
// ---------------------------------------------------------------------------

static void fault_ttl_tick(EcovacsState& s)
{
    const uint32_t now = xTaskGetTickCount();
    bool changed = false;

    auto expired = [now](uint32_t since_tick, int ttl_ms) {
        return since_tick != 0 &&
               (now - since_tick) * portTICK_PERIOD_MS >= (uint32_t)ttl_ms;
    };

    if (s.wheel_protection && expired(s.last_wf_tick, ECOVACS_WF_TTL_MS)) {
        s.wheel_protection = false;
        s.last_wf_tick = 0;
        g_health.ecovacs_wheel_protection.store(false);
        if (!s.estop) {
            g_cmd_vel.estop_active.store(false);
            std::strncpy(s.estop_description, "Normal operation",
                         sizeof(s.estop_description) - 1);
        }
        ESP_LOGW(TAG, "Wheel protection latch expired (no re-trigger for %ds)",
                 ECOVACS_WF_TTL_MS / 1000);
        changed = true;
    }

    if ((s.left_torque || s.right_torque) &&
        expired(s.last_torque_tick, ECOVACS_WF_TTL_MS))
    {
        s.left_torque = false;
        s.right_torque = false;
        s.last_torque_tick = 0;
        ESP_LOGW(TAG, "Torque protection latch expired (no re-trigger for %ds)",
                 ECOVACS_WF_TTL_MS / 1000);
        changed = true;
    }

    if (s.motors.num_faults > 0 &&
        expired(s.last_fault_tick, ECOVACS_FAULT_TTL_MS))
    {
        s.motors.num_faults = 0;
        s.last_fault_tick = 0;
        ESP_LOGW(TAG, "Motor fault latch expired (no re-report for %ds)",
                 ECOVACS_FAULT_TTL_MS / 1000);
        changed = true;
    }

    if (s.motors.num_warnings > 0 &&
        expired(s.last_warning_tick, ECOVACS_FAULT_TTL_MS))
    {
        s.motors.num_warnings = 0;
        s.last_warning_tick = 0;
        changed = true;
    }

    if (changed) {
        emit_emergency_status(s);
        emit_motor_status(s);
    }
}

// ---------------------------------------------------------------------------
// Cut motor refresh + release gate
// ---------------------------------------------------------------------------

static void cut_motor_refresh_tick(EcovacsState& s)
{
    if (s.cut_target_speed == 0) return;

    // E-stop: drop the request instead of re-arming the blade behind it.
    if (g_cmd_vel.estop_active.load()) {
        s.cut_target_speed = 0;
        s.cut_gate_open    = false;
        g_health.ecovacs_cut_gated.store(false);
        return;
    }

    uint32_t now = xTaskGetTickCount();

    const bool wheels_turning =
        s.last_wheel_motion_tick != 0 &&
        (now - s.last_wheel_motion_tick) * portTICK_PERIOD_MS <
            (uint32_t)BLADE_GATE_STOP_HOLD_MS;

    if (!wheels_turning) {
        if (s.cut_gate_open) {
            s.cut_gate_open = false;
            // Deterministic handoff: the board stops the motor at standstill
            // on its own, but the explicit stop clears any board-side state
            // so wheel jitter cannot re-trigger it.
            uart_send(s.protocol.build_frame(
                "MA", ecovacs::CommandBuilder::cut_motor_speed(0)));
            s.tx_count++;
            ESP_LOGI(TAG, "Cut motor gated OFF (wheels stopped, "
                          "request stays latched)");
        }
        g_health.ecovacs_cut_gated.store(true);
        return;
    }

    if (!s.cut_gate_open) {
        s.cut_gate_open = true;
        s.last_cut_tick = 0;   // engage now, do not wait out the interval
        g_health.ecovacs_cut_gated.store(false);
        ESP_LOGI(TAG, "Cut motor gate open (wheels turning), engaging blade");
    }

    if ((now - s.last_cut_tick) * portTICK_PERIOD_MS < SA_CUT_REFRESH_MS) return;

    auto payload = ecovacs::CommandBuilder::cut_motor_speed(s.cut_target_speed);
    uart_send(s.protocol.build_frame("MA", payload));
    s.tx_count++;
    s.last_cut_tick = now;
}

// ---------------------------------------------------------------------------
// Task entry point
// ---------------------------------------------------------------------------

void ecovacs_sa_task(void* /*param*/)
{
    ESP_LOGI(TAG, "Ecovacs SA task starting on core %d", xPortGetCoreID());

    uart_init();

    EcovacsState state;
    initialize_mcu(state);

    uint8_t rx_buf[256];
    TickType_t last_keepalive = xTaskGetTickCount();

    while (true) {
        // Read UART
        int len = uart_read_bytes(
            UART_LL_NUM, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(5));

        if (len > 0) {
            auto messages = state.protocol.feed(rx_buf, static_cast<size_t>(len));
            for (const auto& msg : messages) {
                state.rx_count++;
                process_message(msg, state);
            }
        }

        // Keepalive timer (every SA_KEEPALIVE_TICK_MS)
        uint32_t now = xTaskGetTickCount();
        if ((now - last_keepalive) * portTICK_PERIOD_MS >= SA_KEEPALIVE_TICK_MS) {
            keepalive_tick(state);
            last_keepalive = now;
        }

        // Process motor commands from subscriber
        process_fault_clear(state);
        fault_ttl_tick(state);
        process_motor_commands(state);
        cut_motor_refresh_tick(state);

        // Push periodic aggregate snapshots
        status_snapshots_tick(state);

        // Detect LL Board silence (cable cut, MCU crash)
        {
            uint32_t last = g_health.ecovacs_last_tick.load();
            if (last > 0) {
                uint32_t silence = (xTaskGetTickCount() - last) * portTICK_PERIOD_MS;
                if (silence >= ECOVACS_DATA_TIMEOUT_MS &&
                    g_health.ecovacs_state.load() == PeripheralState::RUNNING) {
                    ESP_LOGW(TAG, "No data from LL Board for %lu ms",
                             (unsigned long)silence);
                    g_health.ecovacs_state.store(PeripheralState::DATA_TIMEOUT);
                }
            }
        }
    }
}

#endif // ENABLE_ECOVACS
