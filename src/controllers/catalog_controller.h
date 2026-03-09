#pragma once

#include "db/database.h"
#include "httplib.h"
#include "json.hpp"
#include "middleware/auth_middleware.h"

using json = nlohmann::json;

namespace CatalogController {

inline void registerRoutes(httplib::Server &svr) {

  // GET /api/catalog - List catalog items
  svr.Get("/api/catalog",
          [](const httplib::Request &req, httplib::Response &res) {
            auto &db = Database::instance();

            int page = 1, perPage = 50;
            if (!req.get_param_value("page").empty())
              page = std::stoi(req.get_param_value("page"));
            if (!req.get_param_value("per_page").empty())
              perPage = std::stoi(req.get_param_value("per_page"));
            int offset = (page - 1) * perPage;

            std::string type = req.get_param_value("item_type");
            std::string language = req.get_param_value("language");
            std::string yearFrom = req.get_param_value("year_from");
            std::string yearTo = req.get_param_value("year_to");

            std::string sql = "SELECT * FROM catalog_items";
            std::vector<std::string> params;
            std::vector<std::string> conditions;

            if (!type.empty()) {
              conditions.push_back("item_type = ?");
              params.push_back(type);
            }
            if (!language.empty()) {
              conditions.push_back("language = ?");
              params.push_back(language);
            }
            if (!yearFrom.empty()) {
              conditions.push_back("publication_year >= ?");
              params.push_back(yearFrom);
            }
            if (!yearTo.empty()) {
              conditions.push_back("publication_year <= ?");
              params.push_back(yearTo);
            }

            if (!conditions.empty()) {
              sql += " WHERE ";
              for (size_t i = 0; i < conditions.size(); i++) {
                if (i > 0)
                  sql += " AND ";
                sql += conditions[i];
              }
            }

            sql += " ORDER BY title LIMIT " + std::to_string(perPage) +
                   " OFFSET " + std::to_string(offset);

            auto items = db.query(sql, params);

            json response = {{"data", items},
                             {"page", page},
                             {"per_page", perPage},
                             {"count", items.size()}};
            res.set_content(response.dump(), "application/json");
          });

  // GET /api/catalog/:id - Get catalog item with copies
  svr.Get(R"(/api/catalog/(\d+))", [](const httplib::Request &req,
                                      httplib::Response &res) {
    std::string id = req.matches[1];
    auto &db = Database::instance();

    auto item = db.queryOne("SELECT * FROM catalog_items WHERE id = ?", {id});
    if (item.is_null()) {
      res.status = 404;
      res.set_content(json{{"error", "Item not found"}}.dump(),
                      "application/json");
      return;
    }

    // Get copies with branch info
    auto copies = db.query("SELECT c.*, b.name as branch_name FROM copies c "
                           "JOIN branches b ON c.branch_id = b.id "
                           "WHERE c.catalog_item_id = ? ORDER BY b.name",
                           {id});
    item["copies"] = copies;

    // Get active holds count
    auto holdCount =
        db.queryOne("SELECT COUNT(*) as hold_count FROM holds WHERE "
                    "catalog_item_id = ? AND status IN ('pending', 'ready')",
                    {id});
    item["hold_count"] =
        holdCount.is_null() ? 0 : holdCount["hold_count"].get<int>();

    res.set_content(item.dump(), "application/json");
  });

  // POST /api/catalog - Create catalog item (staff only)
  svr.Post("/api/catalog", [](const httplib::Request &req,
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

    if (!body.contains("title") || !body.contains("authors")) {
      res.status = 400;
      res.set_content(json{{"error", "title and authors are required"}}.dump(),
                      "application/json");
      return;
    }

    auto &db = Database::instance();

    bool ok = db.execute(
        "INSERT INTO catalog_items (title, subtitle, authors, isbn, issn, "
        "publisher, "
        "publication_year, edition, language, subjects, description, "
        "item_type) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {body.value("title", ""), body.value("subtitle", ""),
         body.value("authors", ""), body.value("isbn", ""),
         body.value("issn", ""), body.value("publisher", ""),
         body.value("publication_year", ""), body.value("edition", ""),
         body.value("language", "English"), body.value("subjects", ""),
         body.value("description", ""), body.value("item_type", "book")});

    if (!ok) {
      res.status = 500;
      res.set_content(json{{"error", "Failed to create catalog item"}}.dump(),
                      "application/json");
      return;
    }

    int64_t itemId = db.lastInsertId();

    // Audit log
    db.execute("INSERT INTO audit_log (user_id, action, entity_type, "
               "entity_id, details) "
               "VALUES (?, 'create', 'catalog_item', ?, ?)",
               {std::to_string(user.userId), std::to_string(itemId),
                "Created: " + body.value("title", "")});

    auto created = db.queryOne("SELECT * FROM catalog_items WHERE id = ?",
                               {std::to_string(itemId)});
    res.status = 201;
    res.set_content(created.dump(), "application/json");
  });

