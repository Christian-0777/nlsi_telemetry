#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_common_configs.h"
#include "common/scssdk_telemetry_common_gameplay_events.h"
#include "common/scssdk_telemetry_truck_common_channels.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"

namespace {

constexpr unsigned short kAgentPort = 28745;
constexpr auto kSampleInterval = std::chrono::milliseconds(250);

struct NumericField {
    double value = 0.0;
    bool available = false;
};

struct BooleanField {
    bool value = false;
    bool available = false;
};

struct PositionField {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double heading = 0.0;
    double pitch = 0.0;
    double roll = 0.0;
    bool available = false;
};

struct TelemetryState {
    NumericField speed;
    NumericField rpm;
    NumericField gear;
    NumericField steering;
    NumericField input_throttle;
    NumericField effective_throttle;
    NumericField throttle;
    NumericField input_brake;
    NumericField effective_brake;
    NumericField brake;
    NumericField retarder_level;
    NumericField cruise_control;
    NumericField fuel;
    NumericField fuel_range;
    NumericField fuel_consumption;
    NumericField odometer;
    NumericField navigation_distance;
    NumericField navigation_time;
    NumericField engine_wear;
    NumericField transmission_wear;
    NumericField cabin_wear;
    NumericField chassis_wear;
    NumericField wheels_wear;
    BooleanField fuel_warning;
    BooleanField adblue_warning;
    PositionField position;
} g_state;

SOCKET g_socket = INVALID_SOCKET;
sockaddr_in g_agent_address = {};
scs_log_t g_game_log = nullptr;
std::string g_game_id;
std::string g_game_name;
std::string g_game_version;
std::map<std::string, std::string> g_configurations;
bool g_is_driving = false;
bool g_initialized = false;
std::chrono::steady_clock::time_point g_last_sample;
std::chrono::steady_clock::time_point g_last_heartbeat;

std::string json_quote(const std::string &value)
{
    std::ostringstream out;
    out << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (character < 0x20) {
                out << "\\u"
                    << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned int>(character)
                    << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(character);
            }
        }
    }
    out << '"';
    return out.str();
}

std::string json_number(const double value)
{
    if (!std::isfinite(value)) {
        return "null";
    }
    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

std::string json_field(const NumericField &field)
{
    return field.available ? json_number(field.value) : "null";
}

std::string json_field(const BooleanField &field)
{
    if (!field.available) {
        return "null";
    }
    return field.value ? "true" : "false";
}

std::string utc_timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc = {};
    gmtime_s(&utc, &time);
    char date[24] = {};
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &utc);
    std::ostringstream out;
    out << date << '.' << std::setw(3) << std::setfill('0')
        << milliseconds.count() << 'Z';
    return out.str();
}

std::string game_json()
{
    return "{\"id\":" + json_quote(g_game_id)
        + ",\"name\":" + json_quote(g_game_name)
        + ",\"version\":" + json_quote(g_game_version)
        + ",\"telemetry_api_version\":\"1.01\"}";
}

void send_packet(const std::string &packet)
{
    if (g_socket == INVALID_SOCKET) {
        return;
    }
    const int result = sendto(
        g_socket,
        packet.data(),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr *>(&g_agent_address),
        sizeof(g_agent_address));
    if (result == SOCKET_ERROR && g_game_log) {
        g_game_log(SCS_LOG_TYPE_warning, "NLSI telemetry: local UDP send failed.");
    }
}

std::string configurations_json()
{
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto &configuration : g_configurations) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << json_quote(configuration.first) << ':' << configuration.second;
    }
    out << '}';
    return out.str();
}

