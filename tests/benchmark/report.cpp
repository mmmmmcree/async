#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Fields = std::vector<std::string>;
using Row = std::map<std::string, std::string>;
using Rows = std::vector<Row>;
using Backends = std::vector<std::string>;
struct Comparison
{
    std::string m_name;
    std::string m_size;
    std::vector<std::optional<Row>> m_rows;
};
using Comparisons = std::vector<Comparison>;
Fields parse_csv_line(std::string_view line);
Rows read_csv(const std::filesystem::path & path);
std::ofstream open_output(const std::filesystem::path & path);
void write_csv_row(std::ostream & output, const Fields & fields);
double number(const std::string & text);
std::optional<double> time_us(const std::optional<Row> & row_opt);
std::string format(std::optional<double> value_opt, int precision = 3);
std::string metric(const std::optional<Row> & row_opt, const std::string & name, double scale = 1.0);
std::string display_name(const Comparison & comparison, std::string_view suffix = {});
std::string label(const std::string & backend);
Comparisons join_rows(const std::vector<Rows> & backends);
Comparisons read_metrics(const std::filesystem::path & directory, const Backends & backends, const std::string & kind);
void write_metric_csv(const std::filesystem::path & path, const Comparisons & comparisons, const Fields & metrics, const Backends & backends);
bool write_timing(const std::filesystem::path & directory, std::ostream & summary, const Comparisons & comparisons, const Backends & backends);
void write_diagnostics(const std::filesystem::path & directory, std::ostream & summary, const Comparisons & comparisons, const Backends & backends);
void write_memory(const std::filesystem::path & directory, std::ostream & summary, const Comparisons & comparisons, const Backends & backends);

}

int main(int argc, char ** argv)
{
    if (argc != 2 and argc != 3) { std::cerr << "Usage: async_comparison_report results_directory [backend,backend,...]\n"; return 1; }
    try {
        const std::filesystem::path directory {argv[1]};
        const Backends backends = argc == 3 ? parse_csv_line(argv[2]) : Backends {"asio", "stdexec"};
        if (backends.size() < 2) { throw std::runtime_error("Comparison requires at least two backends"); }
        std::vector<Rows> rows(backends.size());
        for (const auto & row : read_csv(directory / "timing_raw.csv")) {
            const auto position_it = std::find(backends.begin(), backends.end(), row.at("backend"));
            if (position_it == backends.end()) { throw std::runtime_error("Unexpected timing backend: " + row.at("backend")); }
            rows[static_cast<std::size_t>(position_it - backends.begin())].push_back(row);
        }
        auto summary = open_output(directory / "summary.md");
        summary << "# async benchmark comparison\n\nTimes are wall time per complete workload iteration, in microseconds (us). "
            << "Winner identifies the lower measured time for that case in this run; equal minima are a tie. "
            << "Missing data is n/a; failed measurements are error, never zero.\n\n";
        const bool succeeded = write_timing(directory, summary, join_rows(rows), backends);
        write_diagnostics(directory, summary, read_metrics(directory, backends, "diagnostics"), backends);
        write_memory(directory, summary, read_metrics(directory, backends, "memory"), backends);
        summary << "\n## Raw data\n\n";
        for (const auto & backend : backends) { summary << "- [" << label(backend) << " benchmark JSON](" << backend << ".json)\n"; }
        summary << "- [Timing](timing.csv), [diagnostics](diagnostics.csv), [memory](memory.csv): adjacent backend columns\n"
            << "- [Unrounded timing input](timing_raw.csv); original per-backend diagnostics and memory CSVs are preserved.\n";
        std::cout << "Report: " << (directory / "summary.md").string() << '\n';
        return succeeded ? 0 : 1;
    } catch (const std::exception & error) { std::cerr << "Comparison report: " << error.what() << '\n'; return 1; }
}

namespace {

Fields parse_csv_line(std::string_view line)
{
    Fields fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const auto character = line[index];
        if (character == '"') {
            if (quoted and index + 1 < line.size() and line[index + 1] == '"') { field += '"'; ++index; }
            else { quoted = not quoted; }
        } else if (character == ',' and not quoted) {
            fields.push_back(std::move(field));
            field.clear();
        } else { field += character; }
    }
    if (quoted) { throw std::runtime_error("Unterminated CSV field"); }
    fields.push_back(std::move(field));
    return fields;
}


Rows read_csv(const std::filesystem::path & path)
{
    std::ifstream input {path};
    if (not input) { throw std::runtime_error("Cannot read " + path.string()); }
    input.exceptions(std::ios::badbit);
    std::string line;
    const auto next_line = [&] {
        if (not std::getline(input, line)) { return false; }
        if (not line.empty() and line.back() == '\r') { line.pop_back(); }
        return true;
    };
    if (not next_line()) { throw std::runtime_error("Missing CSV header: " + path.string()); }
    const auto header = parse_csv_line(line);
    Rows rows;
    while (next_line()) {
        if (line.empty()) { continue; }
        const auto fields = parse_csv_line(line);
        if (fields.size() != header.size()) { throw std::runtime_error("CSV column count mismatch: " + path.string()); }
        Row row;
        for (std::size_t index = 0; index < header.size(); ++index) { row.emplace(header[index], fields[index]); }
        rows.push_back(std::move(row));
    }
    return rows;
}


