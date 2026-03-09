-- Library Management System Database Schema
-- SQLite3 with FTS5 for full-text search

PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;

-- ============================================================
-- BRANCHES
-- ============================================================
CREATE TABLE IF NOT EXISTS branches (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    address TEXT,
    phone TEXT,
    email TEXT,
    operating_hours TEXT,
    is_active INTEGER DEFAULT 1,
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now'))
);

-- ============================================================
-- MEMBERS
-- ============================================================
CREATE TABLE IF NOT EXISTS members (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    first_name TEXT NOT NULL,
    last_name TEXT NOT NULL,
    email TEXT UNIQUE,
    phone TEXT,
    address TEXT,
    date_of_birth TEXT,
    membership_type TEXT DEFAULT 'standard' CHECK(membership_type IN ('standard', 'student', 'senior', 'child', 'premium')),
    status TEXT DEFAULT 'active' CHECK(status IN ('active', 'suspended', 'expired', 'banned')),
    start_date TEXT DEFAULT (date('now')),
    expiry_date TEXT DEFAULT (date('now', '+1 year')),
    notes TEXT,
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now'))
);

-- ============================================================
-- USER ACCOUNTS (Authentication)
-- ============================================================
CREATE TABLE IF NOT EXISTS user_accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    salt TEXT NOT NULL,
    role TEXT NOT NULL CHECK(role IN ('member', 'librarian', 'branch_manager', 'admin')),
    member_id INTEGER,
    branch_id INTEGER,
    is_active INTEGER DEFAULT 1,
    last_login TEXT,
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (member_id) REFERENCES members(id) ON DELETE SET NULL,
    FOREIGN KEY (branch_id) REFERENCES branches(id) ON DELETE SET NULL
);

-- ============================================================
-- CATALOG ITEMS (Bibliographic records)
-- ============================================================
CREATE TABLE IF NOT EXISTS catalog_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    subtitle TEXT,
    authors TEXT NOT NULL,
    isbn TEXT,
    issn TEXT,
    publisher TEXT,
    publication_year INTEGER,
    edition TEXT,
    language TEXT DEFAULT 'English',
    subjects TEXT,
    description TEXT,
    item_type TEXT DEFAULT 'book' CHECK(item_type IN ('book', 'magazine', 'dvd', 'audiobook', 'ebook', 'reference', 'other')),
    cover_image_url TEXT,
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now'))
);

-- ============================================================
-- COPIES (Physical items per branch)
-- ============================================================
CREATE TABLE IF NOT EXISTS copies (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    catalog_item_id INTEGER NOT NULL,
    branch_id INTEGER NOT NULL,
    barcode TEXT UNIQUE NOT NULL,
    shelf_location TEXT,
    status TEXT DEFAULT 'available' CHECK(status IN ('available', 'on_loan', 'on_hold', 'in_transit', 'lost', 'damaged', 'withdrawn')),
    condition_notes TEXT,
    acquired_date TEXT DEFAULT (date('now')),
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (catalog_item_id) REFERENCES catalog_items(id) ON DELETE CASCADE,
    FOREIGN KEY (branch_id) REFERENCES branches(id) ON DELETE RESTRICT
);

-- ============================================================
-- LOANS (Circulation records)
-- ============================================================
CREATE TABLE IF NOT EXISTS loans (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    copy_id INTEGER NOT NULL,
    member_id INTEGER NOT NULL,
    staff_id INTEGER,
    checkout_date TEXT DEFAULT (datetime('now')),
    due_date TEXT NOT NULL,
    return_date TEXT,
    renewals_count INTEGER DEFAULT 0,
    status TEXT DEFAULT 'active' CHECK(status IN ('active', 'returned', 'overdue', 'lost')),
    notes TEXT,
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (copy_id) REFERENCES copies(id) ON DELETE RESTRICT,
    FOREIGN KEY (member_id) REFERENCES members(id) ON DELETE RESTRICT,
    FOREIGN KEY (staff_id) REFERENCES user_accounts(id) ON DELETE SET NULL
);

