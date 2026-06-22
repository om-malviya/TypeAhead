#include <csignal>
#include <cstdio>
#include <nlohmann/json.hpp>

#include "Engine.h"
#include "httplib.h"

using nlohmann::json;
using namespace ta;

namespace {
httplib::Server* g_server = nullptr;
void onSignal(int) {
  if (g_server) g_server->stop();
}

json toJson(const Suggestion& s) {
  return {{"query", s.query},
          {"score", s.score},
          {"total_count", s.total_count},
          {"recent_count", s.recent_count}};
}
}  // namespace

int main() {
  Config cfg = Config::fromEnv();

  Engine engine(cfg);
  try {
    engine.start();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[fatal] startup failed: %s\n", e.what());
    return 1;
  }

  httplib::Server svr;
  g_server = &svr;
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  // Vite dev server runs on a different port, so we need open CORS.
  svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
  });
  svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
    res.status = 204;
  });

  svr.Get("/suggest", [&](const httplib::Request& req, httplib::Response& res) {
    std::string q = req.has_param("q") ? req.get_param_value("q") : "";
    auto sugg = engine.suggest(q);
    json out = {{"q", q}, {"suggestions", json::array()}};
    for (const auto& s : sugg) out["suggestions"].push_back(toJson(s));
    res.set_content(out.dump(), "application/json");
  });

  svr.Post("/search", [&](const httplib::Request& req, httplib::Response& res) {
    json body = json::parse(req.body, nullptr, /*allow_exceptions=*/false);
    std::string q;
    if (body.is_object()) {
      if (body.contains("query")) q = body.value("query", std::string());
      else if (body.contains("q")) q = body.value("q", std::string());
    }
    if (q.empty() && req.has_param("q")) q = req.get_param_value("q");
    engine.recordSearch(q);
    res.set_content(json{{"message", "Searched"}}.dump(), "application/json");
  });

  svr.Get("/cache/debug", [&](const httplib::Request& req, httplib::Response& res) {
    std::string p = req.has_param("prefix") ? req.get_param_value("prefix") : "";
    auto d = engine.cacheDebug(p);
    json out = {{"prefix", d.prefix},
                {"cache_key", d.cache_key},
                {"node", d.node_name},
                {"node_index", d.node_index},
                {"vnode_hash", d.vnode_hash},
                {"hit", d.hit},
                {"cached_count", d.cached_count}};
    res.set_content(out.dump(), "application/json");
  });

  svr.Get("/trending", [&](const httplib::Request& req, httplib::Response& res) {
    int n = req.has_param("n") ? std::atoi(req.get_param_value("n").c_str()) : 10;
    if (n <= 0) n = 10;
    auto t = engine.trending(n);
    json out = {{"trending", json::array()}};
    for (const auto& s : t) out["trending"].push_back(toJson(s));
    res.set_content(out.dump(), "application/json");
  });

  svr.Get("/stats", [&](const httplib::Request&, httplib::Response& res) {
    auto s = engine.stats();
    json out = {{"cache_hits", s.cache_hits},
                {"cache_misses", s.cache_misses},
                {"cache_hit_rate", s.cache_hit_rate},
                {"db_reads", s.db_reads},
                {"db_write_statements", s.db_write_statements},
                {"rows_written", s.rows_written},
                {"flush_ops", s.flush_ops},
                {"searches_received", s.searches_received},
                {"suggest_requests", s.suggest_requests},
                {"write_reduction_ratio", s.write_reduction_ratio},
                {"suggest_avg_ms", s.suggest_avg_ms},
                {"suggest_p95_ms", s.suggest_p95_ms},
                {"num_queries", s.num_queries},
                {"redis_nodes", s.redis_nodes},
                {"ring_vnodes", s.ring_vnodes},
                {"pending_batch", s.pending_batch},
                {"ranking_mode", s.ranking_mode}};
    res.set_content(out.dump(2), "application/json");
  });

  svr.Get("/admin/ranking", [&](const httplib::Request& req, httplib::Response& res) {
    std::string mode = req.has_param("mode") ? req.get_param_value("mode") : "";
    if (mode != "count" && mode != "recency") {
      res.status = 400;
      res.set_content(json{{"error", "mode must be count|recency"}}.dump(),
                      "application/json");
      return;
    }
    engine.setRankingMode(mode == "recency");
    res.set_content(json{{"ranking_mode", mode}}.dump(), "application/json");
  });

  svr.Get("/healthz", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content("ok", "text/plain");
  });

  // Serve the built frontend if TA_FRONTEND_DIR is set.
  if (!cfg.frontend_dir.empty()) {
    if (!svr.set_mount_point("/", cfg.frontend_dir))
      std::fprintf(stderr, "[http] could not mount %s\n", cfg.frontend_dir.c_str());
  }

  std::fprintf(stderr, "[http] listening on 0.0.0.0:%d\n", cfg.http_port);
  svr.listen("0.0.0.0", cfg.http_port);
  std::fprintf(stderr, "[http] shutting down (flushing pending batch)\n");
  return 0;
}
