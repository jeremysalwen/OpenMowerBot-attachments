/**
 * @file sa_protocol.cpp
 * @brief SA Protocol implementation
 */

#include "sa_protocol.h"

#include <algorithm>
#include <stdexcept>

#include "esp_random.h"

namespace ecovacs {

// =============================================================================
// CRC-8 Implementation
// =============================================================================

CRC8::CRC8() {
    // Initialize CRC-8/SMBUS table (polynomial 0x07)
    for (int i = 0; i < 256; ++i) {
        uint8_t crc = static_cast<uint8_t>(i);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x80) {
                crc = ((crc << 1) ^ 0x07) & 0xFF;
            } else {
                crc = (crc << 1) & 0xFF;
            }
        }
        table_[i] = crc;
    }
}

uint8_t CRC8::calculate(const uint8_t* data, size_t len, uint8_t init) const {
    uint8_t crc = init;
    for (size_t i = 0; i < len; ++i) {
        crc = table_[crc ^ data[i]];
    }
    return crc;
}

uint8_t CRC8::calculate(const std::vector<uint8_t>& data, uint8_t init) const {
    return calculate(data.data(), data.size(), init);
}

// =============================================================================
// Command Builder
// =============================================================================

std::vector<uint8_t> CommandBuilder::motor_control(uint8_t motor_id, uint8_t direction, uint16_t speed) {
    return {
        motor_id,
        direction,
        static_cast<uint8_t>(speed & 0xFF),
        static_cast<uint8_t>((speed >> 8) & 0xFF)
    };
}

std::vector<uint8_t> CommandBuilder::motor_control(const std::vector<MotorCommand>& commands) {
    std::vector<uint8_t> payload;
    payload.reserve(commands.size() * 4);

    for (const auto& cmd : commands) {
        payload.push_back(cmd.motor_id);
        payload.push_back(cmd.direction);
        payload.push_back(static_cast<uint8_t>(cmd.speed & 0xFF));
        payload.push_back(static_cast<uint8_t>((cmd.speed >> 8) & 0xFF));
    }

    return payload;
}

std::vector<uint8_t> CommandBuilder::motor_control_signed(uint8_t motor_id, int16_t speed) {
    uint8_t direction;
    uint16_t abs_speed;

    if (speed > 0) {
        direction = DIR_FORWARD;
        abs_speed = static_cast<uint16_t>(speed);
    } else if (speed < 0) {
        direction = DIR_REVERSE;
        abs_speed = static_cast<uint16_t>(-speed);
    } else {
        direction = DIR_STOP;
        abs_speed = 0;
    }

    return motor_control(motor_id, direction, abs_speed);
}

std::vector<uint8_t> CommandBuilder::stop_all_motors() {
    std::vector<uint8_t> payload;

    // Auxiliary motors 1-8 (brush, vacuum, etc.) - standard MA format
    for (uint8_t i = 1; i <= 8; ++i) {
        auto cmd = motor_control(i, DIR_STOP, 0);
        payload.insert(payload.end(), cmd.begin(), cmd.end());
    }

    // Cut motor (10) - SPECIAL FORMAT: use cut_motor_speed(0) NOT motor_control!
    auto cut_stop = cut_motor_speed(0);  // 0a 06 00 00
    payload.insert(payload.end(), cut_stop.begin(), cut_stop.end());

    // Lens motor (11) - standard MA format
    auto lens_stop = motor_control(MOTOR_LENS, DIR_STOP, 0);
    payload.insert(payload.end(), lens_stop.begin(), lens_stop.end());

    return payload;
}

std::vector<uint8_t> CommandBuilder::cut_motor(bool on) {
    // Cut motor MA format:
    //   0a 06 25 00 = Cut motor ON  (ID=10, Cmd=0x06, Speed=37)
    //   0a 06 00 00 = Cut motor OFF (ID=10, Cmd=0x06, Speed=0)
    // Byte 1 holds the Command (0x06), not a direction code.
    return {
        MOTOR_CUT,                                         // Motor ID = 10 (mower blade)
        CUT_MOTOR_CMD,                                     // Command = 0x06 (constant)
        static_cast<uint8_t>(on ? CUT_MOTOR_SPEED_ON : 0), // Speed LSB (37 or 0)
        0x00                                               // Speed MSB
    };
}

