#pragma once

// Minimal Dear ImGui declarations - actual implementation from imgui.cpp/imgui_demo.cpp
// This header provides the API surface we need without pulling in full imgui.h

#include <cstdint>
#include <cstddef>

namespace ImGui {

// Core
struct ImVec2 { float x, y; };
struct ImVec4 { float x, y, z, w; };

// Context
struct ImGuiContext;
ImGuiContext* CreateContext();
void DestroyContext(ImGuiContext* ctx);
void SetCurrentContext(ImGuiContext* ctx);
ImGuiContext* GetCurrentContext();

// Main
bool CheckVersion(const char* version_str);
struct ImGuiIO;
ImGuiIO& GetIO();
void NewFrame();
void EndFrame();
void Render();
struct ImDrawData;
ImDrawData* GetDrawData();

// Style
void StyleColorsDark(ImVec4* dest = nullptr);
void StyleColorsLight(ImVec4* dest = nullptr);

// Windows
bool Begin(const char* name, bool* p_open = nullptr, int flags = 0);
void End();
bool BeginChild(const char* str_id, const ImVec2& size = ImVec2(0,0), bool border = false, int flags = 0);
void EndChild();

// Input
bool Button(const char* label, const ImVec2& size = ImVec2(0,0));
bool SmallButton(const char* label);
bool InvisibleButton(const char* str_id, const ImVec2& size);
bool ArrowButton(const char* str_id, int dir);
bool InputText(const char* label, char* buf, size_t buf_size, int flags = 0, void(*)(ImGuiInputTextCallbackData*) = nullptr, void* user_data = nullptr);
bool InputTextMultiline(const char* label, char* buf, size_t buf_size, const ImVec2& size = ImVec2(0,0), int flags = 0, void(*)(ImGuiInputTextCallbackData*) = nullptr, void* user_data = nullptr);
bool Checkbox(const char* label, bool* v);
bool RadioButton(const char* label, bool active);
int GetKeyIndex(int imgui_key);
bool IsKeyPressed(int user_key_index, bool repeat = true);
bool IsKeyDown(int user_key_index);
bool IsMouseClicked(int button, bool repeat = false);
bool IsMouseDown(int button);
ImVec2 GetMousePos();

// Layout
void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f);
void NewLine();
void Spacing();
void Dummy(const ImVec2& size);
void Indent(float indent_w = 0.0f);
void Unindent(float indent_w = 0.0f);
void BeginGroup();
void EndGroup();

// Text
void Text(const char* fmt, ...);
void TextColored(const ImVec4& col, const char* fmt, ...);
void TextDisabled(const char* fmt, ...);
void TextWrapped(const char* fmt, ...);
void LabelText(const char* label, const char* fmt, ...);
void BulletText(const char* fmt, ...);
void Separator();
void SeparatorText(const char* label);

// Tab Bar
bool BeginTabBar(const char* str_id, int flags = 0);
void EndTabBar();
bool BeginTabItem(const char* label, bool* p_open = nullptr, int flags = 0);
void EndTabItem();
bool TabItemButton(const char* label, int flags = 0);
void SetTabItemClosed(const char* tab_or_docked_window_label);

// List/Combo
bool BeginListBox(const char* label, const ImVec2& size = ImVec2(0,0));
void EndListBox();
bool ListBox(const char* label, int* current_item, const char* const items[], int items_count, int height_in_items = -1);
bool BeginCombo(const char* label, const char* preview_value, int flags = 0);
void EndCombo();
bool Selectable(const char* label, bool selected = false, int flags = 0, const ImVec2& size = ImVec2(0,0));

// Popup
bool BeginPopup(const char* str_id, int flags = 0);
bool BeginPopupContextItem(const char* str_id = nullptr, int mouse_button = 1);
void EndPopup();
bool OpenPopup(const char* str_id, int flags = 0);
bool IsPopupOpen(const char* str_id, int flags = 0);

// Menu
bool BeginMenuBar();
void EndMenuBar();
bool BeginMenu(const char* label, bool enabled = true);
void EndMenu();
bool MenuItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);