std::string telemetry_packet()
{
    const auto &position = g_state.position;
    std::ostringstream out;
    out << "{\"type\":\"telemetry\",\"timestamp\":" << json_quote(utc_timestamp())
        << ",\"game\":" << game_json()
        << ",\"state\":" << json_quote(g_is_driving ? "driving" : "paused")
        << ",\"truck\":{"
        << "\"speed_mps\":" << json_field(g_state.speed)
        << ",\"speed_kmh\":";
    out << (g_state.speed.available ? json_number(g_state.speed.value * 3.6) : "null");
    out << ",\"rpm\":" << json_field(g_state.rpm)
        << ",\"gear\":" << json_field(g_state.gear)
        << ",\"steering\":" << json_field(g_state.steering)
        << ",\"input_throttle\":" << json_field(g_state.input_throttle)
        << ",\"effective_throttle\":" << json_field(g_state.effective_throttle)
        << ",\"throttle\":" << json_field(g_state.effective_throttle)
        << ",\"input_brake\":" << json_field(g_state.input_brake)
        << ",\"effective_brake\":" << json_field(g_state.effective_brake)
        << ",\"brake\":" << json_field(g_state.effective_brake)
        << ",\"retarder_level\":" << json_field(g_state.retarder_level)
        << ",\"retarder_active\":" << (g_state.retarder_level.available ? (g_state.retarder_level.value > 0.0 ? "true" : "false") : "null")
        << ",\"cruise_control_speed\":" << json_field(g_state.cruise_control)
        << ",\"cruise_control_active\":" << (g_state.cruise_control.available ? (g_state.cruise_control.value > 0.0 ? "true" : "false") : "null")
        << ",\"fuel_liters\":" << json_field(g_state.fuel)
        << ",\"fuel_range_km\":" << json_field(g_state.fuel_range)
        << ",\"fuel_consumption_l_per_km\":" << json_field(g_state.fuel_consumption)
        << ",\"fuel_warning\":" << json_field(g_state.fuel_warning)
        << ",\"odometer_km\":" << json_field(g_state.odometer)
        << ",\"navigation_distance_m\":" << json_field(g_state.navigation_distance)
        << ",\"navigation_time_s\":" << json_field(g_state.navigation_time)
        << ",\"wear\":{\"engine\":" << json_field(g_state.engine_wear)
        << ",\"transmission\":" << json_field(g_state.transmission_wear)
        << ",\"cabin\":" << json_field(g_state.cabin_wear)
        << ",\"chassis\":" << json_field(g_state.chassis_wear)
        << ",\"wheels\":" << json_field(g_state.wheels_wear)
        << "},\"adblue_warning\":" << json_field(g_state.adblue_warning)
        << "},\"position\":{\"x\":" << (position.available ? json_number(position.x) : "null")
        << ",\"y\":" << (position.available ? json_number(position.y) : "null")
        << ",\"z\":" << (position.available ? json_number(position.z) : "null")
        << ",\"heading_degrees\":" << (position.available ? json_number(position.heading) : "null")
        << ",\"pitch_degrees\":" << (position.available ? json_number(position.pitch) : "null")
        << ",\"roll_degrees\":" << (position.available ? json_number(position.roll) : "null")
        << "},\"configurations\":" << configurations_json() << '}';
    return out.str();
}

double value_as_number(const scs_value_t &value)
{
    switch (value.type) {
    case SCS_VALUE_TYPE_s32: return value.value_s32.value;
    case SCS_VALUE_TYPE_u32: return value.value_u32.value;
    case SCS_VALUE_TYPE_s64: return static_cast<double>(value.value_s64.value);
    case SCS_VALUE_TYPE_u64: return static_cast<double>(value.value_u64.value);
    case SCS_VALUE_TYPE_float: return value.value_float.value;
    case SCS_VALUE_TYPE_double: return value.value_double.value;
    default: return 0.0;
    }
}

std::string value_json(const scs_value_t &value)
{
    switch (value.type) {
    case SCS_VALUE_TYPE_bool:
        return value.value_bool.value ? "true" : "false";
    case SCS_VALUE_TYPE_s32:
        return std::to_string(value.value_s32.value);
    case SCS_VALUE_TYPE_u32:
        return std::to_string(value.value_u32.value);
    case SCS_VALUE_TYPE_s64:
        return std::to_string(value.value_s64.value);
    case SCS_VALUE_TYPE_u64:
        return std::to_string(value.value_u64.value);
    case SCS_VALUE_TYPE_float:
        return json_number(value.value_float.value);
    case SCS_VALUE_TYPE_double:
        return json_number(value.value_double.value);
    case SCS_VALUE_TYPE_string:
        return json_quote(value.value_string.value ? value.value_string.value : "");
    case SCS_VALUE_TYPE_fvector:
        return "[" + json_number(value.value_fvector.x) + ","
            + json_number(value.value_fvector.y) + ","
            + json_number(value.value_fvector.z) + "]";
    case SCS_VALUE_TYPE_dvector:
        return "[" + json_number(value.value_dvector.x) + ","
            + json_number(value.value_dvector.y) + ","
            + json_number(value.value_dvector.z) + "]";
    case SCS_VALUE_TYPE_euler:
        return "{\"heading\":" + json_number(value.value_euler.heading * 360.0)
            + ",\"pitch\":" + json_number(value.value_euler.pitch * 360.0)
            + ",\"roll\":" + json_number(value.value_euler.roll * 360.0) + "}";
    case SCS_VALUE_TYPE_fplacement:
        return "{\"position\":["
            + json_number(value.value_fplacement.position.x) + ","
            + json_number(value.value_fplacement.position.y) + ","
            + json_number(value.value_fplacement.position.z)
            + "],\"orientation\":{\"heading\":"
            + json_number(value.value_fplacement.orientation.heading * 360.0)
            + ",\"pitch\":" + json_number(value.value_fplacement.orientation.pitch * 360.0)
            + ",\"roll\":" + json_number(value.value_fplacement.orientation.roll * 360.0) + "}}";
    case SCS_VALUE_TYPE_dplacement:
        return "{\"position\":["
            + json_number(value.value_dplacement.position.x) + ","
            + json_number(value.value_dplacement.position.y) + ","
            + json_number(value.value_dplacement.position.z)
            + "],\"orientation\":{\"heading\":"
            + json_number(value.value_dplacement.orientation.heading * 360.0)
            + ",\"pitch\":" + json_number(value.value_dplacement.orientation.pitch * 360.0)
            + ",\"roll\":" + json_number(value.value_dplacement.orientation.roll * 360.0) + "}}";
    default:
        return "null";
    }
}