std::vector<uint8_t> CommandBuilder::cut_motor_speed(uint16_t speed) {
    return {
        MOTOR_CUT,
        CUT_MOTOR_CMD,
        static_cast<uint8_t>(speed & 0xFF),
        static_cast<uint8_t>((speed >> 8) & 0xFF)
    };
}

std::vector<uint8_t> CommandBuilder::wheel_action(int32_t left_speed, int32_t right_speed) {
    // WA payload is 19 bytes:
    // [0]     Status/Reserved (0x00)
    // [1]     Left direction (0=forward, 1=reverse, 2=stop)
    // [2-3]   Left speed (uint16_le)
    // [4-9]   Left padding (6 bytes, always 0)
    // [10]    Right direction
    // [11-12] Right speed (uint16_le)
    // [13-18] Right padding (6 bytes, always 0)
    //
    // Direction values (WA uses different encoding than MA!):
    //   0x00 = Forward (move)
    //   0x01 = Reverse
    //   0x02 = Stop

    std::vector<uint8_t> payload(19, 0);

    // Left wheel
    uint8_t left_dir;
    uint16_t left_abs;
    if (left_speed > 0) {
        left_dir = WHEEL_DIR_FORWARD;  // 0x00
        left_abs = static_cast<uint16_t>(std::min(left_speed, static_cast<int32_t>(0xFFFF)));
    } else if (left_speed < 0) {
        left_dir = WHEEL_DIR_REVERSE;  // 0x01
        left_abs = static_cast<uint16_t>(std::min(-left_speed, static_cast<int32_t>(0xFFFF)));
    } else {
        left_dir = WHEEL_DIR_STOP;     // 0x02
        left_abs = 0;
    }

    // Right wheel
    uint8_t right_dir;
    uint16_t right_abs;
    if (right_speed > 0) {
        right_dir = WHEEL_DIR_FORWARD;  // 0x00
        right_abs = static_cast<uint16_t>(std::min(right_speed, static_cast<int32_t>(0xFFFF)));
    } else if (right_speed < 0) {
        right_dir = WHEEL_DIR_REVERSE;  // 0x01
        right_abs = static_cast<uint16_t>(std::min(-right_speed, static_cast<int32_t>(0xFFFF)));
    } else {
        right_dir = WHEEL_DIR_STOP;     // 0x02
        right_abs = 0;
    }

    // Build payload (19 bytes total)
    payload[0] = 0x00;  // Status/Reserved
    payload[1] = left_dir;
    payload[2] = static_cast<uint8_t>(left_abs & 0xFF);         // Left speed LSB
    payload[3] = static_cast<uint8_t>((left_abs >> 8) & 0xFF);  // Left speed MSB
    // [4-9] = 6 bytes padding (already 0)
    payload[10] = right_dir;
    payload[11] = static_cast<uint8_t>(right_abs & 0xFF);        // Right speed LSB
    payload[12] = static_cast<uint8_t>((right_abs >> 8) & 0xFF); // Right speed MSB
    // [13-18] = 6 bytes padding (already 0)

    return payload;
}

std::vector<uint8_t> CommandBuilder::stop_wheels() {
    return wheel_action(0, 0);
}

std::vector<uint8_t> CommandBuilder::stop_all() {
    // Note: This returns WA payload only
    // Caller should send both WA and MA stop commands
    return stop_wheels();
}

std::vector<uint8_t> CommandBuilder::heartbeat(uint16_t timeout_count) {
    return {
        static_cast<uint8_t>(timeout_count & 0xFF),
        static_cast<uint8_t>((timeout_count >> 8) & 0xFF)
    };
}

std::vector<uint8_t> CommandBuilder::clock_sync(uint32_t timestamp_ms) {
    return {
        static_cast<uint8_t>(timestamp_ms & 0xFF),
        static_cast<uint8_t>((timestamp_ms >> 8) & 0xFF),
        static_cast<uint8_t>((timestamp_ms >> 16) & 0xFF),
        static_cast<uint8_t>((timestamp_ms >> 24) & 0xFF)
    };
}

