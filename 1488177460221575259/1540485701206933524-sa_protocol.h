/**
 * @file sa_protocol.h
 * @brief SA Protocol -- Ecovacs LL Board UART communication
 *
 * Frame structure:
 *   TX/RX Frame: ` + SA + TYPE + TIMESTAMP + MSGID + PAYLOAD + CRC8 + \n
 *
 * Standalone library with no ROS or platform dependencies.
 */

#ifndef SA_PROTOCOL_H_
#define SA_PROTOCOL_H_

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <array>
#include <optional>
#include <variant>
#include <functional>
#include <cmath>

namespace ecovacs {

// =============================================================================
// Constants
// =============================================================================

constexpr uint8_t FRAME_START = 0x60;  // Backtick `
constexpr uint8_t FRAME_END = 0x0A;    // Newline \n
constexpr uint8_t MAGIC_S = 0x53;      // 'S'
constexpr uint8_t MAGIC_A = 0x41;      // 'A'
constexpr uint8_t MSG_TYPE_DATA = 0x02;
constexpr uint8_t MSG_TYPE_HEARTBEAT = 0x01;

constexpr uint8_t ESCAPE_CHAR = 0x5C;  // Backslash
constexpr uint8_t ESC_BACKSLASH = 0x01;
constexpr uint8_t ESC_BACKTICK = 0x02;
constexpr uint8_t ESC_NEWLINE = 0x03;

// Gyro conversion factor: radians = raw * PI / 18000.0
constexpr double GYRO_SCALE = M_PI / 18000.0;
constexpr int16_t GYRO_INVALID = 0x7FFF;

// Motor IDs.
// Note: the cut motor (mower blade) is ID 10 and uses a special 0x06 command
// format. The drive wheels are addressed via WA, not MA.
constexpr uint8_t MOTOR_CUT = 10;    // Mower blade motor (special 0x06 command format)
constexpr uint8_t MOTOR_LENS = 11;   // Camera lens motor
// Motors 1-8 are auxiliary motors (brush, vacuum, etc.) using the standard MA format.

// Motor directions (MA message - auxiliary motors)
constexpr uint8_t DIR_STOP = 0;
constexpr uint8_t DIR_FORWARD = 1;
constexpr uint8_t DIR_REVERSE = 2;

// Wheel directions (WA message - drive wheels)
constexpr uint8_t WHEEL_DIR_FORWARD = 0;
constexpr uint8_t WHEEL_DIR_REVERSE = 1;
constexpr uint8_t WHEEL_DIR_STOP = 2;

// Default heartbeat timeout (50 * 400ms = 20 seconds)
constexpr uint16_t DEFAULT_HEARTBEAT_TIMEOUT = 50;

// =============================================================================
// Decoded Payload Structures
// =============================================================================

/// Wheel Distance Data (WD) - 9 bytes
struct WheelData {
    uint8_t status;
    int32_t left_ticks;
    int32_t right_ticks;

    bool is_valid() const { return status == 0; }
};

/// Wheel Protection (WF) - 1 byte
struct WheelProtection {
    bool triggered;
};

/// Wheel Hub Torque (WH) - 2 bytes
struct WheelHubTorque {
    uint8_t wheel_index;  // 0=left, 1=right
    bool torque_triggered;
};

/// Wheel Action Acknowledge (WA response) - 19 bytes
/// Layout:
///   [0]     Status/Reserved (0x00)
///   [1]     Left direction (0=FWD, 1=REV, 2=STOP)
///   [2-3]   Left speed (uint16_le)
///   [4-9]   Left padding (6 bytes, always 0)
///   [10]    Right direction
///   [11-12] Right speed (uint16_le)
///   [13-18] Right padding (6 bytes, always 0)
struct WheelActionAck {
    uint8_t status;               // Byte 0: Status/Reserved
    uint8_t left_direction;       // Byte 1: 0=forward, 1=reverse, 2=stop
    uint16_t left_speed;          // Bytes 2-3: Speed (uint16_le)
    // Bytes 4-9: padding (not stored)
    uint8_t right_direction;      // Byte 10: 0=forward, 1=reverse, 2=stop
    uint16_t right_speed;         // Bytes 11-12: Speed (uint16_le)
    // Bytes 13-18: padding (not stored)

