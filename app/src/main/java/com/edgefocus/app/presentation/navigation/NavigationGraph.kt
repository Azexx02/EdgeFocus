package com.edgefocus.app.presentation.navigation

import android.content.Intent
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.edgefocus.app.ControlPanelActivity
import com.edgefocus.app.BuildConfig
import com.edgefocus.app.presentation.ui.components.BottomNavigationBar
import com.edgefocus.app.presentation.ui.dashboard.DashboardScreen
import com.edgefocus.app.presentation.ui.home.HomeScreen
import com.edgefocus.app.presentation.ui.settings.BleConnectScreen
import com.edgefocus.app.presentation.ui.settings.DistractionAppsScreen
import com.edgefocus.app.presentation.ui.settings.SettingsScreen
import com.edgefocus.app.presentation.ui.settings.TestScreen
import com.edgefocus.app.presentation.viewmodel.DebugFeaturePolicy

sealed class Screen(val route: String) {
    data object Home : Screen("home")
    data object Dashboard : Screen("dashboard")
    data object Settings : Screen("settings")
    data object DistractionApps : Screen("distraction_apps")
    data object BleConnect : Screen("ble_connect")
    data object Test : Screen("test")
}

@Composable
fun NavigationGraph() {
    val navController = rememberNavController()
    val context = LocalContext.current

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
                HomeScreen(
                    onNavigateToControlPanel = {
                        val intent = Intent(context, ControlPanelActivity::class.java)
                        context.startActivity(intent)
                    }
                )
            }
            composable(Screen.Dashboard.route) {
                DashboardScreen()
            }
            composable(Screen.Settings.route) {
                SettingsScreen(
                    onNavigateToDistractionApps = {
                        navController.navigate(Screen.DistractionApps.route)
                    },
                    onNavigateToBleConnect = {
                        navController.navigate(Screen.BleConnect.route)
                    },
                    onNavigateToTest = {
                        if (DebugFeaturePolicy.canNavigateToTest(BuildConfig.DEBUG)) {
                            navController.navigate(Screen.Test.route)
                        }
                    }
                )
            }
            composable(Screen.DistractionApps.route) {
                DistractionAppsScreen(
                    onBack = { navController.popBackStack() }
                )
            }
            composable(Screen.BleConnect.route) {
                BleConnectScreen(
                    onBack = { navController.popBackStack() }
                )
            }
            if (DebugFeaturePolicy.shouldRegisterTestRoute(BuildConfig.DEBUG)) {
                composable(Screen.Test.route) {
                    TestScreen(
                        onBack = { navController.popBackStack() }
                    )
                }
            }
        }
    }
}
