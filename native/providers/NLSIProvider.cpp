#define WIN32_LEAN_AND_MEAN
#include "NLSIProvider.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct JsonValue {
    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Object,
        Array
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::map<std::string, JsonValue> object;
    std::vector<JsonValue> array;

    const JsonValue* Find(std::string_view key) const {
        if (type != Type::Object) {
            return nullptr;
        }
        const auto found = object.find(std::string(key));
        return found == object.end() ? nullptr : &found->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {
    }

    bool Parse(JsonValue& value, std::string& error) {
        SkipWhitespace();
        if (!ParseValue(value, 0)) {
            error = error_;
            return false;
        }
        SkipWhitespace();
        if (position_ != input_.size()) {
            error_ = "Unexpected characters after JSON value.";
            error = error_;
            return false;
        }
        return true;
    }

private:
    bool ParseValue(JsonValue& value, unsigned int depth) {
        if (depth > 64) {
            return Fail("JSON nesting exceeds the supported limit.");
        }
        SkipWhitespace();
        if (position_ >= input_.size()) {
            return Fail("Unexpected end of JSON input.");
        }
        switch (input_[position_]) {
        case '{': return ParseObject(value, depth + 1);
        case '[': return ParseArray(value, depth + 1);
        case '"':
            value.type = JsonValue::Type::String;
            return ParseString(value.string);
        case 't':
            if (Consume("true")) {
                value.type = JsonValue::Type::Boolean;
                value.boolean = true;
                return true;
            }
            return Fail("Invalid JSON token.");
        case 'f':
            if (Consume("false")) {
                value.type = JsonValue::Type::Boolean;
                value.boolean = false;
                return true;
            }
            return Fail("Invalid JSON token.");
        case 'n':
            if (Consume("null")) {
                value.type = JsonValue::Type::Null;
                return true;
            }
            return Fail("Invalid JSON token.");
        default:
            return ParseNumber(value);
        }
    }

    bool ParseObject(JsonValue& value, unsigned int depth) {
        value.type = JsonValue::Type::Object;
        ++position_;
        SkipWhitespace();
        if (Consume("}")) {
            return true;
        }
        while (position_ < input_.size()) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (!Consume(":")) {
                return Fail("Expected ':' after JSON object key.");
            }
            JsonValue member;
            if (!ParseValue(member, depth)) {
                return false;
            }
            value.object.insert_or_assign(std::move(key), std::move(member));
            SkipWhitespace();
            if (Consume("}")) {
                return true;
            }
            if (!Consume(",")) {
                return Fail("Expected ',' or '}' in JSON object.");
            }
        }
        return Fail("Unterminated JSON object.");
    }

    bool ParseArray(JsonValue& value, unsigned int depth) {
        value.type = JsonValue::Type::Array;
        ++position_;
        SkipWhitespace();
        if (Consume("]")) {
            return true;
        }
        while (position_ < input_.size()) {
            JsonValue element;
            if (!ParseValue(element, depth)) {
                return false;
            }
            value.array.push_back(std::move(element));
            SkipWhitespace();
            if (Consume("]")) {
                return true;
            }
            if (!Consume(",")) {
                return Fail("Expected ',' or ']' in JSON array.");
            }
        }
        return Fail("Unterminated JSON array.");
    }

    bool ParseString(std::string& value) {
        if (!Consume("\"")) {
            return Fail("Expected a JSON string.");
        }
        value.clear();
        while (position_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == '"') {
                return true;
            }
            if (ch < 0x20) {
                return Fail("Unescaped control character in JSON string.");
            }
            if (ch != '\\') {
                value.push_back(static_cast<char>(ch));
                continue;
            }
            if (position_ >= input_.size()) {
                return Fail("Incomplete JSON escape sequence.");
            }
            const char escape = input_[position_++];
            switch (escape) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u': {
                unsigned int codepoint = 0;
                if (!ParseHexCodepoint(codepoint)) {
                    return false;
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (position_ + 2 > input_.size() || input_[position_] != '\\'
                        || input_[position_ + 1] != 'u') {
                        return Fail("Unpaired high surrogate in JSON string.");
                    }
                    position_ += 2;
                    unsigned int low = 0;
                    if (!ParseHexCodepoint(low) || low < 0xdc00 || low > 0xdfff) {
                        return Fail("Invalid low surrogate in JSON string.");
                    }
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return Fail("Unpaired low surrogate in JSON string.");
                }
                AppendUtf8(codepoint, value);
                break;
            }
            default:
                return Fail("Invalid JSON escape sequence.");
            }
        }
        return Fail("Unterminated JSON string.");
    }

    bool ParseHexCodepoint(unsigned int& value) {
        if (position_ + 4 > input_.size()) {
            return Fail("Incomplete Unicode escape in JSON string.");
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = input_[position_++];
            value <<= 4;
            if (ch >= '0' && ch <= '9') {
                value += static_cast<unsigned int>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value += static_cast<unsigned int>(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                value += static_cast<unsigned int>(ch - 'A' + 10);
            } else {
                return Fail("Invalid Unicode escape in JSON string.");
            }
        }
        return true;
    }

    bool ParseNumber(JsonValue& value) {
        const std::size_t start = position_;
        Consume("-");
        if (Consume("0")) {
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                return Fail("Leading zero in JSON number.");
            }
        } else {
            if (!ConsumeDigits()) {
                return Fail("Invalid JSON number.");
            }
        }
        if (Consume(".")) {
            if (!ConsumeDigits()) {
                return Fail("Invalid JSON fraction.");
            }
        }
        if (Consume("e") || Consume("E")) {
            if (!Consume("+")) {
                Consume("-");
            }
            if (!ConsumeDigits()) {
                return Fail("Invalid JSON exponent.");
            }
        }
        const std::string number_text(input_.substr(start, position_ - start));
        char* end = nullptr;
        errno = 0;
        const double number = std::strtod(number_text.c_str(), &end);
        if (errno == ERANGE || end != number_text.c_str() + number_text.size() || !std::isfinite(number)) {
            return Fail("JSON number is outside the supported range.");
        }
        value.type = JsonValue::Type::Number;
        value.number = number;
        return true;
    }

    bool ConsumeDigits() {
        const std::size_t start = position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            ++position_;
        }
        return position_ > start;
    }

    bool Consume(std::string_view token) {
        if (input_.substr(position_, token.size()) != token) {
            return false;
        }
        position_ += token.size();
        return true;
    }

    void SkipWhitespace() {
        while (position_ < input_.size()) {
            const char ch = input_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                return;
            }
            ++position_;
        }
    }

    bool Fail(const char* message) {
        if (error_.empty()) {
            error_ = message;
        }
        return false;
    }

    static void AppendUtf8(unsigned int codepoint, std::string& output) {
        if (codepoint <= 0x7f) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    std::string_view input_;
    std::size_t position_ = 0;
    std::string error_;
};

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), required);
    return result;
}

