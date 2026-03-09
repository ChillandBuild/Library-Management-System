#pragma once

#include "db/database.h"
#include "httplib.h"
#include "json.hpp"
#include "middleware/auth_middleware.h"
#include "utils/password.h"

using json = nlohmann::json;

namespace MemberController {

inline void registerRoutes(httplib::Server &svr) {

  // GET /api/members - List all members (staff only)
  svr.Get("/api/members", [](const httplib::Request &req,
                             httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"librarian", "branch_manager", "admin"}))
      return;

    auto &db = Database::instance();
    std::string sql = "SELECT * FROM members ORDER BY last_name, first_name";

    // Optional filters
    std::string status = req.get_param_value("status");
    std::string type = req.get_param_value("membership_type");
    std::string search = req.get_param_value("q");

    std::vector<std::string> params;
    std::vector<std::string> conditions;

    if (!status.empty()) {
      conditions.push_back("status = ?");
      params.push_back(status);
    }
    if (!type.empty()) {
      conditions.push_back("membership_type = ?");
      params.push_back(type);
    }
    if (!search.empty()) {
      conditions.push_back(
          "(first_name LIKE ? OR last_name LIKE ? OR email LIKE ?)");
      params.push_back("%" + search + "%");
      params.push_back("%" + search + "%");
      params.push_back("%" + search + "%");
    }

    if (!conditions.empty()) {
      sql = "SELECT * FROM members WHERE ";
      for (size_t i = 0; i < conditions.size(); i++) {
        if (i > 0)
          sql += " AND ";
        sql += conditions[i];
      }
      sql += " ORDER BY last_name, first_name";
    }

    // Pagination
    int page = 1, perPage = 50;
    if (!req.get_param_value("page").empty())
      page = std::stoi(req.get_param_value("page"));
    if (!req.get_param_value("per_page").empty())
      perPage = std::stoi(req.get_param_value("per_page"));
    int offset = (page - 1) * perPage;

    sql += " LIMIT " + std::to_string(perPage) + " OFFSET " +
           std::to_string(offset);

    auto members = db.query(sql, params);

    json response = {{"data", members},
                     {"page", page},
                     {"per_page", perPage},
                     {"count", members.size()}};

