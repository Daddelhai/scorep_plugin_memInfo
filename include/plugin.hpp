#include <scorep/plugin/plugin.hpp>

#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <cstdint>

struct meminfo_t {
    meminfo_t(const std::int64_t& line_nr, const std::string& name, const std::string& unit)
        : line_nr(line_nr), name(name), unit(unit)
    {
    }

    std::int64_t line_nr;
    std::string name;
    std::string unit;
};

template <typename T, typename Policies>
using meminfo_object_id = scorep::plugin::policy::object_id<meminfo_t, T, Policies>;

class meminfo_plugin : public scorep::plugin::base<meminfo_plugin,
                                                   scorep::plugin::policy::async,
                                                   scorep::plugin::policy::post_mortem,
                                                   scorep::plugin::policy::scorep_clock,
                                                   scorep::plugin::policy::once,
                                                   meminfo_object_id> {
public:
    meminfo_plugin()
    {
#ifdef DEBUG
        std::cout << "[MEMINFO][DEBUG][CALL] meminfo_plugin " << std::endl;
#endif
        auto interval_str =
            scorep::environment_variable::get("INTERVAL", "10ms");

        std::regex r("([0-9]+)([mun]?s)");
        std::smatch s;

        if (std::regex_match(interval_str, s, r)) {
            std::string time = s[1];
            std::string unit = s[2];

            switch (unit[0]) {
            case 's':
                intervall_ = std::chrono::seconds(std::stol(time));
                break;
            case 'm':
                intervall_ = std::chrono::milliseconds(std::stol(time));
                break;
            case 'u':
                intervall_ = std::chrono::microseconds(std::stol(time));
                break;
            case 'n':
                intervall_ = std::chrono::nanoseconds(std::stol(time));
                break;

            default:
                break;
            }
        }
        else {
            intervall_ = std::chrono::milliseconds(10);
        }
    }

    std::vector<scorep::plugin::metric_property> get_metric_properties(const std::string& pattern /* = "mem.*"*/)
    {
#ifdef DEBUG
        std::cout << "[MEMINFO][DEBUG][CALL] get_metric_properties " << std::endl;
#endif
        std::vector<scorep::plugin::metric_property> result;

        for (const auto& match : init({pattern})) {
            if (values_by_id_.find(match.line_nr) == values_by_id_.end()) {
                auto handle = make_handle(match.name, match);
                result.push_back(
                    scorep::plugin::metric_property{match.name, "", match.unit}
                        .absolute_point()
                        .value_int());
                values_by_id_.emplace(match.line_nr, std::vector<int64_t>());
            }
        }

        return result;
    }

    void add_metric(const meminfo_t& id_obj)
    {
#ifdef DEBUG
        std::cout << "[MEMINFO][DEBUG][CALL] add_metric " << std::endl;
#endif
        if (id_obj.name == "MemTotal") {
            mem_total_pos = id_obj.line_nr;
        }

        if (id_obj.name == "MemFree") {
            mem_free_pos = id_obj.line_nr;
        }

        if (id_obj.name == "Buffers") {
            buffers_pos = id_obj.line_nr;
        }

        if (id_obj.name == "Cached") {
            cached_pos = id_obj.line_nr;
        }

        if (id_obj.name == "SwapTotal") {
            swap_total_pos = id_obj.line_nr;
        }

        if (id_obj.name == "SwapFree") {
            swap_free_pos = id_obj.line_nr;
        }

        if (id_obj.name == "SwapCached") {
            swap_cached_pos = id_obj.line_nr;
        }

        if (id_obj.name == "MemUsed") {
            mem_used_pos = id_obj.line_nr;
        }

        if (id_obj.name == "SwapUsed") {
            swap_used_pos = id_obj.line_nr;
        }
    }

    void start()
    {
#ifdef DEBUG
        std::cout << "[MEMINFO][DEBUG] mem_total_pos: " << mem_total_pos << std::endl;
        std::cout << "[MEMINFO][DEBUG] mem_free_pos: " << mem_free_pos << std::endl;
        std::cout << "[MEMINFO][DEBUG] buffers_pos: " << buffers_pos << std::endl;
        std::cout << "[MEMINFO][DEBUG] cached_pos: " << cached_pos << std::endl;
        std::cout << "[MEMINFO][DEBUG] swap_total_pos: " << swap_total_pos << std::endl;
        std::cout << "[MEMINFO][DEBUG] swap_cached_pos: " << swap_cached_pos << std::endl;
        std::cout << "[MEMINFO][DEBUG] mem_used_pos: " << mem_used_pos << std::endl;
        std::cout << "[MEMINFO][DEBUG] swap_used_pos: " << swap_used_pos << std::endl;
#endif

        if (running) {
            return;
        }

        running = true;
        last_measurement_ = std::chrono::system_clock::now();
        thread_ = std::thread([&]() { this->exec(); });
    }

    void stop()
    {
        if (!running) {
            return;
        }

        running = false;

        thread_.join();
    }

    void exec()
    {
        while (running) {
            parse(values_by_id_);
            times_.push_back(scorep::chrono::measurement_clock::now());

            while (last_measurement_ < std::chrono::system_clock::now()) {
                last_measurement_ += intervall_;
            }

            std::this_thread::sleep_until(last_measurement_);
        }
    }

    template <typename Cursor>
    void get_all_values(const meminfo_t& id_obj, Cursor& c)
    {
        const auto& values = values_by_id_.find(id_obj.line_nr)->second;

        for (int i = 0; i < values.size(); ++i) {
            c.write(times_[i], values[i]);
        }
    }

private:
    std::atomic<bool> running = false;
    std::thread thread_;
    std::map<std::int64_t, std::vector<std::int64_t>> values_by_id_;
    std::vector<scorep::chrono::ticks> times_;
    std::chrono::nanoseconds intervall_;
    std::chrono::system_clock::time_point last_measurement_ =
        std::chrono::system_clock::now();

    int mem_total_pos = -1;
    int mem_free_pos = -1;
    int buffers_pos = -1;
    int cached_pos = -1;
    int swap_total_pos = -1;
    int swap_free_pos = -1;
    int swap_cached_pos = -1;
    int mem_used_pos = -1;
    int swap_used_pos = -1;

    std::regex regex_parse =
        std::regex(".*:[^a-zA-Z0-9]*([0-9]+).?([kKmMgGtT][bB])?.*");

    std::vector<meminfo_t> init(const std::vector<std::string>& search)
    {
#ifdef DEBUG
        std::cout << "[MEMINFO][DEBUG][CALL] init " << std::endl;
#endif

        std::string line;
        std::ifstream meminfo("/proc/meminfo");
        std::vector<meminfo_t> results;

        std::string regex_custom_str;
        std::string regex_str = "(";

        if (search.empty()) {
            regex_str += "[a-zA-Z0-9_]+";
        }
        else {
            bool first = true;
            for (const auto& s : search) {
                if (first)
                    first = false;
                else {
                    regex_str += '|';
                }

                regex_str += s;
            }
        }
        regex_str += ")";
        regex_custom_str = regex_str;
        regex_str += ":[^a-zA-Z0-9]*([0-9]+).?([kKmMgGtT][bB])?[^a-zA-Z0-9]*";

#ifdef DEBUG
        std::cout << "[MEMINFO][DEBUG] regex_custom_str: " << regex_custom_str << std::endl;
        std::cout << "[MEMINFO][DEBUG] regex_str: " << regex_str << std::endl;
#endif

        std::regex regex(regex_str);

        std::int64_t line_nr = 0;
        while (std::getline(meminfo, line)) {
            std::smatch match;

            bool save = false;

            if (std::regex_match(line, match, regex)) {
                save = true;
            }
            else if (std::regex_match(line, match, std::regex("(MemTotal|MemFree|SwapTotal|SwapFree|SwapCached|Cached|Buffers):[^a-zA-Z0-9]*([0-9]+).?[kKmMgGtT]?([bB])?[^a-zA-Z0-9]*"))) {
                save = true;
            }

            if (save) {
                results.push_back(meminfo_t{line_nr, match[1].str(), match[3].str()});
            }

            ++line_nr;
        }

        // CUSTOM

        std::regex regex_custom(regex_custom_str);
        std::smatch match_custom;
        std::string s;

        {
            s = "MemUsed";
            if (std::regex_match(s, match_custom, regex_custom)) {
                results.push_back(meminfo_t{line_nr, s, "B"});
            }
            ++line_nr;
        }

        {
            s = "SwapUsed";
            if (std::regex_match(s, match_custom, regex_custom)) {
                results.push_back(meminfo_t{line_nr, s, "B"});
            }
            ++line_nr;
        }

        return results;
    }

    void parse(std::map<std::int64_t, std::vector<std::int64_t>>& data)
    {
#ifdef DEBUG
        std::cout << "[MEMINFO][DEBUG][CALL] parse " << std::endl;
#endif

        std::string line;
        std::ifstream meminfo("/proc/meminfo");

        std::int64_t mem_total = -1;
        std::int64_t mem_free = -1;
        std::int64_t buffers = -1;
        std::int64_t cached = -1;
        std::int64_t swap_total = -1;
        std::int64_t swap_free = -1;
        std::int64_t swap_cached = -1;

        std::int64_t line_nr = 0;

        while (std::getline(meminfo, line)) {
            std::smatch match;

            bool run = false;
            bool save = false;

            if (auto it = data.find(line_nr); it != data.end()) {
                std::regex_match(line, match, regex_parse);

                std::int64_t value = std::stoll(match[1].str());
                std::string unit = match[2].str();

                if (unit.size() == 2 && std::tolower(unit[1]) == 'b') {
                    switch (std::tolower(unit[0])) {
                    case 't':
                        value *= 1024;
                    case 'g':
                        value *= 1024;
                    case 'm':
                        value *= 1024;
                    case 'k':
                        value *= 1024;
                    default:
                        break;
                    }
                }

                if (mem_total_pos == line_nr) {
#ifdef DEBUG
                    std::cout << "[MEMINFO][DEBUG] mem_total: " << value << std::endl;
#endif
                    mem_total = value;
                }
                if (mem_free_pos == line_nr) {
#ifdef DEBUG
                    std::cout << "[MEMINFO][DEBUG] mem_free: " << value << std::endl;
#endif
                    mem_free = value;
                }
                if (buffers_pos == line_nr) {
#ifdef DEBUG
                    std::cout << "[MEMINFO][DEBUG] buffers: " << value << std::endl;
#endif
                    buffers = value;
                }
                if (cached_pos == line_nr) {
#ifdef DEBUG
                    std::cout << "[MEMINFO][DEBUG] cached: " << value << std::endl;
#endif
                    cached = value;
                }
                if (swap_total_pos == line_nr) {
#ifdef DEBUG
                    std::cout << "[MEMINFO][DEBUG] swap_total: " << value << std::endl;
#endif
                    swap_total = value;
                }
                if (swap_free_pos == line_nr) {
#ifdef DEBUG
                    std::cout << "[MEMINFO][DEBUG] swap_free: " << value << std::endl;
#endif
                    swap_free = value;
                }
                if (swap_cached_pos == line_nr) {
#ifdef DEBUG
                    std::cout << "[MEMINFO][DEBUG] swap_cached: " << value << std::endl;
#endif
                    swap_cached = value;
                }

                it->second.push_back(value);
            }
            ++line_nr;
        }

        // MemUsed

        if (auto it = data.find(mem_used_pos); it != data.end()) {
            if (mem_total >= 0 && mem_free >= 0 && buffers >= 0 && cached >= 0)
                it->second.push_back(mem_total - mem_free - buffers - cached);
            else {
#ifdef DEBUG
                std::cout << "[MEMINFO][DEBUG] mem_total: " << mem_total
                          << " | mem_free: " << mem_free << " | buffers: " << buffers
                          << " | cached: " << cached << std::endl;
#endif
                it->second.push_back(0);
            }
        }

        ++line_nr;

        // SwapUsed

        if (auto it = data.find(swap_used_pos); it != data.end()) {
            if (swap_total >= 0 && swap_free >= 0 && swap_cached >= 0)
                it->second.push_back(swap_total - swap_free - swap_cached);
            else {
#ifdef DEBUG
                std::cout << "[MEMINFO][DEBUG] swap_total: " << swap_total
                          << " | swap_free: " << swap_free
                          << " | swap_cached: " << swap_cached << std::endl;
#endif
                it->second.push_back(0);
            }
        }
    }
};