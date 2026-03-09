#include "db/database.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

Database::~Database() { close(); }

bool Database::initialize(const std::string &dbPath) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (db_)
    return true; // Already initialized

  int rc = sqlite3_open(dbPath.c_str(), &db_);
  if (rc != SQLITE_OK) {
    std::cerr << "[DB] Failed to open database: " << sqlite3_errmsg(db_)
              << std::endl;
    return false;
  }

  // Enable WAL mode and foreign keys
  char *errMsg = nullptr;
  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
  sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, &errMsg);

  // Run schema
  if (!runSchemaFile("schema.sql")) {
    std::cerr << "[DB] Failed to run schema file" << std::endl;
    return false;
  }

  std::cout << "[DB] Database initialized successfully: " << dbPath
            << std::endl;
  return true;
}

void Database::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool Database::execute(const std::string &sql,
                       const std::vector<std::string> &params) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_)
    return false;

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    std::cerr << "[DB] Prepare error: " << sqlite3_errmsg(db_)
              << "\n  SQL: " << sql << std::endl;
    return false;
  }

  for (size_t i = 0; i < params.size(); i++) {
    sqlite3_bind_text(stmt, static_cast<int>(i + 1), params[i].c_str(), -1,
                      SQLITE_TRANSIENT);
  }

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
    std::cerr << "[DB] Execute error: " << sqlite3_errmsg(db_)
              << "\n  SQL: " << sql << std::endl;
    return false;
  }

  return true;
}

json Database::query(const std::string &sql,
                     const std::vector<std::string> &params) {
  std::lock_guard<std::mutex> lock(mutex_);
  json results = json::array();

  if (!db_)
    return results;

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    std::cerr << "[DB] Query prepare error: " << sqlite3_errmsg(db_)
              << "\n  SQL: " << sql << std::endl;
    return results;
  }

  for (size_t i = 0; i < params.size(); i++) {
    sqlite3_bind_text(stmt, static_cast<int>(i + 1), params[i].c_str(), -1,
                      SQLITE_TRANSIENT);
  }

  int colCount = sqlite3_column_count(stmt);

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    json row = json::object();
    for (int i = 0; i < colCount; i++) {
      const char *colName = sqlite3_column_name(stmt, i);
      int colType = sqlite3_column_type(stmt, i);

      switch (colType) {
      case SQLITE_INTEGER:
        row[colName] = sqlite3_column_int64(stmt, i);
        break;
      case SQLITE_FLOAT:
        row[colName] = sqlite3_column_double(stmt, i);
        break;
      case SQLITE_TEXT:
        row[colName] = std::string(
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, i)));
        break;
      case SQLITE_NULL:
        row[colName] = nullptr;
        break;
      default:
        row[colName] = std::string(
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, i)));
        break;
      }
    }
    results.push_back(row);
  }

  sqlite3_finalize(stmt);
  return results;
}

json Database::queryOne(const std::string &sql,
                        const std::vector<std::string> &params) {
  auto results = query(sql, params);
  if (results.empty())
    return nullptr;
  return results[0];
}

int64_t Database::lastInsertId() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_)
    return -1;
  return sqlite3_last_insert_rowid(db_);
}

int Database::changesCount() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_)
    return 0;
  return sqlite3_changes(db_);
}

bool Database::runSchemaFile(const std::string &schemaPath) {
  std::ifstream file(schemaPath);
  if (!file.is_open()) {
    std::cerr << "[DB] Could not open schema file: " << schemaPath << std::endl;
    // Try relative to executable
    std::string altPath =
        std::filesystem::current_path().string() + "/" + schemaPath;
    file.open(altPath);
    if (!file.is_open()) {
      std::cerr << "[DB] Also could not open: " << altPath << std::endl;
      return false;
    }
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string sql = buffer.str();

  char *errMsg = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK) {
    std::cerr << "[DB] Schema error: " << (errMsg ? errMsg : "unknown")
              << std::endl;
    sqlite3_free(errMsg);
    return false;
  }

  std::cout << "[DB] Schema applied successfully" << std::endl;
  return true;
}
