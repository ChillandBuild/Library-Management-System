#pragma once

#include "db/database.h"
#include "httplib.h"
#include "json.hpp"
#include "middleware/auth_middleware.h"

using json = nlohmann::json;

namespace ReportController {

inline void registerRoutes(httplib::Server &svr) {

  // GET /api/reports/circulation
  svr.Get("/api/reports/circulation", [](const httplib::Request &req,
                                         httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"librarian", "branch_manager", "admin"}))
      return;

    auto &db = Database::instance();
    std::string period = req.get_param_value("period");
    if (period.empty())
      period = "30";

    auto stats = db.queryOne(
        "SELECT "
        "(SELECT COUNT(*) FROM loans WHERE checkout_date >= date('now', '-' || "
        "? || ' days')) as checkouts, "
        "(SELECT COUNT(*) FROM loans WHERE return_date >= date('now', '-' || ? "
        "|| ' days') AND return_date IS NOT NULL) as returns, "
        "(SELECT COUNT(*) FROM loans WHERE status = 'active') as active_loans, "
        "(SELECT COUNT(*) FROM loans WHERE status = 'active' AND due_date < "
        "date('now')) as overdue, "
        "(SELECT COUNT(*) FROM holds WHERE status = 'pending') as "
        "pending_holds, "
        "(SELECT COUNT(*) FROM members WHERE status = 'active') as "
        "active_members",
        {period, period});

    auto dailyLoans = db.query(
        "SELECT date(checkout_date) as date, COUNT(*) as count FROM loans "
        "WHERE checkout_date >= date('now', '-' || ? || ' days') "
        "GROUP BY date(checkout_date) ORDER BY date",
        {period});