// Color
uint32_t ColorConvertFloat4ToU32(const ImVec4& in);
ImVec4 ColorConvertU32ToFloat4(uint32_t in);

// Draw List
struct ImDrawList;
ImDrawList* GetWindowDrawList();
void DrawList_AddRectFilled(ImDrawList* draw_list, const ImVec2& p_min, const ImVec2& p_max, uint32_t col, float rounding = 0.0f, int rounding_corners_flags = 0);
void DrawList_AddText(ImDrawList* draw_list, const ImVec2& pos, uint32_t col, const char* text_begin, const char* text_end = nullptr);

// Font
struct ImFont;
ImFont* GetFont();
void PushFont(ImFont* font);
void PopFont();

// Clipboard
const char* GetClipboardText();
void SetClipboardText(const char* text);

// INI
void LoadIniSettingsFromDisk(const char* ini_filename);
void SaveIniSettingsToDisk(const char* ini_filename);

// Logging
void LogToClipboard();
void LogFinish();

// Debug
void ShowDemoWindow(bool* p_open = nullptr);
void ShowMetricsWindow(bool* p_open = nullptr);
void ShowStyleEditor(ImGuiStyle* ref = nullptr);
void ShowFontSelector(const char* label);
void ShowUserGuide();
void ShowStyleSelector(const char* label);

// Flags
enum ImGuiWindowFlags_ {
    ImGuiWindowFlags_None = 0,
    ImGuiWindowFlags_NoTitleBar = 1 << 0,
    ImGuiWindowFlags_NoResize = 1 << 1,
    ImGuiWindowFlags_NoMove = 1 << 2,
    ImGuiWindowFlags_NoScrollbar = 1 << 3,
    ImGuiWindowFlags_NoScrollWithMouse = 1 << 4,
    ImGuiWindowFlags_NoCollapse = 1 << 5,
    ImGuiWindowFlags_AlwaysAutoResize = 1 << 6,
    ImGuiWindowFlags_NoBackground = 1 << 7,
    ImGuiWindowFlags_NoSavedSettings = 1 << 8,
    ImGuiWindowFlags_NoMouseInputs = 1 << 9,
    ImGuiWindowFlags_MenuBar = 1 << 10,
    ImGuiWindowFlags_HorizontalScrollbar = 1 << 11,
    ImGuiWindowFlags_NoFocusOnAppearing = 1 << 12,
    ImGuiWindowFlags_NoBringToFrontOnFocus = 1 << 13,
    ImGuiWindowFlags_AlwaysVerticalScrollbar = 1 << 14,
    ImGuiWindowFlags_AlwaysHorizontalScrollbar = 1 << 15,
    ImGuiWindowFlags_AlwaysUseWindowPadding = 1 << 16,
    ImGuiWindowFlags_NoNavInputs = 1 << 17,
    ImGuiWindowFlags_NoNavFocus = 1 << 18,
    ImGuiWindowFlags_UnsavedDocument = 1 << 19,
    ImGuiWindowFlags_NoNav = ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus,
    ImGuiWindowFlags_NoDecoration = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse,
    ImGuiWindowFlags_NoInputs = ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus,
};

enum ImGuiTabBarFlags_ {
    ImGuiTabBarFlags_None = 0,
    ImGuiTabBarFlags_Reorderable = 1 << 0,
    ImGuiTabBarFlags_AutoSelectNewTabs = 1 << 1,
    ImGuiTabBarFlags_TabListPopupButton = 1 << 2,
    ImGuiTabBarFlags_NoCloseWithMiddleMouseButton = 1 << 3,
    ImGuiTabBarFlags_NoTabListScrollingButtons = 1 << 4,
    ImGuiTabBarFlags_NoTooltip = 1 << 5,
    ImGuiTabBarFlags_FittingPolicyResizeDown = 1 << 6,
    ImGuiTabBarFlags_FittingPolicyScroll = 1 << 7,
    ImGuiTabBarFlags_FittingPolicyMask_ = ImGuiTabBarFlags_FittingPolicyResizeDown | ImGuiTabBarFlags_FittingPolicyScroll,
    ImGuiTabBarFlags_FittingPolicyDefault_ = ImGuiTabBarFlags_FittingPolicyResizeDown,
};