-- ============================================================
-- HOLDS (Reservations)
-- ============================================================
CREATE TABLE IF NOT EXISTS holds (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    catalog_item_id INTEGER NOT NULL,
    member_id INTEGER NOT NULL,
    pickup_branch_id INTEGER NOT NULL,
    hold_date TEXT DEFAULT (datetime('now')),
    expiry_date TEXT,
    notification_date TEXT,
    status TEXT DEFAULT 'pending' CHECK(status IN ('pending', 'ready', 'fulfilled', 'cancelled', 'expired')),
    queue_position INTEGER DEFAULT 0,
    notes TEXT,
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (catalog_item_id) REFERENCES catalog_items(id) ON DELETE CASCADE,
    FOREIGN KEY (member_id) REFERENCES members(id) ON DELETE CASCADE,
    FOREIGN KEY (pickup_branch_id) REFERENCES branches(id) ON DELETE RESTRICT
);

-- ============================================================
-- FINES
-- ============================================================
CREATE TABLE IF NOT EXISTS fines (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id INTEGER NOT NULL,
    loan_id INTEGER,
    amount REAL NOT NULL DEFAULT 0.0,
    paid_amount REAL NOT NULL DEFAULT 0.0,
    status TEXT DEFAULT 'unpaid' CHECK(status IN ('unpaid', 'partial', 'paid', 'waived')),
    reason TEXT DEFAULT 'overdue',
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (member_id) REFERENCES members(id) ON DELETE CASCADE,
    FOREIGN KEY (loan_id) REFERENCES loans(id) ON DELETE SET NULL
);

-- ============================================================
-- FINE PAYMENTS
-- ============================================================
CREATE TABLE IF NOT EXISTS fine_payments (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fine_id INTEGER NOT NULL,
    amount REAL NOT NULL,
    payment_method TEXT DEFAULT 'cash' CHECK(payment_method IN ('cash', 'card', 'online', 'waiver')),
    staff_id INTEGER,
    notes TEXT,
    created_at TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (fine_id) REFERENCES fines(id) ON DELETE CASCADE,
    FOREIGN KEY (staff_id) REFERENCES user_accounts(id) ON DELETE SET NULL
);

-- ============================================================
-- CIRCULATION RULES
-- ============================================================
CREATE TABLE IF NOT EXISTS circulation_rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    membership_type TEXT NOT NULL,
    item_type TEXT NOT NULL,
    loan_period_days INTEGER DEFAULT 14,
    max_loans INTEGER DEFAULT 5,
    max_renewals INTEGER DEFAULT 2,
    fine_per_day REAL DEFAULT 0.50,
    fine_cap REAL DEFAULT 25.00,
    grace_period_days INTEGER DEFAULT 0,
    is_active INTEGER DEFAULT 1,
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now'))
);

-- ============================================================
-- NOTIFICATIONS
-- ============================================================
CREATE TABLE IF NOT EXISTS notifications (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id INTEGER NOT NULL,
    type TEXT NOT NULL CHECK(type IN ('pre_due', 'due_today', 'overdue', 'hold_ready', 'membership_expiry', 'general')),
    subject TEXT,
    body TEXT,
    status TEXT DEFAULT 'pending' CHECK(status IN ('pending', 'sent', 'failed')),
    sent_at TEXT,
    created_at TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (member_id) REFERENCES members(id) ON DELETE CASCADE
);

-- ============================================================
-- AUDIT LOG
-- ============================================================
CREATE TABLE IF NOT EXISTS audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER,
    action TEXT NOT NULL,
    entity_type TEXT,
    entity_id INTEGER,
    details TEXT,
    ip_address TEXT,
    created_at TEXT DEFAULT (datetime('now')),
    FOREIGN KEY (user_id) REFERENCES user_accounts(id) ON DELETE SET NULL
);

-- ============================================================
-- FULL-TEXT SEARCH (FTS5)
-- ============================================================
CREATE VIRTUAL TABLE IF NOT EXISTS catalog_fts USING fts5(
    title, subtitle, authors, isbn, subjects, description,
    content='catalog_items',
    content_rowid='id'
);

-- Triggers to keep FTS in sync
CREATE TRIGGER IF NOT EXISTS catalog_fts_insert AFTER INSERT ON catalog_items BEGIN
    INSERT INTO catalog_fts(rowid, title, subtitle, authors, isbn, subjects, description)
    VALUES (new.id, new.title, new.subtitle, new.authors, new.isbn, new.subjects, new.description);
END;

