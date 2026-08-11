/**
 * @file style.h
 * @brief Unified style constants for rawdraw UI components in C
 *
 * Centralizes spacing, sizing, and layout constants used across all
 * rawdraw components.
 */

#ifndef RAWDRAW_STYLE_H
#define RAWDRAW_STYLE_H

// ============================================================
// Spacing tokens (SM / MD / LG scale)
// ============================================================

#define STYLE_SPACING_XXS 2 ///< Extra-extra-small gap
#define STYLE_SPACING_XS 4 ///< Extra-small gap
#define STYLE_SPACING_SM 6 ///< Small gap
#define STYLE_SPACING_MD 8 ///< Medium gap (default padding)
#define STYLE_SPACING_LG 12 ///< Large gap
#define STYLE_SPACING_XL 16 ///< Extra-large gap
#define STYLE_SPACING_XXL 24 ///< Extra-extra-large gap

// ============================================================
#define STYLE_BORDER_RADIUS_SM 4 ///< Small corners (badges, tags)
#define STYLE_BORDER_RADIUS_MD 8 ///< Default corners (bubbles, cards)
#define STYLE_BORDER_RADIUS_LG 12 ///< Large corners (panels, dialogs)
#define STYLE_BORDER_RADIUS_XL 16 ///< Extra-large (full rounded buttons)
#define STYLE_BORDER_RADIUS_PILL 999 ///< Pill shape (half-height)

// ============================================================
// Border thickness
// ============================================================

#define STYLE_BORDER_THIN 1 ///< Default border
#define STYLE_BORDER_MEDIUM 2 ///< Emphasized border
#define STYLE_BORDER_THICK 3 ///< Heavy border

// ============================================================
// Component: Bubble (chat messages)
// ============================================================

#define STYLE_BUBBLE_PADDING 8 ///< Internal padding inside bubble
#define STYLE_BUBBLE_MARGIN 24 ///< Distance from screen edge
#define STYLE_BUBBLE_MAX_WIDTH_PCT 80 ///< Max width as percentage of screen
#define STYLE_BUBBLE_RADIUS STYLE_BORDER_RADIUS_MD
#define STYLE_BUBBLE_LINE_SPACING 2 ///< Extra space between text lines
#define STYLE_BUBBLE_GAP 4 ///< Vertical gap between consecutive bubbles

// ============================================================
// Component: Button
// ============================================================

#define STYLE_BUTTON_PADDING_H 12 ///< Horizontal padding
#define STYLE_BUTTON_PADDING_V 8 ///< Vertical padding
#define STYLE_BUTTON_MIN_WIDTH 40 ///< Minimum button width
#define STYLE_BUTTON_MIN_HEIGHT 40 ///< Minimum button height
#define STYLE_BUTTON_RADIUS STYLE_BORDER_RADIUS_MD
#define STYLE_BUTTON_ICON_GAP 6 ///< Gap between icon and text

// ============================================================
// Component: Panel
// ============================================================

#define STYLE_PANEL_PADDING STYLE_SPACING_MD
#define STYLE_PANEL_TITLE_HEIGHT 28 ///< Title bar height
#define STYLE_PANEL_RADIUS STYLE_BORDER_RADIUS_LG
#define STYLE_PANEL_BORDER_WIDTH STYLE_BORDER_THIN
#define STYLE_PANEL_GAP STYLE_SPACING_SM ///< Gap between sections

// ============================================================
// Component: ScrollView
// ============================================================

#define STYLE_SCROLLBAR_WIDTH 3 ///< Scrollbar indicator width
#define STYLE_SCROLLBAR_MIN_H 20 ///< Minimum scrollbar thumb height
#define STYLE_SCROLL_MARGIN 4 ///< Margin between content and scrollbar

// ============================================================
// Component: ProgressBar
// ============================================================

#define STYLE_PROGRESS_HEIGHT 14 ///< Bar height
#define STYLE_PROGRESS_RADIUS STYLE_BORDER_RADIUS_PILL
#define STYLE_PROGRESS_PADDING 2 ///< Padding around bar

