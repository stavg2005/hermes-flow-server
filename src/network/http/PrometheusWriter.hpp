// PrometheusWriter.hpp
// A lightweight builder for Prometheus text exposition format (0.0.4)
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hermes::net::http {

/**
 * @brief Builds a Prometheus text format metrics body incrementally.
 *
 * Usage:
 *   PrometheusWriter pw;
 *   pw.counter("my_counter", "Help text", 42)
 *     .gauge("my_gauge", "Help text", 7);
 *   std::string body = pw.take();
 */
class PrometheusWriter {
 public:
  /// Appends a counter metric (monotonically increasing)
  PrometheusWriter& counter(std::string_view name, std::string_view help,
                            uint64_t val) {
    return append_metric("counter", name, help, val);
  }

  /// Appends a gauge metric (can go up or down)
  PrometheusWriter& gauge(std::string_view name, std::string_view help,
                          uint64_t val) {
    return append_metric("gauge", name, help, val);
  }

  /// Appends a labeled counter metric (e.g. per-session stats)
  PrometheusWriter& labeled_counter(std::string_view name,
                                    std::string_view help,
                                    std::string_view label_key,
                                    std::string_view label_val, uint64_t val) {
    buf_ += "# HELP ";
    buf_ += name;
    buf_ += ' ';
    buf_ += help;
    buf_ += "\n# TYPE ";
    buf_ += name;
    buf_ += " counter\n";
    buf_ += name;
    buf_ += '{';
    buf_ += label_key;
    buf_ += "=\"";
    buf_ += label_val;
    buf_ += "\"} ";
    buf_ += std::to_string(val);
    buf_ += '\n';
    return *this;
  }

  /// Moves and returns the accumulated metrics string. Leaves the writer empty.
  std::string take() { return std::move(buf_); }

 private:
  std::string buf_;

  PrometheusWriter& append_metric(std::string_view type, std::string_view name,
                                  std::string_view help, uint64_t val) {
    buf_ += "# HELP ";
    buf_ += name;
    buf_ += ' ';
    buf_ += help;
    buf_ += "\n# TYPE ";
    buf_ += name;
    buf_ += ' ';
    buf_ += type;
    buf_ += '\n';
    buf_ += name;
    buf_ += ' ';
    buf_ += std::to_string(val);
    buf_ += '\n';
    return *this;
  }
};

}  // namespace hermes::net::http
