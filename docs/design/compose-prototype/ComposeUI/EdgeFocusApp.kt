package com.example.edgefocus.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BarChart
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import com.example.edgefocus.ui.theme.EdgeFocusTheme

enum class Screen {
    Home, Data, Settings, AppSelection, ControlPanel
}

@Composable
fun EdgeFocusApp() {
    var currentScreen by remember { mutableStateOf(Screen.Home) }

    EdgeFocusTheme {
        Scaffold(
            bottomBar = {
                if (currentScreen in listOf(Screen.Home, Screen.Data, Screen.Settings)) {
                    EdgeFocusBottomBar(
                        currentScreen = currentScreen,
                        onNavigate = { screen -> currentScreen = screen }
                    )
                }
            }
        ) { innerPadding ->
            Box(
                modifier = Modifier
                    .padding(innerPadding)
                    .background(MaterialTheme.colorScheme.background)
            ) {
                when (currentScreen) {
                    Screen.Home -> HomeScreen()
                    Screen.Data -> DashboardScreen()
                    Screen.Settings -> SettingsScreen(
                        onNavigateToAppSelection = { currentScreen = Screen.AppSelection },
                        onNavigateToControlPanel = { currentScreen = Screen.ControlPanel }
                    )
                    Screen.AppSelection -> AppSettingsScreen(
                        onBack = { currentScreen = Screen.Settings }
                    )
                    Screen.ControlPanel -> ControlPanelScreen(
                        onBack = { currentScreen = Screen.Settings }
                    )
                }
            }
        }
    }
}

@Composable
fun EdgeFocusBottomBar(
    currentScreen: Screen,
    onNavigate: (Screen) -> Unit
) {
    NavigationBar(
        containerColor = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurfaceVariant
    ) {
        NavigationBarItem(
            selected = currentScreen == Screen.Home,
            onClick = { onNavigate(Screen.Home) },
            icon = { Icon(Icons.Default.Home, contentDescription = "Home") },
            label = { Text("Home") },
            colors = NavigationBarItemDefaults.colors(
                selectedIconColor = Color.White,
                selectedTextColor = MaterialTheme.colorScheme.primary,
                indicatorColor = MaterialTheme.colorScheme.primary
            )
        )
        NavigationBarItem(
            selected = currentScreen == Screen.Data,
            onClick = { onNavigate(Screen.Data) },
            icon = { Icon(Icons.Default.BarChart, contentDescription = "Data") },
            label = { Text("Data") },
            colors = NavigationBarItemDefaults.colors(
                selectedIconColor = Color.White,
                selectedTextColor = MaterialTheme.colorScheme.primary,
                indicatorColor = MaterialTheme.colorScheme.primary
            )
        )
        NavigationBarItem(
            selected = currentScreen == Screen.Settings,
            onClick = { onNavigate(Screen.Settings) },
            icon = { Icon(Icons.Default.Settings, contentDescription = "Settings") },
            label = { Text("Settings") },
            colors = NavigationBarItemDefaults.colors(
                selectedIconColor = Color.White,
                selectedTextColor = MaterialTheme.colorScheme.primary,
                indicatorColor = MaterialTheme.colorScheme.primary
            )
        )
    }
}
