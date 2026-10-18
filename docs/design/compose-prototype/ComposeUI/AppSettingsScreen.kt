package com.example.edgefocus.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

data class AppItemInfo(val name: String, val pkg: String, val level: DistractionLevel, val rec: String)

enum class DistractionLevel(val label: String, val colorProvider: @Composable () -> Color, val bgColorProvider: @Composable () -> Color) {
    STRONG("Strong Distraction", { MaterialTheme.colorScheme.primary }, { MaterialTheme.colorScheme.primaryFixed }),
    WEAK("Weak Distraction", { MaterialTheme.colorScheme.tertiary }, { MaterialTheme.colorScheme.surfaceContainer }),
    LEARNING("Learning App", { MaterialTheme.colorScheme.secondary }, { MaterialTheme.colorScheme.surfaceContainer }),
    UNSELECTED("Background", { MaterialTheme.colorScheme.onSurfaceVariant }, { MaterialTheme.colorScheme.surfaceContainer })
}

@Composable
fun AppSettingsScreen(onBack: () -> Unit) {
    val mockApps = listOf(
        AppItemInfo("WeChat", "com.tencent.mm", DistractionLevel.STRONG, "Rec: Social Media"),
        AppItemInfo("TikTok", "com.ss.android.ugc.aweme", DistractionLevel.STRONG, "Rec: Short Video"),
        AppItemInfo("YouTube", "com.google.android.youtube", DistractionLevel.STRONG, "Rec: Long Video"),
        AppItemInfo("Kindle", "com.amazon.kindle", DistractionLevel.LEARNING, "No reminders"),
        AppItemInfo("Spotify", "com.spotify.music", DistractionLevel.WEAK, "Rec: Audio Only"),
        AppItemInfo("Maps", "com.google.android.apps.maps", DistractionLevel.UNSELECTED, "Default state"),
        AppItemInfo("Genshin Impact", "com.miHoYo.GenshinImpact", DistractionLevel.STRONG, "Rec: Gaming")
    )

    Column(modifier = Modifier.fillMaxSize()) {
        // Header
        Row(
            modifier = Modifier.fillMaxWidth().padding(24.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(
                onClick = onBack,
                modifier = Modifier.background(MaterialTheme.colorScheme.surface, CircleShape)
            ) {
                Icon(Icons.Default.ArrowBack, contentDescription = "Back", tint = MaterialTheme.colorScheme.primary)
            }
            Text("App Settings", style = MaterialTheme.typography.headlineMedium, color = MaterialTheme.colorScheme.primary)
            Spacer(modifier = Modifier.size(48.dp)) // For centering
        }

        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(horizontal = 24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                Surface(
                    shape = RoundedCornerShape(12.dp),
                    color = MaterialTheme.colorScheme.primaryFixed,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Distraction Level Description", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onPrimaryFixed, modifier = Modifier.padding(bottom = 8.dp))
                        Text("· Strong: Short videos, games (High frequency)", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onPrimaryFixed)
                        Text("· Weak: Social media (Periodic reminders)", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onPrimaryFixed)
                        Text("· Learning: No reminders", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onPrimaryFixed)
                        Text("· Unselected: Default background processing", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onPrimaryFixed)
                    }
                }
            }
            
            item {
                Text(
                    "Strong: 4 | Weak: 1 | Learn: 1 | Unselected: 1",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(vertical = 8.dp)
                )
                Text("Installed Apps", style = MaterialTheme.typography.headlineMedium, modifier = Modifier.padding(bottom = 8.dp))
            }

            items(mockApps) { app ->
                AppListItem(app)
            }
            
            item { Spacer(modifier = Modifier.height(24.dp)) }
        }
    }
}

@Composable
fun AppListItem(app: AppItemInfo) {
    Surface(
        shape = RoundedCornerShape(12.dp),
        color = app.level.bgColorProvider(),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier.padding(16.dp).fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
                // Placeholder for app icon
                Box(modifier = Modifier.size(40.dp).background(Color.LightGray, RoundedCornerShape(8.dp)))
                Column {
                    Text(app.name, style = MaterialTheme.typography.bodyLarge)
                    Text(app.pkg, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            Column(horizontalAlignment = Alignment.End) {
                Text(app.level.label, style = MaterialTheme.typography.labelMedium, color = app.level.colorProvider())
                Text(app.rec, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}
