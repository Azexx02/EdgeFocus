package com.edgefocus.app.presentation.ui.components

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

data class FeedbackOption(
    val id: String,
    val label: String
)

val feedbackOptions = listOf(
    FeedbackOption("too_many", "提醒太多"),
    FeedbackOption("not_enough", "提醒太少"),
    FeedbackOption("perfect", "时机刚好"),
    FeedbackOption("well_detected", "分心检测准确"),
    FeedbackOption("missed", "遗漏了一些分心"),
    FeedbackOption("other", "其他")
)

@Composable
fun FeedbackDialog(
    onDismiss: () -> Unit,
    onSubmit: (selectedId: String, otherText: String) -> Unit
) {
    var selectedOptionId by remember { mutableStateOf<String?>(null) }
    var otherText by remember { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("会话反馈") },
        text = {
            Column {
                feedbackOptions.forEach { option ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { selectedOptionId = option.id }
                            .padding(vertical = 4.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        RadioButton(
                            selected = selectedOptionId == option.id,
                            onClick = { selectedOptionId = option.id }
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(option.label)
                    }
                }

                if (selectedOptionId == "other") {
                    Spacer(modifier = Modifier.height(8.dp))
                    TextField(
                        value = otherText,
                        onValueChange = { otherText = it },
                        modifier = Modifier.fillMaxWidth(),
                        placeholder = { Text("请说明...") },
                        singleLine = true
                    )
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = {
                    selectedOptionId?.let { id ->
                        onSubmit(id, if (id == "other") otherText else "")
                    }
                },
                enabled = selectedOptionId != null &&
                        (selectedOptionId != "other" || otherText.isNotBlank())
            ) {
                Text("提交")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("取消")
            }
        }
    )
}