std::vector<uint8_t> CommandBuilder::gyro_calibration(bool save_to_flash) {
    // GC (Gyro Calibration) payload: 2 bytes
    //   Byte 0: Reserved (always 0x00)
    //   Byte 1: Mode
    //     0x00 = Start calibration, NO flash save (OEM default)
    //     0x01 = Set calibration flag only
    //     0x02 = Start calibration AND save to flash (persistent!)
    //
    // OEM sends: 60 53 41 00 XX XX XX XX 47 43 00 00 YY 0A (mode=0)
    // For persistent fix, use mode=2 (saves calibration data to flash)
    uint8_t mode = save_to_flash ? GYRO_CAL_MODE_START_AND_SAVE : GYRO_CAL_MODE_START_NO_SAVE;
    return {0x00, mode};
}

// =============================================================================
// SA Protocol
// =============================================================================

SAProtocol::SAProtocol() {
    buffer_.reserve(1024);
}

void SAProtocol::reset() {
    buffer_.clear();
    in_frame_ = false;
    frame_start_idx_ = 0;
}

std::vector<uint8_t> SAProtocol::escape_bytes(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result;
    result.reserve(data.size() * 2);  // Worst case

    for (uint8_t b : data) {
        if (b == ESCAPE_CHAR) {  // Backslash
            result.push_back(ESCAPE_CHAR);
            result.push_back(ESC_BACKSLASH);
        } else if (b == FRAME_START) {  // Backtick
            result.push_back(ESCAPE_CHAR);
            result.push_back(ESC_BACKTICK);
        } else if (b == FRAME_END) {  // Newline
            result.push_back(ESCAPE_CHAR);
            result.push_back(ESC_NEWLINE);
        } else {
            result.push_back(b);
        }
    }

    return result;
}

std::vector<uint8_t> SAProtocol::unescape_bytes(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result;
    result.reserve(data.size());

    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] == ESCAPE_CHAR && i + 1 < data.size()) {
            uint8_t next = data[i + 1];
            if (next == ESC_BACKSLASH) {
                result.push_back(ESCAPE_CHAR);
                ++i;
                continue;
            } else if (next == ESC_BACKTICK) {
                result.push_back(FRAME_START);
                ++i;
                continue;
            } else if (next == ESC_NEWLINE) {
                result.push_back(FRAME_END);
                ++i;
                continue;
            }
        }
        result.push_back(data[i]);
    }

    return result;
}

std::vector<SAMessage> SAProtocol::feed(const uint8_t* data, size_t len) {
    std::vector<SAMessage> messages;

    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];

        if (b == FRAME_START) {
            // Start of new frame
            in_frame_ = true;
            frame_start_idx_ = buffer_.size();
            buffer_.push_back(b);
        } else if (b == FRAME_END && in_frame_) {
            // End of frame - try to parse
            if (frame_start_idx_ < buffer_.size()) {
                std::vector<uint8_t> frame_data(
                    buffer_.begin() + frame_start_idx_ + 1,  // Skip start byte
                    buffer_.end()
                );

                auto msg = parse_frame(frame_data);
                if (msg) {
                    messages.push_back(std::move(*msg));
                    stats_.frames_parsed++;
                } else {
                    stats_.frame_errors++;
                }
            }

            // Reset buffer
            buffer_.clear();
            in_frame_ = false;
            frame_start_idx_ = 0;
        } else if (in_frame_) {
            buffer_.push_back(b);
        }
    }

    // Prevent buffer from growing indefinitely
    if (buffer_.size() > 4096) {
        buffer_.clear();
        in_frame_ = false;
        frame_start_idx_ = 0;
    }

    return messages;
}

std::vector<SAMessage> SAProtocol::feed(const std::vector<uint8_t>& data) {
    return feed(data.data(), data.size());
}

std::optional<SAMessage> SAProtocol::parse_frame(const std::vector<uint8_t>& raw_data) {
    // Unescape the data
    auto data = unescape_bytes(raw_data);

    // Minimum length: SA(2) + type(1) + ts(4) + msgid(2) + crc(1) = 10
    if (data.size() < 10) {
        return std::nullopt;
    }

    // Check magic
    if (data[0] != MAGIC_S || data[1] != MAGIC_A) {
        return std::nullopt;
    }

    // Parse header
    uint8_t msg_type = data[2];
    uint32_t timestamp =
        static_cast<uint32_t>(data[3]) |
        (static_cast<uint32_t>(data[4]) << 8) |
        (static_cast<uint32_t>(data[5]) << 16) |
        (static_cast<uint32_t>(data[6]) << 24);

    std::string msg_id;
    msg_id += static_cast<char>(data[7]);
    msg_id += static_cast<char>(data[8]);

    // Extract payload (everything between msgid and crc)
    std::vector<uint8_t> payload(data.begin() + 9, data.end() - 1);

    // Verify CRC
    uint8_t received_crc = data.back();
    uint8_t calculated_crc = crc_.calculate(data.data(), data.size() - 1);

    if (received_crc != calculated_crc) {
        stats_.crc_errors++;
        return std::nullopt;
    }

    return SAMessage(msg_type, timestamp, msg_id, std::move(payload));
}

