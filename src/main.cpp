#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "controllers/auth_controller.h"
#include "controllers/catalog_controller.h"
#include "controllers/circulation_controller.h"
#include "controllers/fine_controller.h"
#include "controllers/member_controller.h"
#include "controllers/report_controller.h"
#include "controllers/search_controller.h"
#include "db/database.h"
#include "httplib.h"
#include "json.hpp"
#include "middleware/auth_middleware.h"
#include "utils/password.h"

using json = nlohmann::json;

httplib::Server *globalServer = nullptr;

void signalHandler(int sig) {
  std::cout << "\n[LMS] Shutting down (signal " << sig << ")..." << std::endl;
  if (globalServer)
    globalServer->stop();
}

void seedAdminAccount() {
  auto &db = Database::instance();
  auto admin = db.queryOne(
      "SELECT id, password_hash FROM user_accounts WHERE username = 'admin'");
  if (!admin.is_null() &&
      admin["password_hash"].get<std::string>() == "@@PLACEHOLDER@@") {
    std::string salt = PasswordUtil::generateSalt();
    std::string hash = PasswordUtil::hashPassword("admin123", salt);
    db.execute("UPDATE user_accounts SET password_hash = ?, salt = ? WHERE "
               "username = 'admin'",
               {hash, salt});
    std::cout << "[LMS] Admin account initialized (username: admin, password: "
                 "admin123)"
              << std::endl;
  }
}

void seedSampleData() {
  auto &db = Database::instance();

  // Check if sample data exists
  auto itemCount = db.queryOne("SELECT COUNT(*) as count FROM catalog_items");
  if (!itemCount.is_null() && itemCount["count"].get<int>() > 0)
    return;

  std::cout << "[LMS] Seeding sample catalog data..." << std::endl;

  // Sample books
  struct Book {
    std::string title;
    std::string authors;
    std::string isbn;
    std::string subjects;
    int year;
    std::string desc;
  };
  std::vector<Book> books = {
      {"The Great Gatsby", "F. Scott Fitzgerald", "978-0743273565",
       "Fiction, Classic, American Literature", 1925,
       "A story of the mysteriously wealthy Jay Gatsby and his love for Daisy "
       "Buchanan."},
      {"To Kill a Mockingbird", "Harper Lee", "978-0061120084",
       "Fiction, Classic, Legal Drama", 1960,
       "The story of racial injustice and the loss of innocence in the "
       "American South."},
      {"1984", "George Orwell", "978-0451524935",
       "Fiction, Dystopian, Political", 1949,
       "A dystopian social science fiction novel and cautionary tale about "
       "totalitarianism."},
      {"Pride and Prejudice", "Jane Austen", "978-0141439518",
       "Fiction, Classic, Romance", 1813,
       "A romantic novel of manners following Elizabeth Bennet."},
      {"The Catcher in the Rye", "J.D. Salinger", "978-0316769488",
       "Fiction, Coming-of-age", 1951,
       "The story of Holden Caulfield navigating life in New York City."},
      {"Introduction to Algorithms", "Thomas H. Cormen, Charles E. Leiserson",
       "978-0262033848", "Computer Science, Algorithms, Reference", 2009,
       "Comprehensive textbook on computer algorithms."},
      {"Design Patterns",
       "Erich Gamma, Richard Helm, Ralph Johnson, John Vlissides",
       "978-0201633610", "Computer Science, Software Engineering", 1994,
       "Elements of reusable object-oriented software."},
      {"Clean Code", "Robert C. Martin", "978-0132350884",
       "Computer Science, Software Engineering, Best Practices", 2008,
       "A handbook of agile software craftsmanship."},
      {"The Art of War", "Sun Tzu", "978-1599869773",
       "Non-Fiction, Strategy, Philosophy", -500,
       "Ancient Chinese military treatise."},
      {"Sapiens: A Brief History of Humankind", "Yuval Noah Harari",
       "978-0062316097", "Non-Fiction, History, Anthropology", 2011,
       "A narrative history of humankind from the Stone Age to the present."},
      {"Cosmos", "Carl Sagan", "978-0345539434",
       "Non-Fiction, Science, Astronomy", 1980,
       "A personal voyage through the universe."},
      {"The Hobbit", "J.R.R. Tolkien", "978-0547928227",
       "Fiction, Fantasy, Adventure", 1937,
       "Bilbo Baggins embarks on an unexpected journey."},
      {"Dune", "Frank Herbert", "978-0441013593",
       "Fiction, Science Fiction, Epic", 1965,
       "A science fiction masterpiece about politics, religion, and ecology."},
      {"The Lean Startup", "Eric Ries", "978-0307887894",
       "Business, Entrepreneurship, Management", 2011,
       "How continuous innovation creates radically successful businesses."},
      {"Thinking, Fast and Slow", "Daniel Kahneman", "978-0374533557",
       "Psychology, Non-Fiction, Decision Making", 2011,
       "An exploration of the two systems that drive the way we think."},
  };

  for (const auto &b : books) {
    db.execute("INSERT INTO catalog_items (title, authors, isbn, subjects, "
               "publication_year, description, item_type) "
               "VALUES (?, ?, ?, ?, ?, ?, 'book')",
               {b.title, b.authors, b.isbn, b.subjects, std::to_string(b.year),
                b.desc});
  }

  // Add copies for each book at both branches
  auto allItems = db.query("SELECT id FROM catalog_items");
  int barcodeNum = 1001;
  for (const auto &item : allItems) {
    int itemId = item["id"].get<int>();
    // 2 copies at Main, 1 at North
    for (int branchId : {1, 1, 2}) {
      std::string barcode = "LIB-" + std::to_string(barcodeNum++);
      db.execute("INSERT INTO copies (catalog_item_id, branch_id, barcode, "
                 "shelf_location) VALUES (?, ?, ?, ?)",
                 {std::to_string(itemId), std::to_string(branchId), barcode,
                  "Shelf " + std::to_string((itemId % 10) + 1)});
    }
  }

  // Sample members
  struct Member {
    std::string first;
    std::string last;
    std::string email;
    std::string type;
  };
  std::vector<Member> members = {
      {"John", "Smith", "john.smith@email.com", "standard"},
      {"Jane", "Doe", "jane.doe@email.com", "premium"},
      {"Alice", "Johnson", "alice.j@email.com", "student"},
      {"Bob", "Williams", "bob.w@email.com", "standard"},
      {"Emily", "Brown", "emily.b@email.com", "senior"},
  };

  for (const auto &m : members) {
    db.execute("INSERT INTO members (first_name, last_name, email, "
               "membership_type) VALUES (?, ?, ?, ?)",
               {m.first, m.last, m.email, m.type});
    int64_t memberId = db.lastInsertId();
    std::string salt = PasswordUtil::generateSalt();
    std::string hash = PasswordUtil::hashPassword("password123", salt);
    std::string username = m.email.substr(0, m.email.find('@'));
    db.execute("INSERT INTO user_accounts (username, password_hash, salt, "
               "role, member_id) VALUES (?, ?, ?, 'member', ?)",
               {username, hash, salt, std::to_string(memberId)});
  }

  // Create a librarian account
  std::string libSalt = PasswordUtil::generateSalt();
  std::string libHash = PasswordUtil::hashPassword("librarian123", libSalt);
  db.execute("INSERT INTO user_accounts (username, password_hash, salt, role, "
             "branch_id) VALUES ('librarian', ?, ?, 'librarian', 1)",
             {libHash, libSalt});

  std::cout << "[LMS] Sample data seeded: " << books.size() << " books, "
            << members.size() << " members" << std::endl;
}

