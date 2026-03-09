#pragma once

#include "db/database.h"
#include "httplib.h"
#include "json.hpp"
#include "middleware/auth_middleware.h"

using json = nlohmann::json;

namespace CirculationController {

// Get circulation rules for member+item type combo
inline json getRules(Database &db, const std::string &membershipType,
                     const std::string &itemType) {
  auto rules =
      db.queryOne("SELECT * FROM circulation_rules WHERE membership_type = ? "
                  "AND item_type = ? AND is_active = 1",
                  {membershipType, itemType});
  if (rules.is_null()) {
    // Default fallback
    return json{{"loan_period_days", 14}, {"max_loans", 5},
                {"max_renewals", 2},      {"fine_per_day", 0.50},
                {"fine_cap", 25.00},      {"grace_period_days", 0}};
  }
  return rules;
}

inline void registerRoutes(httplib::Server &svr) {

  // POST /api/circulation/checkout
  svr.Post("/api/circulation/checkout", [](const httplib::Request &req,
                                           httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"librarian", "branch_manager", "admin"}))
      return;

    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(json{{"error", "Invalid JSON"}}.dump(),
                      "application/json");
      return;
    }

    if (!body.contains("member_id") || !body.contains("barcode")) {
      res.status = 400;
      res.set_content(json{{"error", "member_id and barcode required"}}.dump(),
                      "application/json");
      return;
    }

    auto &db = Database::instance();
    std::string memberId = std::to_string(body["member_id"].get<int>());
    std::string barcode = body["barcode"].get<std::string>();

    // Verify member
    auto member = db.queryOne("SELECT * FROM members WHERE id = ?", {memberId});
    if (member.is_null()) {
      res.status = 404;
      res.set_content(json{{"error", "Member not found"}}.dump(),
                      "application/json");
      return;
    }
    if (member["status"].get<std::string>() != "active") {
      res.status = 403;
      res.set_content(json{{"error", "Member is not active"}}.dump(),
                      "application/json");
      return;
    }

    // Verify copy
    auto copy =
        db.queryOne("SELECT c.*, ci.item_type FROM copies c "
                    "JOIN catalog_items ci ON c.catalog_item_id = ci.id "
                    "WHERE c.barcode = ?",
                    {barcode});
    if (copy.is_null()) {
      res.status = 404;
      res.set_content(
          json{{"error", "Item not found with barcode: " + barcode}}.dump(),
          "application/json");
      return;
    }
    if (copy["status"].get<std::string>() != "available") {
      res.status = 409;
      res.set_content(
          json{{"error", "Item is not available (status: " +
                             copy["status"].get<std::string>() + ")"}}
              .dump(),
          "application/json");
      return;
    }

    // Check circulation rules
    auto rules = getRules(db, member["membership_type"].get<std::string>(),
                          copy["item_type"].get<std::string>());

    // Check max loans
    auto loanCount = db.queryOne("SELECT COUNT(*) as count FROM loans WHERE "
                                 "member_id = ? AND status = 'active'",
                                 {memberId});
    if (!loanCount.is_null() &&
        loanCount["count"].get<int>() >= rules["max_loans"].get<int>()) {
      res.status = 403;
      res.set_content(
          json{{"error", "Maximum loan limit reached (" +
                             std::to_string(rules["max_loans"].get<int>()) +
                             ")"}}
              .dump(),
          "application/json");
      return;
    }

    // Check outstanding fines threshold
    auto fineTotal = db.queryOne(
        "SELECT COALESCE(SUM(amount - paid_amount), 0) as total FROM fines "
        "WHERE member_id = ? AND status IN ('unpaid', 'partial')",
        {memberId});
    if (!fineTotal.is_null() && fineTotal["total"].get<double>() > 10.0) {
      res.status = 403;
      res.set_content(
          json{{"error",
                "Outstanding fines exceed threshold. Please pay fines first."}}
              .dump(),
          "application/json");
      return;
    }

    // Create loan
    int loanDays = rules["loan_period_days"].get<int>();
    std::string copyId = std::to_string(copy["id"].get<int>());

    db.execute(
        "INSERT INTO loans (copy_id, member_id, staff_id, due_date, status) "
        "VALUES (?, ?, ?, date('now', '+' || ? || ' days'), 'active')",
        {copyId, memberId, std::to_string(user.userId),
         std::to_string(loanDays)});

    // Update copy status
    db.execute("UPDATE copies SET status = 'on_loan', updated_at = "
               "datetime('now') WHERE id = ?",
               {copyId});

    int64_t loanId = db.lastInsertId();
    auto loan =
        db.queryOne("SELECT l.*, c.barcode, ci.title, ci.authors FROM loans l "
                    "JOIN copies c ON l.copy_id = c.id "
                    "JOIN catalog_items ci ON c.catalog_item_id = ci.id "
                    "WHERE l.id = ?",
                    {std::to_string(loanId)});

    // Audit
    db.execute("INSERT INTO audit_log (user_id, action, entity_type, "
               "entity_id, details) VALUES (?, 'checkout', 'loan', ?, ?)",
               {std::to_string(user.userId), std::to_string(loanId),
                "Checkout: " + barcode + " to member " + memberId});

    res.status = 201;
    res.set_content(loan.dump(), "application/json");
  });

  // POST /api/circulation/checkin
  svr.Post("/api/circulation/checkin", [](const httplib::Request &req,
                                          httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"librarian", "branch_manager", "admin"}))
      return;

    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(json{{"error", "Invalid JSON"}}.dump(),
                      "application/json");
      return;
    }

    if (!body.contains("barcode")) {
      res.status = 400;
      res.set_content(json{{"error", "barcode required"}}.dump(),
                      "application/json");
      return;
    }

    auto &db = Database::instance();
    std::string barcode = body["barcode"].get<std::string>();

    // Find active loan for this barcode
    auto loan =
        db.queryOne("SELECT l.*, c.barcode, c.catalog_item_id, ci.title, "
                    "ci.item_type, m.membership_type "
                    "FROM loans l "
                    "JOIN copies c ON l.copy_id = c.id "
                    "JOIN catalog_items ci ON c.catalog_item_id = ci.id "
                    "JOIN members m ON l.member_id = m.id "
                    "WHERE c.barcode = ? AND l.status = 'active'",
                    {barcode});

    if (loan.is_null()) {
      res.status = 404;
      res.set_content(
          json{{"error", "No active loan found for barcode: " + barcode}}
              .dump(),
          "application/json");
      return;
    }

    std::string loanId = std::to_string(loan["id"].get<int>());
    std::string copyId = std::to_string(loan["copy_id"].get<int>());
    std::string memberId = std::to_string(loan["member_id"].get<int>());

    // Calculate overdue fine
    double fineAmount = 0.0;
    auto overdueCheck =
        db.queryOne("SELECT julianday('now') - julianday(due_date) as "
                    "days_overdue FROM loans WHERE id = ?",
                    {loanId});

    json result = loan;
    result["return_date"] = "now";
    result["fine"] = nullptr;

    if (!overdueCheck.is_null() &&
        overdueCheck["days_overdue"].get<double>() > 0) {
      int daysOverdue =
          static_cast<int>(overdueCheck["days_overdue"].get<double>());
      auto rules = getRules(db, loan["membership_type"].get<std::string>(),
                            loan["item_type"].get<std::string>());
      int grace = rules["grace_period_days"].get<int>();
      daysOverdue -= grace;
      if (daysOverdue > 0) {
        fineAmount = daysOverdue * rules["fine_per_day"].get<double>();
        double cap = rules["fine_cap"].get<double>();
        if (fineAmount > cap)
          fineAmount = cap;

        db.execute("INSERT INTO fines (member_id, loan_id, amount, reason) "
                   "VALUES (?, ?, ?, 'overdue')",
                   {memberId, loanId, std::to_string(fineAmount)});

        result["fine"] = {{"amount", fineAmount},
                          {"days_overdue", daysOverdue}};
      }
    }

    // Update loan
    db.execute("UPDATE loans SET return_date = datetime('now'), status = "
               "'returned', updated_at = datetime('now') WHERE id = ?",
               {loanId});

    // Check if there's a hold for this item
    std::string catalogItemId =
        std::to_string(loan["catalog_item_id"].get<int>());
    auto nextHold =
        db.queryOne("SELECT * FROM holds WHERE catalog_item_id = ? AND status "
                    "= 'pending' ORDER BY queue_position, hold_date LIMIT 1",
                    {catalogItemId});

    if (!nextHold.is_null()) {
      db.execute("UPDATE copies SET status = 'on_hold', updated_at = "
                 "datetime('now') WHERE id = ?",
                 {copyId});
      db.execute(
          "UPDATE holds SET status = 'ready', notification_date = "
          "datetime('now'), expiry_date = date('now', '+3 days') WHERE id = ?",
          {std::to_string(nextHold["id"].get<int>())});
      result["routed_to_hold"] = true;
    } else {
      db.execute("UPDATE copies SET status = 'available', updated_at = "
                 "datetime('now') WHERE id = ?",
                 {copyId});
      result["routed_to_hold"] = false;
    }

    db.execute("INSERT INTO audit_log (user_id, action, entity_type, "
               "entity_id, details) VALUES (?, 'checkin', 'loan', ?, ?)",
               {std::to_string(user.userId), loanId, "Checkin: " + barcode});

    res.set_content(result.dump(), "application/json");
  });

  // POST /api/circulation/renew
  svr.Post("/api/circulation/renew", [](const httplib::Request &req,
                                        httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireAuth(req, res, user))
      return;

    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(json{{"error", "Invalid JSON"}}.dump(),
                      "application/json");
      return;
    }

    if (!body.contains("loan_id")) {
      res.status = 400;
      res.set_content(json{{"error", "loan_id required"}}.dump(),
                      "application/json");
      return;
    }

    auto &db = Database::instance();
    std::string loanId = std::to_string(body["loan_id"].get<int>());

    auto loan =
        db.queryOne("SELECT l.*, ci.item_type, m.membership_type FROM loans l "
                    "JOIN copies c ON l.copy_id = c.id "
                    "JOIN catalog_items ci ON c.catalog_item_id = ci.id "
                    "JOIN members m ON l.member_id = m.id "
                    "WHERE l.id = ? AND l.status = 'active'",
                    {loanId});

    if (loan.is_null()) {
      res.status = 404;
      res.set_content(json{{"error", "Active loan not found"}}.dump(),
                      "application/json");
      return;
    }

    // Members can only renew their own loans
    if (user.role == "member") {
      auto ua = db.queryOne("SELECT member_id FROM user_accounts WHERE id = ?",
                            {std::to_string(user.userId)});
      if (ua.is_null() ||
          ua["member_id"].get<int>() != loan["member_id"].get<int>()) {
        res.status = 403;
        res.set_content(
            json{{"error", "Cannot renew other member's loans"}}.dump(),
            "application/json");
        return;
      }
    }

    auto rules = getRules(db, loan["membership_type"].get<std::string>(),
                          loan["item_type"].get<std::string>());

    if (loan["renewals_count"].get<int>() >= rules["max_renewals"].get<int>()) {
      res.status = 403;
      res.set_content(json{{"error", "Maximum renewals reached"}}.dump(),
                      "application/json");
      return;
    }

    // Check no pending holds on this item's copies
    auto holdCheck =
        db.queryOne("SELECT COUNT(*) as count FROM holds h "
                    "JOIN copies c ON h.catalog_item_id = c.catalog_item_id "
                    "WHERE c.id = ? AND h.status = 'pending'",
                    {std::to_string(loan["copy_id"].get<int>())});
    if (!holdCheck.is_null() && holdCheck["count"].get<int>() > 0) {
      res.status = 403;
      res.set_content(
          json{{"error", "Item has pending holds; cannot renew"}}.dump(),
          "application/json");
      return;
    }

    int loanDays = rules["loan_period_days"].get<int>();
    db.execute("UPDATE loans SET due_date = date('now', '+' || ? || ' days'), "
               "renewals_count = renewals_count + 1, updated_at = "
               "datetime('now') WHERE id = ?",
               {std::to_string(loanDays), loanId});

    auto updated = db.queryOne("SELECT * FROM loans WHERE id = ?", {loanId});
    res.set_content(updated.dump(), "application/json");
  });

  // GET /api/loans - Get loans (filterable)
  svr.Get("/api/loans", [](const httplib::Request &req,
                           httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireAuth(req, res, user))
      return;

    auto &db = Database::instance();
    std::string sql = "SELECT l.*, c.barcode, ci.title, ci.authors, "
                      "m.first_name, m.last_name "
                      "FROM loans l JOIN copies c ON l.copy_id = c.id "
                      "JOIN catalog_items ci ON c.catalog_item_id = ci.id "
                      "JOIN members m ON l.member_id = m.id";

    std::vector<std::string> params;
    std::string memberId = req.get_param_value("member_id");
    std::string status = req.get_param_value("status");

    // Members can only see their own loans
    if (user.role == "member") {
      auto ua = db.queryOne("SELECT member_id FROM user_accounts WHERE id = ?",
                            {std::to_string(user.userId)});
      if (!ua.is_null()) {
        sql += " WHERE l.member_id = ?";
        params.push_back(std::to_string(ua["member_id"].get<int>()));
      }
    } else {
      if (!memberId.empty()) {
        sql += " WHERE l.member_id = ?";
        params.push_back(memberId);
      }
    }

    if (!status.empty()) {
      sql += (params.empty() ? " WHERE" : " AND");
      sql += " l.status = ?";
      params.push_back(status);
    }

    sql += " ORDER BY l.checkout_date DESC LIMIT 100";
    auto loans = db.query(sql, params);
    res.set_content(json{{"data", loans}, {"count", loans.size()}}.dump(),
                    "application/json");
  });

  // ============ HOLDS ============

  // POST /api/holds - Place a hold
  svr.Post("/api/holds", [](const httplib::Request &req,
                            httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireAuth(req, res, user))
      return;

    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(json{{"error", "Invalid JSON"}}.dump(),
                      "application/json");
      return;
    }

    if (!body.contains("catalog_item_id") ||
        !body.contains("pickup_branch_id")) {
      res.status = 400;
      res.set_content(
          json{{"error", "catalog_item_id and pickup_branch_id required"}}
              .dump(),
          "application/json");
      return;
    }

    auto &db = Database::instance();
    std::string itemId = std::to_string(body["catalog_item_id"].get<int>());
    std::string branchId = std::to_string(body["pickup_branch_id"].get<int>());

    // Determine member_id
    std::string memberId;
    if (body.contains("member_id") && user.role != "member") {
      memberId = std::to_string(body["member_id"].get<int>());
    } else {
      auto ua = db.queryOne("SELECT member_id FROM user_accounts WHERE id = ?",
                            {std::to_string(user.userId)});
      if (ua.is_null() || ua["member_id"].is_null()) {
        res.status = 400;
        res.set_content(json{{"error", "No member account linked"}}.dump(),
                        "application/json");
        return;
      }
      memberId = std::to_string(ua["member_id"].get<int>());
    }

    // Get next queue position
    auto maxPos = db.queryOne(
        "SELECT COALESCE(MAX(queue_position), 0) + 1 as next_pos FROM holds "
        "WHERE catalog_item_id = ? AND status = 'pending'",
        {itemId});
    int pos = maxPos.is_null() ? 1 : maxPos["next_pos"].get<int>();

    db.execute("INSERT INTO holds (catalog_item_id, member_id, "
               "pickup_branch_id, queue_position) VALUES (?, ?, ?, ?)",
               {itemId, memberId, branchId, std::to_string(pos)});

    int64_t holdId = db.lastInsertId();
    auto hold = db.queryOne("SELECT * FROM holds WHERE id = ?",
                            {std::to_string(holdId)});
    res.status = 201;
    res.set_content(hold.dump(), "application/json");
  });

  // GET /api/holds
  svr.Get("/api/holds", [](const httplib::Request &req,
                           httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireAuth(req, res, user))
      return;

    auto &db = Database::instance();
    std::string sql =
        "SELECT h.*, ci.title, ci.authors, b.name as branch_name "
        "FROM holds h JOIN catalog_items ci ON h.catalog_item_id = ci.id "
        "JOIN branches b ON h.pickup_branch_id = b.id";
    std::vector<std::string> params;

    if (user.role == "member") {
      auto ua = db.queryOne("SELECT member_id FROM user_accounts WHERE id = ?",
                            {std::to_string(user.userId)});
      if (!ua.is_null()) {
        sql += " WHERE h.member_id = ?";
        params.push_back(std::to_string(ua["member_id"].get<int>()));
      }
    }

    sql += " ORDER BY h.hold_date DESC LIMIT 100";
    auto holds = db.query(sql, params);
    res.set_content(json{{"data", holds}, {"count", holds.size()}}.dump(),
                    "application/json");
  });

  // DELETE /api/holds/:id - Cancel hold
  svr.Delete(R"(/api/holds/(\d+))",
             [](const httplib::Request &req, httplib::Response &res) {
               AuthMiddleware::AuthUser user;
               if (!AuthMiddleware::requireAuth(req, res, user))
                 return;

               std::string id = req.matches[1];
               auto &db = Database::instance();

               db.execute("UPDATE holds SET status = 'cancelled', updated_at = "
                          "datetime('now') WHERE id = ?",
                          {id});
               res.set_content(json{{"message", "Hold cancelled"}}.dump(),
                               "application/json");
             });
}

} // namespace CirculationController
