#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <sqlite3.h>
#include "json.hpp"

using json = nlohmann::json;

class Database {
public:
    static Database& instance() {
        static Database db;
        return db;
    }

    bool initialize(const std::string& dbPath = "library.db");
    void close();

    // Execute a statement (INSERT, UPDATE, DELETE)
    bool execute(const std::string& sql, const std::vector<std::string>& params = {});

    // Query returning JSON array of rows
    json query(const std::string& sql, const std::vector<std::string>& params = {});

    // Query returning a single row (or null)
    json queryOne(const std::string& sql, const std::vector<std::string>& params = {});

    // Get last inserted row ID
    int64_t lastInsertId();

    // Get number of rows affected by last statement
    int changesCount();

private:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool runSchemaFile(const std::string& schemaPath);
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};