std::ofstream open_output(const std::filesystem::path & path)
{
    std::ofstream output {path};
    output.exceptions(std::ios::badbit | std::ios::failbit);
    return output;
}


void write_csv_row(std::ostream & output, const Fields & fields)
{
    bool first = true;
    for (const auto & field : fields) {
        if (not first) { output << ','; }
        first = false;
        output << '"';
        for (const auto character : field) {
            if (character == '"') { output << '"'; }
            output << character;
        }
        output << '"';
    }
    output << '\n';
}


double number(const std::string & text)
{
    std::size_t consumed = 0;
    const auto value = std::stod(text, &consumed);
    if (consumed != text.size() or not std::isfinite(value) or value < 0.0) {
        throw std::runtime_error("Invalid measurement: " + text);
    }
    return value;
}


std::optional<double> time_us(const std::optional<Row> & row_opt)
{
    if (not row_opt or row_opt->at("status") != "ok") { return {}; }
    const auto value = number(row_opt->at("real_time"));
    const auto & unit = row_opt->at("time_unit");
    if (unit == "ns") { return value / 1000.0; }
    if (unit == "us") { return value; }
    if (unit == "ms") { return value * 1000.0; }
    if (unit == "s") { return value * 1000000.0; }
    throw std::runtime_error("Unknown benchmark time unit: " + unit);
}


std::string format(std::optional<double> value_opt, int precision)
{
    if (not value_opt) { return "n/a"; }
    std::ostringstream output;
    output << std::fixed << std::setprecision(precision) << *value_opt;
    return output.str();
}


std::string metric(const std::optional<Row> & row_opt, const std::string & name, double scale)
{
    if (not row_opt) { return "n/a"; }
    return format(number(row_opt->at(name)) / scale);
}


std::string display_name(const Comparison & comparison, std::string_view suffix)
{
    auto name = comparison.m_name.substr(0, comparison.m_name.size() - suffix.size());
    if (not comparison.m_size.empty()) { name += "/" + comparison.m_size; }
    std::string escaped;
    for (const auto character : name) {
        if (character == '|') { escaped += '\\'; }
        escaped += character;
    }
    return escaped;
}


std::string label(const std::string & backend) { return backend == "asio" ? "Asio" : backend; }

Comparisons join_rows(const std::vector<Rows> & backends)
{
    Comparisons comparisons;
    std::map<std::pair<std::string, std::string>, std::size_t> indices;
    for (std::size_t backend = 0; backend < backends.size(); ++backend) {
        for (const auto & row : backends[backend]) {
            const auto & name = row.at("case");
            const auto size = row.contains("size") ? row.at("size") : "";
            const auto [position_it, inserted] = indices.emplace(std::pair {name, size}, comparisons.size());
            if (inserted) { comparisons.push_back({name, size, std::vector<std::optional<Row>>(backends.size())}); }
            auto & destination_opt = comparisons[position_it->second].m_rows[backend];
            if (destination_opt) { throw std::runtime_error("Duplicate benchmark case: " + name); }
            destination_opt = row;
        }
    }
    return comparisons;
}
Comparisons read_metrics(const std::filesystem::path & directory, const Backends & backends, const std::string & kind)
{
    std::vector<Rows> rows;
    for (const auto & backend : backends) { rows.push_back(read_csv(directory / (backend + "_" + kind + ".csv"))); }
    return join_rows(rows);
}
void write_metric_csv(const std::filesystem::path & path, const Comparisons & comparisons, const Fields & metrics, const Backends & backends)
{
    auto output = open_output(path);
    Fields header {"case", "size"};
    for (const auto & name : metrics) {
        for (const auto & backend : backends) { header.push_back(backend + "_" + name); }
    }
    write_csv_row(output, header);
    for (const auto & comparison : comparisons) {
        Fields fields {comparison.m_name, comparison.m_size};
        for (const auto & name : metrics) {
            for (const auto & row_opt : comparison.m_rows) { fields.push_back(row_opt ? row_opt->at(name) : ""); }
        }
        write_csv_row(output, fields);
    }
}

