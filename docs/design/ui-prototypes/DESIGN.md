---
name: EdgeFocus
colors:
  surface: '#fbf9f7'
  surface-dim: '#dbdad8'
  surface-bright: '#fbf9f7'
  surface-container-lowest: '#ffffff'
  surface-container-low: '#f5f3f1'
  surface-container: '#efedec'
  surface-container-high: '#e9e8e6'
  surface-container-highest: '#e4e2e0'
  on-surface: '#1b1c1b'
  on-surface-variant: '#5b403e'
  inverse-surface: '#30302f'
  inverse-on-surface: '#f2f0ee'
  outline: '#8f706d'
  outline-variant: '#e3bebb'
  surface-tint: '#b81d27'
  primary: '#b51925'
  on-primary: '#ffffff'
  primary-container: '#d8363a'
  on-primary-container: '#fffbff'
  inverse-primary: '#ffb3ae'
  secondary: '#006e1c'
  on-secondary: '#ffffff'
  secondary-container: '#91f78e'
  on-secondary-container: '#00731e'
  tertiary: '#805200'
  on-tertiary: '#ffffff'
  tertiary-container: '#a06900'
  on-tertiary-container: '#fffbff'
  error: '#ba1a1a'
  on-error: '#ffffff'
  error-container: '#ffdad6'
  on-error-container: '#93000a'
  primary-fixed: '#ffdad7'
  primary-fixed-dim: '#ffb3ae'
  on-primary-fixed: '#410004'
  on-primary-fixed-variant: '#930015'
  secondary-fixed: '#94f990'
  secondary-fixed-dim: '#78dc77'
  on-secondary-fixed: '#002204'
  on-secondary-fixed-variant: '#005313'
  tertiary-fixed: '#ffddb4'
  tertiary-fixed-dim: '#ffb954'
  on-tertiary-fixed: '#291800'
  on-tertiary-fixed-variant: '#633f00'
  background: '#fbf9f7'
  on-background: '#1b1c1b'
  surface-variant: '#e4e2e0'
typography:
  display-timer:
    fontFamily: Quicksand
    fontSize: 80px
    fontWeight: '700'
    lineHeight: 90px
    letterSpacing: -0.02em
  headline-lg:
    fontFamily: Quicksand
    fontSize: 32px
    fontWeight: '700'
    lineHeight: 40px
  headline-lg-mobile:
    fontFamily: Quicksand
    fontSize: 28px
    fontWeight: '700'
    lineHeight: 36px
  headline-md:
    fontFamily: Quicksand
    fontSize: 24px
    fontWeight: '600'
    lineHeight: 32px
  body-lg:
    fontFamily: Quicksand
    fontSize: 18px
    fontWeight: '500'
    lineHeight: 28px
  body-md:
    fontFamily: Quicksand
    fontSize: 16px
    fontWeight: '500'
    lineHeight: 24px
  label-md:
    fontFamily: Quicksand
    fontSize: 14px
    fontWeight: '600'
    lineHeight: 20px
    letterSpacing: 0.01em
  label-sm:
    fontFamily: Quicksand
    fontSize: 12px
    fontWeight: '700'
    lineHeight: 16px
rounded:
  sm: 0.5rem
  DEFAULT: 1rem
  md: 1.5rem
  lg: 2rem
  xl: 3rem
  full: 9999px
spacing:
  unit: 8px
  container-padding-mobile: 24px
  container-padding-desktop: 48px
  stack-gap: 16px
  card-padding: 24px
  section-margin: 40px
---

## Brand & Style
The design system is engineered to foster a high-energy yet approachable environment for deep work. It captures a "Youthful Productivity" aesthetic—moving away from the sterile nature of traditional SaaS tools toward something that feels alive, tactile, and encouraging.

The style is a blend of **Soft Minimalism** and **Tactile UI**. It prioritizes high-impact color blocks, generous whitespace, and an "inflated" geometry that makes every interactive element feel physically satisfying to engage with. The emotional goal is to transform the stress of a deadline into the rhythmic satisfaction of a "Tomato" interval, using bouncy physics and friendly shapes to lower the cognitive barrier to starting a task.

