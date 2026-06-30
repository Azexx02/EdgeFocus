#pragma once

constexpr const char* DB_SCHEMA = R"(
CREATE TABLE IF NOT EXISTS session (
    id TEXT PRIMARY KEY,
    start_time INTEGER,
    end_time INTEGER,
    target_duration INTEGER,
    actual_duration INTEGER,
    focus_duration INTEGER,
    status TEXT,
    end_reason TEXT,
    distraction_count INTEGER DEFAULT 0,
    reminder_count INTEGER DEFAULT 0,
    phone_distraction_count INTEGER DEFAULT 0,
    feedback TEXT DEFAULT 'just_right'
);

CREATE TABLE IF NOT EXISTS state_segment (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT,
    state TEXT,
    start_time INTEGER,
    end_time INTEGER,
    duration INTEGER
);

CREATE TABLE IF NOT EXISTS event (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT,
    event_type TEXT,
    timestamp INTEGER,
    data TEXT
);

CREATE TABLE IF NOT EXISTS reminder (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT,
    timestamp INTEGER,
    type TEXT,
    response TEXT
);

CREATE TABLE IF NOT EXISTS feedback (
    session_id TEXT PRIMARY KEY,
    rating TEXT NOT NULL DEFAULT 'just_right',
    updated_at INTEGER
);

CREATE TABLE IF NOT EXISTS preferences (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS user_baseline (
    user_id TEXT PRIMARY KEY,
    avg_focus_time INTEGER,
    distraction_frequency REAL
);
)";