bool write_timing(const std::filesystem::path & directory, std::ostream & summary, const Comparisons & comparisons, const Backends & backends)
{
    if (comparisons.empty()) { throw std::runtime_error("No benchmark timing results"); }
    std::string suffix;
    const auto options_position = comparisons.front().m_name.find("/min_time:");
    if (options_position != std::string::npos) {
        suffix = comparisons.front().m_name.substr(options_position);
        for (const auto & comparison : comparisons) { if (not comparison.m_name.ends_with(suffix)) { suffix.clear(); break; } }
    }
    auto output = open_output(directory / "timing.csv");
    Fields header {"case"};
    for (const auto & backend : backends) { header.push_back(backend + "_us"); }
    header.push_back("winner");
    for (const auto & backend : backends) { header.push_back(backend + "_iterations"); }
    for (const auto & backend : backends) { header.push_back(backend + "_status"); }
    write_csv_row(output, header);
    summary << "## Workload time\n\n";
    if (not suffix.empty()) { summary << "Shared benchmark options: `" << suffix << "`. CSV retains full case names.\n\n"; }
    summary << "| Case";
    for (const auto & backend : backends) { summary << " | " << label(backend) << " (us)"; }
    summary << " | Winner |\n| ---";
    for (const auto & backend : backends) { (void)backend; summary << " | ---:"; }
    summary << " | --- |\n";
    std::size_t width = 4;
    for (const auto & comparison : comparisons) { width = std::max(width, comparison.m_name.size() - suffix.size()); }
    std::cout << std::left << std::setw(static_cast<int>(width)) << "Case";
    for (const auto & backend : backends) { std::cout << std::right << std::setw(18) << (label(backend) + " (us)"); }
    std::cout << std::setw(14) << "Winner" << '\n';
    bool succeeded = true;
    for (const auto & comparison : comparisons) {
        std::vector<std::optional<double>> times;
        for (const auto & row_opt : comparison.m_rows) { times.push_back(time_us(row_opt)); }
        std::string winner = "n/a";
        if (std::all_of(times.begin(), times.end(), [](auto value) { return value.has_value(); })) {
            auto minimum_it = std::min_element(times.begin(), times.end(), [](auto first, auto second) { return *first < *second; });
            const bool tied = std::count(times.begin(), times.end(), *minimum_it) > 1;
            winner = tied ? "tie" : label(backends[static_cast<std::size_t>(minimum_it - times.begin())]);
        }
        summary << "| " << display_name(comparison, suffix);
        Fields fields {comparison.m_name};
        std::cout << std::left << std::setw(static_cast<int>(width)) << comparison.m_name.substr(0, comparison.m_name.size() - suffix.size());
        for (std::size_t index = 0; index < backends.size(); ++index) {
            const auto & row_opt = comparison.m_rows[index];
            const bool failed = row_opt and not times[index];
            if (failed) { succeeded = false; }
            summary << " | " << (failed ? "error" : format(times[index]));
            std::cout << std::right << std::setw(18) << (failed ? "error" : format(times[index]));
            fields.push_back(times[index] ? format(times[index], 9) : "");
        }
        summary << " | " << winner << " |\n";
        std::cout << std::setw(14) << winner << '\n';
        fields.push_back(winner);
        for (const auto & row_opt : comparison.m_rows) { fields.push_back(row_opt ? row_opt->at("iterations") : ""); }
        for (const auto & row_opt : comparison.m_rows) { fields.push_back(row_opt ? row_opt->at("status") : "missing"); }
        write_csv_row(output, fields);
    }
    return succeeded;
}

void write_diagnostics(const std::filesystem::path & directory, std::ostream & summary, const Comparisons & comparisons, const Backends & backends)
{
    write_metric_csv(directory / "diagnostics.csv", comparisons,
        {"callbacks", "poll_p50_us", "poll_p99_us", "poll_max_us", "max_steps_per_poll", "timer_p99_lateness_us"}, backends);
    summary << "\n## Poll latency\n\nSuccessful control-context polls; OS preemption affects tails. Full metrics: [diagnostics.csv](diagnostics.csv).\n\n| Case / size";
    for (const auto & metric : {"p99", "max"}) {
        for (const auto & backend : backends) { summary << " | " << label(backend) << ' ' << metric << " (us)"; }
    }
    summary << " |\n| ---";
    for (std::size_t index = 0; index < backends.size() * 2; ++index) { summary << " | ---:"; }
    summary << " |\n";
    for (const auto & comparison : comparisons) {
        summary << "| " << display_name(comparison);
        for (const auto & name : {"poll_p99_us", "poll_max_us"}) {
            for (const auto & row_opt : comparison.m_rows) { summary << " | " << metric(row_opt, name); }
        }
        summary << " |\n";
    }
}
void write_memory(const std::filesystem::path & directory, std::ostream & summary, const Comparisons & comparisons, const Backends & backends)
{
    write_metric_csv(directory / "memory.csv", comparisons,
        {"new_calls", "new_requested_bytes", "new_peak_live_bytes", "new_remaining_bytes"}, backends);
    summary << "\n## Observable C++ allocations\n\nSeparate instrumented processes; these are not total process memory or RSS. Full metrics: [memory.csv](memory.csv).\n\n| Case / size";
    for (const auto & metric : {"new calls", "peak (KiB)"}) {
        for (const auto & backend : backends) { summary << " | " << label(backend) << ' ' << metric; }
    }
    summary << " |\n| ---";
    for (std::size_t index = 0; index < backends.size() * 2; ++index) { summary << " | ---:"; }
    summary << " |\n";
    for (const auto & comparison : comparisons) {
        summary << "| " << display_name(comparison);
        for (const auto & row_opt : comparison.m_rows) { summary << " | " << (row_opt ? row_opt->at("new_calls") : "n/a"); }
        for (const auto & row_opt : comparison.m_rows) { summary << " | " << metric(row_opt, "new_peak_live_bytes", 1024.0); }
        summary << " |\n";
    }
}

}