    bool left_is_forward() const { return left_direction == WHEEL_DIR_FORWARD; }
    bool left_is_reverse() const { return left_direction == WHEEL_DIR_REVERSE; }
    bool left_is_stopped() const { return left_direction == WHEEL_DIR_STOP; }
    bool right_is_forward() const { return right_direction == WHEEL_DIR_FORWARD; }
    bool right_is_reverse() const { return right_direction == WHEEL_DIR_REVERSE; }
    bool right_is_stopped() const { return right_direction == WHEEL_DIR_STOP; }
};

/// Gyro/IMU Data (GD) - 21 bytes
/// CORRECTED layout from Main MCU firmware analysis (sub_800B952):
///   Byte 0:     Sub-type (always 0x02)
///   Bytes 1-2:  Heading / yaw angle (AHRS fusion output)
///   Bytes 3-8:  AHRS rotation X/Y/Z (9-DOF fusion: IMU + magnetometer, * 100)
///   Bytes 9-14: Accelerometer X/Y/Z (IIM-42652, bias-compensated, * 100)
///   Bytes 15-20: Gyroscope X/Y/Z (IIM-42652, rad/s, bias-compensated, * 100)
struct GyroData {
    uint8_t status;          // Byte 0: sub-type (0x02)
    int16_t heading;         // Bytes 1-2: yaw angle from AHRS fusion

    // AHRS 9-DOF fusion output (IIM-42652 gyro+accel + QMC5883P magnetometer)
    int16_t ahrs_x;          // Bytes 3-4: fused rotation X (* 100)
    int16_t ahrs_y;          // Bytes 5-6: fused rotation Y (* 100)
    int16_t ahrs_z;          // Bytes 7-8: fused rotation Z (* 100)

    // IIM-42652 accelerometer (bias-compensated, scaled, low-pass filtered)
    int16_t accel_x;         // Bytes 9-10: accel X (* 100)
    int16_t accel_y;         // Bytes 11-12: accel Y (* 100)
    int16_t accel_z;         // Bytes 13-14: accel Z (* 100)

    // IIM-42652 gyroscope (bias-compensated, converted to rad/s, low-pass filtered)
    int16_t gyro_x;          // Bytes 15-16: gyro X in rad/s (* 100)
    int16_t gyro_y;          // Bytes 17-18: gyro Y in rad/s (* 100)
    int16_t gyro_z;          // Bytes 19-20: gyro Z in rad/s (* 100)

    bool is_valid() const { return heading != GYRO_INVALID; }

    // Gyroscope angular velocity in rad/s (divide by 100 to undo MCU scaling)
    double gyro_x_rad() const { return gyro_x / 100.0; }
    double gyro_y_rad() const { return gyro_y / 100.0; }
    double gyro_z_rad() const { return gyro_z / 100.0; }

    // Accelerometer in m/s^2 (divide by 100 to undo MCU scaling)
    // The MCU's imu_accel_bias_compensate already outputs in m/s^2, then
    // multiplies by 100 for int16 transmission. No additional gravity
    // conversion needed.
    double accel_x_ms2() const { return accel_x / 100.0; }
    double accel_y_ms2() const { return accel_y / 100.0; }
    double accel_z_ms2() const { return accel_z / 100.0; }

    // AHRS fusion angles (divide by 100 for degrees)
    double ahrs_x_deg() const { return ahrs_x / 100.0; }
    double ahrs_y_deg() const { return ahrs_y / 100.0; }
    double ahrs_z_deg() const { return ahrs_z / 100.0; }
    double heading_deg() const { return heading / 100.0; }
};

/// Geomagnetic Info (GI) - 6 bytes
struct GeomagneticInfo {
    int16_t mag_x;
    int16_t mag_y;
    int16_t mag_z;
};

/// Geomagnetic Hardware Status (GH) - 6 bytes
struct GeomagneticHardware {
    uint8_t status;
    uint16_t raw_compass;
    uint16_t combined_compass;
    uint8_t compass_level;