enum ImGuiTabItemFlags_ {
    ImGuiTabItemFlags_None = 0,
    ImGuiTabItemFlags_UnsavedDocument = 1 << 0,
    ImGuiTabItemFlags_SetSelected = 1 << 1,
    ImGuiTabItemFlags_NoCloseWithMiddleMouseButton = 1 << 2,
    ImGuiTabItemFlags_NoPushId = 1 << 3,
    ImGuiTabItemFlags_NoTooltip = 1 << 4,
    ImGuiTabItemFlags_NoReorder = 1 << 5,
    ImGuiTabItemFlags_Leading = 1 << 6,
    ImGuiTabItemFlags_Trailing = 1 << 7,
};

enum ImGuiInputTextFlags_ {
    ImGuiInputTextFlags_None = 0,
    ImGuiInputTextFlags_CharsDecimal = 1 << 0,
    ImGuiInputTextFlags_CharsHexadecimal = 1 << 1,
    ImGuiInputTextFlags_CharsUppercase = 1 << 2,
    ImGuiInputTextFlags_CharsNoBlank = 1 << 3,
    ImGuiInputTextFlags_AutoSelectAll = 1 << 4,
    ImGuiInputTextFlags_EnterReturnsTrue = 1 << 5,
    ImGuiInputTextFlags_CallbackCompletion = 1 << 6,
    ImGuiInputTextFlags_CallbackHistory = 1 << 7,
    ImGuiInputTextFlags_CallbackAlways = 1 << 8,
    ImGuiInputTextFlags_CallbackCharFilter = 1 << 9,
    ImGuiInputTextFlags_AllowTabInput = 1 << 10,
    ImGuiInputTextFlags_CtrlEnterForNewLine = 1 << 11,
    ImGuiInputTextFlags_NoHorizontalScroll = 1 << 12,
    ImGuiInputTextFlags_AlwaysOverwrite = 1 << 13,
    ImGuiInputTextFlags_ReadOnly = 1 << 14,
    ImGuiInputTextFlags_Password = 1 << 15,
    ImGuiInputTextFlags_NoUndoRedo = 1 << 16,
    ImGuiInputTextFlags_CharsScientific = 1 << 17,
    ImGuiInputTextFlags_CallbackResize = 1 << 18,
    ImGuiInputTextFlags_Multiline = 1 << 19,
    ImGuiInputTextFlags_NoMarkEdited = 1 << 20,
};

enum ImGuiKey_ {
    ImGuiKey_Tab = 512,
    ImGuiKey_LeftArrow = 513,
    ImGuiKey_RightArrow = 514,
    ImGuiKey_UpArrow = 515,
    ImGuiKey_DownArrow = 516,
    ImGuiKey_PageUp = 517,
    ImGuiKey_PageDown = 518,
    ImGuiKey_Home = 519,
    ImGuiKey_End = 520,
    ImGuiKey_Insert = 521,
    ImGuiKey_Delete = 522,
    ImGuiKey_Backspace = 523,
    ImGuiKey_Space = 524,
    ImGuiKey_Enter = 525,
    ImGuiKey_Escape = 526,
    ImGuiKey_KeyPadEnter = 527,
    ImGuiKey_A = 528, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E, ImGuiKey_F,
    ImGuiKey_G, ImGuiKey_H, ImGuiKey_I, ImGuiKey_J, ImGuiKey_K, ImGuiKey_L,
    ImGuiKey_M, ImGuiKey_N, ImGuiKey_O, ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R,
    ImGuiKey_S, ImGuiKey_T, ImGuiKey_U, ImGuiKey_V, ImGuiKey_W, ImGuiKey_X,
    ImGuiKey_Y, ImGuiKey_Z,
    ImGuiKey_F1 = 556, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5,
    ImGuiKey_F6, ImGuiKey_F7, ImGuiKey_F8, ImGuiKey_F9, ImGuiKey_F10,
    ImGuiKey_F11, ImGuiKey_F12,
    ImGuiKey_LeftCtrl = 568, ImGuiKey_LeftShift, ImGuiKey_LeftAlt, ImGuiKey_LeftSuper,
    ImGuiKey_RightCtrl, ImGuiKey_RightShift, ImGuiKey_RightAlt, ImGuiKey_RightSuper,
    ImGuiKey_Menu = 576,
    ImGuiKey_0 = 577, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
    ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
};

