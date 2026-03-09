#pragma once

#include "db/database.h"
#include "httplib.h"
#include "json.hpp"
#include "middleware/auth_middleware.h"

using json = nlohmann::json;

namespace FineController {

inline void registerRoutes(httplib::Server &svr) {

  // GET /api/fines - List fines
  svr.Get("/api/fines", [](const httplib::Request &req,
                           httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireAuth(req, res, user))
      return;

    auto &db = Database::instance();
    std::string sql = "SELECT f.*, m.first_name, m.last_name, ci.title "
                      "FROM fines f JOIN members m ON f.member_id = m.id "
                      "LEFT JOIN loans l ON f.loan_id = l.id "
                      "LEFT JOIN copies c ON l.copy_id = c.id "
                      "LEFT JOIN catalog_items ci ON c.catalog_item_id = ci.id";
    std::vector<std::string> params;

    if (user.role == "member") {
      auto ua = db.queryOne("SELECT member_id FROM user_accounts WHERE id = ?",
                            {std::to_string(user.userId)});
      if (!ua.is_null()) {
        sql += " WHERE f.member_id = ?";
        params.push_back(std::to_string(ua["member_id"].get<int>()));
      }
    } else {
      std::string memberId = req.get_param_value("member_id");
      std::string status = req.get_param_value("status");
      bool hasWhere = false;
      if (!memberId.empty()) {
        sql += " WHERE f.member_id = ?";
        params.push_back(memberId);
        hasWhere = true;
      }
      if (!status.empty()) {
        sql += (hasWhere ? " AND" : " WHERE");
        sql += " f.status = ?";
        params.push_back(status);
      }
    }

    sql += " ORDER BY f.created_at DESC LIMIT 100";
    auto fines = db.query(sql, params);
    res.set_content(json{{"data", fines}, {"count", fines.size()}}.dump(),
                    "application/json");
  });

  // POST /api/fines/:id/pay - Record payment
  svr.Post(R"(/api/fines/(\d+)/pay)", [](const httplib::Request &req,
                                         httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"librarian", "branch_manager", "admin"}))
      return;

    std::string id = req.matches[1];
    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(json{{"error", "Invalid JSON"}}.dump(),
                      "application/json");
      return;
    }

    auto &db = Database::instance();
    auto fine = db.queryOne("SELECT * FROM fines WHERE id = ?", {id});
    if (fine.is_null()) {
      res.status = 404;
      res.set_content(json{{"error", "Fine not found"}}.dump(),
                      "application/json");
      return;
    }

    double amount = body.value("amount", 0.0);
    std::string method = body.value("payment_method", "cash");
    std::string notes = body.value("notes", "");
    bool isWaiver = body.value("waive", false);

    if (isWaiver) {
      db.execute("UPDATE fines SET status = 'waived', updated_at = "
                 "datetime('now') WHERE id = ?",
                 {id});
      db.execute("INSERT INTO fine_payments (fine_id, amount, payment_method, "
                 "staff_id, notes) VALUES (?, ?, 'waiver', ?, ?)",
                 {id,
                  std::to_string(fine["amount"].get<double>() -
                                 fine["paid_amount"].get<double>()),
                  std::to_string(user.userId),
                  notes.empty() ? "Waived by staff" : notes});
    } else {
      if (amount <= 0) {
        res.status = 400;
        res.set_content(json{{"error", "amount must be positive"}}.dump(),
                        "application/json");
        return;
      }

      db.execute("INSERT INTO fine_payments (fine_id, amount, payment_method, "
                 "staff_id, notes) VALUES (?, ?, ?, ?, ?)",
                 {id, std::to_string(amount), method,
                  std::to_string(user.userId), notes});

      double newPaid = fine["paid_amount"].get<double>() + amount;
      std::string newStatus =
          (newPaid >= fine["amount"].get<double>()) ? "paid" : "partial";
      db.execute("UPDATE fines SET paid_amount = ?, status = ?, updated_at = "
                 "datetime('now') WHERE id = ?",
                 {std::to_string(newPaid), newStatus, id});
    }

    auto updated = db.queryOne("SELECT * FROM fines WHERE id = ?", {id});
    auto payments = db.query(
        "SELECT * FROM fine_payments WHERE fine_id = ? ORDER BY created_at",
        {id});
    updated["payments"] = payments;

    db.execute(
        "INSERT INTO audit_log (user_id, action, entity_type, entity_id, "
        "details) VALUES (?, 'fine_payment', 'fine', ?, ?)",
        {std::to_string(user.userId), id,
         isWaiver ? "Fine waived" : "Payment: $" + std::to_string(amount)});

    res.set_content(updated.dump(), "application/json");
  });
}

} // namespace FineController
