package com.edgefocus.app.data.local

import androidx.room.testing.MigrationTestHelper
import androidx.sqlite.db.SupportSQLiteDatabase
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AppDatabaseMigrationTest {
    @get:Rule
    val helper = MigrationTestHelper(
        InstrumentationRegistry.getInstrumentation(),
        AppDatabase::class.java
    )

    @Test
    fun migrate3To7KeepsSessionsSettingsAndAppUsage() {
        val db = helper.createDatabase(TEST_DB, 3).apply {
            execSQL(
                """
                INSERT INTO sessions
                (id, startTime, endTime, duration, focusDuration, distractionCount, reminderCount, feedback, boardSessionId)
                VALUES ('session_1', 1000, 61000, 60, 55, 2, 1, NULL, NULL)
                """.trimIndent()
            )
            execSQL(
                """
                INSERT INTO settings
                (id, pomodoroDuration, useEdgeFocusBoard, autoRotateNFC, notificationOnExit)
                VALUES (1, 35, 1, 1, 1)
                """.trimIndent()
            )
            execSQL(
                """
                INSERT INTO app_usage
                (id, packageName, category, usageDuration, sessionId)
                VALUES (1, 'com.study.app', 'learning', 30, 'session_1')
                """.trimIndent()
            )
            close()
        }

        val migrated = helper.runMigrationsAndValidate(
            TEST_DB,
            7,
            true,
            MIGRATION_3_4,
            MIGRATION_4_5,
            MIGRATION_5_6,
            MIGRATION_6_7
        )

        assertSingleInt(migrated, "SELECT COUNT(*) FROM sessions", 1)
        assertSingleInt(migrated, "SELECT phoneDistractionCount FROM sessions WHERE id='session_1'", 0)
        assertSingleString(migrated, "SELECT source FROM sessions WHERE id='session_1'", "LOCAL")
        assertSingleInt(migrated, "SELECT synced FROM sessions WHERE id='session_1'", 0)
        assertSingleInt(migrated, "SELECT COUNT(*) FROM settings", 1)
        assertSingleString(migrated, "SELECT edgeFocusMacAddress FROM settings WHERE id=1", "")
        assertSingleInt(migrated, "SELECT COUNT(*) FROM app_usage", 1)
        migrated.close()
    }

    private fun assertSingleInt(db: SupportSQLiteDatabase, sql: String, expected: Int) {
        db.query(sql).use { cursor ->
            cursor.moveToFirst()
            assertEquals(expected, cursor.getInt(0))
        }
    }

    private fun assertSingleString(db: SupportSQLiteDatabase, sql: String, expected: String) {
        db.query(sql).use { cursor ->
            cursor.moveToFirst()
            assertEquals(expected, cursor.getString(0))
        }
    }

    companion object {
        private const val TEST_DB = "migration-test"
    }
}