enum ImGuiMouseButton_ {
    ImGuiMouseButton_Left = 0,
    ImGuiMouseButton_Right = 1,
    ImGuiMouseButton_Middle = 2,
};

enum ImGuiChildFlags_ {
    ImGuiChildFlags_None = 0,
    ImGuiChildFlags_Border = 1 << 0,
    ImGuiChildFlags_AlwaysUseWindowPadding = 1 << 1,
    ImGuiChildFlags_ResizeX = 1 << 2,
    ImGuiChildFlags_ResizeY = 1 << 3,
    ImGuiChildFlags_AutoResizeX = 1 << 4,
    ImGuiChildFlags_AutoResizeY = 1 << 5,
    ImGuiChildFlags_AlwaysAutoResize = ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY,
    ImGuiChildFlags_FrameStyle = 1 << 6,
};

enum ImGuiSelectableFlags_ {
    ImGuiSelectableFlags_None = 0,
    ImGuiSelectableFlags_DontClosePopups = 1 << 0,
    ImGuiSelectableFlags_SpanAllColumns = 1 << 1,
    ImGuiSelectableFlags_AllowDoubleClick = 1 << 2,
    ImGuiSelectableFlags_Disabled = 1 << 3,
    ImGuiSelectableFlags_AllowOverlap = 1 << 4,
};

enum ImGuiComboFlags_ {
    ImGuiComboFlags_None = 0,
    ImGuiComboFlags_PopupAlignLeft = 1 << 0,
    ImGuiComboFlags_HeightSmall = 1 << 1,
    ImGuiComboFlags_HeightRegular = 1 << 2,
    ImGuiComboFlags_HeightLarge = 1 << 3,
    ImGuiComboFlags_HeightLargest = 1 << 4,
    ImGuiComboFlags_NoArrowButton = 1 << 5,
    ImGuiComboFlags_NoPreview = 1 << 6,
};

enum ImGuiPopupFlags_ {
    ImGuiPopupFlags_None = 0,
    ImGuiPopupFlags_MouseButtonLeft = 0,
    ImGuiPopupFlags_MouseButtonRight = 1,
    ImGuiPopupFlags_MouseButtonMiddle = 2,
    ImGuiPopupFlags_MouseButtonMask_ = 0x1F,
    ImGuiPopupFlags_MouseButtonDefault_ = 1,
    ImGuiPopupFlags_NoOpenOverExistingPopup = 1 << 5,
    ImGuiPopupFlags_NoOpenOverItems = 1 << 6,
    ImGuiPopupFlags_AnyPopupId = 1 << 7,
    ImGuiPopupFlags_AnyPopupLevel = 1 << 8,
    ImGuiPopupFlags_AnyPopup = ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel,
};

enum ImGuiDrawFlags_ {
    ImGuiDrawFlags_None = 0,
    ImGuiDrawFlags_Closed = 1 << 0,
    ImGuiDrawFlags_RoundCornersTopLeft = 1 << 1,
    ImGuiDrawFlags_RoundCornersTopRight = 1 << 2,
    ImGuiDrawFlags_RoundCornersBottomLeft = 1 << 3,
    ImGuiDrawFlags_RoundCornersBottomRight = 1 << 4,
    ImGuiDrawFlags_RoundCornersAll = ImGuiDrawFlags_RoundCornersTopLeft | ImGuiDrawFlags_RoundCornersTopRight | ImGuiDrawFlags_RoundCornersBottomLeft | ImGuiDrawFlags_RoundCornersBottomRight,
    ImGuiDrawFlags_RoundCornersDefault_ = ImGuiDrawFlags_RoundCornersAll,
};

} // namespace ImGui