    bool is_valid() const { return status == 0; }
};

/// Gyro Bias Factory (GF) - 13 bytes
struct GyroBiasFactory {
    uint8_t status;
    std::array<int16_t, 6> bias;

    bool is_valid() const { return status == 0; }
};

/// Gyro Status (GS) - 2 bytes
struct GyroStatus {
    bool ready;
    uint8_t status;
};

/// Gyro Bias Single (GB) - 3 bytes
struct GyroBiasSingle {
    uint8_t status;
    int16_t bias;

    bool is_valid() const { return status == 0 && bias != GYRO_INVALID; }
};

/// Charge Controller (CC) - 7 bytes
/// Edge-triggered ~50-80 ms after charger contact change (2x on connect,
/// 1x on disconnect) plus periodic updates.
struct ChargeController {
    uint8_t battery_percent;
    uint8_t charge_state;  // 0=not charging, 1=charging, 2=full,
                           // 3=charge error, 4=charge error (switched off)
    uint16_t voltage_mv;   // decoder scales from raw 10 mV units to mV
    int16_t current_ma;    // negative = discharging; raw scale unverified
    int8_t temperature_c;

    bool is_charging() const { return charge_state == 1; }
    bool is_charged() const { return charge_state == 2; }
};

/// Charge Overview (CO) - 9 bytes, MCU -> Host
/// Event-driven: the MCU sends CO on a charge contact voltage threshold
/// crossing (~20 ms after physical contact, before the ~30-60 ms debounce
/// that gates the CC charge_state), on battery SOC +1% steps while
/// charging, and on charge alarms. Earliest "contacts energized" signal.
struct ChargeOverview {
    uint16_t contact_mv;       // charge contact voltage in mV
    int16_t  current_ma;       // battery current in mA (negative = discharging)
    uint16_t battery_mv;       // battery voltage in mV
    uint8_t  charge_status;    // charge state machine phase 0-5 (NOT the CC
                               // charge_state enum and NOT battery percent):
                               // 0 pre-charge, 1 soft-start, 2 CC, 3 CV,
                               // 4 trickle, 5 maintenance
    int8_t   temperature_c;    // battery temperature in deg C
    uint8_t  charge_alarm;     // 0 = none, 1 soft-start fail, 2 CC fail,
                               // 3 current error, 4 voltage error at start,
                               // 5 voltage/current anomaly
};

/// Charge Analog (CA) - 5 bytes
/// NOT sent by the GOAT/A1600RTK Main MCU firmware (no CA builder exists in
/// mcu_gkr.bin); kept for other Ecovacs models. OEM host semantics:
/// byte[0] bit 6 = charger contact, bit 7 = 3D charger type.
struct ChargeAnalog {
    bool charger_contact;   // byte[0] bit 6
    bool charger_type_3d;   // byte[0] bit 7
    struct Pin {
        uint8_t level;
        uint8_t raw_value;
    };
    std::array<Pin, 4> pins;
};

/// Charge Warning (CW) - 2 bytes
struct ChargeWarning {
    uint8_t warning_type1;
    uint8_t warning_type2;
};

/// Return Signal (OR) - 6 bytes
struct ReturnSignal {
    uint8_t status;
    int16_t value1;
    uint8_t flag;
    int16_t value2;
};

/// Motor Current Entry
struct MotorCurrent {
    uint8_t type;
    uint8_t index;
    uint16_t current_ma;
};

/// Motor Battery/Current (MB) - 9 bytes (3x3 blocks)
struct MotorBattery {
    std::vector<MotorCurrent> motors;
};

/// Motor Acknowledge Entry
struct MotorAckEntry {
    uint8_t motor_id;
    uint8_t direction;
    uint8_t speed_lsb;
    uint8_t result;
};

/// Motor Acknowledge (MA response) - variable
struct MotorAcknowledge {
    std::vector<MotorAckEntry> motors;
};

/// Motor Done (MD) - 2 bytes
struct MotorDone {
    uint8_t motor_index;
    uint8_t result;  // 0=unknown, 1=success, 2=failure
};

/// Motor Error (ME) - 1 byte
struct MotorError {
    uint8_t raw_code;
    uint8_t mapped_code;
};

/// Motor Speed (MS) - 3 bytes
struct MotorSpeed {
    uint8_t motor_index;
    int16_t speed;
};

/// On/Off Device Entry
struct DeviceStatus {
    uint8_t type;  // 0=ultrasonic, 1=gyro
    uint8_t error_code;
};

/// On/Off Device (OD) - variable
struct OnOffDevice {
    std::vector<DeviceStatus> devices;
};

/// Battery Charger Entry
struct BatteryChargerEntry {
    uint8_t index;
    uint8_t value;
    bool is_imu_state;
};

/// Battery Charger Status (BC) - variable
struct BatteryChargerStatus {
    std::vector<BatteryChargerEntry> pairs;
};

/// Heartbeat (HA) - 2 bytes
struct Heartbeat {
    uint16_t timeout_count;
    double timeout_seconds() const { return timeout_count * 0.4; }
};

/// Wheel Action Command (WA TX) - for building commands
struct WheelActionCommand {
    int32_t left_speed;   // positive=forward, negative=reverse, 0=stop
    int32_t right_speed;  // positive=forward, negative=reverse, 0=stop
};

/// Motor Warning (MF) - 1 byte
struct MotorWarning {
    uint8_t warning_code;
};

// =============================================================================
// OnOffInfo Module Structures (BC/SE/DF messages)
// =============================================================================

/// OnOff sensor types (from common::OnOffInfo)
constexpr uint8_t ONOFF_TYPE_BUMP = 0;
constexpr uint8_t ONOFF_TYPE_DOWNIN = 1;
constexpr uint8_t ONOFF_TYPE_FALL = 2;
constexpr uint8_t ONOFF_TYPE_DIRTBOX = 3;
constexpr uint8_t ONOFF_TYPE_CARPET = 4;
constexpr uint8_t ONOFF_TYPE_MOP = 5;

/// Bump sub-types
constexpr uint8_t BUMP_LEFT = 0;
constexpr uint8_t BUMP_RIGHT = 1;
constexpr uint8_t BUMP_LDS = 2;

/// Fall sub-types (wheel drop / cliff)
constexpr uint8_t FALL_LEFT = 0;
constexpr uint8_t FALL_RIGHT = 1;

/// DownIn sub-types (robot lifted positions)
constexpr uint8_t DOWNIN_LEFT = 0;
constexpr uint8_t DOWNIN_FRONT = 1;
constexpr uint8_t DOWNIN_FRONT_RIGHT = 2;
constexpr uint8_t DOWNIN_RIGHT = 3;
constexpr uint8_t DOWNIN_LEFT_BACK = 4;
constexpr uint8_t DOWNIN_RIGHT_BACK = 5;

/// Special BC message indices
constexpr uint8_t BC_INDEX_SENSOR_ERROR = 10;   // Value 0xFF = sensor malfunction
constexpr uint8_t BC_INDEX_ESTOP = 14;          // Emergency stop state

/// Single OnOff sensor value entry
struct OnOffSensorEntry {
    uint8_t type;
    uint8_t value;
};

/// OnOff Sensor Info (BC message parsed as sensor array)
/// This is the OnOffInfo sensor interpretation of BC messages
struct OnOffSensorInfo {
    std::vector<OnOffSensorEntry> sensors;
    uint8_t estop_state = 0;        // Index 14 value (0 = normal)
    bool sensor_error = false;       // Index 10 = 0xFF detected
    bool has_estop = false;          // True if estop was in the message

