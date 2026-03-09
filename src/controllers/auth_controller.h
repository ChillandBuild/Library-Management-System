#pragma once

#include "db/database.h"
#include "httplib.h"
#include "json.hpp"
#include "jwt.h"
#include "middleware/auth_middleware.h"
#include "utils/password.h"

using json = nlohmann::json;

namespace AuthController {

inline void registerRoutes(httplib::Server &svr) {

  // POST /api/auth/login
  svr.Post("/api/auth/login", [](const httplib::Request &req,
                                 httplib::Response &res) {
    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(json{{"error", "Invalid JSON body"}}.dump(),
                      "application/json");
      return;
    }

    if (!body.contains("username") || !body.contains("password")) {
      res.status = 400;
      res.set_content(json{{"error", "Username and password required"}}.dump(),
                      "application/json");
      return;
    }

    std::string username = body["username"].get<std::string>();
    std::string password = body["password"].get<std::string>();

    auto &db = Database::instance();
    auto user = db.queryOne("SELECT id, username, password_hash, salt, role, "
                            "member_id, branch_id, is_active "
                            "FROM user_accounts WHERE username = ?",
                            {username});

    if (user.is_null()) {
      res.status = 401;
      res.set_content(json{{"error", "Invalid credentials"}}.dump(),
                      "application/json");
      return;
    }

    if (user["is_active"].get<int>() != 1) {
      res.status = 403;
      res.set_content(json{{"error", "Account is disabled"}}.dump(),
                      "application/json");
      return;
    }

    std::string storedHash = user["password_hash"].get<std::string>();
    std::string salt = user["salt"].get<std::string>();

    if (!PasswordUtil::verifyPassword(password, storedHash, salt)) {
      res.status = 401;
      res.set_content(json{{"error", "Invalid credentials"}}.dump(),
                      "application/json");
      return;
    }

    // Update last login
    db.execute(
        "UPDATE user_accounts SET last_login = datetime('now') WHERE id = ?",
        {std::to_string(user["id"].get<int>())});

    // Create JWT
    std::string token = JWT::createToken(user["id"].get<int>(),
                                         user["username"].get<std::string>(),
                                         user["role"].get<std::string>());

    json response = {{"token", token},
                     {"user",
                      {{"id", user["id"]},
                       {"username", user["username"]},
                       {"role", user["role"]},
                       {"member_id", user["member_id"]},
                       {"branch_id", user["branch_id"]}}}};

    // Audit log
    db.execute("INSERT INTO audit_log (user_id, action, entity_type, "
               "entity_id, details) "
               "VALUES (?, 'login', 'user_account', ?, ?)",
               {std::to_string(user["id"].get<int>()),
                std::to_string(user["id"].get<int>()), "User logged in"});

    res.set_content(response.dump(), "application/json");
  });

  // GET /api/auth/me
  svr.Get(
      "/api/auth/me", [](const httplib::Request &req, httplib::Response &res) {
        AuthMiddleware::AuthUser authUser;
        if (!AuthMiddleware::requireAuth(req, res, authUser))
          return;

        auto &db = Database::instance();
        auto user =
            db.queryOne("SELECT ua.id, ua.username, ua.role, ua.member_id, "
                        "ua.branch_id, ua.last_login, "
                        "m.first_name, m.last_name, m.email AS member_email "
                        "FROM user_accounts ua "
                        "LEFT JOIN members m ON ua.member_id = m.id "
                        "WHERE ua.id = ?",
                        {std::to_string(authUser.userId)});

        if (user.is_null()) {
          res.status = 404;
          res.set_content(json{{"error", "User not found"}}.dump(),
                          "application/json");
          return;
        }

        res.set_content(user.dump(), "application/json");
      });
}

} // namespace AuthController
