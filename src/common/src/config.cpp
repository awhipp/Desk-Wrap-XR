#include "deskwrap/common/config.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cctype>
#include <unordered_map>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace deskwrap {

namespace {

// Minimal recursive-descent JSON parser
struct JsonValue;
using JsonObject = std::unordered_map<std::string, JsonValue>;

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Object, Array };
    Type type = Type::Null;
    bool bool_val = false;
    double number_val = 0.0;
    std::string string_val;
    JsonObject obj_val;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : src_(input), pos_(0) {}

    bool parse(JsonValue& out, std::string& error) {
        skip_whitespace();
        if (pos_ >= src_.size()) {
            error = "Empty input";
            return false;
        }
        if (!parse_value(out, error)) {
            return false;
        }
        skip_whitespace();
        if (pos_ < src_.size()) {
            error = "Unexpected trailing characters after JSON value at index " + std::to_string(pos_);
            return false;
        }
        return true;
    }

private:
    std::string_view src_;
    size_t pos_ = 0;

    void skip_whitespace() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                pos_++;
            } else {
                break;
            }
        }
    }

    char peek() const {
        return (pos_ < src_.size()) ? src_[pos_] : '\0';
    }

    char get() {
        return (pos_ < src_.size()) ? src_[pos_++] : '\0';
    }

    bool parse_value(JsonValue& val, std::string& error) {
        skip_whitespace();
        char c = peek();
        if (c == '{') {
            return parse_object(val, error);
        } else if (c == '"') {
            return parse_string(val.string_val, error) ? (val.type = JsonValue::Type::String, true) : false;
        } else if (c == 't' || c == 'f') {
            return parse_bool(val, error);
        } else if (c == 'n') {
            return parse_null(val, error);
        } else if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parse_number(val, error);
        } else if (c == '[') {
            return parse_array(val, error);
        }
        error = std::string("Unexpected character '") + c + "' at position " + std::to_string(pos_);
        return false;
    }

    bool parse_object(JsonValue& val, std::string& error) {
        get(); // '{'
        val.type = JsonValue::Type::Object;
        val.obj_val.clear();
        skip_whitespace();

        if (peek() == '}') {
            get();
            return true;
        }

        while (pos_ < src_.size()) {
            skip_whitespace();
            if (peek() != '"') {
                error = "Expected string key in object at position " + std::to_string(pos_);
                return false;
            }
            std::string key;
            if (!parse_string(key, error)) return false;

            skip_whitespace();
            if (get() != ':') {
                error = "Expected ':' after key in object at position " + std::to_string(pos_ - 1);
                return false;
            }

            JsonValue child;
            if (!parse_value(child, error)) return false;
            val.obj_val[key] = std::move(child);

            skip_whitespace();
            char next = get();
            if (next == '}') {
                return true;
            } else if (next == ',') {
                skip_whitespace();
                if (peek() == '}') {
                    error = "Trailing comma in object at position " + std::to_string(pos_ - 1);
                    return false;
                }
            } else {
                error = "Expected ',' or '}' in object at position " + std::to_string(pos_ - 1);
                return false;
            }
        }
        error = "Unterminated object";
        return false;
    }

    bool parse_array(JsonValue& val, std::string& error) {
        get(); // '['
        val.type = JsonValue::Type::Array;
        skip_whitespace();

        if (peek() == ']') {
            get();
            return true;
        }

        while (pos_ < src_.size()) {
            JsonValue child;
            if (!parse_value(child, error)) return false;
            skip_whitespace();
            char next = get();
            if (next == ']') {
                return true;
            } else if (next == ',') {
                skip_whitespace();
                if (peek() == ']') {
                    error = "Trailing comma in array at position " + std::to_string(pos_ - 1);
                    return false;
                }
            } else {
                error = "Expected ',' or ']' in array at position " + std::to_string(pos_ - 1);
                return false;
            }
        }
        error = "Unterminated array";
        return false;
    }

    bool parse_string(std::string& out, std::string& error) {
        get(); // '"'
        out.clear();
        while (pos_ < src_.size()) {
            char c = get();
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (pos_ >= src_.size()) {
                    error = "Unterminated string escape";
                    return false;
                }
                char esc = get();
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        error = "Unterminated string literal";
        return false;
    }

    bool parse_number(JsonValue& val, std::string& error) {
        size_t start = pos_;
        if (peek() == '-') pos_++;
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
            pos_++;
        }
        if (pos_ < src_.size() && peek() == '.') {
            pos_++;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
                pos_++;
            }
        }
        if (pos_ < src_.size() && (peek() == 'e' || peek() == 'E')) {
            pos_++;
            if (pos_ < src_.size() && (peek() == '+' || peek() == '-')) pos_++;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
                pos_++;
            }
        }

        std::string num_str(src_.substr(start, pos_ - start));
        char* end_ptr = nullptr;
        double d = std::strtod(num_str.c_str(), &end_ptr);
        if (end_ptr != num_str.c_str() + num_str.size()) {
            error = "Invalid number format: " + num_str;
            return false;
        }
        val.type = JsonValue::Type::Number;
        val.number_val = d;
        return true;
    }

    bool parse_bool(JsonValue& val, std::string& error) {
        if (src_.substr(pos_, 4) == "true") {
            pos_ += 4;
            val.type = JsonValue::Type::Bool;
            val.bool_val = true;
            return true;
        } else if (src_.substr(pos_, 5) == "false") {
            pos_ += 5;
            val.type = JsonValue::Type::Bool;
            val.bool_val = false;
            return true;
        }
        error = "Invalid boolean literal at position " + std::to_string(pos_);
        return false;
    }

    bool parse_null(JsonValue& val, std::string& error) {
        if (src_.substr(pos_, 4) == "null") {
            pos_ += 4;
            val.type = JsonValue::Type::Null;
            return true;
        }
        error = "Invalid literal at position " + std::to_string(pos_);
        return false;
    }
};

} // namespace