    // Convenience accessors for common sensors
    bool get_bump_left() const;
    bool get_bump_right() const;
    bool get_bump_lds() const;
    bool get_fall_left() const;
    bool get_fall_right() const;
    bool get_dirtbox_present() const;
    bool get_carpet_detected() const;
    bool get_mop_attached() const;
};

/// Robot Invalid State (SE message) - 1 byte
/// Indicates if robot is not on a level plane (lifted/tilted)
struct RobotInvalidState {
    bool is_not_on_plane;  // True = robot is lifted or tilted
};

/// Full Bump (DF message) - 3 bytes
/// Provides detailed collision info with angle
struct FullBump {
    uint8_t type;           // Zone/type indicator
    uint16_t angle;         // Collision angle
    std::vector<uint8_t> raw_data;  // Original payload
};

/// Unknown/Raw payload
struct RawPayload {
    std::vector<uint8_t> data;
};

// Variant type for all decoded payloads
using DecodedPayload = std::variant<
    WheelData,
    WheelProtection,
    WheelHubTorque,
    WheelActionAck,
    GyroData,
    GeomagneticInfo,
    GeomagneticHardware,
    GyroBiasFactory,
    GyroStatus,
    GyroBiasSingle,
    ChargeController,
    ChargeOverview,
    ChargeAnalog,
    ChargeWarning,
    ReturnSignal,
    MotorBattery,
    MotorAcknowledge,
    MotorDone,
    MotorError,
    MotorSpeed,
    MotorWarning,
    OnOffDevice,
    BatteryChargerStatus,
    OnOffSensorInfo,
    RobotInvalidState,
    FullBump,
    Heartbeat,
    RawPayload
>;

// =============================================================================
// SA Message
// =============================================================================

/// Represents a parsed SA protocol message
class SAMessage {
public:
    SAMessage() = default;
    SAMessage(uint8_t type, uint32_t ts, const std::string& id, std::vector<uint8_t> payload)
        : msg_type_(type), timestamp_(ts), msg_id_(id), payload_(std::move(payload)) {}