std::vector<uint8_t> SAProtocol::build_frame(const std::string& msg_id,
                                              const std::vector<uint8_t>& payload,
                                              uint8_t msg_type,
                                              uint32_t timestamp) {
    // If timestamp is 0, generate random one (like original firmware)
    if (timestamp == 0) {
        timestamp = esp_random();
    }

    // Build inner frame (before escaping)
    std::vector<uint8_t> inner;
    inner.reserve(16 + payload.size());

    // Type
    inner.push_back(msg_type);

    // Timestamp (little-endian)
    inner.push_back(static_cast<uint8_t>(timestamp & 0xFF));
    inner.push_back(static_cast<uint8_t>((timestamp >> 8) & 0xFF));
    inner.push_back(static_cast<uint8_t>((timestamp >> 16) & 0xFF));
    inner.push_back(static_cast<uint8_t>((timestamp >> 24) & 0xFF));

    // Message ID
    if (msg_id.size() >= 2) {
        inner.push_back(static_cast<uint8_t>(msg_id[0]));
        inner.push_back(static_cast<uint8_t>(msg_id[1]));
    } else {
        inner.push_back(0);
        inner.push_back(0);
    }

    // Payload
    inner.insert(inner.end(), payload.begin(), payload.end());

    // Calculate CRC over "SA" + inner
    std::vector<uint8_t> crc_data = {MAGIC_S, MAGIC_A};
    crc_data.insert(crc_data.end(), inner.begin(), inner.end());
    uint8_t checksum = crc_.calculate(crc_data);
    inner.push_back(checksum);

    // Escape the inner frame
    auto escaped = escape_bytes(inner);

    // Build final frame
    std::vector<uint8_t> frame;
    frame.reserve(4 + escaped.size());
    frame.push_back(FRAME_START);
    frame.push_back(MAGIC_S);
    frame.push_back(MAGIC_A);
    frame.insert(frame.end(), escaped.begin(), escaped.end());
    frame.push_back(FRAME_END);

    return frame;
}

// =============================================================================
// Payload Decoder
// =============================================================================

template<typename T>
T PayloadDecoder::read_le(const uint8_t* data) {
    T result = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        result |= static_cast<T>(data[i]) << (i * 8);
    }
    return result;
}

DecodedPayload PayloadDecoder::decode(const std::string& msg_id, const std::vector<uint8_t>& payload) {
    if (msg_id == "WD") return decode_wd(payload);
    if (msg_id == "WF") return decode_wf(payload);
    if (msg_id == "WH") return decode_wh(payload);
    if (msg_id == "GD") return decode_gd(payload);
    if (msg_id == "GI") return decode_gi(payload);
    if (msg_id == "GH") return decode_gh(payload);
    if (msg_id == "GF") return decode_gf(payload);
    if (msg_id == "GS") return decode_gs(payload);
    if (msg_id == "GB") return decode_gb(payload);
    if (msg_id == "CC") return decode_cc(payload);
    if (msg_id == "CO") return decode_co(payload);
    if (msg_id == "CA") return decode_ca(payload);
    if (msg_id == "CW") return decode_cw(payload);
    if (msg_id == "OR") return decode_or(payload);
    if (msg_id == "MB") return decode_mb(payload);
    if (msg_id == "MA") return decode_ma(payload);
    if (msg_id == "MD") return decode_md(payload);
    if (msg_id == "ME") return decode_me(payload);
    if (msg_id == "MS") return decode_ms(payload);
    if (msg_id == "MF") return decode_mf(payload);  // Motor Warning
    if (msg_id == "OD") return decode_od(payload);
    // BC messages: Decode as OnOffSensorInfo (sensor array format)
    // The original BatteryChargerStatus interpretation is kept for compatibility
    // but OnOffSensorInfo provides the proper sensor semantics
    if (msg_id == "BC") return decode_bc_as_onoff(payload);
    if (msg_id == "SE") return decode_se(payload);  // Robot Invalid State
    if (msg_id == "DF") return decode_df(payload);  // Full Bump
    if (msg_id == "HA") return decode_ha(payload);
    if (msg_id == "WA") return decode_wa(payload);

    // Unknown message type
    return RawPayload{payload};
}