  // PUT /api/catalog/:id - Update catalog item
  svr.Put(R"(/api/catalog/(\d+))", [](const httplib::Request &req,
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
    std::vector<std::string> sets;
    std::vector<std::string> params;

    auto addField = [&](const std::string &key, const std::string &col) {
      if (body.contains(key)) {
        sets.push_back(col + " = ?");
        if (body[key].is_number())
          params.push_back(std::to_string(body[key].get<int>()));
        else
          params.push_back(body[key].get<std::string>());
      }
    };

    addField("title", "title");
    addField("subtitle", "subtitle");
    addField("authors", "authors");
    addField("isbn", "isbn");
    addField("publisher", "publisher");
    addField("publication_year", "publication_year");
    addField("edition", "edition");
    addField("language", "language");
    addField("subjects", "subjects");
    addField("description", "description");
    addField("item_type", "item_type");

    if (sets.empty()) {
      res.status = 400;
      res.set_content(json{{"error", "No fields to update"}}.dump(),
                      "application/json");
      return;
    }

    sets.push_back("updated_at = datetime('now')");
    params.push_back(id);

    std::string sql = "UPDATE catalog_items SET ";
    for (size_t i = 0; i < sets.size(); i++) {
      if (i > 0)
        sql += ", ";
      sql += sets[i];
    }
    sql += " WHERE id = ?";

    db.execute(sql, params);
    auto updated =
        db.queryOne("SELECT * FROM catalog_items WHERE id = ?", {id});
    res.set_content(updated.dump(), "application/json");
  });

  // DELETE /api/catalog/:id - Delete/withdraw catalog item
  svr.Delete(R"(/api/catalog/(\d+))", [](const httplib::Request &req,
                                         httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user, {"admin"}))
      return;

    std::string id = req.matches[1];
    auto &db = Database::instance();

    // Check for active loans
    auto activeLoan =
        db.queryOne("SELECT COUNT(*) as count FROM loans l "
                    "JOIN copies c ON l.copy_id = c.id "
                    "WHERE c.catalog_item_id = ? AND l.status = 'active'",
                    {id});

    if (!activeLoan.is_null() && activeLoan["count"].get<int>() > 0) {
      res.status = 409;
      res.set_content(
          json{{"error", "Item has active loans; withdraw copies first"}}
              .dump(),
          "application/json");
      return;
    }

    db.execute("DELETE FROM catalog_items WHERE id = ?", {id});
    res.set_content(json{{"message", "Catalog item deleted"}}.dump(),
                    "application/json");
  });

  // ============ COPIES ============

  // POST /api/catalog/:id/copies - Add copy to item
  svr.Post(R"(/api/catalog/(\d+)/copies)", [](const httplib::Request &req,
                                              httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"librarian", "branch_manager", "admin"}))
      return;

    std::string itemId = req.matches[1];
    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(json{{"error", "Invalid JSON"}}.dump(),
                      "application/json");
      return;
    }

    if (!body.contains("barcode") || !body.contains("branch_id")) {
      res.status = 400;
      res.set_content(json{{"error", "barcode and branch_id required"}}.dump(),
                      "application/json");
      return;
    }

    auto &db = Database::instance();

    // Check barcode uniqueness
    auto existing = db.queryOne("SELECT id FROM copies WHERE barcode = ?",
                                {body["barcode"].get<std::string>()});
    if (!existing.is_null()) {
      res.status = 409;
      res.set_content(json{{"error", "Barcode already exists"}}.dump(),
                      "application/json");
      return;
    }

    db.execute("INSERT INTO copies (catalog_item_id, branch_id, barcode, "
               "shelf_location) VALUES (?, ?, ?, ?)",
               {itemId, std::to_string(body["branch_id"].get<int>()),
                body["barcode"].get<std::string>(),
                body.value("shelf_location", "")});

    int64_t copyId = db.lastInsertId();
    auto created = db.queryOne("SELECT * FROM copies WHERE id = ?",
                               {std::to_string(copyId)});
    res.status = 201;
    res.set_content(created.dump(), "application/json");
  });

  // PUT /api/copies/:id/status - Update copy status
  svr.Put(R"(/api/copies/(\d+)/status)", [](const httplib::Request &req,
                                            httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"librarian", "branch_manager", "admin"}))
      return;

    std::string copyId = req.matches[1];
    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(json{{"error", "Invalid JSON"}}.dump(),
                      "application/json");
      return;
    }

    if (!body.contains("status")) {
      res.status = 400;
      res.set_content(json{{"error", "status required"}}.dump(),
                      "application/json");
      return;
    }

    auto &db = Database::instance();
    db.execute("UPDATE copies SET status = ?, updated_at = datetime('now') "
               "WHERE id = ?",
               {body["status"].get<std::string>(), copyId});

    auto updated = db.queryOne("SELECT * FROM copies WHERE id = ?", {copyId});
    res.set_content(updated.dump(), "application/json");
  });

  // GET /api/branches - List branches
  svr.Get("/api/branches", [](const httplib::Request &req,
                              httplib::Response &res) {
    auto &db = Database::instance();
    auto branches =
        db.query("SELECT * FROM branches WHERE is_active = 1 ORDER BY name");

    json response = {{"data", branches}, {"count", branches.size()}};
    res.set_content(response.dump(), "application/json");
  });

  // POST /api/branches - Create branch (admin only)
  svr.Post("/api/branches",
           [](const httplib::Request &req, httplib::Response &res) {
             AuthMiddleware::AuthUser user;
             if (!AuthMiddleware::requireRole(req, res, user, {"admin"}))
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

             auto &db = Database::instance();
             db.execute("INSERT INTO branches (name, address, phone, email, "
                        "operating_hours) VALUES (?, ?, ?, ?, ?)",
                        {body.value("name", ""), body.value("address", ""),
                         body.value("phone", ""), body.value("email", ""),
                         body.value("operating_hours", "")});

             int64_t branchId = db.lastInsertId();
             auto created = db.queryOne("SELECT * FROM branches WHERE id = ?",
                                        {std::to_string(branchId)});
             res.status = 201;
             res.set_content(created.dump(), "application/json");
           });
}

} // namespace CatalogController