    uint8_t type() const { return msg_type_; }
    uint32_t timestamp() const { return timestamp_; }
    const std::string& msg_id() const { return msg_id_; }
    const std::vector<uint8_t>& payload() const { return payload_; }

    /// Decode the payload based on message ID
    DecodedPayload decode() const;

    /// Check if this is a specific message type
    bool is_wheel_data() const { return msg_id_ == "WD"; }
    bool is_wheel_action() const { return msg_id_ == "WA"; }
    bool is_gyro_data() const { return msg_id_ == "GD"; }
    bool is_charge_controller() const { return msg_id_ == "CC"; }
    bool is_motor_ack() const { return msg_id_ == "MA"; }
    bool is_heartbeat() const { return msg_id_ == "HA"; }

private:
    uint8_t msg_type_ = MSG_TYPE_DATA;
    uint32_t timestamp_ = 0;
    std::string msg_id_;
    std::vector<uint8_t> payload_;
};

// =============================================================================
// CRC-8 Implementation
// =============================================================================

/// CRC-8/SMBUS calculator (polynomial 0x07)
class CRC8 {
public:
    CRC8();

    /// Calculate CRC over data
    uint8_t calculate(const uint8_t* data, size_t len, uint8_t init = 0) const;
    uint8_t calculate(const std::vector<uint8_t>& data, uint8_t init = 0) const;

private:
    std::array<uint8_t, 256> table_;
};

// =============================================================================
// Command Builder
// =============================================================================

/// Builds payloads for TX commands
class CommandBuilder {
public:
    /// Build MA (Motor Action) payload
    /// @param motor_id Motor ID (1-8, 10, 11)
    /// @param direction DIR_STOP, DIR_FORWARD, DIR_REVERSE
    /// @param speed Speed value (0-65535)
    static std::vector<uint8_t> motor_control(uint8_t motor_id, uint8_t direction, uint16_t speed);

    /// Build MA payload for multiple motors
    struct MotorCommand {
        uint8_t motor_id;
        uint8_t direction;
        uint16_t speed;
    };
    static std::vector<uint8_t> motor_control(const std::vector<MotorCommand>& commands);

    /// Build MA payload with speed sign determining direction
    /// Positive = forward, negative = reverse, zero = stop
    static std::vector<uint8_t> motor_control_signed(uint8_t motor_id, int16_t speed);

    /// Build MA payload to stop all auxiliary motors
    static std::vector<uint8_t> stop_all_motors();