WheelData PayloadDecoder::decode_wd(const std::vector<uint8_t>& payload) {
    WheelData data{};
    if (payload.size() >= 9) {
        data.status = payload[0];
        data.left_ticks = read_le<int32_t>(&payload[1]);
        data.right_ticks = read_le<int32_t>(&payload[5]);
    }
    return data;
}

WheelProtection PayloadDecoder::decode_wf(const std::vector<uint8_t>& payload) {
    WheelProtection data{};
    if (!payload.empty()) {
        data.triggered = (payload[0] == 1);
    }
    return data;
}

WheelHubTorque PayloadDecoder::decode_wh(const std::vector<uint8_t>& payload) {
    WheelHubTorque data{};
    if (payload.size() >= 2) {
        data.wheel_index = payload[0];
        data.torque_triggered = (payload[1] == 1);
    }
    return data;
}

GyroData PayloadDecoder::decode_gd(const std::vector<uint8_t>& payload) {
    GyroData data{};
    if (payload.size() >= 21) {
        data.status  = payload[0];                        // Byte 0: sub-type
        data.heading = read_le<int16_t>(&payload[1]);     // Bytes 1-2: AHRS yaw
        data.ahrs_x  = read_le<int16_t>(&payload[3]);     // Bytes 3-4: AHRS rotation X
        data.ahrs_y  = read_le<int16_t>(&payload[5]);     // Bytes 5-6: AHRS rotation Y
        data.ahrs_z  = read_le<int16_t>(&payload[7]);     // Bytes 7-8: AHRS rotation Z
        data.accel_x = read_le<int16_t>(&payload[9]);     // Bytes 9-10: accel X
        data.accel_y = read_le<int16_t>(&payload[11]);    // Bytes 11-12: accel Y
        data.accel_z = read_le<int16_t>(&payload[13]);    // Bytes 13-14: accel Z
        data.gyro_x  = read_le<int16_t>(&payload[15]);    // Bytes 15-16: gyro X rad/s
        data.gyro_y  = read_le<int16_t>(&payload[17]);    // Bytes 17-18: gyro Y rad/s
        data.gyro_z  = read_le<int16_t>(&payload[19]);    // Bytes 19-20: gyro Z rad/s
    }
    return data;
}

GeomagneticInfo PayloadDecoder::decode_gi(const std::vector<uint8_t>& payload) {
    GeomagneticInfo data{};
    if (payload.size() >= 6) {
        data.mag_x = read_le<int16_t>(&payload[0]);
        data.mag_y = read_le<int16_t>(&payload[2]);
        data.mag_z = read_le<int16_t>(&payload[4]);
    }
    return data;
}

GeomagneticHardware PayloadDecoder::decode_gh(const std::vector<uint8_t>& payload) {
    GeomagneticHardware data{};
    if (payload.size() >= 6) {
        data.status = payload[0];
        data.raw_compass = read_le<uint16_t>(&payload[1]);
        data.combined_compass = read_le<uint16_t>(&payload[3]);
        data.compass_level = payload[5];
    }
    return data;
}

GyroBiasFactory PayloadDecoder::decode_gf(const std::vector<uint8_t>& payload) {
    GyroBiasFactory data{};
    if (payload.size() >= 13) {
        data.status = payload[0];
        for (int i = 0; i < 6; ++i) {
            data.bias[i] = read_le<int16_t>(&payload[1 + i * 2]);
        }
    }
    return data;
}

GyroStatus PayloadDecoder::decode_gs(const std::vector<uint8_t>& payload) {
    GyroStatus data{};
    if (payload.size() >= 2) {
        data.ready = (payload[0] != 0);
        data.status = payload[1];
    }
    return data;
}

