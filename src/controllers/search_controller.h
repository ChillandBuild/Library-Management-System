#pragma once

#include "db/database.h"
#include "httplib.h"
#include "json.hpp"
#include "middleware/auth_middleware.h"

using json = nlohmann::json;

namespace SearchController {

inline void registerRoutes(httplib::Server &svr) {

  svr.Get("/api/search", [](const httplib::Request &req,
                            httplib::Response &res) {
    auto &db = Database::instance();

    std::string query = req.get_param_value("q");
    std::string branch = req.get_param_value("branch_id");
    std::string itemType = req.get_param_value("item_type");
    std::string language = req.get_param_value("language");
    std::string available = req.get_param_value("available");

    int page = 1, perPage = 20;
    if (!req.get_param_value("page").empty())
      page = std::stoi(req.get_param_value("page"));
    if (!req.get_param_value("per_page").empty())
      perPage = std::stoi(req.get_param_value("per_page"));
    int offset = (page - 1) * perPage;

    std::string sql;
    std::vector<std::string> params;

    if (!query.empty()) {
      sql = "SELECT ci.* FROM catalog_fts "
            "JOIN catalog_items ci ON catalog_fts.rowid = ci.id "
            "WHERE catalog_fts MATCH ?";
      std::string ftsQuery = query + "* OR \"" + query + "\"";
      params.push_back(ftsQuery);
    } else {
      sql = "SELECT ci.* FROM catalog_items ci WHERE 1=1";
    }

    if (!itemType.empty()) {
      sql += " AND ci.item_type = ?";
      params.push_back(itemType);
    }
    if (!language.empty()) {
      sql += " AND ci.language = ?";
      params.push_back(language);
    }

    if (!query.empty())
      sql += " ORDER BY rank";
    else
      sql += " ORDER BY ci.title";

    sql += " LIMIT " + std::to_string(perPage) + " OFFSET " +
           std::to_string(offset);

    auto items = db.query(sql, params);

    // Enrich with availability
    for (auto &item : items) {
      std::string itemId = std::to_string(item["id"].get<int>());
      std::string copySql =
          "SELECT c.*, b.name as branch_name FROM copies c "
          "JOIN branches b ON c.branch_id = b.id WHERE c.catalog_item_id = ?";
      std::vector<std::string> cp = {itemId};
      if (!branch.empty()) {
        copySql += " AND c.branch_id = ?";
        cp.push_back(branch);
      }

      auto copies = db.query(copySql, cp);
      int avail = 0;
      for (const auto &c : copies)
        if (c["status"].get<std::string>() == "available")
          avail++;

      item["total_copies"] = (int)copies.size();
      item["available_copies"] = avail;
      item["copies"] = copies;
    }

    // Filter available only
    if (available == "true") {
      json filtered = json::array();
      for (const auto &item : items)
        if (item["available_copies"].get<int>() > 0)
          filtered.push_back(item);
      items = filtered;
    }

    res.set_content(json{{"data", items},
                         {"page", page},
                         {"per_page", perPage},
                         {"count", items.size()},
                         {"query", query}}
                        .dump(),
                    "application/json");
  });
}

} // namespace SearchController