    // =========================================================================
    // Cut Motor Control (special MA format)
    // =========================================================================
    // The cut motor (mower blade, ID=10) uses a distinct MA layout:
    //   0a 06 25 00 = Cut motor ON  (ID=10, Cmd=0x06, Speed=37)
    //   0a 06 00 00 = Cut motor OFF (ID=10, Cmd=0x06, Speed=0)
    // Byte 1 holds the "Command" (always 0x06) instead of a direction.
    // The cutting mode (GC) must be enabled before any cut motor command
    // is accepted by the MCU.

    /// Cut motor command value (constant 0x06 for mower blade)
    static constexpr uint8_t CUT_MOTOR_CMD = 0x06;

    /// Default cut motor speed when ON (37 = 0x25)
    static constexpr uint16_t CUT_MOTOR_SPEED_ON = 37;

    /// NOTE: there is no "grass cutting mode" command. The GC message is Gyro
    /// Calibration; the only prerequisite the cut motor has is the single GC
    /// of the boot sequence. See gyro_calibration().

    /// Build MA payload for Cut Motor control
    /// @param on True = start cutting, False = stop cutting
    static std::vector<uint8_t> cut_motor(bool on);

    /// Build MA payload for Cut Motor with custom speed
    /// @param speed Speed value (37 = typical ON, 0 = OFF)
    static std::vector<uint8_t> cut_motor_speed(uint16_t speed);

    // =========================================================================
    // WA (Wheel Action) -- drive wheels
    // =========================================================================
    // WA addresses the left/right drive wheels. MA is reserved for
    // auxiliary motors (brush, vacuum, lens).

    /// Build WA (Wheel Action) payload for drive wheel control
    /// @param left_speed Left wheel speed (positive=forward, negative=reverse)
    /// @param right_speed Right wheel speed (positive=forward, negative=reverse)
    /// @return 19-byte WA payload
    static std::vector<uint8_t> wheel_action(int32_t left_speed, int32_t right_speed);

    /// Build WA payload to stop both drive wheels
    static std::vector<uint8_t> stop_wheels();

    /// Build payload to stop EVERYTHING (wheels + auxiliary motors)
    /// Combines WA stop + MA stop
    static std::vector<uint8_t> stop_all();

    /// Build HA (Heartbeat Alive) payload
    /// @param timeout_count Timeout in heartbeat cycles (default: 50 = ~20s)
    static std::vector<uint8_t> heartbeat(uint16_t timeout_count = DEFAULT_HEARTBEAT_TIMEOUT);

    /// Build UC (Clock Sync) payload
    static std::vector<uint8_t> clock_sync(uint32_t timestamp_ms);

    // =========================================================================
    // GC (Gyro Calibration) -- clears SENSOR_ERROR
    // =========================================================================
    // The MCU reports SENSOR_ERROR (BC message type=10, value=0xFF) when
    // gyro calibration data is missing from flash. GC clears that state.
    //
    // GC modes:
    //   Mode 0x00: Start calibration, no flash save -- clears the error
    //              for the current session only; it returns after reboot
    //   Mode 0x01: Set calibration flag only
    //   Mode 0x02: Start calibration and save to flash -- permanently
    //              clears the error (writes to MCU flash, use sparingly)

    /// Gyro calibration modes
    static constexpr uint8_t GYRO_CAL_MODE_START_NO_SAVE = 0x00;
    static constexpr uint8_t GYRO_CAL_MODE_FLAG_ONLY = 0x01;
    static constexpr uint8_t GYRO_CAL_MODE_START_AND_SAVE = 0x02; // persistent

    /// Build GC (Gyro Calibration) payload
    /// @param save_to_flash If false (default), use mode=0 (non-persistent).
    ///                      If true, use mode=2 (persist to flash, use with caution).
    /// @return 2-byte payload: [0x00, mode]
    static std::vector<uint8_t> gyro_calibration(bool save_to_flash = false);
};

// =============================================================================
// Protocol Parser/Encoder
// =============================================================================

/// SA Protocol parser and encoder
class SAProtocol {
public:
    SAProtocol();

