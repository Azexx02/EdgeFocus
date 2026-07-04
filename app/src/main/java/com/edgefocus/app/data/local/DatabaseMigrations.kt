package com.edgefocus.app.data.local

import androidx.room.migration.Migration
import androidx.sqlite.db.SupportSQLiteDatabase

val MIGRATION_2_3 = object : Migration(2, 3) {
    override fun migrate(db: SupportSQLiteDatabase) = Unit
}

val MIGRATION_3_4 = object : Migration(3, 4) {
    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL("ALTER TABLE sessions ADD COLUMN phoneDistractionCount INTEGER NOT NULL DEFAULT 0")
        db.execSQL(
            """
            CREATE TABLE IF NOT EXISTS settings_new (
                id INTEGER NOT NULL,
                pomodoroDuration INTEGER NOT NULL,
                useEdgeFocusBoard INTEGER NOT NULL,
                edgeFocusMacAddress TEXT NOT NULL DEFAULT '',
                PRIMARY KEY(id)
            )
            """.trimIndent()
        )
        db.execSQL(
            """
            INSERT INTO settings_new (id, pomodoroDuration, useEdgeFocusBoard, edgeFocusMacAddress)
            SELECT id, pomodoroDuration, useEdgeFocusBoard, ''
            FROM settings
            """.trimIndent()
        )
        db.execSQL("DROP TABLE settings")
        db.execSQL("ALTER TABLE settings_new RENAME TO settings")
    }
}

val MIGRATION_4_5 = object : Migration(4, 5) {
    override fun migrate(db: SupportSQLiteDatabase) = Unit
}

val MIGRATION_5_6 = object : Migration(5, 6) {
    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL("ALTER TABLE sessions ADD COLUMN source TEXT NOT NULL DEFAULT 'LOCAL'")
    }
}

val MIGRATION_6_7 = object : Migration(6, 7) {
    override fun migrate(db: SupportSQLiteDatabase) {
        db.execSQL("ALTER TABLE sessions ADD COLUMN synced INTEGER NOT NULL DEFAULT 0")
    }
}