// ============================================================
// Component: Toggle
// ============================================================

#define STYLE_TOGGLE_WIDTH 48 ///< Default track width
#define STYLE_TOGGLE_HEIGHT 24 ///< Default track height (thumb = h/2)
#define STYLE_TOGGLE_PADDING 2 ///< Thumb inset from track edge
#define STYLE_TOGGLE_LABEL_GAP STYLE_SPACING_SM ///< Gap between toggle and label

// ============================================================
// Component: Slider
// ============================================================

#define STYLE_SLIDER_HEIGHT 24 ///< Default track + thumb height
#define STYLE_SLIDER_TRACK_H 4 ///< Track bar height
#define STYLE_SLIDER_THUMB_SIZE 8 ///< Diamond thumb half-size
#define STYLE_SLIDER_LABEL_GAP STYLE_SPACING_XS ///< Label distance from track

// ============================================================
// Component: Card
// ============================================================

#define STYLE_CARD_PADDING STYLE_SPACING_MD
#define STYLE_CARD_RADIUS STYLE_BORDER_RADIUS_MD
#define STYLE_CARD_BORDER_WIDTH STYLE_BORDER_THIN
#define STYLE_CARD_SHADOW_OFFSET 2 ///< Shadow offset pixels
#define STYLE_CARD_TITLE_HEIGHT STYLE_PANEL_TITLE_HEIGHT
#define STYLE_CARD_GAP STYLE_SPACING_SM ///< Gap between stacked cards

// ============================================================
// Component: ListItem
// ============================================================

#define STYLE_LIST_ITEM_HEIGHT 36 ///< Default item height
#define STYLE_LIST_ITEM_PADDING STYLE_SPACING_MD
#define STYLE_LIST_ITEM_ICON_GAP STYLE_SPACING_SM ///< Gap after icon
#define STYLE_LIST_ITEM_CHEVRON_W 10 ///< Chevron reserved width
#define STYLE_LIST_ITEM_SEP_WIDTH 1 ///< Separator line thickness

// ============================================================
// Component: StatusBar
// ============================================================

#define STYLE_STATUS_BAR_HEIGHT 28
#define STYLE_STATUS_BAR_PADDING 4
#define STYLE_STATUS_BAR_ICON_SIZE 16 ///< Icon font size
#define STYLE_FOOTER_BAR_HEIGHT 22 ///< Bottom footer / hint bar height
#define STYLE_FOOTER_BAR_PADDING 8 ///< Horizontal padding for footer text
#define STYLE_SHELL_DIVIDER_THICKNESS 2 ///< Reusable shell/status/footer divider thickness
#define STYLE_SETTINGS_SIDEBAR_WIDTH 58
#define STYLE_GALLERY_INFO_WIDTH 120

// ============================================================
// Component: TodoList
// ============================================================

#define STYLE_TODO_ITEM_HEIGHT 32 ///< Height of each todo item
#define STYLE_TODO_ITEM_PADDING 8 ///< Horizontal padding inside item
#define STYLE_TODO_TEXT_OFFSET 28 ///< X offset for text after checkbox
#define STYLE_TODO_MAX_VISIBLE_ITEMS 7 ///< Max items visible without scroll

// ============================================================
// Component: Card (generic card-style containers)
// ============================================================

#define STYLE_ITEM_MIN_HEIGHT 28 ///< Minimum item row height
#define STYLE_ITEM_GAP 4 ///< Gap between list items
#define STYLE_CHECKBOX_SIZE 16 ///< Checkbox square size (F3: 16x16 per SPEC)

// ============================================================
// Component: Settings items
// ============================================================

