#pragma once

#include "httplib.h"
#include "json.hpp"
#include "jwt.h"
#include <functional>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace AuthMiddleware {

struct AuthUser {
  int userId;
  std::string username;
  std::string role;
};

// Extract JWT token from Authorization header
inline std::string extractToken(const httplib::Request &req) {
  auto auth = req.get_header_value("Authorization");
  if (auth.substr(0, 7) == "Bearer ") {
    return auth.substr(7);
  }
  return "";
}

// Authenticate request and populate user info
inline bool authenticate(const httplib::Request &req, AuthUser &user) {
  std::string token = extractToken(req);
  if (token.empty())
    return false;

  json payload = JWT::verifyToken(token);
  if (payload.is_null())
    return false;

  user.userId = payload["user_id"].get<int>();
  user.username = payload["username"].get<std::string>();
  user.role = payload["role"].get<std::string>();
  return true;
}

// Check if user has required role
inline bool hasRole(const AuthUser &user,
                    const std::vector<std::string> &roles) {
  for (const auto &role : roles) {
    if (user.role == role)
      return true;
  }
  return false;
}

// Middleware: Require authentication
inline bool requireAuth(const httplib::Request &req, httplib::Response &res,
                        AuthUser &user) {
  if (!authenticate(req, user)) {
    res.status = 401;
    res.set_content(json{{"error", "Authentication required"}}.dump(),
                    "application/json");
    return false;
  }
  return true;
}

// Middleware: Require specific roles
inline bool requireRole(const httplib::Request &req, httplib::Response &res,
                        AuthUser &user, const std::vector<std::string> &roles) {
  if (!requireAuth(req, res, user))
    return false;
  if (!hasRole(user, roles)) {
    res.status = 403;
    res.set_content(json{{"error", "Insufficient permissions"}}.dump(),
                    "application/json");
    return false;
  }
  return true;
}

} // namespace AuthMiddleware