GyroBiasSingle PayloadDecoder::decode_gb(const std::vector<uint8_t>& payload) {
    GyroBiasSingle data{};
    if (payload.size() >= 3) {
        data.status = payload[0];
        data.bias = read_le<int16_t>(&payload[1]);
    }
    return data;
}

ChargeController PayloadDecoder::decode_cc(const std::vector<uint8_t>& payload) {
    ChargeController data{};
    if (payload.size() >= 7) {
        data.battery_percent = payload[0];
        data.charge_state = payload[1];
        // Raw voltage is in 10 mV units (verified against a multimeter:
        // raw 1890 = 18.9 V). Scale to mV here so all consumers agree.
        data.voltage_mv = read_le<uint16_t>(&payload[2]) * 10;
        data.current_ma = read_le<int16_t>(&payload[4]);
        data.temperature_c = static_cast<int8_t>(payload[6]);
    }
    return data;
}

ChargeOverview PayloadDecoder::decode_co(const std::vector<uint8_t>& payload) {
    // CO payload (9 bytes, little-endian), OEM "power/BatteryInfo":
    //   [0-1] uint16 chargeVoltage (mV, charge contact)
    //   [2-3] int16  batteryCurrent (mA, negative = discharging)
    //   [4-5] uint16 batteryVoltage (mV)
    //   [6]   uint8  chargeStatus (charge state machine phase 0-5)
    //   [7]   int8   batteryTemperature (deg C)
    //   [8]   uint8  chargeAlarm (0 = none, 1-5 = charge error codes)
    ChargeOverview data{};
    if (payload.size() >= 9) {
        data.contact_mv    = read_le<uint16_t>(&payload[0]);
        data.current_ma    = read_le<int16_t>(&payload[2]);
        data.battery_mv    = read_le<uint16_t>(&payload[4]);
        data.charge_status = payload[6];
        data.temperature_c = static_cast<int8_t>(payload[7]);
        data.charge_alarm  = payload[8];
    }
    return data;
}

ChargeAnalog PayloadDecoder::decode_ca(const std::vector<uint8_t>& payload) {
    // Not emitted by the GOAT/A1600RTK Main MCU; decoded for completeness.
    ChargeAnalog data{};
    if (payload.size() >= 5) {
        data.charger_contact = (payload[0] & 0x40) != 0;
        data.charger_type_3d = (payload[0] & 0x80) != 0;
        for (int i = 0; i < 4; ++i) {
            data.pins[i].level = payload[1 + i] & 0x07;
            data.pins[i].raw_value = (payload[1 + i] >> 3) & 0x07;
        }
    }
    return data;
}

ChargeWarning PayloadDecoder::decode_cw(const std::vector<uint8_t>& payload) {
    ChargeWarning data{};
    if (payload.size() >= 2) {
        data.warning_type1 = payload[0];
        data.warning_type2 = payload[1];
    }
    return data;
}

ReturnSignal PayloadDecoder::decode_or(const std::vector<uint8_t>& payload) {
    ReturnSignal data{};
    if (payload.size() >= 6) {
        data.status = payload[0];
        data.value1 = read_le<int16_t>(&payload[1]);
        data.flag = payload[3];
        data.value2 = read_le<int16_t>(&payload[4]);
    }
    return data;
}

MotorBattery PayloadDecoder::decode_mb(const std::vector<uint8_t>& payload) {
    MotorBattery data;

    // Type to index mapping
    static const std::array<uint8_t, 9> type_to_index = {0, 0, 1, 2, 3, 4, 5, 6, 11};

    for (size_t i = 0; i + 2 < payload.size(); i += 3) {
        MotorCurrent motor;
        motor.type = payload[i];
        motor.current_ma = read_le<uint16_t>(&payload[i + 1]);
        motor.index = (motor.type < type_to_index.size()) ? type_to_index[motor.type] : motor.type;
        data.motors.push_back(motor);
    }

    return data;
}

MotorAcknowledge PayloadDecoder::decode_ma(const std::vector<uint8_t>& payload) {
    MotorAcknowledge data;

    for (size_t i = 0; i + 3 < payload.size(); i += 4) {
        MotorAckEntry entry;
        entry.motor_id = payload[i];
        entry.direction = payload[i + 1];
        entry.speed_lsb = payload[i + 2];
        entry.result = payload[i + 3];
        data.motors.push_back(entry);
    }

    return data;
}