#define STYLE_ICON_SIZE 20 ///< Icon size in settings items
#define STYLE_SETTINGS_VIEWPORT_INSET 16 ///< Left/right margin for settings cards
#define STYLE_SETTINGS_CARD_GAP 4 ///< Gap between stacked settings cards
#define STYLE_SETTINGS_CARD_RADIUS STYLE_BORDER_RADIUS_MD
#define STYLE_SETTINGS_TAG_HEIGHT 14 ///< Floating section tag height
#define STYLE_SETTINGS_ICON_BOX 22 ///< Leading icon capsule size
#define STYLE_SETTINGS_DIALOG_INSET 44 ///< Global modal inset from screen edge
#define STYLE_SETTINGS_DIALOG_ROW_H 30 ///< Info row height inside dialogs
#define STYLE_SETTINGS_VALUE_GAP 10 ///< Minimum gap between label and right-side value
#define STYLE_SETTINGS_CARD_RIGHT_RESERVE 14 ///< Reserve for scrollbar/air on the right
#define STYLE_SETTINGS_CARD_INNER_PAD 10 ///< Card horizontal inner padding
#define STYLE_SETTINGS_CONTENT_OFFSET_Y 1 ///< Shared vertical nudge from card center line
#define STYLE_VISUAL_TEXT_OFFSET 1 ///< Optical +1px nudge: e-paper text looks slightly high at exact math center
#define STYLE_MODAL_INSET 36 ///< Default modal inset from screen edges
#define STYLE_MODAL_TITLE_HEIGHT 26 ///< Title strip height for generic modals
#define STYLE_MODAL_FOOTER_HEIGHT 22 ///< Bottom action strip height for generic modals

// ============================================================
// Component: Dialog (centered modal dialogs)
// ============================================================

#define STYLE_DIALOG_W 292 ///< Standard dialog width
#define STYLE_DIALOG_H_SM 156 ///< Small dialog height
#define STYLE_DIALOG_H_MD 166 ///< Medium dialog height
#define STYLE_DIALOG_H_LG 210 ///< Large dialog height

// ============================================================
// Screen dimensions (400x300 SSD1683 EPD)
// ============================================================

#define STYLE_SCREEN_WIDTH 400
#define STYLE_SCREEN_HEIGHT 300
#define STYLE_SCREEN_1BPP_BYTES ((STYLE_SCREEN_WIDTH * STYLE_SCREEN_HEIGHT) / 8) ///< 1bpp framebuffer size (15000)
#define STYLE_SCREEN_2BPP_BYTES ((STYLE_SCREEN_WIDTH * STYLE_SCREEN_HEIGHT) / 4) ///< 2bpp framebuffer size (30000)

// Usable content area (below status bar, with margins)
#define STYLE_CONTENT_TOP (STYLE_STATUS_BAR_HEIGHT + STYLE_SPACING_SM)
#define STYLE_CONTENT_BOTTOM (STYLE_SCREEN_HEIGHT - STYLE_SPACING_SM)
#define STYLE_CONTENT_LEFT STYLE_SPACING_SM
#define STYLE_CONTENT_RIGHT (STYLE_SCREEN_WIDTH - STYLE_SPACING_SM)
#define STYLE_CONTENT_WIDTH (STYLE_SCREEN_WIDTH - 2 * STYLE_SPACING_SM)
#define STYLE_CONTENT_HEIGHT (STYLE_CONTENT_BOTTOM - STYLE_CONTENT_TOP)

// ============================================================
// Corner safe zones (400x300 SSD1683 physical rounded corners)
// ============================================================

#define STYLE_CORNER_SAFE_INSET 15 ///< Pixels to inset from corners

// ============================================================
// Font sizes
// ============================================================

#define STYLE_FONT_SIZE_XS 12 ///< Tiny labels
#define STYLE_FONT_SIZE_SM 16 ///< Body text / icons
#define STYLE_FONT_SIZE_MD 20 ///< Headings
#define STYLE_FONT_SIZE_LG 24 ///< Large headings
#define STYLE_FONT_SIZE_XL 48 ///< Hero / large icons

#endif // RAWDRAW_STYLE_H
