# Self-Developed Board Workspace Consolidation Plan

## Goal
Keep `D:\Aze\Documents\developMP257` as the low-level hardware bring-up workspace, and make `C:\Users\Aze\Desktop\BoardCode\hardware_variants\self_developed` the main project workspace for code changes, updates, and deployment.

## Target Layout
- Hardware exploration workspace remains:
  `D:\Aze\Documents\developMP257`
- Main project workspace:
  `C:\Users\Aze\Desktop\BoardCode\hardware_variants\self_developed`
- Code repository:
  `C:\Users\Aze\Desktop\BoardCode\hardware_variants\self_developed\code\focus_app`
- Project-local non-Git workspace:
  `C:\Users\Aze\Desktop\BoardCode\hardware_variants\self_developed\code\focus_app\_workspace`

## Phases
1. [complete] Inspect existing hardware workspace and target project workspace.
2. [in_progress] Record the consolidation plan and migration criteria.
3. [pending] Ensure `focus_app/_workspace/` exists and is excluded from Git.
4. [pending] Copy selected hardware bring-up materials into `focus_app/_workspace/hardware_bringup/`.
5. [pending] Add an index/README in the copied workspace explaining source, scope, and what remains in `developMP257`.
6. [pending] Verify target tree and Git ignore behavior.

## Migration Criteria
Copy materials that help future app development and deployment understand board constraints:
- Current status summaries and bring-up logs.
- Hardware netlist and board notes.
- DTB/DTS artifacts and version history relevant to Wi-Fi/BT, NPU/GPU, audio, MIPI, and screen attempts.
- Scripts/tools used for deployment, backup, and DTB work.

Do not copy bulky raw archives by default:
- Large vendor ZIP files.
- Full screen data packages unless needed for active display work.
- Generated build scratch directories.

## Constraints
- Do not delete or move the original `developMP257` workspace.
- Preserve Git cleanliness in `focus_app`; `_workspace/` should not be tracked.
- Record what is copied and what is intentionally left behind.
