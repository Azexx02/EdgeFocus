package com.example.edgefocus.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.Timer
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

@Composable
fun ControlPanelScreen(onBack: () -> Unit) {
    val pagerState = rememberPagerState(pageCount = { 3 })

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        // Landscape top bar
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 32.dp, vertical = 12.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = onBack) {
                Icon(Icons.Default.ArrowBack, contentDescription = "Back", tint = MaterialTheme.colorScheme.primary)
            }
            Text("EdgeFocus", style = MaterialTheme.typography.headlineMedium, color = MaterialTheme.colorScheme.primary)
            Icon(Icons.Default.BluetoothConnected, contentDescription = "Bluetooth", tint = MaterialTheme.colorScheme.primary)
        }

        HorizontalPager(
            state = pagerState,
            modifier = Modifier.weight(1f).fillMaxWidth()
        ) { page ->
            when (page) {
                0 -> TimerPage()
                1 -> DashboardPage()
                2 -> HardwarePage()
            }
        }

        // Pager indicator
        Row(
            modifier = Modifier.fillMaxWidth().padding(bottom = 16.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            repeat(3) { index ->
                val color = if (pagerState.currentPage == index) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceVariant
                Box(
                    modifier = Modifier
                        .padding(horizontal = 4.dp)
                        .size(10.dp)
                        .background(color, CircleShape)
                )
            }
        }
    }
}

@Composable
fun TimerPage() {
    Row(modifier = Modifier.fillMaxSize().padding(32.dp), horizontalArrangement = Arrangement.SpaceEvenly, verticalAlignment = Alignment.CenterVertically) {
        // Timer Mock
        Box(
            modifier = Modifier.size(220.dp).background(MaterialTheme.colorScheme.surfaceContainerLowest, CircleShape).shadow(10.dp, CircleShape),
            contentAlignment = Alignment.Center
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text("25:00", style = MaterialTheme.typography.displayLarge, color = MaterialTheme.colorScheme.primary)
                Text("Remaining", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        
        // Config Mock
        Surface(
            modifier = Modifier.width(300.dp).shadow(10.dp, RoundedCornerShape(16.dp)),
            shape = RoundedCornerShape(16.dp),
            color = MaterialTheme.colorScheme.surface
        ) {
            Column(modifier = Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                Text("Durations", style = MaterialTheme.typography.bodyLarge)
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("Focus Time", style = MaterialTheme.typography.bodyMedium)
                    Text("25", style = MaterialTheme.typography.bodyLarge)
                }
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("Short Break", style = MaterialTheme.typography.bodyMedium)
                    Text("5", style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.secondary)
                }
            }
        }
    }
}

@Composable
fun DashboardPage() {
    Row(modifier = Modifier.fillMaxSize().padding(32.dp), horizontalArrangement = Arrangement.spacedBy(24.dp)) {
        // Chart Area
        Surface(
            modifier = Modifier.weight(2f).fillMaxHeight().shadow(10.dp, RoundedCornerShape(16.dp)),
            shape = RoundedCornerShape(16.dp),
            color = MaterialTheme.colorScheme.surfaceContainerLowest
        ) {
            Box(modifier = Modifier.fillMaxSize().padding(24.dp), contentAlignment = Alignment.Center) {
                Text("Bar Chart Area (Mon-Sun)", style = MaterialTheme.typography.headlineMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        
        // Stats
        Column(modifier = Modifier.weight(1f).fillMaxHeight(), verticalArrangement = Arrangement.SpaceBetween) {
            StatCard("Total Focus", "12.5h")
            StatCard("Today", "45m")
            StatCard("Efficiency", "88%")
        }
    }
}

@Composable
fun StatCard(label: String, value: String) {
    Surface(
        modifier = Modifier.fillMaxWidth().height(85.dp).shadow(4.dp, RoundedCornerShape(16.dp)),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceContainerLowest
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.Center) {
            Text(label, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(value, style = MaterialTheme.typography.headlineMedium)
        }
    }
}

@Composable
fun HardwarePage() {
    Row(modifier = Modifier.fillMaxSize().padding(32.dp), horizontalArrangement = Arrangement.SpaceEvenly, verticalAlignment = Alignment.CenterVertically) {
        // Device Mockup
        Box(
            modifier = Modifier.size(192.dp).background(MaterialTheme.colorScheme.surfaceContainerLowest, CircleShape).shadow(10.dp, CircleShape),
            contentAlignment = Alignment.Center
        ) {
            Icon(Icons.Default.Timer, contentDescription = null, modifier = Modifier.size(48.dp), tint = MaterialTheme.colorScheme.surfaceVariant)
        }
        
        // Sliders Mockup
        Column(modifier = Modifier.width(300.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
            HardwareControl("Vibration Strength", "Medium")
            HardwareControl("LED Brightness", "75%")
            HardwareControl("Reminder Sensitivity", "Normal")
        }
    }
}

@Composable
fun HardwareControl(title: String, value: String) {
    Surface(
        modifier = Modifier.fillMaxWidth().shadow(4.dp, RoundedCornerShape(12.dp)),
        shape = RoundedCornerShape(12.dp),
        color = MaterialTheme.colorScheme.surfaceContainerLowest
    ) {
        Row(modifier = Modifier.padding(16.dp).fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(title, style = MaterialTheme.typography.bodyMedium)
            Text(value, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
        }
    }
}
