#pragma once

constexpr const char* DB_SCHEMA = R"(
CREATE TABLE IF NOT EXISTS session (
    id TEXT PRIMARY KEY,
    start_time INTEGER,
    end_time INTEGER
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
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT,
    rating INTEGER,
    comments TEXT
);

CREATE TABLE IF NOT EXISTS user_baseline (
    user_id TEXT PRIMARY KEY,
    avg_focus_time INTEGER,
    distraction_frequency REAL
);
)";
