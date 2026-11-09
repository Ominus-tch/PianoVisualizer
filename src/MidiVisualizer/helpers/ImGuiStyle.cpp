#include "ImGuiStyle.h"

#include <imgui/imgui.h>
#include <cmath>

#include "../resources/font_data_Lato.h"

void ImGui::configureFont(ImFontConfig& font)
{
	font = ImFontConfig();
	font.SizePixels = 16.0f;

	ImGuiIO& io = ImGui::GetIO();
	io.FontDefault = io.Fonts->AddFontFromFileTTF(
		"C:\\Windows\\Fonts\\segoeui.ttf",
		font.SizePixels,
		&font
	);

	// Keep the embedded font as a reliable fallback for minimal Windows images.
	if (!io.FontDefault)
	{
		font.FontData = (void*)fontDataLato;
		font.FontDataSize = size_fontDataLato;
		font.FontDataOwnedByAtlas = false;
		io.FontDefault = io.Fonts->AddFont(&font);
	}
}

void ImGui::configureStyle(){
	// Piano Visualizer design tokens: a quiet dark canvas with one cyan accent.
	ImGui::StyleColorsDark();
	ImGuiStyle & style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;
	const ImVec4 base(0.063f, 0.075f, 0.102f, 1.0f);
	const ImVec4 raised(0.094f, 0.114f, 0.153f, 1.0f);
	const ImVec4 frame(0.125f, 0.153f, 0.204f, 1.0f);
	const ImVec4 border(0.169f, 0.204f, 0.267f, 1.0f);
	const ImVec4 accent(0.32f, 0.84f, 1.0f, 1.0f);
	const ImVec4 accentHover(0.40f, 0.88f, 1.0f, 1.0f);

	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.0f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.67f, 0.71f, 0.79f, 1.0f);
	colors[ImGuiCol_WindowBg] = base;
	colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	colors[ImGuiCol_PopupBg] = raised;
	colors[ImGuiCol_Border] = border;
	colors[ImGuiCol_FrameBg] = frame;
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.25f, 0.33f, 1.0f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.31f, 0.42f, 1.0f);
	colors[ImGuiCol_TitleBg] = base;
	colors[ImGuiCol_TitleBgActive] = raised;
	colors[ImGuiCol_MenuBarBg] = raised;
	colors[ImGuiCol_ScrollbarBg] = base;
	colors[ImGuiCol_ScrollbarGrab] = border;
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.25f, 0.36f, 0.46f, 1.0f);
	colors[ImGuiCol_CheckMark] = accent;
	colors[ImGuiCol_SliderGrab] = accent;
	colors[ImGuiCol_SliderGrabActive] = accentHover;
	colors[ImGuiCol_Button] = ImVec4(0.12f, 0.22f, 0.30f, 1.0f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.15f, 0.32f, 0.43f, 1.0f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.41f, 0.54f, 1.0f);
	colors[ImGuiCol_Header] = ImVec4(0.12f, 0.24f, 0.33f, 1.0f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.15f, 0.32f, 0.43f, 1.0f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.41f, 0.54f, 1.0f);
	colors[ImGuiCol_Separator] = border;
	colors[ImGuiCol_ResizeGrip] = border;
	colors[ImGuiCol_ResizeGripHovered] = accent;
	colors[ImGuiCol_ResizeGripActive] = accentHover;
	colors[ImGuiCol_Tab] = raised;
	colors[ImGuiCol_TabHovered] = ImVec4(0.15f, 0.32f, 0.43f, 1.0f);
	colors[ImGuiCol_TabActive] = ImVec4(0.12f, 0.30f, 0.42f, 1.0f);
	colors[ImGuiCol_NavHighlight] = accent;
	colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.28f);
	colors[ImGuiCol_TableHeaderBg] = raised;
	colors[ImGuiCol_TableBorderStrong] = border;
	colors[ImGuiCol_TableBorderLight] = ImVec4(border.x, border.y, border.z, 0.55f);

	style.WindowRounding = 8.0f;
	style.ChildRounding = 8.0f;
	style.FrameRounding = 7.0f;
	style.PopupRounding = 7.0f;
	style.GrabRounding = 7.0f;
	style.ScrollbarRounding = 10.0f;
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.FrameBorderSize = 1.0f;
	style.WindowPadding = ImVec2(16.0f, 14.0f);
	style.FramePadding = ImVec2(10.0f, 7.0f);
	style.ItemSpacing = ImVec2(10.0f, 8.0f);
	style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
	style.ScrollbarSize = 12.0f;
}

bool ImGui::SliderPercent( const char* label, float* v, float v_min, float v_max )
{
	float tmp = ( *v ) * 100.0f;
	const bool press = ImGui::SliderFloat( label, &tmp, v_min * 100.0f, v_max * 100.0f, "%.0f%%" );
	if( press )
	{
		*v = tmp / 100.0f;
	}
	return press;
}

void ImGui::helpTooltip( const char* msg )
{
	if( ImGui::IsItemHovered() )
	{
		ImGui::SetTooltip( "%s", msg );
	}
}