std::filesystem::path get_default_config_path() {
#ifdef _WIN32
    PWSTR appdata_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata_path))) {
        std::filesystem::path path = appdata_path;
        CoTaskMemFree(appdata_path);
        return path / "DeskWrapXR" / "config.json";
    }
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) {
        return std::filesystem::path(appdata) / "DeskWrapXR" / "config.json";
    }
    return std::filesystem::path("config.json");
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".config" / "deskwrap" / "config.json";
    }
    return std::filesystem::path("config.json");
#endif
}

ConfigLoadResult load_config(const std::filesystem::path& filepath) {
    ConfigLoadResult result;
    std::filesystem::path target_path = filepath.empty() ? get_default_config_path() : filepath;

    if (!std::filesystem::exists(target_path)) {
        result.status = ConfigLoadStatus::FileNotFound;
        result.message = "[Config] Configuration file '" + target_path.string() + 
                         "' not found. Using default configuration (arc: 180.0°).";
        std::cout << result.message << "\n";
        return result;
    }

    std::ifstream file(target_path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        result.status = ConfigLoadStatus::FileNotFound;
        result.message = "[Config] Could not open configuration file '" + target_path.string() + 
                         "'. Using default configuration (arc: 180.0°).";
        std::cout << result.message << "\n";
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    JsonParser parser(content);
    JsonValue root;
    std::string parse_error;
    if (!parser.parse(root, parse_error) || root.type != JsonValue::Type::Object) {
        result.status = ConfigLoadStatus::MalformedJson;
        if (root.type != JsonValue::Type::Object && parse_error.empty()) {
            parse_error = "Root JSON element must be an object";
        }
        result.message = "[Config] Error: Malformed JSON in configuration file '" + target_path.string() + 
                         "': " + parse_error + ". Falling back to default configuration.";
        std::cerr << result.message << "\n";
        return result;
    }

    const auto& obj = root.obj_val;
    auto get_num = [&](const std::string& key, double def) {
        auto it = obj.find(key);
        return (it != obj.end() && it->second.type == JsonValue::Type::Number) ? it->second.number_val : def;
    };
    auto get_bool = [&](const std::string& key, bool def) {
        auto it = obj.find(key);
        return (it != obj.end() && it->second.type == JsonValue::Type::Bool) ? it->second.bool_val : def;
    };
    auto get_str = [&](const std::string& key, const std::string& def) {
        auto it = obj.find(key);
        return (it != obj.end() && it->second.type == JsonValue::Type::String) ? it->second.string_val : def;
    };

    result.config.arc_degrees = static_cast<float>(get_num("arc_degrees", result.config.arc_degrees));
    result.config.min_arc_degrees = static_cast<float>(get_num("min_arc_degrees", result.config.min_arc_degrees));
    result.config.max_arc_degrees = static_cast<float>(get_num("max_arc_degrees", result.config.max_arc_degrees));
    result.config.radius = static_cast<float>(get_num("radius", result.config.radius));
    result.config.height = static_cast<float>(get_num("height", result.config.height));
    result.config.pose_prediction_enabled = get_bool("pose_prediction_enabled", result.config.pose_prediction_enabled);
    result.config.monitor_hardware_id = get_str("monitor_hardware_id", result.config.monitor_hardware_id);
    result.config.monitor_edid_name = get_str("monitor_edid_name", result.config.monitor_edid_name);
    result.config.pipe_name = get_str("pipe_name", result.config.pipe_name);

    if (obj.find("virtual_display_width") != obj.end()) {
        result.config.virtual_display_mode.width = static_cast<uint32_t>(get_num("virtual_display_width", 5760));
    }
    if (obj.find("virtual_display_height") != obj.end()) {
        result.config.virtual_display_mode.height = static_cast<uint32_t>(get_num("virtual_display_height", 1080));
    }
    if (obj.find("virtual_display_refresh_rate_hz") != obj.end()) {
        result.config.virtual_display_mode.refresh_rate_hz = static_cast<uint32_t>(get_num("virtual_display_refresh_rate_hz", 60));
    }

    result.status = ConfigLoadStatus::Success;
    result.message = "[Config] Successfully loaded configuration from '" + target_path.string() + 
                     "' (arc: " + std::to_string(result.config.arc_degrees) + "°).";
    std::cout << result.message << "\n";

    return result;
}

std::string serialize_config_json(const DeskWrapConfig& config) {
    std::ostringstream ss;
    ss << "{\n"
       << "  \"arc_degrees\": " << config.arc_degrees << ",\n"
       << "  \"min_arc_degrees\": " << config.min_arc_degrees << ",\n"
       << "  \"max_arc_degrees\": " << config.max_arc_degrees << ",\n"
       << "  \"radius\": " << config.radius << ",\n"
       << "  \"height\": " << config.height << ",\n"
       << "  \"pose_prediction_enabled\": " << (config.pose_prediction_enabled ? "true" : "false") << ",\n"
       << "  \"monitor_hardware_id\": \"" << config.monitor_hardware_id << "\",\n"
       << "  \"monitor_edid_name\": \"" << config.monitor_edid_name << "\",\n"
       << "  \"pipe_name\": \"" << config.pipe_name << "\",\n"
       << "  \"virtual_display_width\": " << config.virtual_display_mode.width << ",\n"
       << "  \"virtual_display_height\": " << config.virtual_display_mode.height << ",\n"
       << "  \"virtual_display_refresh_rate_hz\": " << config.virtual_display_mode.refresh_rate_hz << "\n"
       << "}\n";
    return ss.str();
}

bool save_config(const DeskWrapConfig& config, const std::filesystem::path& filepath) {
    std::filesystem::path target_path = filepath.empty() ? get_default_config_path() : filepath;
    try {
        if (target_path.has_parent_path()) {
            std::filesystem::create_directories(target_path.parent_path());
        }
        std::ofstream out(target_path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;
        out << serialize_config_json(config);
        return true;
    } catch (...) {
        return false;
    }
}

void print_config(const DeskWrapConfig& config, std::ostream& os) {
    os << "DeskWrap Configuration:\n"
       << "  Arc Degrees:             " << config.arc_degrees << "° (bounds: " 
       << config.min_arc_degrees << "° - " << config.max_arc_degrees << "°)\n"
       << "  Radius:                  " << config.radius << " m\n"
       << "  Height:                  " << config.height << " m\n"
       << "  Pose Prediction:         " << (config.pose_prediction_enabled ? "Enabled" : "Disabled") << "\n"
       << "  Monitor HW ID:           " << config.monitor_hardware_id << "\n"
       << "  Monitor EDID Name:       " << config.monitor_edid_name << "\n"
       << "  IPC Pipe Name:           " << config.pipe_name << "\n"
       << "  Virtual Display Mode:    " << config.virtual_display_mode.width << "x"
       << config.virtual_display_mode.height << " @" << config.virtual_display_mode.refresh_rate_hz << "Hz\n";
}

} // namespace deskwrap