    /// Feed incoming bytes and return parsed messages
    std::vector<SAMessage> feed(const uint8_t* data, size_t len);
    std::vector<SAMessage> feed(const std::vector<uint8_t>& data);

    /// Build a complete frame ready for transmission
    std::vector<uint8_t> build_frame(const std::string& msg_id,
                                      const std::vector<uint8_t>& payload,
                                      uint8_t msg_type = 0x00,
                                      uint32_t timestamp = 0);

    /// Reset parser state (e.g., after error)
    void reset();

    /// Get parser statistics
    struct Stats {
        uint64_t frames_parsed = 0;
        uint64_t crc_errors = 0;
        uint64_t frame_errors = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    /// Parse a raw frame (between ` and \n) into an SAMessage
    std::optional<SAMessage> parse_frame(const std::vector<uint8_t>& raw_data);

    /// Escape special bytes for transmission
    static std::vector<uint8_t> escape_bytes(const std::vector<uint8_t>& data);

    /// Unescape received bytes
    static std::vector<uint8_t> unescape_bytes(const std::vector<uint8_t>& data);

    CRC8 crc_;
    std::vector<uint8_t> buffer_;
    bool in_frame_ = false;
    size_t frame_start_idx_ = 0;
    Stats stats_;
};

// =============================================================================
// Payload Decoder
// =============================================================================

/// Decodes message payloads
class PayloadDecoder {
public:
    static DecodedPayload decode(const std::string& msg_id, const std::vector<uint8_t>& payload);

private:
    static WheelData decode_wd(const std::vector<uint8_t>& payload);
    static WheelProtection decode_wf(const std::vector<uint8_t>& payload);
    static WheelHubTorque decode_wh(const std::vector<uint8_t>& payload);
    static GyroData decode_gd(const std::vector<uint8_t>& payload);
    static GeomagneticInfo decode_gi(const std::vector<uint8_t>& payload);
    static GeomagneticHardware decode_gh(const std::vector<uint8_t>& payload);
    static GyroBiasFactory decode_gf(const std::vector<uint8_t>& payload);
    static GyroStatus decode_gs(const std::vector<uint8_t>& payload);
    static GyroBiasSingle decode_gb(const std::vector<uint8_t>& payload);
    static ChargeController decode_cc(const std::vector<uint8_t>& payload);
    static ChargeOverview decode_co(const std::vector<uint8_t>& payload);
    static ChargeAnalog decode_ca(const std::vector<uint8_t>& payload);
    static ChargeWarning decode_cw(const std::vector<uint8_t>& payload);
    static ReturnSignal decode_or(const std::vector<uint8_t>& payload);
    static MotorBattery decode_mb(const std::vector<uint8_t>& payload);
    static MotorAcknowledge decode_ma(const std::vector<uint8_t>& payload);
    static MotorDone decode_md(const std::vector<uint8_t>& payload);
    static MotorError decode_me(const std::vector<uint8_t>& payload);
    static MotorSpeed decode_ms(const std::vector<uint8_t>& payload);
    static MotorWarning decode_mf(const std::vector<uint8_t>& payload);
    static OnOffDevice decode_od(const std::vector<uint8_t>& payload);
    static BatteryChargerStatus decode_bc(const std::vector<uint8_t>& payload);
    static OnOffSensorInfo decode_bc_as_onoff(const std::vector<uint8_t>& payload);
    static RobotInvalidState decode_se(const std::vector<uint8_t>& payload);
    static FullBump decode_df(const std::vector<uint8_t>& payload);
    static Heartbeat decode_ha(const std::vector<uint8_t>& payload);
    static WheelActionAck decode_wa(const std::vector<uint8_t>& payload);

    // Helper to read little-endian values
    template<typename T>
    static T read_le(const uint8_t* data);
};

// =============================================================================
// Serial Port Abstraction (Optional)
// =============================================================================

/// Callback type for received messages
using MessageCallback = std::function<void(const SAMessage&)>;

}  // namespace ecovacs

#endif  // SA_PROTOCOL_H_