std::wstring JsonText(const JsonValue* value) {
    if (!value) {
        return {};
    }
    if (value->type == JsonValue::Type::String) {
        return Utf8ToWide(value->string);
    }
    if (value->type == JsonValue::Type::Number) {
        std::wostringstream output;
        output.precision(15);
        output << value->number;
        return output.str();
    }
    if (value->type == JsonValue::Type::Boolean) {
        return value->boolean ? L"true" : L"false";
    }
    return {};
}

std::optional<double> JsonNumber(const JsonValue* value) {
    if (!value || value->type != JsonValue::Type::Number || !std::isfinite(value->number)) {
        return std::nullopt;
    }
    return value->number;
}

std::optional<bool> JsonBoolean(const JsonValue* value) {
    if (!value || value->type != JsonValue::Type::Boolean) {
        return std::nullopt;
    }
    return value->boolean;
}

std::string NormalizeKey(std::string_view key) {
    std::string result;
    result.reserve(key.size());
    for (const unsigned char character : key) {
        if (character == '_' || character == '.') {
            continue;
        }
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

const JsonValue* FindNormalized(
    const JsonValue* object,
    std::initializer_list<std::string_view> keys) {
    if (!object || object->type != JsonValue::Type::Object) {
        return nullptr;
    }
    for (const std::string_view key : keys) {
        const std::string normalized_key = NormalizeKey(key);
        for (const auto& item : object->object) {
            if (NormalizeKey(item.first) == normalized_key && item.second.type != JsonValue::Type::Null) {
                return &item.second;
            }
        }
    }
    return nullptr;
}

void SetNumber(
    nlsi::telemetry::TelemetryField<double>& field,
    const JsonValue* value,
    const std::wstring& timestamp,
    bool require_nonnegative = false) {
    const auto number = JsonNumber(value);
    if (number && (!require_nonnegative || *number >= 0.0)) {
        field.Set(*number, L"NLSI", timestamp);
    }
}

void SetText(
    nlsi::telemetry::TelemetryField<std::wstring>& field,
    const JsonValue* value,
    const std::wstring& timestamp) {
    if (value && value->type != JsonValue::Type::Null) {
        field.Set(JsonText(value), L"NLSI", timestamp);
    }
}

void SetBoolean(
    nlsi::telemetry::TelemetryField<bool>& field,
    const JsonValue* value,
    const std::wstring& timestamp) {
    const auto boolean = JsonBoolean(value);
    if (boolean) {
        field.Set(*boolean, L"NLSI", timestamp);
    }
}

const JsonValue* ChooseJob(const JsonValue* configurations) {
    for (const std::string_view key : {"job", "car_job", "bus_job"}) {
        const JsonValue* job = configurations ? configurations->Find(key) : nullptr;
        if (job && job->type == JsonValue::Type::Object && !job->object.empty()) {
            return job;
        }
    }
    return nullptr;
}

void SetJobText(
    nlsi::telemetry::TelemetryField<std::wstring>& field,
    const JsonValue* job,
    std::initializer_list<std::string_view> keys,
    const std::wstring& timestamp) {
    SetText(field, FindNormalized(job, keys), timestamp);
}

bool BuildSnapshot(
    const JsonValue& packet,
    const JsonValue* configurations_override,
    nlsi::telemetry::TelemetrySnapshot& snapshot,
    std::wstring* error) {
    using nlsi::telemetry::TelemetrySnapshot;
    if (packet.type != JsonValue::Type::Object) {
        if (error) *error = L"Telemetry packet must be a JSON object.";
        return false;
    }
    const JsonValue* type = packet.Find("type");
    if (!type || type->type != JsonValue::Type::String || type->string != "telemetry") {
        if (error) *error = L"Packet type is not telemetry.";
        return false;
    }
    const JsonValue* timestamp_value = packet.Find("timestamp");
    if (!timestamp_value || timestamp_value->type != JsonValue::Type::String || timestamp_value->string.empty()) {
        if (error) *error = L"Telemetry packet is missing its timestamp.";
        return false;
    }
    const std::wstring timestamp = Utf8ToWide(timestamp_value->string);
    const JsonValue* state_value = packet.Find("state");
    if (!state_value || state_value->type != JsonValue::Type::String
        || (state_value->string != "driving" && state_value->string != "paused")) {
        if (error) *error = L"Telemetry packet has an unsupported game state.";
        return false;
    }
    const JsonValue* truck = packet.Find("truck");
    if (!truck || truck->type != JsonValue::Type::Object) {
        if (error) *error = L"Telemetry packet is missing its truck object.";
        return false;
    }

    snapshot = TelemetrySnapshot{};
    snapshot.timestamp = timestamp;
    snapshot.connected = true;
    const JsonValue* game = packet.Find("game");
    if (game && game->type == JsonValue::Type::Object) {
        const JsonValue* game_id = game->Find("id");
        const JsonValue* game_name = game->Find("name");
        if (game_id && game_id->type == JsonValue::Type::String && !game_id->string.empty()) {
            snapshot.game_id.Set(Utf8ToWide(game_id->string), L"NLSI", timestamp);
        }
        if (game_name && game_name->type == JsonValue::Type::String && !game_name->string.empty()) {
            snapshot.game_name.Set(Utf8ToWide(game_name->string), L"NLSI", timestamp);
        }
    }
    const bool paused = state_value->string == "paused";
    snapshot.paused.Set(paused, L"NLSI", timestamp);
    snapshot.driving.Set(!paused, L"NLSI", timestamp);
    snapshot.session_active.Set(true, L"NLSI", timestamp);

    const JsonValue* speed = FindNormalized(truck, {"speed_kmh"});
    if (speed) {
        SetNumber(snapshot.speed_kmh, speed, timestamp, true);
    } else if (const auto speed_mps = JsonNumber(FindNormalized(truck, {"speed_mps"}))) {
        if (*speed_mps >= 0.0) {
            snapshot.speed_kmh.Set(*speed_mps * 3.6, L"NLSI", timestamp);
        }
    }
    SetNumber(snapshot.rpm, FindNormalized(truck, {"rpm"}), timestamp, true);
    SetNumber(snapshot.gear, FindNormalized(truck, {"gear"}), timestamp);
    SetNumber(snapshot.input_throttle, FindNormalized(truck, {"input_throttle"}), timestamp);
    SetNumber(snapshot.effective_throttle, FindNormalized(truck, {"effective_throttle", "throttle"}), timestamp);
    SetNumber(snapshot.input_brake, FindNormalized(truck, {"input_brake"}), timestamp);
    SetNumber(snapshot.effective_brake, FindNormalized(truck, {"effective_brake", "brake"}), timestamp);
    SetNumber(snapshot.retarder_level, FindNormalized(truck, {"retarder_level", "retarder"}), timestamp, true);
    if (snapshot.retarder_level.available) {
        snapshot.retarder_active.Set(snapshot.retarder_level.value > 0.0, L"NLSI", timestamp);
    }
    SetNumber(snapshot.cruise_control_speed,
        FindNormalized(truck, {"cruise_control_speed", "cruise_control"}), timestamp, true);
    SetBoolean(snapshot.cruise_control_active, FindNormalized(truck, {"cruise_control_active"}), timestamp);
    SetNumber(snapshot.fuel_liters, FindNormalized(truck, {"fuel_liters", "fuel"}), timestamp, true);
    SetNumber(snapshot.fuel_range_km, FindNormalized(truck, {"fuel_range_km", "fuel_range"}), timestamp, true);
    SetNumber(snapshot.odometer_km, FindNormalized(truck, {"odometer_km", "odometer"}), timestamp, true);

    SetNumber(snapshot.navigation_distance_m,
        FindNormalized(truck, {"navigation_distance_m"}), timestamp, true);
    if (snapshot.navigation_distance_m.available) {
        snapshot.navigation_distance_km.Set(
            nlsi::telemetry::MetersToKilometers(snapshot.navigation_distance_m.value),
            L"NLSI",
            timestamp);
    } else if (const auto distance_km = JsonNumber(FindNormalized(truck, {"navigation_distance_km"}));
        distance_km && *distance_km >= 0.0) {
        snapshot.navigation_distance_km.Set(*distance_km, L"NLSI", timestamp);
        snapshot.navigation_distance_m.Set(*distance_km * 1000.0, L"NLSI", timestamp);
    }
    SetNumber(snapshot.navigation_time_s, FindNormalized(truck, {"navigation_time_s"}), timestamp, true);
    if (const auto eta = nlsi::telemetry::CalculateEtaSeconds(
            snapshot.navigation_distance_m, snapshot.speed_kmh)) {
        snapshot.eta_seconds.Set(*eta, L"NLSI", timestamp);
    }

    const JsonValue* configurations = configurations_override;
    if (!configurations) {
        configurations = packet.Find("configurations");
    }
    const JsonValue* job = ChooseJob(configurations);
    snapshot.has_job.Set(job != nullptr, L"NLSI", timestamp);
    if (job) {
        SetJobText(snapshot.cargo_id, job, {"cargo_id", "cargo.id", "job_id", "id"}, timestamp);
        SetJobText(snapshot.cargo_name, job, {"cargo", "cargo_name", "cargo.name"}, timestamp);
        SetJobText(snapshot.source_company, job, {"source_company", "source.company"}, timestamp);
        SetJobText(snapshot.source_city, job, {"source_city", "source_city_name"}, timestamp);
        if (!snapshot.source_city.available) {
            SetJobText(snapshot.source_city, job, {"source"}, timestamp);
        }
        SetJobText(snapshot.destination_company, job, {"destination_company", "destination.company"}, timestamp);
        SetJobText(snapshot.destination_city, job, {"destination_city", "destination_city_name"}, timestamp);
        if (!snapshot.destination_city.available) {
            SetJobText(snapshot.destination_city, job, {"destination"}, timestamp);
        }
        SetJobText(snapshot.income, job, {"income", "job_income", "revenue"}, timestamp);
        SetJobText(snapshot.planned_distance, job, {"planned_distance_km", "planned_distance"}, timestamp);
        SetJobText(snapshot.delivery_time, job, {"delivery_time", "delivery_deadline"}, timestamp);
        SetBoolean(snapshot.loaded, FindNormalized(job, {"loaded"}), timestamp);
        SetJobText(snapshot.market, job, {"market", "job_market", "car_job_market"}, timestamp);
        SetJobText(snapshot.special_job, job, {"special_job", "is_special_job", "job_type", "type"}, timestamp);
    }
    return true;
}

std::wstring Win32Error(const wchar_t* operation, int error_code) {
    std::wostringstream output;
    output << operation << L" failed (Winsock error " << error_code << L").";
    return output.str();
}

} // namespace

namespace nlsi::providers {

NLSIProvider::NLSIProvider() = default;

NLSIProvider::~NLSIProvider() {
    Stop();
}

bool NLSIProvider::Start(UpdateCallback callback, EventCallback event_callback) {
    if (running_.load() || worker_.joinable()) {
        return false;
    }
    callback_ = std::move(callback);
    event_callback_ = std::move(event_callback);
    stopping_ = false;
    state_ = telemetry::ProviderState::Connecting;
    Publish({}, telemetry::ProviderState::Connecting, L"");
    try {
        worker_ = std::thread(&NLSIProvider::ReceiveLoop, this);
    } catch (const std::system_error&) {
        state_ = telemetry::ProviderState::Disconnected;
        Publish({}, telemetry::ProviderState::Disconnected, L"Unable to start telemetry receiver thread.");
        return false;
    }
    return true;
}

void NLSIProvider::Stop() {
    stopping_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    running_ = false;
    state_ = telemetry::ProviderState::Disconnected;
}

bool NLSIProvider::IsRunning() const {
    return running_.load();
}

telemetry::ProviderState NLSIProvider::State() const {
    return state_.load();
}

std::wstring NLSIProvider::Name() const {
    return L"NLSI";
}

bool NLSIProvider::ParseTelemetryPacket(
    const std::string& packet,
    telemetry::TelemetrySnapshot& snapshot,
    std::wstring* error) {
    JsonValue root;
    std::string parse_error;
    if (!JsonParser(packet).Parse(root, parse_error)) {
        if (error) {
            *error = Utf8ToWide(parse_error);
        }
        return false;
    }
    return BuildSnapshot(root, nullptr, snapshot, error);
}

void NLSIProvider::Publish(
    const telemetry::TelemetrySnapshot& snapshot,
    telemetry::ProviderState state,
    const std::wstring& error) {
    state_ = state;
    if (callback_) {
        callback_(snapshot, state, error);
    }
}

void NLSIProvider::ReceiveLoop() {
    using namespace std::chrono_literals;
    using nlsi::telemetry::ProviderState;
    running_ = true;

    telemetry::TelemetrySnapshot latest_snapshot;
    std::map<std::string, JsonValue> configuration_cache;
    auto last_telemetry = std::chrono::steady_clock::time_point{};
    auto current_state = ProviderState::Disconnected;

    while (!stopping_.load()) {
        WSADATA winsock_data{};
        const int startup_result = WSAStartup(MAKEWORD(2, 2), &winsock_data);
        if (startup_result != 0) {
            Publish(latest_snapshot, ProviderState::Disconnected, Win32Error(L"WSAStartup", startup_result));
            std::this_thread::sleep_for(200ms);
            continue;
        }

        SOCKET receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (receiver == INVALID_SOCKET) {
            const int error_code = WSAGetLastError();
            WSACleanup();
            Publish(latest_snapshot, ProviderState::Disconnected, Win32Error(L"UDP socket creation", error_code));
            std::this_thread::sleep_for(200ms);
            continue;
        }

        BOOL exclusive = TRUE;
        if (setsockopt(receiver, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
            const int error_code = WSAGetLastError();
            closesocket(receiver);
            WSACleanup();
            Publish(latest_snapshot, ProviderState::Disconnected,
                Win32Error(L"Setting exclusive telemetry socket", error_code));
            std::this_thread::sleep_for(200ms);
            continue;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(kUdpPort);
        if (InetPtonW(AF_INET, L"127.0.0.1", &address.sin_addr) != 1) {
            closesocket(receiver);
            WSACleanup();
            Publish(latest_snapshot, ProviderState::Disconnected, L"Unable to configure telemetry loopback address.");
            break;
        }
        if (bind(receiver, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            const int error_code = WSAGetLastError();
            closesocket(receiver);
            WSACleanup();
            Publish(latest_snapshot, ProviderState::Disconnected,
                Win32Error(L"Binding telemetry receiver to 127.0.0.1:28745", error_code));
            for (int i = 0; i < 10 && !stopping_.load(); ++i) {
                std::this_thread::sleep_for(100ms);
            }
            continue;
        }
        current_state = ProviderState::Disconnected;
        while (!stopping_.load()) {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(receiver, &read_set);
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 250000;
            const int ready = select(0, &read_set, nullptr, nullptr, &timeout);
            if (ready == SOCKET_ERROR) {
                const int error_code = WSAGetLastError();
                Publish(latest_snapshot, ProviderState::Disconnected, Win32Error(L"Telemetry socket wait", error_code));
                break;
            }
            if (ready > 0 && FD_ISSET(receiver, &read_set)) {
                char buffer[65536];
                const int received = recvfrom(receiver, buffer, static_cast<int>(sizeof(buffer)), 0, nullptr, nullptr);
                if (received == SOCKET_ERROR) {
                    const int error_code = WSAGetLastError();
                    Publish(latest_snapshot, ProviderState::Disconnected, Win32Error(L"Receiving telemetry", error_code));
                    break;
                }
                JsonValue packet;
                std::string parse_error;
                if (!JsonParser(std::string_view(buffer, static_cast<std::size_t>(received))).Parse(packet, parse_error)) {
                    Publish(latest_snapshot, current_state, Utf8ToWide(parse_error));
                    continue;
                }
                const JsonValue* type = packet.Find("type");
                if (!type || type->type != JsonValue::Type::String) {
                    Publish(latest_snapshot, current_state, L"Received a telemetry packet without a type.");
                    continue;
                }
                if ((type->string == "gameplay_event"
                        || type->string == "lifecycle"
                        || type->string == "plugin_init"
                        || type->string == "plugin_shutdown")
                    && event_callback_) {
                    event_callback_(std::string(buffer, static_cast<std::size_t>(received)));
                }
                if (type->string == "configuration") {
                    const JsonValue* id = packet.Find("id");
                    const JsonValue* attributes = packet.Find("attributes");
                    if (id && id->type == JsonValue::Type::String && attributes
                        && attributes->type == JsonValue::Type::Object) {
                        configuration_cache.insert_or_assign(id->string, *attributes);
                    }
                    continue;
                }
                if (type->string == "plugin_shutdown") {
                    telemetry::MarkSnapshotStale(latest_snapshot);
                    current_state = ProviderState::Disconnected;
                    last_telemetry = {};
                    configuration_cache.clear();
                    Publish(latest_snapshot, current_state, L"");
                    continue;
                }
                if (type->string == "plugin_init") {
                    configuration_cache.clear();
                    continue;
                }
                if (type->string == "lifecycle") {
                    const JsonValue* lifecycle_state = packet.Find("state");
                    if (lifecycle_state && lifecycle_state->type == JsonValue::Type::String
                        && current_state == ProviderState::Connected
                        && telemetry::IsTelemetryFreshFor(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - last_telemetry))) {
                        const std::wstring lifecycle_timestamp = JsonText(packet.Find("timestamp"));
                        const std::wstring field_timestamp = lifecycle_timestamp.empty()
                            ? latest_snapshot.timestamp
                            : lifecycle_timestamp;
                        const bool paused = lifecycle_state->string == "paused";
                        latest_snapshot.paused.Set(paused, L"NLSI", field_timestamp);
                        latest_snapshot.driving.Set(!paused, L"NLSI", field_timestamp);
                        latest_snapshot.session_active.Set(true, L"NLSI", field_timestamp);
                        Publish(latest_snapshot, current_state, L"");
                    }
                    continue;
                }
                if (type->string != "telemetry") {
                    continue;
                }

                const JsonValue* packet_configurations = packet.Find("configurations");
                if (packet_configurations && packet_configurations->type == JsonValue::Type::Object) {
                    configuration_cache.clear();
                    for (const auto& entry : packet_configurations->object) {
                        if (entry.second.type == JsonValue::Type::Object) {
                            configuration_cache.insert_or_assign(entry.first, entry.second);
                        }
                    }
                }
                JsonValue combined_configurations;
                combined_configurations.type = JsonValue::Type::Object;
                combined_configurations.object = configuration_cache;
                std::wstring parse_message;
                if (!BuildSnapshot(packet, &combined_configurations, latest_snapshot, &parse_message)) {
                    Publish(latest_snapshot, current_state, parse_message);
                    continue;
                }
                last_telemetry = std::chrono::steady_clock::now();
                current_state = ProviderState::Connected;
                Publish(latest_snapshot, current_state, L"");
            }

            if (current_state == ProviderState::Connected
                && !telemetry::IsTelemetryFreshFor(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - last_telemetry))) {
                telemetry::MarkSnapshotStale(latest_snapshot);
                current_state = ProviderState::Stale;
                Publish(latest_snapshot, current_state, L"Telemetry has not been updated for 5 seconds.");
            }
        }

        closesocket(receiver);
        WSACleanup();
        if (!stopping_.load()) {
            telemetry::MarkSnapshotStale(latest_snapshot);
            last_telemetry = {};
            current_state = ProviderState::Disconnected;
            Publish(latest_snapshot, ProviderState::Disconnected, L"Telemetry receiver disconnected; retrying.");
            for (int i = 0; i < 10 && !stopping_.load(); ++i) {
                std::this_thread::sleep_for(100ms);
            }
        }
    }

    running_ = false;
    state_ = ProviderState::Disconnected;
}

} // namespace nlsi::providers