    json report = {{"summary", stats},
                   {"daily_checkouts", dailyLoans},
                   {"period_days", std::stoi(period)}};
    res.set_content(report.dump(), "application/json");
  });

  // GET /api/reports/top-items
  svr.Get("/api/reports/top-items", [](const httplib::Request &req,
                                       httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"librarian", "branch_manager", "admin"}))
      return;

    auto &db = Database::instance();
    std::string period = req.get_param_value("period");
    if (period.empty())
      period = "90";
    std::string limit = req.get_param_value("limit");
    if (limit.empty())
      limit = "20";

    auto items =
        db.query("SELECT ci.id, ci.title, ci.authors, ci.item_type, "
                 "COUNT(l.id) as checkout_count "
                 "FROM loans l JOIN copies c ON l.copy_id = c.id "
                 "JOIN catalog_items ci ON c.catalog_item_id = ci.id "
                 "WHERE l.checkout_date >= date('now', '-' || ? || ' days') "
                 "GROUP BY ci.id ORDER BY checkout_count DESC LIMIT ?",
                 {period, limit});

    res.set_content(
        json{{"data", items}, {"period_days", std::stoi(period)}}.dump(),
        "application/json");
  });

  // GET /api/reports/overdue
  svr.Get("/api/reports/overdue", [](const httplib::Request &req,
                                     httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"librarian", "branch_manager", "admin"}))
      return;

    auto &db = Database::instance();
    auto items =
        db.query("SELECT l.id as loan_id, l.due_date, "
                 "CAST(julianday('now') - julianday(l.due_date) AS INTEGER) as "
                 "days_overdue, "
                 "c.barcode, ci.title, ci.authors, m.first_name, m.last_name, "
                 "m.email, m.phone, "
                 "b.name as branch_name "
                 "FROM loans l JOIN copies c ON l.copy_id = c.id "
                 "JOIN catalog_items ci ON c.catalog_item_id = ci.id "
                 "JOIN members m ON l.member_id = m.id "
                 "JOIN branches b ON c.branch_id = b.id "
                 "WHERE l.status = 'active' AND l.due_date < date('now') "
                 "ORDER BY days_overdue DESC");

    res.set_content(json{{"data", items}, {"count", items.size()}}.dump(),
                    "application/json");
  });

  // GET /api/reports/inactive-items
  svr.Get("/api/reports/inactive-items", [](const httplib::Request &req,
                                            httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"branch_manager", "admin"}))
      return;

    auto &db = Database::instance();
    std::string months = req.get_param_value("months");
    if (months.empty())
      months = "6";

    auto items = db.query("SELECT ci.id, ci.title, ci.authors, ci.item_type, "
                          "MAX(l.checkout_date) as last_checkout, "
                          "COUNT(c.id) as copy_count "
                          "FROM catalog_items ci "
                          "LEFT JOIN copies c ON ci.id = c.catalog_item_id "
                          "LEFT JOIN loans l ON c.id = l.copy_id "
                          "GROUP BY ci.id "
                          "HAVING last_checkout IS NULL OR last_checkout < "
                          "date('now', '-' || ? || ' months') "
                          "ORDER BY last_checkout",
                          {months});

    res.set_content(json{{"data", items},
                         {"count", items.size()},
                         {"months", std::stoi(months)}}
                        .dump(),
                    "application/json");
  });

  // GET /api/reports/members
  svr.Get("/api/reports/members", [](const httplib::Request &req,
                                     httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"branch_manager", "admin"}))
      return;

    auto &db = Database::instance();
    std::string period = req.get_param_value("period");
    if (period.empty())
      period = "90";
    std::string limit = req.get_param_value("limit");
    if (limit.empty())
      limit = "20";

    auto topBorrowers =
        db.query("SELECT m.id, m.first_name, m.last_name, m.membership_type, "
                 "COUNT(l.id) as checkout_count "
                 "FROM members m JOIN loans l ON m.id = l.member_id "
                 "WHERE l.checkout_date >= date('now', '-' || ? || ' days') "
                 "GROUP BY m.id ORDER BY checkout_count DESC LIMIT ?",
                 {period, limit});

    auto summary = db.queryOne(
        "SELECT "
        "(SELECT COUNT(*) FROM members WHERE status = 'active') as active, "
        "(SELECT COUNT(*) FROM members WHERE status = 'suspended') as "
        "suspended, "
        "(SELECT COUNT(*) FROM members WHERE status = 'expired') as expired, "
        "(SELECT COUNT(*) FROM members) as total");

    res.set_content(
        json{{"top_borrowers", topBorrowers}, {"summary", summary}}.dump(),
        "application/json");
  });

  // GET /api/reports/export/:type - CSV export
  svr.Get(R"(/api/reports/export/(\w+))", [](const httplib::Request &req,
                                             httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"branch_manager", "admin"}))
      return;

    auto &db = Database::instance();
    std::string type = req.matches[1];
    json data;

    if (type == "overdue") {
      data = db.query("SELECT l.id, l.due_date, c.barcode, ci.title, "
                      "m.first_name, m.last_name, m.email "
                      "FROM loans l JOIN copies c ON l.copy_id = c.id "
                      "JOIN catalog_items ci ON c.catalog_item_id = ci.id "
                      "JOIN members m ON l.member_id = m.id "
                      "WHERE l.status = 'active' AND l.due_date < date('now')");
    } else if (type == "members") {
      data = db.query(
          "SELECT id, first_name, last_name, email, phone, membership_type, "
          "status, start_date, expiry_date FROM members");
    } else if (type == "catalog") {
      data =
          db.query("SELECT id, title, authors, isbn, publisher, "
                   "publication_year, item_type, language FROM catalog_items");
    } else {
      res.status = 400;
      res.set_content(json{{"error", "Unknown report type"}}.dump(),
                      "application/json");
      return;
    }

    // Convert to CSV
    std::string csv;
    if (!data.empty()) {
      // Header
      for (auto it = data[0].begin(); it != data[0].end(); ++it) {
        if (it != data[0].begin())
          csv += ",";
        csv += "\"" + it.key() + "\"";
      }
      csv += "\n";
      // Rows
      for (const auto &row : data) {
        bool first = true;
        for (auto it = row.begin(); it != row.end(); ++it) {
          if (!first)
            csv += ",";
          first = false;
          if (it->is_string())
            csv += "\"" + it->get<std::string>() + "\"";
          else
            csv += it->dump();
        }
        csv += "\n";
      }
    }

    res.set_header("Content-Disposition",
                   "attachment; filename=" + type + "_report.csv");
    res.set_content(csv, "text/csv");
  });
}

} // namespace ReportController