CREATE TRIGGER IF NOT EXISTS catalog_fts_delete AFTER DELETE ON catalog_items BEGIN
    INSERT INTO catalog_fts(catalog_fts, rowid, title, subtitle, authors, isbn, subjects, description)
    VALUES ('delete', old.id, old.title, old.subtitle, old.authors, old.isbn, old.subjects, old.description);
END;

CREATE TRIGGER IF NOT EXISTS catalog_fts_update AFTER UPDATE ON catalog_items BEGIN
    INSERT INTO catalog_fts(catalog_fts, rowid, title, subtitle, authors, isbn, subjects, description)
    VALUES ('delete', old.id, old.title, old.subtitle, old.authors, old.isbn, old.subjects, old.description);
    INSERT INTO catalog_fts(rowid, title, subtitle, authors, isbn, subjects, description)
    VALUES (new.id, new.title, new.subtitle, new.authors, new.isbn, new.subjects, new.description);
END;

-- ============================================================
-- INDEXES
-- ============================================================
CREATE INDEX IF NOT EXISTS idx_members_email ON members(email);
CREATE INDEX IF NOT EXISTS idx_members_status ON members(status);
CREATE INDEX IF NOT EXISTS idx_user_accounts_username ON user_accounts(username);
CREATE INDEX IF NOT EXISTS idx_copies_barcode ON copies(barcode);
CREATE INDEX IF NOT EXISTS idx_copies_status ON copies(status);
CREATE INDEX IF NOT EXISTS idx_copies_branch ON copies(branch_id);
CREATE INDEX IF NOT EXISTS idx_copies_catalog_item ON copies(catalog_item_id);
CREATE INDEX IF NOT EXISTS idx_loans_member ON loans(member_id);
CREATE INDEX IF NOT EXISTS idx_loans_copy ON loans(copy_id);
CREATE INDEX IF NOT EXISTS idx_loans_status ON loans(status);
CREATE INDEX IF NOT EXISTS idx_loans_due_date ON loans(due_date);
CREATE INDEX IF NOT EXISTS idx_holds_member ON holds(member_id);
CREATE INDEX IF NOT EXISTS idx_holds_item ON holds(catalog_item_id);
CREATE INDEX IF NOT EXISTS idx_holds_status ON holds(status);
CREATE INDEX IF NOT EXISTS idx_fines_member ON fines(member_id);
CREATE INDEX IF NOT EXISTS idx_fines_status ON fines(status);
CREATE INDEX IF NOT EXISTS idx_audit_log_user ON audit_log(user_id);
CREATE INDEX IF NOT EXISTS idx_audit_log_action ON audit_log(action);

-- ============================================================
-- SEED DATA
-- ============================================================

-- Default branch
INSERT OR IGNORE INTO branches (id, name, address, phone, email, operating_hours)
VALUES (1, 'Main Library', '123 Library Street, Downtown', '+1-555-0100', 'main@library.org', 'Mon-Sat: 9AM-8PM, Sun: 10AM-5PM');

INSERT OR IGNORE INTO branches (id, name, address, phone, email, operating_hours)
VALUES (2, 'North Branch', '456 North Avenue', '+1-555-0200', 'north@library.org', 'Mon-Fri: 9AM-6PM, Sat: 10AM-4PM');

-- Default circulation rules
INSERT OR IGNORE INTO circulation_rules (id, membership_type, item_type, loan_period_days, max_loans, max_renewals, fine_per_day, fine_cap)
VALUES
    (1, 'standard', 'book', 14, 5, 2, 0.50, 25.00),
    (2, 'standard', 'dvd', 7, 3, 1, 1.00, 15.00),
    (3, 'standard', 'magazine', 7, 3, 0, 0.25, 10.00),
    (4, 'student', 'book', 21, 8, 3, 0.25, 15.00),
    (5, 'student', 'dvd', 7, 2, 1, 0.50, 10.00),
    (6, 'senior', 'book', 21, 8, 3, 0.25, 15.00),
    (7, 'child', 'book', 21, 5, 2, 0.10, 5.00),
    (8, 'premium', 'book', 28, 10, 4, 0.25, 20.00);

-- Default admin account (password: admin123)
-- Salt: 'lms_default_salt' | Hash is SHA256 of 'admin123' + salt
INSERT OR IGNORE INTO user_accounts (id, username, password_hash, salt, role, is_active)
VALUES (1, 'admin', '@@PLACEHOLDER@@', 'lms_default_salt', 'admin', 1);