std::string attributes_json(const scs_named_value_t *attributes)
{
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const scs_named_value_t *attribute = attributes;
         attribute && attribute->name;
         ++attribute) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << json_quote(attribute->name) << ':' << value_json(attribute->value);
    }
    out << '}';
    return out.str();
}

SCSAPI_VOID numeric_callback(
    const scs_string_t,
    const scs_u32_t,
    const scs_value_t *const value,
    const scs_context_t context)
{
    auto *field = static_cast<NumericField *>(context);
    if (!value) {
        field->available = false;
        return;
    }
    field->value = value_as_number(*value);
    field->available = value->type == SCS_VALUE_TYPE_s32
        || value->type == SCS_VALUE_TYPE_u32
        || value->type == SCS_VALUE_TYPE_s64
        || value->type == SCS_VALUE_TYPE_u64
        || value->type == SCS_VALUE_TYPE_float
        || value->type == SCS_VALUE_TYPE_double;
}

SCSAPI_VOID boolean_callback(
    const scs_string_t,
    const scs_u32_t,
    const scs_value_t *const value,
    const scs_context_t context)
{
    auto *field = static_cast<BooleanField *>(context);
    if (!value || value->type != SCS_VALUE_TYPE_bool) {
        field->available = false;
        return;
    }
    field->value = value->value_bool.value != 0;
    field->available = true;
}

SCSAPI_VOID position_callback(
    const scs_string_t,
    const scs_u32_t,
    const scs_value_t *const value,
    const scs_context_t context)
{
    auto *field = static_cast<PositionField *>(context);
    if (!value || value->type != SCS_VALUE_TYPE_dplacement) {
        field->available = false;
        return;
    }
    field->x = value->value_dplacement.position.x;
    field->y = value->value_dplacement.position.y;
    field->z = value->value_dplacement.position.z;
    field->heading = value->value_dplacement.orientation.heading * 360.0;
    field->pitch = value->value_dplacement.orientation.pitch * 360.0;
    field->roll = value->value_dplacement.orientation.roll * 360.0;
    field->available = true;
}

void register_numeric(
    const scs_telemetry_register_for_channel_t register_channel,
    const scs_string_t name,
    const scs_value_type_t type,
    NumericField &field)
{
    const scs_result_t result = register_channel(
        name,
        SCS_U32_NIL,
        type,
        SCS_TELEMETRY_CHANNEL_FLAG_each_frame | SCS_TELEMETRY_CHANNEL_FLAG_no_value,
        numeric_callback,
        &field);
    if (result != SCS_RESULT_ok && g_game_log) {
        g_game_log(SCS_LOG_TYPE_warning, name);
    }
}

void register_boolean(
    const scs_telemetry_register_for_channel_t register_channel,
    const scs_string_t name,
    BooleanField &field)
{
    const scs_result_t result = register_channel(
        name,
        SCS_U32_NIL,
        SCS_VALUE_TYPE_bool,
        SCS_TELEMETRY_CHANNEL_FLAG_each_frame | SCS_TELEMETRY_CHANNEL_FLAG_no_value,
        boolean_callback,
        &field);
    if (result != SCS_RESULT_ok && g_game_log) {
        g_game_log(SCS_LOG_TYPE_warning, name);
    }
}