MotorDone PayloadDecoder::decode_md(const std::vector<uint8_t>& payload) {
    MotorDone data{};
    if (payload.size() >= 2) {
        data.motor_index = payload[0];
        data.result = payload[1];
    }
    return data;
}

MotorError PayloadDecoder::decode_me(const std::vector<uint8_t>& payload) {
    MotorError data{};
    if (!payload.empty()) {
        data.raw_code = payload[0];
        // Error code mapping from firmware
        if (data.raw_code == 4) {
            data.mapped_code = 5;
        } else if (data.raw_code == 5) {
            data.mapped_code = 6;
        } else if (data.raw_code == 6) {
            data.mapped_code = 4;
        } else {
            data.mapped_code = data.raw_code;
        }
    }
    return data;
}

MotorSpeed PayloadDecoder::decode_ms(const std::vector<uint8_t>& payload) {
    MotorSpeed data{};
    if (payload.size() >= 3) {
        data.motor_index = payload[0];
        data.speed = read_le<int16_t>(&payload[1]);
    }
    return data;
}

OnOffDevice PayloadDecoder::decode_od(const std::vector<uint8_t>& payload) {
    OnOffDevice data;

    for (size_t i = 0; i + 1 < payload.size(); i += 2) {
        DeviceStatus status;
        status.type = payload[i];
        status.error_code = payload[i + 1];
        data.devices.push_back(status);
    }

    return data;
}

BatteryChargerStatus PayloadDecoder::decode_bc(const std::vector<uint8_t>& payload) {
    BatteryChargerStatus data;

    for (size_t i = 0; i + 1 < payload.size(); i += 2) {
        BatteryChargerEntry entry;
        entry.index = payload[i];
        entry.value = payload[i + 1];
        entry.is_imu_state = (entry.index == 10);
        if (entry.is_imu_state && entry.value == 0xFF) {
            entry.value = 2;
        }
        data.pairs.push_back(entry);
    }

    return data;
}

Heartbeat PayloadDecoder::decode_ha(const std::vector<uint8_t>& payload) {
    Heartbeat data{};
    if (payload.size() >= 2) {
        data.timeout_count = read_le<uint16_t>(&payload[0]);
    }
    return data;
}

WheelActionAck PayloadDecoder::decode_wa(const std::vector<uint8_t>& payload) {
    // WA payload is 19 bytes:
    //   [0]     Status/Reserved (0x00)
    //   [1]     Left direction (0=FWD, 1=REV, 2=STOP)
    //   [2-3]   Left speed (uint16_le)
    //   [4-9]   Left padding (6 bytes, always 0)
    //   [10]    Right direction
    //   [11-12] Right speed (uint16_le)
    //   [13-18] Right padding (6 bytes, always 0)

    WheelActionAck data{};
    if (payload.size() >= 19) {
        data.status = payload[0];
        data.left_direction = payload[1];
        data.left_speed = read_le<uint16_t>(&payload[2]);   // Bytes 2-3
        // Bytes 4-9 are padding (ignored)
        data.right_direction = payload[10];
        data.right_speed = read_le<uint16_t>(&payload[11]); // Bytes 11-12
        // Bytes 13-18 are padding (ignored)
    }
    return data;
}

MotorWarning PayloadDecoder::decode_mf(const std::vector<uint8_t>& payload) {
    MotorWarning data{};
    if (!payload.empty()) {
        data.warning_code = payload[0];
    }
    return data;
}

// =============================================================================
// OnOffInfo Module Decoders (BC/SE/DF messages)
// =============================================================================

// OnOffSensorInfo convenience accessors
bool OnOffSensorInfo::get_bump_left() const {
    for (const auto& s : sensors) {
        if (s.type == ONOFF_TYPE_BUMP && s.value == BUMP_LEFT) return true;
    }
    return false;
}

bool OnOffSensorInfo::get_bump_right() const {
    for (const auto& s : sensors) {
        if (s.type == ONOFF_TYPE_BUMP && s.value == BUMP_RIGHT) return true;
    }
    return false;
}

bool OnOffSensorInfo::get_bump_lds() const {
    for (const auto& s : sensors) {
        if (s.type == ONOFF_TYPE_BUMP && s.value == BUMP_LDS) return true;
    }
    return false;
}