    res.set_content(response.dump(), "application/json");
  });

  // GET /api/members/:id - Get single member
  svr.Get(R"(/api/members/(\d+))", [](const httplib::Request &req,
                                      httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireAuth(req, res, user))
      return;

    std::string id = req.matches[1];
    auto &db = Database::instance();
    auto member = db.queryOne("SELECT * FROM members WHERE id = ?", {id});

    if (member.is_null()) {
      res.status = 404;
      res.set_content(json{{"error", "Member not found"}}.dump(),
                      "application/json");
      return;
    }

    // Also fetch active loans
    auto loanCount = db.queryOne("SELECT COUNT(*) as active_loans FROM loans "
                                 "WHERE member_id = ? AND status = 'active'",
                                 {id});
    member["active_loans"] =
        loanCount.is_null() ? 0 : loanCount["active_loans"].get<int>();

    // Fetch outstanding fines
    auto fineTotal = db.queryOne(
        "SELECT COALESCE(SUM(amount - paid_amount), 0) as outstanding_fines "
        "FROM fines WHERE member_id = ? AND status IN ('unpaid', 'partial')",
        {id});
    member["outstanding_fines"] =
        fineTotal.is_null() ? 0.0
                            : fineTotal["outstanding_fines"].get<double>();

    res.set_content(member.dump(), "application/json");
  });

  // POST /api/members - Create new member (staff only)
  svr.Post("/api/members", [](const httplib::Request &req,
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
      res.set_content(json{{"error", "Invalid JSON body"}}.dump(),
                      "application/json");
      return;
    }

    if (!body.contains("first_name") || !body.contains("last_name")) {
      res.status = 400;
      res.set_content(
          json{{"error", "first_name and last_name are required"}}.dump(),
          "application/json");
      return;
    }

    auto &db = Database::instance();

    std::string firstName = body["first_name"].get<std::string>();
    std::string lastName = body["last_name"].get<std::string>();
    std::string email = body.value("email", "");
    std::string phone = body.value("phone", "");
    std::string address = body.value("address", "");
    std::string dob = body.value("date_of_birth", "");
    std::string membershipType = body.value("membership_type", "standard");

    // Check email uniqueness if provided
    if (!email.empty()) {
      auto existing =
          db.queryOne("SELECT id FROM members WHERE email = ?", {email});
      if (!existing.is_null()) {
        res.status = 409;
        res.set_content(json{{"error", "Email already registered"}}.dump(),
                        "application/json");
        return;
      }
    }

    bool ok = db.execute(
        "INSERT INTO members (first_name, last_name, email, phone, address, "
        "date_of_birth, membership_type) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        {firstName, lastName, email, phone, address, dob, membershipType});

    if (!ok) {
      res.status = 500;
      res.set_content(json{{"error", "Failed to create member"}}.dump(),
                      "application/json");
      return;
    }

    int64_t memberId = db.lastInsertId();

    // Optionally create user account
    bool createAccount = body.value("create_account", false);
    if (createAccount && !email.empty()) {
      std::string username = body.value("username", email);
      std::string password = body.value("password", "changeme123");
      std::string salt = PasswordUtil::generateSalt();
      std::string hash = PasswordUtil::hashPassword(password, salt);

      db.execute("INSERT INTO user_accounts (username, password_hash, salt, "
                 "role, member_id) "
                 "VALUES (?, ?, ?, 'member', ?)",
                 {username, hash, salt, std::to_string(memberId)});
    }

    // Audit log
    db.execute("INSERT INTO audit_log (user_id, action, entity_type, "
               "entity_id, details) "
               "VALUES (?, 'create', 'member', ?, ?)",
               {std::to_string(user.userId), std::to_string(memberId),
                "Created member: " + firstName + " " + lastName});

    auto created = db.queryOne("SELECT * FROM members WHERE id = ?",
                               {std::to_string(memberId)});
    res.status = 201;
    res.set_content(created.dump(), "application/json");
  });

  // PUT /api/members/:id - Update member
  svr.Put(R"(/api/members/(\d+))", [](const httplib::Request &req,
                                      httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireAuth(req, res, user))
      return;

    std::string id = req.matches[1];
    auto &db = Database::instance();

    // Members can only update their own profile
    if (user.role == "member") {
      auto ua = db.queryOne("SELECT member_id FROM user_accounts WHERE id = ?",
                            {std::to_string(user.userId)});
      if (ua.is_null() || std::to_string(ua["member_id"].get<int>()) != id) {
        res.status = 403;
        res.set_content(json{{"error", "Cannot update other members"}}.dump(),
                        "application/json");
        return;
      }
    }

    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(json{{"error", "Invalid JSON"}}.dump(),
                      "application/json");
      return;
    }

    std::vector<std::string> sets;
    std::vector<std::string> params;

    auto addField = [&](const std::string &jsonKey, const std::string &dbCol) {
      if (body.contains(jsonKey)) {
        sets.push_back(dbCol + " = ?");
        params.push_back(body[jsonKey].get<std::string>());
      }
    };

    addField("first_name", "first_name");
    addField("last_name", "last_name");
    addField("email", "email");
    addField("phone", "phone");
    addField("address", "address");
    addField("date_of_birth", "date_of_birth");
    addField("membership_type", "membership_type");
    addField("status", "status");
    addField("expiry_date", "expiry_date");
    addField("notes", "notes");

    if (sets.empty()) {
      res.status = 400;
      res.set_content(json{{"error", "No fields to update"}}.dump(),
                      "application/json");
      return;
    }

    sets.push_back("updated_at = datetime('now')");
    params.push_back(id);

    std::string sql = "UPDATE members SET ";
    for (size_t i = 0; i < sets.size(); i++) {
      if (i > 0)
        sql += ", ";
      sql += sets[i];
    }
    sql += " WHERE id = ?";

    db.execute(sql, params);

    auto updated = db.queryOne("SELECT * FROM members WHERE id = ?", {id});
    res.set_content(updated.dump(), "application/json");
  });

  // DELETE /api/members/:id - Delete member (admin only)
  svr.Delete(R"(/api/members/(\d+))", [](const httplib::Request &req,
                                         httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user, {"admin"}))
      return;

    std::string id = req.matches[1];
    auto &db = Database::instance();

    // Check for active loans
    auto activeLoan = db.queryOne("SELECT COUNT(*) as count FROM loans WHERE "
                                  "member_id = ? AND status = 'active'",
                                  {id});
    if (!activeLoan.is_null() && activeLoan["count"].get<int>() > 0) {
      res.status = 409;
      res.set_content(
          json{{"error", "Member has active loans; cannot delete"}}.dump(),
          "application/json");
      return;
    }

    db.execute("DELETE FROM user_accounts WHERE member_id = ?", {id});
    db.execute("DELETE FROM members WHERE id = ?", {id});

    res.set_content(json{{"message", "Member deleted"}}.dump(),
                    "application/json");
  });
}

} // namespace MemberController