void register_configuration(
    const scs_telemetry_configuration_t &configuration)
{
    const std::string id = configuration.id ? configuration.id : "";
    const std::string attributes = attributes_json(configuration.attributes);
    g_configurations[id] = attributes;
    const std::string packet = "{\"type\":\"configuration\",\"timestamp\":"
        + json_quote(utc_timestamp()) + ",\"game\":" + game_json()
        + ",\"id\":" + json_quote(id) + ",\"attributes\":" + attributes + "}";
    send_packet(packet);
}

SCSAPI_VOID event_callback(
    const scs_event_t event,
    const void *const event_info,
    const scs_context_t)
{
    if (event == SCS_TELEMETRY_EVENT_configuration && event_info) {
        register_configuration(
            *static_cast<const scs_telemetry_configuration_t *>(event_info));
        return;
    }
    if (event == SCS_TELEMETRY_EVENT_gameplay && event_info) {
        const auto *info = static_cast<const scs_telemetry_gameplay_event_t *>(event_info);
        const std::string id = info->id ? info->id : "";
        send_packet("{\"type\":\"gameplay_event\",\"timestamp\":"
            + json_quote(utc_timestamp()) + ",\"game\":" + game_json()
            + ",\"event\":" + json_quote(id) + ",\"data\":"
            + attributes_json(info->attributes) + "}");
        return;
    }
    if (event == SCS_TELEMETRY_EVENT_started || event == SCS_TELEMETRY_EVENT_paused) {
        g_is_driving = event == SCS_TELEMETRY_EVENT_started;
        send_packet("{\"type\":\"lifecycle\",\"timestamp\":"
            + json_quote(utc_timestamp()) + ",\"game\":" + game_json()
            + ",\"state\":" + json_quote(g_is_driving ? "driving" : "paused") + "}");
        return;
    }
    if (event == SCS_TELEMETRY_EVENT_frame_start) {
        const auto now = std::chrono::steady_clock::now();
        if (g_last_heartbeat.time_since_epoch().count() == 0
            || now - g_last_heartbeat >= std::chrono::seconds(1)) {
            g_last_heartbeat = now;
            send_packet("{\"type\":\"plugin_heartbeat\",\"timestamp\":"
                + json_quote(utc_timestamp()) + ",\"game\":" + game_json() + "}");
        }
        return;
    }
    if (event == SCS_TELEMETRY_EVENT_frame_end) {
        const auto now = std::chrono::steady_clock::now();
        if (g_last_sample.time_since_epoch().count() == 0
            || now - g_last_sample >= kSampleInterval) {
            g_last_sample = now;
            send_packet(telemetry_packet());
        }
    }
}

} // namespace

