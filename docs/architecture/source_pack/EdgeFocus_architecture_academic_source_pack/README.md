# EdgeFocus architecture diagrams - academic style

This package contains four architecture figures in SVG and 1920 x 1080 PNG formats:

1. `(a) 端侧系统闭环`
2. `(b) 硬件`
3. `(c) Edge AI + Fusion`
4. `(d) 异核调度与安全闭环`

## Visual system

- White background, thin technical rules, square or minimally rounded modules.
- Muted, print-friendly semantic colors:
  - blue: external interfaces and communication;
  - teal: A35 / Linux orchestration and fusion;
  - violet: NPU and AI inference;
  - orange: real-time and safety domains;
  - green: devices and actuators;
  - red: critical or exceptional outcomes.
- Dashed borders denote logical domains or isolation boundaries.
- Solid arrows denote data, state, or control flow; labels specify protocol or semantic payload.
- Panel labels `(a)-(d)` support direct use in papers, theses, posters, and defense slides.

## Typography

- Titles and section headers: Noto Serif CJK SC / Chinese serif fallback.
- Module labels and annotations: Noto Sans CJK SC / Microsoft YaHei fallback.

The architecture semantics remain aligned with the EdgeFocus unified architecture specification, version 2026-07-16.