bool OnOffSensorInfo::get_fall_left() const {
    for (const auto& s : sensors) {
        if (s.type == ONOFF_TYPE_FALL && s.value == FALL_LEFT) return true;
    }
    return false;
}

bool OnOffSensorInfo::get_fall_right() const {
    for (const auto& s : sensors) {
        if (s.type == ONOFF_TYPE_FALL && s.value == FALL_RIGHT) return true;
    }
    return false;
}

bool OnOffSensorInfo::get_dirtbox_present() const {
    for (const auto& s : sensors) {
        if (s.type == ONOFF_TYPE_DIRTBOX && s.value != 0) return true;
    }
    return false;
}

bool OnOffSensorInfo::get_carpet_detected() const {
    for (const auto& s : sensors) {
        if (s.type == ONOFF_TYPE_CARPET && s.value != 0) return true;
    }
    return false;
}

bool OnOffSensorInfo::get_mop_attached() const {
    for (const auto& s : sensors) {
        if (s.type == ONOFF_TYPE_MOP && s.value != 0) return true;
    }
    return false;
}

OnOffSensorInfo PayloadDecoder::decode_bc_as_onoff(const std::vector<uint8_t>& payload) {
    OnOffSensorInfo data;

    // BC Message Interpretation (verified via real firmware behavior):
    // Each 2-byte pair is (type, status) where:
    //   - type: Sensor category (0=BUMP, 1=DOWNIN, 2=FALL, etc.)
    //   - status: 0 = all sensors of this type are OK (not triggered)
    //             non-zero = a sensor is triggered, value indicates which sub-sensor
    //
    // Example BC payloads:
    //   00 00 02 00 = BUMP OK, FALL OK (all sensors normal)
    //   00 01 02 00 = BUMP_RIGHT triggered, FALL OK
    //   00 00 02 01 = BUMP OK, FALL_RIGHT triggered
    //
    // Parse (type, status) byte pairs from payload
    for (size_t i = 0; i + 1 < payload.size(); i += 2) {
        uint8_t type = payload[i];      // Sensor category (0=BUMP, 1=DOWNIN, etc.)
        uint8_t status = payload[i + 1]; // 0 = OK, non-zero = triggered sub-sensor

        // Special handling for index 14 (EStop)
        if (type == BC_INDEX_ESTOP) {
            data.estop_state = status;
            data.has_estop = true;
            continue;
        }

        // Special handling for index 10 (gyro calibration / sensor error state)
        // Matches OEM behavior: only value 0xFF signals a sensor malfunction
        // (gyro calibration data missing from flash). Any other value is OK.
        if (type == BC_INDEX_SENSOR_ERROR) {
            data.sensor_error = (status == 0xFF);
            continue;
        }

        // Only add to sensor array if status indicates a triggered sensor
        // status = 0 means all sensors of this type are OK (not triggered)
        // status > 0 means sensor at sub-index (status - 1) is triggered
        // status = 0xFF typically means "all OK" as well
        if (status != 0 && status != 0xFF) {
            // status is 1-based sub-sensor index (1 = first sub-sensor, etc.)
            // Convert to 0-based for consistency with BUMP_LEFT=0, etc.
            uint8_t sub_sensor = status - 1;
            data.sensors.push_back({type, sub_sensor});
        }
    }

    return data;
}

RobotInvalidState PayloadDecoder::decode_se(const std::vector<uint8_t>& payload) {
    RobotInvalidState data{};
    if (!payload.empty()) {
        data.is_not_on_plane = (payload[0] != 0);
    }
    return data;
}

FullBump PayloadDecoder::decode_df(const std::vector<uint8_t>& payload) {
    FullBump data{};
    data.raw_data = payload;

    if (payload.size() >= 3) {
        data.type = payload[0];
        // Angle is formed from bytes 1 and 2 (little-endian)
        data.angle = static_cast<uint16_t>(payload[1]) |
                     (static_cast<uint16_t>(payload[2]) << 8);
    }
    return data;
}

// =============================================================================
// SAMessage decode implementation
// =============================================================================

DecodedPayload SAMessage::decode() const {
    return PayloadDecoder::decode(msg_id_, payload_);
}

}  // namespace ecovacs