SCSAPI_RESULT scs_telemetry_init(
    const scs_u32_t version,
    const scs_telemetry_init_params_t *const params)
{
    if (version != SCS_TELEMETRY_VERSION_1_01 || !params) {
        return SCS_RESULT_unsupported;
    }

    const auto *init = static_cast<const scs_telemetry_init_params_v101_t *>(params);
    g_game_log = init->common.log;
    const scs_string_t raw_id = init->common.game_id;
    const scs_string_t raw_name = init->common.game_name;
    if (!raw_id || !raw_name) {
        return SCS_RESULT_invalid_parameter;
    }
    if (std::string(raw_id) == SCS_GAME_ID_EUT2) {
        g_game_id = "ets2";
    } else if (std::string(raw_id) == SCS_GAME_ID_ATS) {
        g_game_id = "ats";
    } else {
        g_game_log(SCS_LOG_TYPE_error, "NLSI telemetry supports ETS2 and ATS only.");
        return SCS_RESULT_unsupported;
    }
    g_game_name = raw_name;
    std::ostringstream game_version;
    game_version << SCS_GET_MAJOR_VERSION(init->common.game_version)
        << '.' << SCS_GET_MINOR_VERSION(init->common.game_version);
    g_game_version = game_version.str();

    WSADATA winsock_data = {};
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        g_game_log(SCS_LOG_TYPE_error, "NLSI telemetry: Winsock initialization failed.");
        return SCS_RESULT_generic_error;
    }
    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_socket == INVALID_SOCKET) {
        WSACleanup();
        g_game_log(SCS_LOG_TYPE_error, "NLSI telemetry: local UDP socket creation failed.");
        return SCS_RESULT_generic_error;
    }
    g_agent_address.sin_family = AF_INET;
    g_agent_address.sin_port = htons(kAgentPort);
    if (InetPtonA(AF_INET, "127.0.0.1", &g_agent_address.sin_addr) != 1) {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
        WSACleanup();
        g_game_log(SCS_LOG_TYPE_error, "NLSI telemetry: loopback address setup failed.");
        return SCS_RESULT_generic_error;
    }

    const scs_event_t events[] = {
        SCS_TELEMETRY_EVENT_frame_start,
        SCS_TELEMETRY_EVENT_frame_end,
        SCS_TELEMETRY_EVENT_paused,
        SCS_TELEMETRY_EVENT_started,
        SCS_TELEMETRY_EVENT_configuration,
        SCS_TELEMETRY_EVENT_gameplay
    };
    for (const scs_event_t event : events) {
        if (init->register_for_event(event, event_callback, nullptr) != SCS_RESULT_ok) {
            closesocket(g_socket);
            g_socket = INVALID_SOCKET;
            WSACleanup();
            g_game_log(SCS_LOG_TYPE_error, "NLSI telemetry: event callback registration failed.");
            return SCS_RESULT_generic_error;
        }
    }

    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_speed,
        SCS_VALUE_TYPE_float, g_state.speed);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm,
        SCS_VALUE_TYPE_float, g_state.rpm);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_engine_gear,
        SCS_VALUE_TYPE_s32, g_state.gear);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_input_steering,
        SCS_VALUE_TYPE_float, g_state.steering);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_input_throttle,
        SCS_VALUE_TYPE_float, g_state.input_throttle);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle,
        SCS_VALUE_TYPE_float, g_state.effective_throttle);
    g_state.throttle = g_state.effective_throttle;
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_input_brake,
        SCS_VALUE_TYPE_float, g_state.input_brake);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_effective_brake,
        SCS_VALUE_TYPE_float, g_state.effective_brake);
    g_state.brake = g_state.effective_brake;
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_retarder_level,
        SCS_VALUE_TYPE_u32, g_state.retarder_level);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_cruise_control,
        SCS_VALUE_TYPE_float, g_state.cruise_control);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_fuel,
        SCS_VALUE_TYPE_float, g_state.fuel);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_fuel_range,
        SCS_VALUE_TYPE_float, g_state.fuel_range);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_fuel_average_consumption,
        SCS_VALUE_TYPE_float, g_state.fuel_consumption);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_odometer,
        SCS_VALUE_TYPE_float, g_state.odometer);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_navigation_distance,
        SCS_VALUE_TYPE_float, g_state.navigation_distance);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_navigation_time,
        SCS_VALUE_TYPE_float, g_state.navigation_time);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_wear_engine,
        SCS_VALUE_TYPE_float, g_state.engine_wear);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_wear_transmission,
        SCS_VALUE_TYPE_float, g_state.transmission_wear);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_wear_cabin,
        SCS_VALUE_TYPE_float, g_state.cabin_wear);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_wear_chassis,
        SCS_VALUE_TYPE_float, g_state.chassis_wear);
    register_numeric(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_wear_wheels,
        SCS_VALUE_TYPE_float, g_state.wheels_wear);
    register_boolean(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_fuel_warning,
        g_state.fuel_warning);
    register_boolean(init->register_for_channel, SCS_TELEMETRY_TRUCK_CHANNEL_adblue_warning,
        g_state.adblue_warning);

    const scs_result_t position_result = init->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_world_placement,
        SCS_U32_NIL,
        SCS_VALUE_TYPE_dplacement,
        SCS_TELEMETRY_CHANNEL_FLAG_each_frame | SCS_TELEMETRY_CHANNEL_FLAG_no_value,
        position_callback,
        &g_state.position);
    if (position_result != SCS_RESULT_ok) {
        g_game_log(SCS_LOG_TYPE_warning, SCS_TELEMETRY_TRUCK_CHANNEL_world_placement);
    }

    g_is_driving = false;
    g_last_sample = {};
    g_last_heartbeat = {};
    g_initialized = true;
    send_packet("{\"type\":\"plugin_init\",\"timestamp\":"
        + json_quote(utc_timestamp()) + ",\"game\":" + game_json() + "}");
    g_game_log(SCS_LOG_TYPE_message, "NLSI telemetry plugin initialized for local UDP agent.");
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown(void)
{
    if (!g_initialized) {
        return;
    }
    send_packet("{\"type\":\"plugin_shutdown\",\"timestamp\":"
        + json_quote(utc_timestamp()) + ",\"game\":" + game_json() + "}");
    if (g_socket != INVALID_SOCKET) {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }
    WSACleanup();
    g_game_log = nullptr;
    g_configurations.clear();
    g_initialized = false;
}
