package com.edgefocus.app.presentation.navigation

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.edgefocus.app.presentation.ui.components.BottomNavigationBar
import com.edgefocus.app.presentation.ui.dashboard.DashboardScreen
import com.edgefocus.app.presentation.ui.home.HomeScreen
import com.edgefocus.app.presentation.ui.settings.SettingsScreen

sealed class Screen(val route: String) {
    data object Home : Screen("home")
    data object Dashboard : Screen("dashboard")
    data object Settings : Screen("settings")
}

@Composable
fun NavigationGraph() {
    val navController = rememberNavController()

    Scaffold(
        bottomBar = {
            BottomNavigationBar(navController = navController)
        }
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = Screen.Home.route,
            modifier = Modifier.padding(innerPadding)
        ) {
            composable(Screen.Home.route) {
                HomeScreen()
            }
            composable(Screen.Dashboard.route) {
                DashboardScreen()
            }
            composable(Screen.Settings.route) {
                SettingsScreen()
            }
        }
    }
}