## Colors
The palette is rooted in a culinary "Tomato & Basil" metaphor, providing natural contrast that signals state changes clearly.

- **Primary (Tomato Red):** Used for active focus sessions, primary action buttons, and critical progress indicators. It is the "Work" signal.
- **Secondary (Basil Green):** Reserved for break states, success celebrations, and "Task Complete" affirmations. It represents rest and growth.
- **Tertiary (Sunny Orange):** Used for warnings, short-break transitions, or secondary focus-related metrics.
- **Neutral (Creamy White):** The foundation. This off-white base reduces eye strain compared to pure hex-white and adds a soft, organic warmth to the interface.
- **Text (Charcoal):** A high-contrast, slightly softened black for maximum readability without the harshness of #000000.

## Typography
This design system utilizes **Quicksand** for all roles to maintain a consistent, friendly, and rounded character. 

The type hierarchy is dominated by a "Display Timer" role, which is oversized to ensure the remaining time is visible from a distance. Headlines use a bold weight to anchor cards and sections, while body text remains medium-weight to ensure the rounded terminals of the font don't become blurry at smaller scales. All caps are used sparingly for labels to provide a structural contrast to the otherwise soft letterforms.

## Layout & Spacing
The layout follows a **Fluid Content** model centered on a maximum width of 600px for the primary timer interface, ensuring focus remains central. 

Spacing is generous, utilizing an 8px grid with a preference for larger gaps (16px, 24px, 32px) to prevent the UI from feeling cluttered. Elements should never feel "tight"; the abundance of negative space reinforces the "clear mind" philosophy of the app. Containers use wide internal padding (24px+) to accommodate large border radii without crowding the content.

## Elevation & Depth
This design system uses **Ambient Shadows** to create a sense of physical layering. Surfaces do not feel "flat," but rather like soft plastic or clay resting on the cream background.

- **Level 1 (Cards/Inputs):** A soft, multi-layered shadow with a large blur radius (20px-30px) and low opacity (8%) using a slight Primary Color tint rather than pure black.
- **Level 2 (Buttons/Active States):** Increased shadow spread to simulate the element lifting off the page.
- **Active Press:** When a button is pressed, the shadow Y-offset and blur decrease to 0, simulating a physical "push" into the surface.

## Shapes
The shape language is defined by extreme roundedness. 

A base radius of **24px** is applied to primary cards and containers. Buttons utilize a **Pill-shaped** (fully rounded) geometry to emphasize their tactile, "squishy" nature. These large radii work in tandem with the "bouncy" animation philosophy—elements should look like they could easily roll or bounce if dropped.

## Components

### Buttons
Primary buttons are large (56px+ height), pill-shaped, and use high-contrast fills (Primary Red or Secondary Green). They must include a subtle 2px inner-glow or "rim light" on the top edge to enhance the tactile feel.

### Circular Progress Indicator
The central timer is a custom thick-stroke circular ring (12px - 16px weight) with rounded end-caps. The track should be a faint version of the background cream, while the active progress uses the Primary Tomato Red.

### Cards
Cards are the primary container for tasks and settings. They must have a minimum border-radius of 24px and a soft ambient shadow. Backgrounds for cards can be pure white to subtly pop against the Creamy White app background.

### Input Fields
Inputs are pill-shaped with a thick 2px border that transitions from a neutral grey to Primary Red on focus. Labels sit outside the input, using the `label-md` style.

### Chips & Tags
Small, highly-rounded (16px+) pills used for categorizing tasks. These should use low-saturation versions of the palette to keep the focus on the primary timer.

### Micro-interactions
All state changes (hover, active, toggle) must use a "Spring" curve (`stiffness: 300, damping: 20`). Buttons should scale down slightly (0.95) when pressed to reinforce the bouncy, tactile theme.