int main(int argc, char *argv[]) {
  std::cout << R"(
  ╔══════════════════════════════════════════════╗
  ║   📚  Library Management System v1.0        ║
  ║   C++ Backend with REST API                 ║
  ╚══════════════════════════════════════════════╝
)" << std::endl;

  // Configuration
  int port = 8080;
  if (const char *env_p = std::getenv("PORT")) {
    port = std::stoi(env_p);
  }
  std::string dbPath = "library.db";

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc)
      port = std::stoi(argv[++i]);
    if (arg == "--db" && i + 1 < argc)
      dbPath = argv[++i];
  }

  // Initialize database
  if (!Database::instance().initialize(dbPath)) {
    std::cerr << "[LMS] Failed to initialize database. Exiting." << std::endl;
    return 1;
  }

  // Seed data
  seedAdminAccount();
  seedSampleData();

  // Create server
  httplib::Server svr;
  globalServer = &svr;

  // Signal handling
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  // CORS middleware
  svr.set_pre_routing_handler(
      [](const httplib::Request &req, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods",
                       "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers",
                       "Content-Type, Authorization");

        if (req.method == "OPTIONS") {
          res.status = 200;
          return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
      });

  // Serve static files
  svr.set_mount_point("/", "./static");

  // Register API routes
  AuthController::registerRoutes(svr);
  MemberController::registerRoutes(svr);
  CatalogController::registerRoutes(svr);
  SearchController::registerRoutes(svr);
  CirculationController::registerRoutes(svr);
  FineController::registerRoutes(svr);
  ReportController::registerRoutes(svr);

  // Health check
  svr.Get("/api/health", [](const httplib::Request &, httplib::Response &res) {
    res.set_content(json{{"status", "ok"},
                         {"service", "Library Management System"},
                         {"version", "1.0.0"}}
                        .dump(),
                    "application/json");
  });

  // Dashboard stats
  svr.Get("/api/dashboard", [](const httplib::Request &req,
                               httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireAuth(req, res, user))
      return;

    auto &db = Database::instance();
    auto stats = db.queryOne(
        "SELECT "
        "(SELECT COUNT(*) FROM loans WHERE status = 'active') as active_loans, "
        "(SELECT COUNT(*) FROM loans WHERE status = 'active' AND due_date < "
        "date('now')) as overdue_items, "
        "(SELECT COUNT(*) FROM holds WHERE status = 'pending') as "
        "pending_holds, "
        "(SELECT COUNT(*) FROM holds WHERE status = 'ready') as ready_holds, "
        "(SELECT COUNT(*) FROM members WHERE status = 'active') as "
        "active_members, "
        "(SELECT COUNT(*) FROM catalog_items) as total_items, "
        "(SELECT COUNT(*) FROM copies WHERE status = 'available') as "
        "available_copies, "
        "(SELECT COUNT(*) FROM loans WHERE date(checkout_date) = date('now')) "
        "as today_checkouts, "
        "(SELECT COUNT(*) FROM loans WHERE date(return_date) = date('now')) as "
        "today_returns");

    // Recent activity
    auto recent = db.query("SELECT l.id, l.checkout_date, l.due_date, "
                           "l.status, c.barcode, ci.title, "
                           "m.first_name || ' ' || m.last_name as member_name "
                           "FROM loans l JOIN copies c ON l.copy_id = c.id "
                           "JOIN catalog_items ci ON c.catalog_item_id = ci.id "
                           "JOIN members m ON l.member_id = m.id "
                           "ORDER BY l.created_at DESC LIMIT 10");

    res.set_content(json{{"stats", stats}, {"recent_activity", recent}}.dump(),
                    "application/json");
  });

  // Circulation rules endpoint
  svr.Get("/api/circulation-rules", [](const httplib::Request &req,
                                       httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user,
                                     {"admin", "branch_manager"}))
      return;

    auto &db = Database::instance();
    auto rules = db.query(
        "SELECT * FROM circulation_rules ORDER BY membership_type, item_type");
    res.set_content(json{{"data", rules}}.dump(), "application/json");
  });

  svr.Put(R"(/api/circulation-rules/(\d+))", [](const httplib::Request &req,
                                                httplib::Response &res) {
    AuthMiddleware::AuthUser user;
    if (!AuthMiddleware::requireRole(req, res, user, {"admin"}))
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

    if (body.contains("loan_period_days")) {
      sets.push_back("loan_period_days = ?");
      params.push_back(std::to_string(body["loan_period_days"].get<int>()));
    }
    if (body.contains("max_loans")) {
      sets.push_back("max_loans = ?");
      params.push_back(std::to_string(body["max_loans"].get<int>()));
    }
    if (body.contains("max_renewals")) {
      sets.push_back("max_renewals = ?");
      params.push_back(std::to_string(body["max_renewals"].get<int>()));
    }
    if (body.contains("fine_per_day")) {
      sets.push_back("fine_per_day = ?");
      params.push_back(std::to_string(body["fine_per_day"].get<double>()));
    }
    if (body.contains("fine_cap")) {
      sets.push_back("fine_cap = ?");
      params.push_back(std::to_string(body["fine_cap"].get<double>()));
    }

    if (!sets.empty()) {
      sets.push_back("updated_at = datetime('now')");
      params.push_back(id);
      std::string sql = "UPDATE circulation_rules SET ";
      for (size_t i = 0; i < sets.size(); i++) {
        if (i > 0)
          sql += ", ";
        sql += sets[i];
      }
      sql += " WHERE id = ?";
      db.execute(sql, params);
    }

    auto updated =
        db.queryOne("SELECT * FROM circulation_rules WHERE id = ?", {id});
    res.set_content(updated.dump(), "application/json");
  });

  std::cout << "[LMS] Server starting on http://localhost:" << port
            << std::endl;
  std::cout << "[LMS] API docs: http://localhost:" << port << "/api/health"
            << std::endl;
  std::cout << "[LMS] Default login — admin:admin123 | librarian:librarian123"
            << std::endl;
  std::cout << "[LMS] Press Ctrl+C to stop\n" << std::endl;

  if (!svr.listen("0.0.0.0", port)) {
    std::cerr << "[LMS] Failed to start server on port " << port << std::endl;
    return 1;
  }

  Database::instance().close();
  return 0;
}
