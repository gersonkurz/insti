#include "pch.h"

// instinctiv - GUI for insti snapshot management
// Phase 4 - Milestone 4.5: Snapshot Browser with Detail Panel

#include <insti/insti.h>
#include "app_state.h"
#include "settings.h"

#include <pnq/config/toml_backend.h>
#include <pnq/path.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <misc/cpp/imgui_stdlib.h>
#include <misc/cpp/imgui_stdlib.cpp>

#include <d3d11.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <tchar.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <format>
#include <filesystem>
#include <chrono>
#include <sstream>


#include "instinctiv.h"


// Config globals

// Drag-and-drop
static std::string g_droppedFile;  // File dropped onto window (processed in main loop)

static std::string FormatFileSize(uint64_t bytes);
static std::string FormatTimestamp(std::chrono::system_clock::time_point tp);
static std::string show_save_dialog(HWND hwnd, const char* filter, const char* default_name, const char* default_ext);

// Helper to constrain dialog size and position within the main window
static void ConstrainDialogToWindow(ImVec2 desiredSize, float padding = 20.0f)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImVec2 availableSize(viewport->Size.x - padding * 2, viewport->Size.y - padding * 2);

	// Clamp size to available space
	ImVec2 size(
		(std::min)(desiredSize.x, availableSize.x),
		(std::min)(desiredSize.y, availableSize.y)
	);

	// Calculate centered position, clamped to stay within bounds
	ImVec2 center = viewport->GetCenter();
	ImVec2 pos(
		(std::max)(viewport->Pos.x + padding, center.x - size.x * 0.5f),
		(std::max)(viewport->Pos.y + padding, center.y - size.y * 0.5f)
	);

	// Ensure right/bottom edges don't exceed bounds
	pos.x = (std::min)(pos.x, viewport->Pos.x + viewport->Size.x - size.x - padding);
	pos.y = (std::min)(pos.y, viewport->Pos.y + viewport->Size.y - size.y - padding);

	ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing);
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

constexpr std::string_view ALL_PROJECTS = "(All Projects)";
constexpr std::string_view NO_PROJECTS = "(No Projects Defined)";


// Win32 message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace instinctiv
{
	Instinctiv* instance = nullptr;

	// Forward declarations for theme functions
	static void apply_tomorrow_night_blue();
	static void apply_light_theme();

	LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return instance->wndproc(hWnd, msg, wParam, lParam);
	}

	Instinctiv::Instinctiv()
		: m_pConfigBackend{ nullptr }
	{
		initialize_config();
		initialize_logging();
		spdlog::info("instinctiv starting up");
		assert(instance == nullptr);
		instance = this;
		
	}

	Instinctiv::~Instinctiv()
	{
		spdlog::info("instinctiv shutting down");

		// Save window state before shutdown
		{
			WINDOWPLACEMENT wp{};
			wp.length = sizeof(WINDOWPLACEMENT);
			if (GetWindowPlacement(m_hWnd, &wp))
			{
				auto& ws = config::theSettings.window;
				ws.maximized.set(wp.showCmd == SW_SHOWMAXIMIZED);
				RECT& rc = wp.rcNormalPosition;
				ws.positionX.set(rc.left);
				ws.positionY.set(rc.top);
				ws.width.set(rc.right - rc.left);
				ws.height.set(rc.bottom - rc.top);
			}
		}

		// Save configuration
		config::theSettings.save();
		spdlog::info("Configuration saved to: {}", m_configPath.string());

		// Cleanup
		m_state.shutdown();

		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		cleanup_device_d3d();
		DestroyWindow(m_hWnd);
		UnregisterClassW(m_wc.lpszClassName, m_hInstance);


		spdlog::info("instinctiv shutting down");

		delete m_pConfigBackend;
		m_pConfigBackend = nullptr;
		instance = nullptr;
		spdlog::info("instinctiv shutdown complete");
	}

	void Instinctiv::cleanup_device_d3d()
	{
		cleanup_render_target();
		if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = nullptr; }
		if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = nullptr; }
		if (m_pd3dDevice) { m_pd3dDevice->Release(); m_pd3dDevice = nullptr; }
	}

	void Instinctiv::create_render_target()
	{
		ID3D11Texture2D* pBackBuffer = nullptr;
		m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
		if (pBackBuffer)
		{
			m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
			pBackBuffer->Release();
		}
	}

	void Instinctiv::cleanup_render_target()
	{
		if (m_mainRenderTargetView) { m_mainRenderTargetView->Release(); m_mainRenderTargetView = nullptr; }
	}

	LRESULT Instinctiv::wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
			return true;

		switch (msg)
		{
		case WM_NCCALCSIZE:
			// Return 0 to remove the entire non-client area (title bar, borders)
			// This eliminates the white pixels at the top
			if (wParam == TRUE)
			{
				// When wParam is TRUE, lParam points to NCCALCSIZE_PARAMS
				// Returning 0 tells Windows to use the entire window as client area
				return 0;
			}
			return DefWindowProcW(hWnd, msg, wParam, lParam);

		case WM_NCHITTEST:
		{
			// Handle hit testing for resize borders since we removed the non-client area
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			ScreenToClient(hWnd, &pt);

			RECT rc;
			GetClientRect(hWnd, &rc);

			const int borderWidth = 5; // Resize border thickness

			bool onLeft = pt.x < borderWidth;
			bool onRight = pt.x >= rc.right - borderWidth;
			bool onTop = pt.y < borderWidth;
			bool onBottom = pt.y >= rc.bottom - borderWidth;

			if (onTop && onLeft) return HTTOPLEFT;
			if (onTop && onRight) return HTTOPRIGHT;
			if (onBottom && onLeft) return HTBOTTOMLEFT;
			if (onBottom && onRight) return HTBOTTOMRIGHT;
			if (onLeft) return HTLEFT;
			if (onRight) return HTRIGHT;
			if (onTop) return HTTOP;
			if (onBottom) return HTBOTTOM;

			return HTCLIENT;
		}

		case WM_ACTIVATE:
		case WM_ACTIVATEAPP:
			m_windowFocused = (wParam != 0);
			return 0;

		case WM_SIZE:
			if (wParam == SIZE_MINIMIZED)
				return 0;
			m_ResizeWidth = LOWORD(lParam);
			m_ResizeHeight = HIWORD(lParam);
			return 0;
		case WM_SYSCOMMAND:
			if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
				return 0;
			break;
		case WM_DROPFILES:
		{
			HDROP hDrop = (HDROP)wParam;
			UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
			if (count > 0)
			{
				// Get first file
				wchar_t path[MAX_PATH];
				if (DragQueryFileW(hDrop, 0, path, MAX_PATH))
				{
					int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
					g_droppedFile.resize(size - 1);
					WideCharToMultiByte(CP_UTF8, 0, path, -1, &g_droppedFile[0], size, nullptr, nullptr);
				}
			}
			DragFinish(hDrop);
		}
		return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	void Instinctiv::run()
	{
		// Main loop
		m_done = false;
		while (!m_done)
		{
			// Poll and handle messages
			MSG msg;
			while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
				if (msg.message == WM_QUIT)
					m_done = true;
			}
			if (m_done)
				break;

			render();
		}
	}

	void Instinctiv::render()
	{
		// Handle window resize
		if (m_ResizeWidth != 0 && m_ResizeHeight != 0)
		{
			cleanup_render_target();
			m_pSwapChain->ResizeBuffers(0, m_ResizeWidth, m_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
			m_ResizeWidth = m_ResizeHeight = 0;
			create_render_target();
		}

		// Apply pending font size change BEFORE starting any ImGui frame
		if (m_pendingFontSize > 0.0f)
		{
			rebuild_font_atlas(m_pendingFontSize);
			m_pendingFontSize = 0.0f;
		}

		// Start the Dear ImGui frame
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// Input handling
		handle_keyboard_shortcuts();
		handle_dropped_file();

		// Process worker thread messages
		process_worker_messages();

		ImGuiIO& io = ImGui::GetIO();
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float titleBarHeight = static_cast<float>(GetSystemMetrics(SM_CYCAPTION));

		// Render custom title bar first
		const float titleBarOverlap = 2.0f;
		{
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, titleBarHeight + titleBarOverlap));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
			ImGuiWindowFlags titleBarFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
			                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
			                                 ImGuiWindowFlags_NoBringToFrontOnFocus;
			ImGui::Begin("TitleBar", nullptr, titleBarFlags);
			render_title_bar();
			ImGui::End();
			ImGui::PopStyleColor(1);
			ImGui::PopStyleVar(2);
		}

		// Main window below title bar - start slightly higher to account for overlap
		const float menuBarOffset = 2.0f;  // Overlap with title bar
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + titleBarHeight - menuBarOffset));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - titleBarHeight + menuBarOffset));

		// Add vertical padding for the menu bar
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
		                                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar;
		ImGui::Begin("##MainWindow", nullptr, window_flags);

		// Render menu bar (ImGui positions this automatically at the top of this window)
		render_menu_bar();

		ImGui::PopStyleVar(2); // Pop FramePadding and WindowBorderSize

		// Handle Ctrl+Mousewheel for font size changes
		if (io.KeyCtrl && io.MouseWheel != 0.0f)
		{
			auto& appSettings = config::theSettings.application;
			int32_t currentSizeScaled = appSettings.fontSizeScaled.get();
			float currentSize = currentSizeScaled / 100.0f;
			float newSize = currentSize + io.MouseWheel * 1.0f; // 1 pixel per wheel notch

			// Clamp font size between 8 and 32
			newSize = std::max(8.0f, std::min(32.0f, newSize));

			if (newSize != currentSize)
			{
				int32_t newSizeScaled = static_cast<int32_t>(newSize * 100.0f);
				appSettings.fontSizeScaled.set(newSizeScaled);
				config::theSettings.save();
				// Defer font rebuild until next frame
				m_pendingFontSize = newSize;
			}
		}

		render_toolbar();
		ImGui::Separator();
		render_snapshot_table();

		ImGui::End();

		// Modal dialogs
		render_progress_dialog();
		render_font_dialog();
		render_backup_dialog();
		render_settings_dialog();
		render_blueprint_editor();
		render_uninstall_confirm_dialog();

		// Rendering
		ImGui::Render();
		const float clear_color[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
		m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
		m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clear_color);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		m_pSwapChain->Present(1, 0); // VSync
	}

	void Instinctiv::handle_keyboard_shortcuts()
	{
		if (ImGui::GetIO().WantTextInput)
			return;

		// Ctrl+R or F5 - Refresh
		bool refresh_pressed = (ImGui::IsKeyPressed(ImGuiKey_R) && ImGui::GetIO().KeyCtrl) ||
			ImGui::IsKeyPressed(ImGuiKey_F5);
		if (refresh_pressed && !m_state.is_refreshing)
		{
			m_state.is_refreshing = true;
			m_state.status_message = "Scanning for snapshots...";
			m_state.worker->post(RefreshRegistry{ m_state.registry_roots });
		}

		// Escape - Close dialogs
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			if (m_state.show_progress_dialog && !m_state.worker->is_busy())
				m_state.show_progress_dialog = false;
			else if (m_showSettingsDialog)
				m_showSettingsDialog = false;
		}
	}

	void Instinctiv::handle_dropped_file()
	{
		if (g_droppedFile.empty())
			return;

		std::filesystem::path dropped{ g_droppedFile };
		std::string ext = dropped.extension().string();

		if (pnq::string::equals_nocase(ext, ".zip"))
		{
			// TODO: Find matching instance and select it
			m_state.status_message = "Dropped snapshot: " + dropped.filename().string();
		}
		else if (pnq::string::equals_nocase(ext, ".xml"))
		{
			m_state.status_message = "Dropped blueprint: " + dropped.filename().string();
		}
		else
		{
			m_state.status_message = "Unsupported file type: " + ext;
		}

		g_droppedFile.clear();
	}

	void Instinctiv::render_menu_bar()
	{
		if (!ImGui::BeginMenuBar())
			return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Exit", "Alt+F4"))
				m_done = true;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Blueprint Editor..."))
			{
				m_showBlueprintEditor = true;
				m_blueprintEditorMode = BlueprintEditorMode::None;
				m_blueprintEditorSelectedProject = -1;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			if (ImGui::MenuItem("Refresh", "Ctrl+R", false, !m_state.is_refreshing))
			{
				m_state.is_refreshing = true;
				m_state.status_message = "Scanning for snapshots...";
				m_state.worker->post(RefreshRegistry{ m_state.registry_roots });
			}

			ImGui::Separator();

			// Theme selection
			auto& appSettings = config::theSettings.application;
			std::string currentTheme = appSettings.theme.get();

			if (ImGui::MenuItem("Dark Theme", nullptr, currentTheme == "Dark"))
			{
				appSettings.theme.set("Dark");
				config::theSettings.save();
				ImGui::StyleColorsDark();
				apply_style();
			}

			if (ImGui::MenuItem("Light Theme", nullptr, currentTheme == "Light"))
			{
				appSettings.theme.set("Light");
				config::theSettings.save();
				apply_light_theme();
				apply_style();
			}

			if (ImGui::MenuItem("Tomorrow Night Blue", nullptr, currentTheme == "Tomorrow Night Blue"))
			{
				appSettings.theme.set("Tomorrow Night Blue");
				config::theSettings.save();
				apply_tomorrow_night_blue();
				apply_style();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Select Font..."))
			{
				m_showFontDialog = true;
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Settings..."))
			{
				// Copy current roots to editable list
				m_settingsRoots.clear();
				for (const auto& root : m_state.registry_roots)
					m_settingsRoots.push_back(root);
				m_settingsSelectedRoot = -1;
				m_showSettingsDialog = true;
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About insti..."))
			{
				ShellExecuteW(m_hWnd, L"open", L"https://github.com/gersonkurz/insti", nullptr, nullptr, SW_SHOWNORMAL);
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	void Instinctiv::render_toolbar()
	{
		// Get registry data
		pnq::RefCountedVector<insti::Instance*>* instances = nullptr;
		pnq::RefCountedVector<insti::Project*>* projects = nullptr;

		if (m_state.m_snapshot_registry)
		{
			projects = &m_state.m_snapshot_registry->m_projects;
			instances = &m_state.m_snapshot_registry->m_instances;
		}

		// Determine current project selection
		m_current_project = nullptr;
		std::string current_project_name;

		if (projects && !projects->empty())
		{
			const auto lastBlueprint = config::theSettings.application.lastBlueprint.get();

			// Find the project matching saved selection
			for (auto* project : *projects)
			{
				if (pnq::string::equals(lastBlueprint, project->project_name()))
				{
					m_current_project = project;
					current_project_name = project->project_name();
					break;
				}
			}

			// Fall back to first project if saved selection not found
			if (!m_current_project)
			{
				m_current_project = projects->at(0);
				current_project_name = m_current_project->project_name();
				config::theSettings.application.lastBlueprint.set(current_project_name);
				config::theSettings.save();
			}
		}

		// Project selector combobox
		ImGui::SetNextItemWidth(400.0f);
		if (ImGui::BeginCombo("##Project", current_project_name.c_str()))
		{
			if (projects)
			{
				for (auto* project : *projects)
				{
					bool is_selected = (project == m_current_project);
					if (ImGui::Selectable(project->project_name().c_str(), is_selected))
					{
						m_current_project = project;
						config::theSettings.application.lastBlueprint.set(project->project_name());
						config::theSettings.save();
					}
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();

		// Filter input
		static char filter_buf[256] = "";
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::InputTextWithHint("##Filter", "Filter snapshots...", filter_buf, sizeof(filter_buf)))
		{
			m_state.filter_text = filter_buf;
			m_state.filter_dirty = true;
		}
		ImGui::SameLine();

		// Refresh button
		ImGui::BeginDisabled(m_state.is_refreshing);
		if (ImGui::Button(m_state.is_refreshing ? "Refreshing..." : "Refresh"))
		{
			m_state.is_refreshing = true;
			m_state.status_message = "Scanning for snapshots...";
			m_state.worker->post(RefreshRegistry{ m_state.registry_roots });
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		// Operation buttons
		bool has_project = (m_current_project != nullptr);
		bool is_busy = m_state.worker->is_busy();

		// Backup button - requires a project selected
		ImGui::BeginDisabled(!has_project || is_busy);
		if (ImGui::Button("Backup"))
		{
			start_backup_from_project(m_current_project);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !has_project)
			ImGui::SetTooltip("Select a project first");

		ImGui::SameLine();

		// Uninstall button - requires a project selected
		ImGui::BeginDisabled(!has_project || is_busy);
		if (ImGui::Button("Uninstall"))
		{
			start_clean_from_project(m_current_project);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !has_project)
			ImGui::SetTooltip("Select a project first");

		ImGui::SameLine();

		// Verify button - requires a project selected
		ImGui::BeginDisabled(!has_project || is_busy);
		if (ImGui::Button("Verify"))
		{
			start_verify_from_project(m_current_project);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !has_project)
			ImGui::SetTooltip("Select a project first");

		// Startup/Shutdown buttons
		if (has_project)
		{
			bool has_startup = !m_current_project->startup_hooks().empty();
			bool has_shutdown = !m_current_project->shutdown_hooks().empty();

			if (has_startup || has_shutdown)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("|");

				if (has_startup)
				{
					ImGui::SameLine();
					ImGui::BeginDisabled(is_busy);
					if (ImGui::Button("Startup"))
					{
						start_startup(m_current_project);
					}
					ImGui::EndDisabled();
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
						ImGui::SetTooltip("Run startup hooks");
				}

				if (has_shutdown)
				{
					ImGui::SameLine();
					ImGui::BeginDisabled(is_busy);
					if (ImGui::Button("Shutdown"))
					{
						start_shutdown(m_current_project);
					}
					ImGui::EndDisabled();
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
						ImGui::SetTooltip("Run shutdown hooks");
				}
			}

			// Standalone hook buttons from current project
			auto standalone = m_current_project->standalone_hooks();
			if (!standalone.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("|");

				for (auto* hook : standalone)
				{
					ImGui::SameLine();
					ImGui::BeginDisabled(is_busy);
					if (ImGui::Button(hook->name().c_str()))
					{
						start_hook_execution(hook);
					}
					ImGui::EndDisabled();
				}
			}
		}

		ImGui::SameLine();

		// Dry-run checkbox
		ImGui::Checkbox("Dry-run", &m_state.dry_run);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Simulate operations without making changes");

		// Status message
		if (!m_state.status_message.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", m_state.status_message.c_str());
		}
	}

	void Instinctiv::render_snapshot_table()
	{
		// Get instances from registry
		pnq::RefCountedVector<insti::Instance*>* instances = nullptr;
		if (m_state.m_snapshot_registry)
			instances = &m_state.m_snapshot_registry->m_instances;

		// Build filtered list
		m_filtered_instances.clear();
		if (instances)
		{
			const auto search_text = pnq::string::lowercase(m_state.filter_text);
			for (auto* instance : *instances)
			{
				if (instance->matches(search_text))
					m_filtered_instances.push_back(instance);
			}

			// Sort by timestamp (newest first)
			std::sort(m_filtered_instances.begin(), m_filtered_instances.end(),
				[](insti::Instance* a, insti::Instance* b) {
					return a->m_timestamp > b->m_timestamp;
				});
		}

		// Validate selection (might have been invalidated by refresh)
		if (m_state.selected_instance)
		{
			bool found = false;
			for (auto* instance : m_filtered_instances)
			{
				if (instance == m_state.selected_instance)
				{
					found = true;
					break;
				}
			}
			if (!found)
				m_state.selected_instance = nullptr;
		}

		// Render table
		float table_width = ImGui::GetContentRegionAvail().x;
		ImGui::BeginChild("SnapshotList", ImVec2(table_width, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

		if (m_filtered_instances.empty())
		{
			ImGui::TextDisabled("No snapshots found");
		}
		else
		{
			ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

			if (ImGui::BeginTable("SnapshotTable", 7, table_flags))
			{
				// Use fixed initial widths that can be resized - enables horizontal scrolling
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed, 150.0f);
				ImGui::TableSetupColumn("Install Directory", ImGuiTableColumnFlags_WidthFixed, 250.0f);
				ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Machine", ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				int row_id = 0;
				for (auto* entry : m_filtered_instances)
				{
					ImGui::PushID(row_id++);
					ImGui::TableNextRow();

					// Row highlighting based on installation status
					auto status = entry->m_install_status;
					if (status == insti::InstallStatus::Installed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(100, 200, 100, 60));
					else if (status == insti::InstallStatus::DifferentVersion)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(220, 180, 50, 40));

					// Name column with selection
					ImGui::TableNextColumn();
					bool is_selected = (m_state.selected_instance == entry);
					ImGuiSelectableFlags sel_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick;
					if (ImGui::Selectable(entry->project_name().c_str(), is_selected, sel_flags))
					{
						m_state.selected_instance = entry;

						// Double-click opens the zip file with default shell action
						if (ImGui::IsMouseDoubleClicked(0))
						{
							ShellExecuteW(m_hWnd, nullptr,
								pnq::win32::wstr_param{ entry->m_snapshot_path },
								nullptr, nullptr, SW_SHOWNORMAL);
						}
					}

					// Right-click context menu
					if (ImGui::BeginPopupContextItem())
					{
						bool is_busy = m_state.worker->is_busy();

						if (ImGui::MenuItem("Restore", nullptr, false, !is_busy))
						{
							start_restore_from_instance(entry);
						}

						if (ImGui::MenuItem("Verify", nullptr, false, !is_busy))
						{
							start_verify_from_instance(entry);
						}

						ImGui::Separator();

						if (ImGui::MenuItem("Backup (Refresh)", nullptr, false, !is_busy))
						{
							start_backup_from_instance(entry);
						}

						if (ImGui::MenuItem("Uninstall", nullptr, false, !is_busy))
						{
							start_clean_from_instance(entry);
						}

						ImGui::Separator();

						// Startup/Shutdown hooks
						bool has_startup = !entry->startup_hooks().empty();
						bool has_shutdown = !entry->shutdown_hooks().empty();

						if (has_startup && ImGui::MenuItem("Startup", nullptr, false, !is_busy))
						{
							start_startup(entry);
						}

						if (has_shutdown && ImGui::MenuItem("Shutdown", nullptr, false, !is_busy))
						{
							start_shutdown(entry);
						}

						if (has_startup || has_shutdown)
							ImGui::Separator();

						if (ImGui::MenuItem("Open Containing Folder"))
						{
							// Open explorer to the folder containing this snapshot
							std::filesystem::path snapshot_path{ entry->m_snapshot_path };
							std::filesystem::path parent = snapshot_path.parent_path();
							ShellExecuteW(m_hWnd, L"explore", parent.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
						}

						ImGui::EndPopup();
					}

					// Other columns
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry->m_description.c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry->timestamp_string().c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry->installdir().c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry->project_version().c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry->m_machine.c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(entry->m_user.c_str());

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		ImGui::EndChild();
	}

	void Instinctiv::start_restore_from_instance(insti::Instance* instance)
	{
		if (!instance)
			return;

		spdlog::info("start_restore_from_instance: {}", instance->m_snapshot_path);

		// Setup progress dialog
		m_state.progress_operation = m_state.dry_run ? "Dry-run Restore" : "Restore";
		m_state.progress_phase = "Starting";
		m_state.progress_detail = "";
		m_state.progress_percent = -1;
		m_state.progress_log.clear();
		m_state.progress_log.push_back("Starting restore: " + instance->project_name());
		m_state.progress_log.push_back("From: " + instance->m_snapshot_path);
		m_state.show_progress_dialog = true;

		// Store reference to instance for the operation
		if (m_state.active_blueprint)
			PNQ_RELEASE(m_state.active_blueprint);
		m_state.active_blueprint = instance;  // Instance inherits from Project
		PNQ_ADDREF(m_state.active_blueprint);

		// Start restore on worker thread (pass archive path)
		std::unordered_map<std::string, std::string> variable_overrides;  // TODO: support overrides in UI
		m_state.worker->post(StartRestore{ m_state.m_snapshot_registry, instance->m_snapshot_path, variable_overrides });
	}

	void Instinctiv::start_hook_execution(insti::IHook* hook)
	{
		if (!hook || !m_current_project)
			return;

		std::string hook_name = hook->name().empty() ? hook->type_name() : hook->name();
		spdlog::info("start_hook_execution: {}", hook_name);

		// Setup progress dialog
		m_state.progress_operation = hook_name;
		m_state.progress_phase = "Starting";
		m_state.progress_detail = "";
		m_state.progress_percent = -1;
		m_state.progress_log.clear();
		m_state.progress_log.push_back("Running hook: " + hook_name);
		m_state.show_progress_dialog = true;

		// Store reference to project for the operation (add ref since we're keeping it)
		if (m_state.active_blueprint)
			PNQ_RELEASE(m_state.active_blueprint);
		m_state.active_blueprint = m_current_project;
		PNQ_ADDREF(m_state.active_blueprint);

		// Start hook execution on worker thread
		m_state.worker->post(StartHook{ m_current_project, hook });
	}

	void Instinctiv::start_startup(insti::Project* blueprint)
	{
		if (!blueprint)
			return;

		spdlog::info("start_startup: {}", blueprint->project_name());

		// Setup progress dialog
		m_state.progress_operation = "Startup";
		m_state.progress_phase = "Starting";
		m_state.progress_detail = "";
		m_state.progress_percent = -1;
		m_state.progress_log.clear();
		m_state.progress_log.push_back("Running startup hooks for: " + blueprint->project_name());
		m_state.show_progress_dialog = true;

		// Store reference to project for the operation
		if (m_state.active_blueprint)
			PNQ_RELEASE(m_state.active_blueprint);
		m_state.active_blueprint = blueprint;
		PNQ_ADDREF(m_state.active_blueprint);

		// Start startup on worker thread
		m_state.worker->post(StartStartup{ m_state.m_snapshot_registry, blueprint });
	}

	void Instinctiv::start_shutdown(insti::Project* blueprint)
	{
		if (!blueprint)
			return;

		spdlog::info("start_shutdown: {}", blueprint->project_name());

		// Setup progress dialog
		m_state.progress_operation = "Shutdown";
		m_state.progress_phase = "Starting";
		m_state.progress_detail = "";
		m_state.progress_percent = -1;
		m_state.progress_log.clear();
		m_state.progress_log.push_back("Running shutdown hooks for: " + blueprint->project_name());
		m_state.show_progress_dialog = true;

		// Store reference to project for the operation
		if (m_state.active_blueprint)
			PNQ_RELEASE(m_state.active_blueprint);
		m_state.active_blueprint = blueprint;
		PNQ_ADDREF(m_state.active_blueprint);

		// Start shutdown on worker thread
		m_state.worker->post(StartShutdown{ m_state.m_snapshot_registry, blueprint });
	}

	bool Instinctiv::initialize(HINSTANCE hInstance)
	{
		m_hInstance = hInstance;

		// Load window settings
		const auto& windowSettings = config::theSettings.window;
		const int width = windowSettings.width;
		const int height = windowSettings.height;
		const int posX = windowSettings.positionX;
		const int posY = windowSettings.positionY;
		const bool maximized = windowSettings.maximized;

		// Create application window
		WNDCLASSEXW wc{
			sizeof(wc),
			CS_CLASSDC,
			WndProc,
			0, 0,
			hInstance,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			L"instinctiv",
			nullptr
		};
		RegisterClassExW(&wc);
		m_wc = wc;


		const auto strWindowTitle{ std::format("instinctiv {}", insti::version()) };

		// Create window with modern borderless style
		// WS_POPUP = no title bar, WS_THICKFRAME = resizable borders
		m_hWnd = CreateWindowExW(
			0,
			wc.lpszClassName,
			pnq::win32::wstr_param{ strWindowTitle },
			WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
			posX, posY,
			width, height,
			nullptr, nullptr,
			hInstance,
			nullptr
		);
		if (!m_hWnd)
		{
			PNQ_LOG_WIN_ERROR(GetLastError(), "CreateWindowEx() failed");
			return false;
		}

		// Query Windows accent color for custom title bar
		DWORD accentColor = 0;
		BOOL opaque = FALSE;
		HRESULT hr = DwmGetColorizationColor(&accentColor, &opaque);
		if (SUCCEEDED(hr))
		{
			m_accentColor = accentColor;
			spdlog::debug("Windows accent color: 0x{:08X}", accentColor);
		}

		// Enable drag-and-drop
		DragAcceptFiles(m_hWnd, TRUE);

		// Initialize Direct3D
		if (!create_device_d3d(m_hWnd))
		{
			PNQ_LOG_WIN_ERROR(GetLastError(), "create_device_d3d() failed");
			cleanup_device_d3d();
			UnregisterClassW(wc.lpszClassName, hInstance);
			return false;
		}

		ShowWindow(m_hWnd, maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
		UpdateWindow(m_hWnd);

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		// Set ImGui ini file path to AppData folder
		if (!m_imguiIniPath.empty())
			io.IniFilename = m_imguiIniPath.c_str();

		// Setup Dear ImGui style
		auto& appSettings = config::theSettings.application;
		std::string savedTheme = appSettings.theme.get();
		if (savedTheme == "Light")
			apply_light_theme();
		else if (savedTheme == "Tomorrow Night Blue")
			apply_tomorrow_night_blue();
		else
			ImGui::StyleColorsDark();
		apply_style();

		// Setup Platform/Renderer backends
		ImGui_ImplWin32_Init(m_hWnd);
		ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

		// Load font with size from settings
		int32_t fontSizeScaled = appSettings.fontSizeScaled.get();
		float fontSize = fontSizeScaled / 100.0f;
		rebuild_font_atlas(fontSize);

		// Initialize application state
		return m_state.initialize();
	}

	bool Instinctiv::create_device_d3d(HWND hWnd)
	{
		DXGI_SWAP_CHAIN_DESC sd = {};
		sd.BufferCount = 2;
		sd.BufferDesc.Width = 0;
		sd.BufferDesc.Height = 0;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = hWnd;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Windowed = TRUE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		UINT createDeviceFlags = 0;
		D3D_FEATURE_LEVEL featureLevel;
		const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
		HRESULT res = D3D11CreateDeviceAndSwapChain(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
			featureLevelArray, 2, D3D11_SDK_VERSION,
			&sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
		if (res != S_OK)
			return false;

		create_render_target();
		return true;
	}

	// Look up font filename from Windows registry
	// Returns empty string if not found
	static std::string lookup_font_file(const std::string& fontName)
	{
		// Fonts are registered in HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts
		// Value names are like "Arial (TrueType)" or "Google Sans Flex (TrueType)"
		// Value data is the filename (or full path for user-installed fonts)
		pnq::regis3::key fontsKey{ "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts" };
		if (!fontsKey.open_for_reading())
			return {};

		// Search for a value that starts with the font name
		std::string searchPrefix = fontName + " (";
		for (const auto& value : fontsKey.enum_values())
		{
			std::string valueName{ value.name() };
			if (valueName.starts_with(searchPrefix))
			{
				return value.get_string();
			}
		}
		return {};
	}

	// Rebuild font atlas with custom font
	void Instinctiv::rebuild_font_atlas(float fontSize)
	{
		ImGuiIO& io = ImGui::GetIO();

		// Clear existing fonts
		io.Fonts->Clear();

		// Get configured font name
		std::string fontName = config::theSettings.application.fontName.get();

		const auto windowsDir{ pnq::directory::windows() };
		const auto fontsDir{ pnq::path::combine(windowsDir, "Fonts") };
		bool fontLoaded = false;

		// Look up font file from registry
		std::string fontFile = lookup_font_file(fontName);
		if (!fontFile.empty())
		{
			// Check if it's a full path or just a filename
			std::string fontPath;
			if (fontFile.find('\\') != std::string::npos || fontFile.find('/') != std::string::npos)
				fontPath = fontFile;  // Full path
			else
				fontPath = pnq::path::combine(fontsDir, fontFile);  // Relative to Fonts dir

			if (io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize))
			{
				spdlog::info("Loaded font: {} ({}) at size {}", fontName, fontPath, fontSize);
				fontLoaded = true;
			}
		}

		// Fallback to Segoe UI, then Arial
		if (!fontLoaded)
		{
			if (!fontFile.empty())
				spdlog::warn("Could not load font '{}' from '{}', trying fallbacks", fontName, fontFile);
			else
				spdlog::warn("Font '{}' not found in registry, trying fallbacks", fontName);

			const char* fallbackNames[] = { "Segoe UI", "Arial" };
			for (const char* fallbackName : fallbackNames)
			{
				std::string fallbackFile = lookup_font_file(fallbackName);
				if (!fallbackFile.empty())
				{
					const auto fontPath{ pnq::path::combine(fontsDir, fallbackFile) };
					if (io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize))
					{
						spdlog::info("Loaded fallback font: {} ({}) at size {}", fallbackName, fontPath, fontSize);
						fontLoaded = true;
						break;
					}
				}
			}

			if (!fontLoaded)
			{
				spdlog::warn("Could not load any font, using ImGui default");
				io.Fonts->AddFontDefault();
			}
		}

		// Rebuild font texture
		io.Fonts->Build();

		// Set the new font as default
		if (io.Fonts->Fonts.Size > 0)
		{
			io.FontDefault = io.Fonts->Fonts[0];
			// Update ImGui's FontSizeBase so it renders at the new size
			ImGui::GetStyle().FontSizeBase = fontSize;
		}

		// Notify ImGui backends to update their font texture
		if (m_pd3dDevice && m_pd3dDeviceContext)
		{
			ImGui_ImplDX11_InvalidateDeviceObjects();
			ImGui_ImplDX11_CreateDeviceObjects();
		}
	}

	// Apply Tomorrow Night Blue theme
	static void apply_tomorrow_night_blue()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors = style.Colors;

		// Tomorrow Night Blue palette
		const ImVec4 bg          = ImVec4(0.000f, 0.145f, 0.318f, 1.00f);  // #002451
		const ImVec4 bgLight     = ImVec4(0.000f, 0.180f, 0.380f, 1.00f);  // Slightly lighter
		const ImVec4 bgLighter   = ImVec4(0.000f, 0.220f, 0.450f, 1.00f);  // Even lighter
		const ImVec4 fg          = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);  // #FFFFFF
		const ImVec4 fgDim       = ImVec4(0.447f, 0.522f, 0.718f, 1.00f);  // #7285B7
		const ImVec4 accent      = ImVec4(0.200f, 0.400f, 0.650f, 1.00f);  // Darker blue for selection
		const ImVec4 accentHover = ImVec4(0.300f, 0.500f, 0.750f, 1.00f);  // Slightly lighter on hover
		const ImVec4 green       = ImVec4(0.820f, 0.945f, 0.663f, 1.00f);  // #D1F1A9

		colors[ImGuiCol_Text]                  = fg;
		colors[ImGuiCol_TextDisabled]          = fgDim;
		colors[ImGuiCol_WindowBg]              = bg;
		colors[ImGuiCol_ChildBg]               = bg;
		colors[ImGuiCol_PopupBg]               = bgLight;
		colors[ImGuiCol_Border]                = bgLighter;
		colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_FrameBg]               = bgLight;
		colors[ImGuiCol_FrameBgHovered]        = bgLighter;
		colors[ImGuiCol_FrameBgActive]         = bgLighter;
		colors[ImGuiCol_TitleBg]               = bg;
		colors[ImGuiCol_TitleBgActive]         = bgLight;
		colors[ImGuiCol_TitleBgCollapsed]      = bg;
		colors[ImGuiCol_MenuBarBg]             = bgLight;
		colors[ImGuiCol_ScrollbarBg]           = bg;
		colors[ImGuiCol_ScrollbarGrab]         = bgLighter;
		colors[ImGuiCol_ScrollbarGrabHovered]  = accent;
		colors[ImGuiCol_ScrollbarGrabActive]   = accentHover;
		colors[ImGuiCol_CheckMark]             = accent;
		colors[ImGuiCol_SliderGrab]            = accent;
		colors[ImGuiCol_SliderGrabActive]      = accentHover;
		colors[ImGuiCol_Button]                = bgLighter;
		colors[ImGuiCol_ButtonHovered]         = accent;
		colors[ImGuiCol_ButtonActive]          = accentHover;
		colors[ImGuiCol_Header]                = bgLighter;
		colors[ImGuiCol_HeaderHovered]         = accent;
		colors[ImGuiCol_HeaderActive]          = accentHover;
		colors[ImGuiCol_Separator]             = bgLighter;
		colors[ImGuiCol_SeparatorHovered]      = accent;
		colors[ImGuiCol_SeparatorActive]       = accentHover;
		colors[ImGuiCol_ResizeGrip]            = bgLighter;
		colors[ImGuiCol_ResizeGripHovered]     = accent;
		colors[ImGuiCol_ResizeGripActive]      = accentHover;
		colors[ImGuiCol_Tab]                   = bgLight;
		colors[ImGuiCol_TabHovered]            = accent;
		colors[ImGuiCol_TabSelected]           = bgLighter;
		colors[ImGuiCol_TableHeaderBg]         = bgLight;
		colors[ImGuiCol_TableBorderStrong]     = bgLighter;
		colors[ImGuiCol_TableBorderLight]      = bgLight;
		colors[ImGuiCol_TableRowBg]            = bg;
		colors[ImGuiCol_TableRowBgAlt]         = bgLight;
		colors[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
		colors[ImGuiCol_NavHighlight]          = accent;
	}

	// Apply Light theme with medium blue accents
	static void apply_light_theme()
	{
		ImGui::StyleColorsLight();

		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors = style.Colors;

		// Medium blue accent - readable with black text
		const ImVec4 blue       = ImVec4(0.40f, 0.55f, 0.75f, 1.00f);  // Medium blue
		const ImVec4 blueHover  = ImVec4(0.50f, 0.65f, 0.85f, 1.00f);  // Lighter on hover
		const ImVec4 blueActive = ImVec4(0.35f, 0.50f, 0.70f, 1.00f);  // Slightly darker when active

		colors[ImGuiCol_Header]            = blue;
		colors[ImGuiCol_HeaderHovered]     = blueHover;
		colors[ImGuiCol_HeaderActive]      = blueActive;
		colors[ImGuiCol_Button]            = blue;
		colors[ImGuiCol_ButtonHovered]     = blueHover;
		colors[ImGuiCol_ButtonActive]      = blueActive;
		colors[ImGuiCol_CheckMark]         = blue;
		colors[ImGuiCol_SliderGrab]        = blue;
		colors[ImGuiCol_SliderGrabActive]  = blueActive;
		colors[ImGuiCol_Tab]               = blue;
		colors[ImGuiCol_TabHovered]        = blueHover;
		colors[ImGuiCol_TabSelected]       = blueActive;
		colors[ImGuiCol_TextSelectedBg]    = ImVec4(blue.x, blue.y, blue.z, 0.35f);
		colors[ImGuiCol_NavHighlight]      = blue;
	}

	// Apply custom style adjustments (called after theme change)
	void Instinctiv::apply_style()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.FrameRounding = 4.0f;
		style.WindowRounding = 6.0f;
		style.ScrollbarRounding = 4.0f;
		style.GrabRounding = 4.0f;
	}

	bool Instinctiv::is_window_maximized() const
	{
		WINDOWPLACEMENT wp{};
		wp.length = sizeof(WINDOWPLACEMENT);
		if (!GetWindowPlacement(m_hWnd, &wp))
			return false;
		return wp.showCmd == SW_SHOWMAXIMIZED;
	}

	void Instinctiv::render_title_bar()
	{
		const float titleBarHeight = static_cast<float>(GetSystemMetrics(SM_CYCAPTION));
		const float buttonWidth = 46.0f;
		const float buttonHeight = titleBarHeight;

		ImGuiIO& io = ImGui::GetIO();
		ImVec2 titleBarMin = ImGui::GetWindowPos();
		ImVec2 titleBarMax = ImVec2(titleBarMin.x + ImGui::GetWindowSize().x, titleBarMin.y + titleBarHeight);

		// Draw title bar background with accent color when focused
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec4 titleBarColor;

		if (m_windowFocused)
		{
			// Use Windows accent color when focused
			float r = GetRValue(m_accentColor) / 255.0f;
			float g = GetGValue(m_accentColor) / 255.0f;
			float b = GetBValue(m_accentColor) / 255.0f;
			titleBarColor = ImVec4(r, g, b, 1.0f);
		}
		else
		{
			// Use neutral gray when unfocused
			titleBarColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
		}

		drawList->AddRectFilled(titleBarMin, titleBarMax, ImGui::ColorConvertFloat4ToU32(titleBarColor));

		// Handle window dragging
		ImVec2 mousePos = io.MousePos;
		bool mouseInTitleBar = mousePos.x >= titleBarMin.x && mousePos.x <= titleBarMax.x &&
			mousePos.y >= titleBarMin.y && mousePos.y <= titleBarMax.y;

		// Check if mouse is NOT over window control buttons (rightmost 3 buttons)
		float buttonsAreaStart = titleBarMax.x - (buttonWidth * 3);
		bool mouseInButtons = mousePos.x >= buttonsAreaStart && mousePos.x <= titleBarMax.x &&
			mousePos.y >= titleBarMin.y && mousePos.y <= titleBarMax.y;

		if (mouseInTitleBar && !mouseInButtons && ImGui::IsMouseClicked(0))
		{
			// Send WM_NCLBUTTONDOWN to enable native window dragging and snapping
			ReleaseCapture();
			SendMessageW(m_hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
		}

		// Double-click to maximize/restore
		if (mouseInTitleBar && !mouseInButtons && ImGui::IsMouseDoubleClicked(0))
		{
			ShowWindow(m_hWnd, is_window_maximized() ? SW_RESTORE : SW_MAXIMIZE);
		}

		// Draw window title on the left
		ImGui::SetCursorScreenPos(ImVec2(titleBarMin.x + 10.0f, titleBarMin.y + (titleBarHeight - ImGui::GetTextLineHeight()) * 0.5f));
		ImGui::Text(std::format("instinctiv {}", insti::version()).c_str());

		// Window control buttons (right side)
		const ImU32 iconColor = IM_COL32(255, 255, 255, 255);
		const float iconSize = 10.0f;
		const float iconThickness = 1.0f;

		// Button positions
		ImVec2 minBtnPos = ImVec2(titleBarMax.x - buttonWidth * 3, titleBarMin.y);
		ImVec2 maxBtnPos = ImVec2(titleBarMax.x - buttonWidth * 2, titleBarMin.y);
		ImVec2 closeBtnPos = ImVec2(titleBarMax.x - buttonWidth, titleBarMin.y);

		bool minHovered = mousePos.x >= minBtnPos.x && mousePos.x < minBtnPos.x + buttonWidth &&
		                  mousePos.y >= minBtnPos.y && mousePos.y < minBtnPos.y + buttonHeight;
		bool maxHovered = mousePos.x >= maxBtnPos.x && mousePos.x < maxBtnPos.x + buttonWidth &&
		                  mousePos.y >= maxBtnPos.y && mousePos.y < maxBtnPos.y + buttonHeight;
		bool closeHovered = mousePos.x >= closeBtnPos.x && mousePos.x < closeBtnPos.x + buttonWidth &&
		                    mousePos.y >= closeBtnPos.y && mousePos.y < closeBtnPos.y + buttonHeight;

		// Draw hover backgrounds
		if (minHovered)
			drawList->AddRectFilled(minBtnPos, ImVec2(minBtnPos.x + buttonWidth, minBtnPos.y + buttonHeight), IM_COL32(255, 255, 255, 30));
		if (maxHovered)
			drawList->AddRectFilled(maxBtnPos, ImVec2(maxBtnPos.x + buttonWidth, maxBtnPos.y + buttonHeight), IM_COL32(255, 255, 255, 30));
		if (closeHovered)
			drawList->AddRectFilled(closeBtnPos, ImVec2(closeBtnPos.x + buttonWidth, closeBtnPos.y + buttonHeight), IM_COL32(196, 43, 28, 255));

		// Minimize icon (horizontal line)
		{
			ImVec2 center = ImVec2(minBtnPos.x + buttonWidth * 0.5f, minBtnPos.y + buttonHeight * 0.5f);
			drawList->AddLine(
				ImVec2(center.x - iconSize * 0.5f, center.y),
				ImVec2(center.x + iconSize * 0.5f, center.y),
				iconColor, iconThickness);
		}

		// Maximize/Restore icon
		bool isMaximized = is_window_maximized();
		{
			ImVec2 center = ImVec2(maxBtnPos.x + buttonWidth * 0.5f, maxBtnPos.y + buttonHeight * 0.5f);
			if (isMaximized)
			{
				// Restore icon: two overlapping rectangles
				float smallSize = iconSize * 0.7f;
				drawList->AddRect(
					ImVec2(center.x - smallSize * 0.5f + 2, center.y - smallSize * 0.5f - 2),
					ImVec2(center.x + smallSize * 0.5f + 2, center.y + smallSize * 0.5f - 2),
					iconColor, 0.0f, 0, iconThickness);
				drawList->AddRect(
					ImVec2(center.x - smallSize * 0.5f - 1, center.y - smallSize * 0.5f + 1),
					ImVec2(center.x + smallSize * 0.5f - 1, center.y + smallSize * 0.5f + 1),
					iconColor, 0.0f, 0, iconThickness);
			}
			else
			{
				// Maximize icon: single rectangle
				drawList->AddRect(
					ImVec2(center.x - iconSize * 0.5f, center.y - iconSize * 0.5f),
					ImVec2(center.x + iconSize * 0.5f, center.y + iconSize * 0.5f),
					iconColor, 0.0f, 0, iconThickness);
			}
		}

		// Close icon (X)
		{
			ImVec2 center = ImVec2(closeBtnPos.x + buttonWidth * 0.5f, closeBtnPos.y + buttonHeight * 0.5f);
			float halfSize = iconSize * 0.5f;
			drawList->AddLine(
				ImVec2(center.x - halfSize, center.y - halfSize),
				ImVec2(center.x + halfSize, center.y + halfSize),
				iconColor, iconThickness);
			drawList->AddLine(
				ImVec2(center.x + halfSize, center.y - halfSize),
				ImVec2(center.x - halfSize, center.y + halfSize),
				iconColor, iconThickness);
		}

		// Handle button clicks
		if (ImGui::IsMouseClicked(0))
		{
			if (minHovered)
				ShowWindow(m_hWnd, SW_MINIMIZE);
			else if (maxHovered)
				ShowWindow(m_hWnd, isMaximized ? SW_RESTORE : SW_MAXIMIZE);
			else if (closeHovered)
				DestroyWindow(m_hWnd);
		}
	}

	// Initialize configuration
	void Instinctiv::initialize_config()
	{
		// Get AppData path: %LOCALAPPDATA%\insti
		m_appDataPath = pnq::path::get_known_folder(FOLDERID_LocalAppData) / "insti";
		std::filesystem::create_directories(m_appDataPath);

		m_configPath = m_appDataPath / "insti.toml";
		m_imguiIniPath = (m_appDataPath / "imgui.ini").string();

		// Load configuration
		m_pConfigBackend = PNQ_NEW pnq::config::TomlBackend{ m_configPath.string() };
		if (m_pConfigBackend)
		{
			config::theSettings.load();
		}

		// Ensure default registry root exists
		auto& registrySettings = config::theSettings.registry;
		const auto roots = registrySettings.roots.get();
		if (!roots.empty())
		{
			// Parse and create first root if needed
			std::istringstream iss{ roots };
			std::string first_root;
			std::getline(iss, first_root, ';');
			if (!first_root.empty())
			{
				std::filesystem::create_directories(first_root);
			}
		}
	}

	// Process messages from worker thread
	void Instinctiv::process_worker_messages()
	{
		while (auto msg = m_state.worker->poll())
		{
			std::visit([&](auto&& m) {
				using T = std::decay_t<decltype(m)>;

				if constexpr (std::is_same_v<T, RegistryRefreshComplete>)
				{
					// Build trees from discovered entries
					m_state.is_refreshing = false;
					PNQ_RELEASE(m_state.m_snapshot_registry);
					m_state.m_snapshot_registry = m.snapshot_registry;
					// ownership is transfered!

					auto& instances = m_state.m_snapshot_registry->m_instances;
					auto& projects = m_state.m_snapshot_registry->m_projects;

					m_state.status_message = std::format("Found {} instance{}, {} project{}",
						instances.size(), instances.size() == 1 ? "" : "s",
						projects.size(), projects.size() == 1 ? "" : "s");

					// Check for empty registry on first refresh - show settings dialog
					if (!m_state.first_refresh_done)
					{
						m_state.first_refresh_done = true;
						if (instances.empty() && projects.empty())
						{
							// Show settings dialog with warning
							m_settingsRoots.clear();
							for (const auto& root : m_state.registry_roots)
								m_settingsRoots.push_back(root);
							m_settingsSelectedRoot = -1;
							m_showSettingsDialog = true;
						}
					}
				}
				else if constexpr (std::is_same_v<T, Progress>)
				{
					m_state.progress_phase = m.phase;
					m_state.progress_detail = m.detail;
					m_state.progress_percent = m.percent;
				}
				else if constexpr (std::is_same_v<T, LogEntry>)
				{
					std::string prefix;
					switch (m.level)
					{
					case LogEntry::Level::Warning: prefix = "[WARN] "; break;
					case LogEntry::Level::Error: prefix = "[ERROR] "; break;
					default: break;
					}
					m_state.progress_log.push_back(prefix + m.message);
				}
				else if constexpr (std::is_same_v<T, OperationComplete>)
				{
					m_state.progress_phase = m.success ? "Complete" : "Failed";
					m_state.progress_percent = m.success ? 100 : -1;
					m_state.progress_log.push_back(m.message);

					// Notify registry of operation completion
					if (m.success && m_state.m_snapshot_registry)
					{
						if (m_state.progress_operation == "Restore")
							m_state.m_snapshot_registry->notify_restore_complete("");
						else if (m_state.progress_operation == "Uninstall")
							m_state.m_snapshot_registry->notify_clean_complete();
						else if (m_state.progress_operation == "Backup" || m_state.progress_operation == "Refresh Snapshot")
						{
							// Refresh registry to pick up new/updated snapshot
							m_state.is_refreshing = true;
							m_state.worker->post(RefreshRegistry{ m_state.registry_roots });
						}
					}
				}
				else if constexpr (std::is_same_v<T, ErrorDecision>)
				{
					// For now, log the error and auto-skip
					// TODO M4.9: Show error decision dialog
					spdlog::warn("Error during operation: {} - {}", m.message, m.context);
					m_state.progress_log.push_back("[ERROR] " + m.message + ": " + m.context);

					// Send SkipAll decision to continue
					m_state.worker->post(DecisionResponse{ insti::IActionCallback::Decision::SkipAll });
				}
				else if constexpr (std::is_same_v<T, FileConflict>)
				{
					// For now, log and auto-continue (overwrite)
					// TODO M4.9: Show file conflict dialog
					spdlog::info("File conflict: {} ({})", m.path, m.action);
					m_state.progress_log.push_back("[CONFLICT] " + m.path + " (" + m.action + ")");

					// Send Continue decision to overwrite
					m_state.worker->post(DecisionResponse{ insti::IActionCallback::Decision::Continue });
				}
				else if constexpr (std::is_same_v<T, VerifyComplete>)
				{
					// Process verification results
					int match_count = 0, mismatch_count = 0, missing_count = 0, extra_count = 0;
					int total_file_match = 0, total_file_mismatch = 0, total_file_missing = 0, total_file_extra = 0;

					for (const auto& result : m.results)
					{
						const char* status_str = "[?]";
						switch (result.status)
						{
						case insti::VerifyResult::Status::Match:
							status_str = "[MATCH]";
							++match_count;
							break;
						case insti::VerifyResult::Status::Mismatch:
							status_str = "[MISMATCH]";
							++mismatch_count;
							break;
						case insti::VerifyResult::Status::Missing:
							status_str = "[MISSING]";
							++missing_count;
							break;
						case insti::VerifyResult::Status::Extra:
							status_str = "[EXTRA]";
							++extra_count;
							break;
						}

						m_state.progress_log.push_back(std::format("{} {}", status_str, result.detail));

						// List individual files (UI always shows details)
						for (const auto& f : result.mismatched_files)
							m_state.progress_log.push_back(std::format("    DIFFER: {}", f));
						for (const auto& f : result.missing_files)
							m_state.progress_log.push_back(std::format("    MISSING: {}", f));
						for (const auto& f : result.extra_files)
							m_state.progress_log.push_back(std::format("    EXTRA: {}", f));

						// Aggregate file counts
						total_file_match += result.file_match_count;
						total_file_mismatch += result.file_mismatch_count;
						total_file_missing += result.file_missing_count;
						total_file_extra += result.file_extra_count;
					}

					// Summary
					m_state.progress_log.push_back("");
					m_state.progress_log.push_back(std::format("Resource summary: {} match, {} mismatch, {} missing, {} extra",
						match_count, mismatch_count, missing_count, extra_count));

					if (total_file_match > 0 || total_file_mismatch > 0 || total_file_missing > 0 || total_file_extra > 0)
					{
						m_state.progress_log.push_back(std::format("File summary: {} match, {} differ, {} missing, {} extra",
							total_file_match, total_file_mismatch, total_file_missing, total_file_extra));
					}

					// Overall judgment
					if (mismatch_count == 0 && missing_count == 0 && extra_count == 0)
						m_state.progress_log.push_back("Status: INSTALLED");
					else if (match_count == 0)
						m_state.progress_log.push_back("Status: NOT INSTALLED");
					else
						m_state.progress_log.push_back("Status: PARTIALLY INSTALLED");

					m_state.progress_phase = "Complete";
					m_state.progress_percent = 100;
				}
				// Other message types handled in future milestones
				}, *msg);
		}
	}

	// Initialize logging
	void Instinctiv::initialize_logging()
	{
		// Use shared logging initialization from the library
		insti::config::initialize_logging();
	}


	// Start backup operation from a blueprint entry
	void Instinctiv::start_backup_from_project(insti::Project* project)
	{
		if (!project)
			return;

		spdlog::info("start_backup_from_project: {}", project->source_path());

		if (m_state.registry_roots.empty())
		{
			m_state.status_message = "No registry roots configured. Use View > Settings to add one.";
			return;
		}

		// Generate filename following the naming pattern: ${project}-${timestamp}
		auto now = std::chrono::system_clock::now();
		auto time_t_now = std::chrono::system_clock::to_time_t(now);
		std::tm tm_now;
		localtime_s(&tm_now, &time_t_now);

		char timestamp[32];
		std::strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &tm_now);

		// Use blueprint name as project
		std::string project_name = project->project_name();

		// Sanitize names (replace spaces with underscores)
		std::replace(project_name.begin(), project_name.end(), ' ', '_');

		// Pre-fill dialog fields
		m_backupProject = project;
		strncpy_s(m_backupDescription, project->project_description().c_str(), sizeof(m_backupDescription) - 1);
		m_backupFilename = std::format("{}-{}.zip", project_name, timestamp);
		m_backupSelectedRoot = 0;  // Default to first root

		// Show the backup options dialog
		m_showBackupDialog = true;
	}

	void Instinctiv::start_clean_from_project(insti::Project* project)
	{
		if (!project)
			return;

		spdlog::info("start_clean_from_project: {}", project->project_name());

		// Collect descriptions of what will be removed
		m_uninstallDescriptions.clear();
		for (const auto* action : project->actions())
		{
			m_uninstallDescriptions.push_back(action->describe_clean());
		}

		// Show confirmation dialog
		m_uninstallTarget = project;
		m_showUninstallConfirm = true;
	}

	void Instinctiv::start_verify_from_project(insti::Project* project)
	{
		if (!project)
			return;

		spdlog::info("start_verify_from_project: {}", project->project_name());

		// Setup progress dialog
		m_state.progress_operation = "Verify";
		m_state.progress_phase = "Starting";
		m_state.progress_detail = "";
		m_state.progress_percent = -1;
		m_state.progress_log.clear();
		m_state.progress_log.push_back("Verifying: " + project->project_name());
		m_state.progress_log.push_back("(Project verification - checking resource existence)");
		m_state.show_progress_dialog = true;

		if (m_state.active_blueprint)
			PNQ_RELEASE(m_state.active_blueprint);
		m_state.active_blueprint = project;
		PNQ_ADDREF(m_state.active_blueprint);

		// Project verification - no archive path
		m_state.worker->post(StartVerify{ m_state.m_snapshot_registry, project });
	}

	void Instinctiv::start_backup_from_instance(insti::Instance* instance)
	{
		if (!instance)
			return;

		spdlog::info("start_backup_from_instance (refresh): {}", instance->m_snapshot_path);

		if (m_state.registry_roots.empty())
		{
			m_state.status_message = "No registry roots configured. Use View > Settings to add one.";
			return;
		}

		// Extract filename from existing snapshot path
		std::filesystem::path snapshot_path{ instance->m_snapshot_path };
		std::string filename = snapshot_path.filename().string();
		std::filesystem::path parent_dir = snapshot_path.parent_path();

		// Find which root this instance belongs to (default to that one)
		int selected_root = 0;
		for (int i = 0; i < static_cast<int>(m_state.registry_roots.size()); ++i)
		{
			std::filesystem::path root_path{ m_state.registry_roots[i] };
			// Check if parent_dir starts with or equals this root
			std::error_code ec;
			auto rel = std::filesystem::relative(parent_dir, root_path, ec);
			if (!ec && !rel.empty() && rel.native()[0] != '.')
			{
				selected_root = i;
				break;
			}
			// Also check exact match
			if (std::filesystem::equivalent(parent_dir, root_path, ec))
			{
				selected_root = i;
				break;
			}
		}

		// Pre-fill dialog fields
		m_backupProject = instance;  // Instance inherits from Project
		strncpy_s(m_backupDescription, instance->m_description.c_str(), sizeof(m_backupDescription) - 1);
		m_backupFilename = filename;
		m_backupSelectedRoot = selected_root;

		// Show the backup options dialog
		m_showBackupDialog = true;
	}

	void Instinctiv::start_clean_from_instance(insti::Instance* instance)
	{
		if (!instance)
			return;

		spdlog::info("start_clean_from_instance: {}", instance->m_snapshot_path);

		// Collect descriptions of what will be removed
		m_uninstallDescriptions.clear();
		for (const auto* action : instance->actions())
		{
			m_uninstallDescriptions.push_back(action->describe_clean());
		}

		// Show confirmation dialog
		m_uninstallTarget = instance;
		m_showUninstallConfirm = true;
	}

	void Instinctiv::start_verify_from_instance(insti::Instance* instance)
	{
		if (!instance)
			return;

		spdlog::info("start_verify_from_instance: {}", instance->m_snapshot_path);

		// Setup progress dialog
		m_state.progress_operation = "Verify";
		m_state.progress_phase = "Starting";
		m_state.progress_detail = "";
		m_state.progress_percent = -1;
		m_state.progress_log.clear();
		m_state.progress_log.push_back("Verifying: " + instance->project_name());
		m_state.progress_log.push_back("(Instance verification - comparing file contents)");
		m_state.progress_log.push_back("Against: " + instance->m_snapshot_path);
		m_state.show_progress_dialog = true;

		if (m_state.active_blueprint)
			PNQ_RELEASE(m_state.active_blueprint);
		m_state.active_blueprint = instance;
		PNQ_ADDREF(m_state.active_blueprint);

		// Instance verification - pass archive path for file-level comparison
		m_state.worker->post(StartVerify{ m_state.m_snapshot_registry, instance, instance->m_snapshot_path });
	}

	// Progress dialog during operations
	void Instinctiv::render_progress_dialog()
	{
		if (!m_state.show_progress_dialog)
			return;

		ConstrainDialogToWindow(ImVec2(500, 350));

		bool open = true;
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

		std::string title = m_state.progress_operation + " Progress";
		if (ImGui::Begin(title.c_str(), &open, flags))
		{
			// Current phase
			ImGui::Text("Phase: %s", m_state.progress_phase.c_str());

			// Current item
			if (!m_state.progress_detail.empty())
			{
				ImGui::TextWrapped("Current: %s", m_state.progress_detail.c_str());
			}

			// Progress bar
			ImGui::Spacing();
			if (m_state.progress_percent >= 0)
			{
				ImGui::ProgressBar(m_state.progress_percent / 100.0f, ImVec2(-1, 0));
			}
			else
			{
				// Indeterminate progress - animate
				static float progress_anim = 0.0f;
				progress_anim += ImGui::GetIO().DeltaTime * 0.5f;
				if (progress_anim > 1.0f) progress_anim = 0.0f;
				ImGui::ProgressBar(progress_anim, ImVec2(-1, 0), "");
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Log area - use InputTextMultiline for selection/copy support
			ImGui::Text("Log:");

			// Build log text from entries
			std::string log_text;
			for (const auto& line : m_state.progress_log)
			{
				if (!log_text.empty())
					log_text += '\n';
				log_text += line;
			}

			// Calculate available height for log area
			float available_height = ImGui::GetContentRegionAvail().y - 30;

			// InputTextMultiline with ReadOnly flag - allows selection and Ctrl+C
			ImGui::InputTextMultiline("##ProgressLog", &log_text[0], log_text.size() + 1,
				ImVec2(-FLT_MIN, available_height),
				ImGuiInputTextFlags_ReadOnly);

			// Cancel button
			if (m_state.worker->is_busy())
			{
				if (ImGui::Button("Cancel", ImVec2(80, 0)))
				{
					m_state.worker->cancel();
					m_state.progress_log.push_back("Cancelling...");
				}
			}
			else
			{
				if (ImGui::Button("Close", ImVec2(80, 0)))
				{
					open = false;
				}
			}
		}
		ImGui::End();

		if (!open)
		{
			m_state.show_progress_dialog = false;
			// Cleanup active blueprint
			if (m_state.active_blueprint)
			{
				m_state.active_blueprint->release(REFCOUNT_DEBUG_ARGS);
				m_state.active_blueprint = nullptr;
			}
		}
	}

	// Font selection dialog
	void Instinctiv::render_font_dialog()
	{
		if (!m_showFontDialog)
			return;

		// Populate font list on first open
		if (m_availableFonts.empty())
		{
			// Save original font for Cancel
			m_originalFontName = config::theSettings.application.fontName.get();

			pnq::regis3::key fontsKey{ "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts" };
			if (fontsKey.open_for_reading())
			{
				for (const auto& value : fontsKey.enum_values())
				{
					std::string valueName{ value.name() };
					// Extract font name (strip " (TrueType)" or " (OpenType)" suffix)
					auto pos = valueName.find(" (");
					if (pos != std::string::npos)
						valueName = valueName.substr(0, pos);
					if (!valueName.empty())
						m_availableFonts.push_back(valueName);
				}
				std::sort(m_availableFonts.begin(), m_availableFonts.end());
			}

			// Find current font in list
			for (size_t i = 0; i < m_availableFonts.size(); ++i)
			{
				if (m_availableFonts[i] == m_originalFontName)
				{
					m_selectedFontIndex = static_cast<int>(i);
					break;
				}
			}
		}

		ConstrainDialogToWindow(ImVec2(400, 500));

		bool open = true;
		bool accepted = false;
		bool cancelled = false;

		if (ImGui::Begin("Select Font", &open, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::Text("Available Fonts:");
			ImGui::Spacing();

			// Listbox with all fonts
			ImVec2 listSize(-FLT_MIN, -ImGui::GetFrameHeightWithSpacing() - 8);
			if (ImGui::BeginListBox("##FontList", listSize))
			{
				for (int i = 0; i < static_cast<int>(m_availableFonts.size()); ++i)
				{
					bool isSelected = (m_selectedFontIndex == i);
					if (ImGui::Selectable(m_availableFonts[i].c_str(), isSelected))
					{
						if (m_selectedFontIndex != i)
						{
							m_selectedFontIndex = i;
							// Apply font immediately for preview
							config::theSettings.application.fontName.set(m_availableFonts[i]);
							float currentSize = config::theSettings.application.fontSizeScaled.get() / 100.0f;
							m_pendingFontSize = currentSize;
						}
					}
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndListBox();
			}

			ImGui::Spacing();

			// OK / Cancel buttons
			if (ImGui::Button("OK", ImVec2(80, 0)))
			{
				accepted = true;
				open = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80, 0)))
			{
				cancelled = true;
				open = false;
			}
		}
		ImGui::End();

		if (!open)
		{
			if (accepted)
			{
				// Save the selection
				config::theSettings.save();
			}
			else if (cancelled)
			{
				// Restore original font
				config::theSettings.application.fontName.set(m_originalFontName);
				float currentSize = config::theSettings.application.fontSizeScaled.get() / 100.0f;
				m_pendingFontSize = currentSize;
			}

			m_showFontDialog = false;
			m_availableFonts.clear();
			m_selectedFontIndex = -1;
			m_originalFontName.clear();
		}
	}

	// Backup options dialog
	void Instinctiv::render_backup_dialog()
	{
		if (!m_showBackupDialog || !m_backupProject)
			return;

		ConstrainDialogToWindow(ImVec2(500, 220));

		bool open = true;
		bool do_backup = false;

		if (ImGui::Begin("Backup Options", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
		{
			// Project info (read-only)
			ImGui::Text("Project:");
			ImGui::SameLine(100);
			ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s v%s",
				m_backupProject->project_name().c_str(),
				m_backupProject->project_version().c_str());

			ImGui::Spacing();

			// Description (editable)
			ImGui::Text("Description:");
			ImGui::SameLine(100);
			ImGui::SetNextItemWidth(-1);
			ImGui::InputText("##Description", m_backupDescription, sizeof(m_backupDescription));

			ImGui::Spacing();

			// Save to (root selector)
			ImGui::Text("Save to:");
			ImGui::SameLine(100);
			if (m_state.registry_roots.empty())
			{
				ImGui::TextDisabled("(No registry roots configured)");
			}
			else
			{
				ImGui::BeginGroup();
				for (int i = 0; i < static_cast<int>(m_state.registry_roots.size()); ++i)
				{
					if (ImGui::RadioButton(m_state.registry_roots[i].c_str(), m_backupSelectedRoot == i))
					{
						m_backupSelectedRoot = i;
					}
				}
				ImGui::EndGroup();
			}

			ImGui::Spacing();

			// Filename (read-only, auto-generated)
			ImGui::Text("Filename:");
			ImGui::SameLine(100);
			ImGui::TextDisabled("%s", m_backupFilename.c_str());

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Buttons
			bool can_backup = !m_state.registry_roots.empty() &&
			                  m_backupSelectedRoot >= 0 &&
			                  m_backupSelectedRoot < static_cast<int>(m_state.registry_roots.size());
			ImGui::BeginDisabled(!can_backup);
			if (ImGui::Button("Backup", ImVec2(80, 0)))
			{
				do_backup = true;
				open = false;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80, 0)))
			{
				open = false;
			}
		}
		ImGui::End();

		if (!open)
		{
			if (do_backup && m_backupProject && m_backupSelectedRoot >= 0)
			{
				// Build full output path - prefer "instances" subdirectory if it exists
				std::filesystem::path root_path{ m_state.registry_roots[m_backupSelectedRoot] };
				std::filesystem::path instances_dir = root_path / "instances";
				if (std::filesystem::is_directory(instances_dir))
					root_path = instances_dir;
				std::filesystem::path output_path = root_path / m_backupFilename;

				// Store blueprint for the operation
				if (m_state.active_blueprint)
					PNQ_RELEASE(m_state.active_blueprint);
				m_state.active_blueprint = m_backupProject;
				PNQ_ADDREF(m_state.active_blueprint);

				// Setup progress dialog
				m_state.show_progress_dialog = true;
				m_state.progress_operation = "Backup";
				m_state.progress_phase = "Starting...";
				m_state.progress_detail.clear();
				m_state.progress_percent = -1;
				m_state.progress_log.clear();
				m_state.progress_log.push_back("Starting backup: " + m_backupProject->project_name());
				m_state.progress_log.push_back("Output: " + output_path.string());

				// Start backup on worker thread with user-specified options
				m_state.worker->post(StartBackup{
					m_state.m_snapshot_registry,
					m_backupProject,
					output_path.string(),
					m_backupDescription
				});
			}

			m_showBackupDialog = false;
			m_backupProject = nullptr;
		}
	}

	// Settings dialog
	void Instinctiv::render_settings_dialog()
	{
		if (!m_showSettingsDialog)
			return;

		ConstrainDialogToWindow(ImVec2(500, 350));

		bool open = true;
		bool save_settings = false;

		if (ImGui::Begin("Settings", &open, ImGuiWindowFlags_NoCollapse))
		{
			// Show warning if no blueprints/instances found
			bool registry_empty = !m_state.m_snapshot_registry ||
				(m_state.m_snapshot_registry->m_instances.empty() &&
				 m_state.m_snapshot_registry->m_projects.empty());
			if (registry_empty)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
				ImGui::TextWrapped("No project blueprints or instance snapshots found. Add a folder containing blueprint files (.xml) or snapshots (.zip).");
				ImGui::PopStyleColor();
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
			}

			ImGui::Text("Registry Roots");
			ImGui::TextDisabled("Folders where blueprints and snapshots are stored");
			ImGui::Spacing();

			// List of roots
			ImVec2 listSize(-1, -ImGui::GetFrameHeightWithSpacing() * 2 - 8);
			if (ImGui::BeginListBox("##RootsList", listSize))
			{
				for (int i = 0; i < static_cast<int>(m_settingsRoots.size()); ++i)
				{
					bool isSelected = (m_settingsSelectedRoot == i);
					if (ImGui::Selectable(m_settingsRoots[i].c_str(), isSelected))
					{
						m_settingsSelectedRoot = i;
					}
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndListBox();
			}

			// Add / Remove buttons
			if (ImGui::Button("Add Folder...", ImVec2(100, 0)))
			{
				std::string folder = browse_for_folder(m_hWnd, "Select Registry Root Folder");
				if (!folder.empty())
				{
					// Check for duplicates
					bool exists = false;
					for (const auto& root : m_settingsRoots)
					{
						if (pnq::string::equals_nocase(root, folder))
						{
							exists = true;
							break;
						}
					}
					if (!exists)
					{
						m_settingsRoots.push_back(folder);
						m_settingsSelectedRoot = static_cast<int>(m_settingsRoots.size()) - 1;
					}
				}
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(m_settingsSelectedRoot < 0 || m_settingsSelectedRoot >= static_cast<int>(m_settingsRoots.size()));
			if (ImGui::Button("Remove", ImVec2(80, 0)))
			{
				m_settingsRoots.erase(m_settingsRoots.begin() + m_settingsSelectedRoot);
				if (m_settingsSelectedRoot >= static_cast<int>(m_settingsRoots.size()))
					m_settingsSelectedRoot = static_cast<int>(m_settingsRoots.size()) - 1;
			}
			ImGui::EndDisabled();

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// OK / Cancel
			if (ImGui::Button("OK", ImVec2(80, 0)))
			{
				save_settings = true;
				open = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80, 0)))
			{
				open = false;
			}
		}
		ImGui::End();

		if (!open)
		{
			if (save_settings)
			{
				// Update settings
				std::string roots_str;
				for (size_t i = 0; i < m_settingsRoots.size(); ++i)
				{
					if (i > 0)
						roots_str += ";";
					roots_str += m_settingsRoots[i];
				}
				config::theSettings.registry.roots.set(roots_str);
				config::theSettings.save();

				// Update runtime state
				m_state.registry_roots.clear();
				for (const auto& root : m_settingsRoots)
					m_state.registry_roots.push_back(root);

				// Trigger refresh
				m_state.is_refreshing = true;
				m_state.status_message = "Scanning for snapshots...";
				m_state.worker->post(RefreshRegistry{ m_state.registry_roots });
			}

			m_showSettingsDialog = false;
			m_settingsRoots.clear();
			m_settingsSelectedRoot = -1;
		}
	}

	// Blueprint editor dialog
	void Instinctiv::render_blueprint_editor()
	{
		if (!m_showBlueprintEditor)
			return;

		ConstrainDialogToWindow(ImVec2(700, 550));

		bool open = true;
		bool do_save = false;
		bool do_delete = false;

		if (ImGui::Begin("Blueprint Editor", &open, ImGuiWindowFlags_NoCollapse))
		{
			static pnq::RefCountedVector<insti::Project*> empty_projects;
			auto& projects = m_state.m_snapshot_registry ? m_state.m_snapshot_registry->m_projects : empty_projects;
			bool is_editing = (m_blueprintEditorMode == BlueprintEditorMode::Add || m_blueprintEditorMode == BlueprintEditorMode::Edit);

			// Project selector row
			ImGui::Text("Project:");
			ImGui::SameLine(80);
			ImGui::SetNextItemWidth(300);
			ImGui::BeginDisabled(is_editing);
			if (ImGui::BeginCombo("##ProjectCombo",
				m_blueprintEditorSelectedProject >= 0 && m_blueprintEditorSelectedProject < static_cast<int>(projects.size())
					? projects[m_blueprintEditorSelectedProject]->project_name().c_str()
					: "(Select a project)"))
			{
				for (int i = 0; i < static_cast<int>(projects.size()); ++i)
				{
					bool selected = (m_blueprintEditorSelectedProject == i);
					if (ImGui::Selectable(projects[i]->project_name().c_str(), selected))
					{
						m_blueprintEditorSelectedProject = i;
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			ImGui::BeginDisabled(is_editing);
			if (ImGui::Button("Add"))
			{
				// Create new blueprint based on selected (or blank template)
				m_blueprintEditorMode = BlueprintEditorMode::Add;
				m_blueprintEditorSourcePath.clear();
				m_blueprintEditorSyntaxResult.clear();

				// Generate unique name
				std::string base_name = "NewProject";
				int counter = 1;
				bool unique = false;
				while (!unique)
				{
					std::string candidate = base_name + std::to_string(counter);
					unique = true;
					for (const auto& p : projects)
					{
						if (p->project_name() == candidate)
						{
							unique = false;
							counter++;
							break;
						}
					}
					if (unique)
						strncpy_s(m_blueprintEditorName, candidate.c_str(), sizeof(m_blueprintEditorName) - 1);
				}

				// Copy XML from selected project or use template
				if (m_blueprintEditorSelectedProject >= 0 && m_blueprintEditorSelectedProject < static_cast<int>(projects.size()))
				{
					std::ifstream file(projects[m_blueprintEditorSelectedProject]->source_path());
					if (file)
					{
						std::ostringstream ss;
						ss << file.rdbuf();
						m_blueprintEditorXml = ss.str();
					}
				}
				else
				{
					m_blueprintEditorXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<project name="NewProject" version="1.0">
    <description>Project description</description>
    <resources>
        <!-- Add resources here -->
    </resources>
</project>
)";
				}
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			bool can_edit = m_blueprintEditorSelectedProject >= 0 && m_blueprintEditorSelectedProject < static_cast<int>(projects.size());
			ImGui::BeginDisabled(is_editing || !can_edit);
			if (ImGui::Button("Edit"))
			{
				m_blueprintEditorMode = BlueprintEditorMode::Edit;
				auto* project = projects[m_blueprintEditorSelectedProject];
				m_blueprintEditorSourcePath = project->source_path();
				strncpy_s(m_blueprintEditorName, project->project_name().c_str(), sizeof(m_blueprintEditorName) - 1);
				m_blueprintEditorSyntaxResult.clear();

				// Load XML content
				std::ifstream file(project->source_path());
				if (file)
				{
					std::ostringstream ss;
					ss << file.rdbuf();
					m_blueprintEditorXml = ss.str();
				}
				else
				{
					m_blueprintEditorXml = "<!-- Failed to load file -->";
				}
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			ImGui::BeginDisabled(is_editing || !can_edit);
			if (ImGui::Button("Remove"))
			{
				m_blueprintEditorMode = BlueprintEditorMode::Remove;
				auto* project = projects[m_blueprintEditorSelectedProject];
				m_blueprintEditorSourcePath = project->source_path();
				strncpy_s(m_blueprintEditorName, project->project_name().c_str(), sizeof(m_blueprintEditorName) - 1);
			}
			ImGui::EndDisabled();

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Remove confirmation
			if (m_blueprintEditorMode == BlueprintEditorMode::Remove)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
				ImGui::TextWrapped("Are you sure you want to delete '%s'?", m_blueprintEditorName);
				ImGui::TextWrapped("File: %s", m_blueprintEditorSourcePath.c_str());
				ImGui::PopStyleColor();

				ImGui::Spacing();
				if (ImGui::Button("Yes, Delete", ImVec2(100, 0)))
				{
					do_delete = true;
					open = false;
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(80, 0)))
				{
					m_blueprintEditorMode = BlueprintEditorMode::None;
				}
			}
			// Edit/Add mode
			else if (is_editing)
			{
				// Name field
				ImGui::Text("Name:");
				ImGui::SameLine(80);
				ImGui::SetNextItemWidth(-1);
				ImGui::InputText("##Name", m_blueprintEditorName, sizeof(m_blueprintEditorName));

				ImGui::Spacing();

				// XML content (multiline)
				ImGui::Text("XML:");
				float available_height = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 3 - 8;
				ImGui::InputTextMultiline("##XmlContent", &m_blueprintEditorXml,
					ImVec2(-1, available_height), ImGuiInputTextFlags_AllowTabInput);

				// Syntax check button and result
				if (ImGui::Button("Syntax Check", ImVec2(100, 0)))
				{
					// Try to parse the XML
					auto* blueprint = insti::Project::load_from_string(m_blueprintEditorXml, "");
					if (blueprint)
					{
						m_blueprintEditorSyntaxResult = "OK: Valid blueprint for '" + blueprint->project_name() + "' v" + blueprint->project_version();
						PNQ_RELEASE(blueprint);
					}
					else
					{
						m_blueprintEditorSyntaxResult = "Error: Failed to parse XML";
					}
				}

				if (!m_blueprintEditorSyntaxResult.empty())
				{
					ImGui::SameLine();
					bool is_ok = m_blueprintEditorSyntaxResult.starts_with("OK:");
					if (is_ok)
						ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", m_blueprintEditorSyntaxResult.c_str());
					else
						ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_blueprintEditorSyntaxResult.c_str());
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				// OK / Cancel
				if (ImGui::Button("OK", ImVec2(80, 0)))
				{
					do_save = true;
					open = false;
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(80, 0)))
				{
					m_blueprintEditorMode = BlueprintEditorMode::None;
					m_blueprintEditorXml.clear();
					m_blueprintEditorSyntaxResult.clear();
				}
			}
			else
			{
				// No mode - show instructions
				ImGui::TextDisabled("Select a project and click Add, Edit, or Remove.");
			}
		}
		ImGui::End();

		if (!open)
		{
			if (do_save)
			{
				// Determine output path
				std::string output_path;
				if (m_blueprintEditorMode == BlueprintEditorMode::Edit)
				{
					output_path = m_blueprintEditorSourcePath;
				}
				else if (m_blueprintEditorMode == BlueprintEditorMode::Add)
				{
					// Save to first registry root's "projects" subfolder if it exists
					if (!m_state.registry_roots.empty())
					{
						std::filesystem::path root{ m_state.registry_roots[0] };
						std::filesystem::path projects_dir = root / "projects";
						if (std::filesystem::is_directory(projects_dir))
							root = projects_dir;

						std::string safe_name = m_blueprintEditorName;
						std::replace(safe_name.begin(), safe_name.end(), ' ', '_');
						output_path = (root / (safe_name + ".xml")).string();
					}
				}

				if (!output_path.empty())
				{
					std::ofstream file(output_path);
					if (file)
					{
						file << m_blueprintEditorXml;
						file.close();
						m_state.status_message = "Saved: " + output_path;

						// Trigger refresh
						m_state.is_refreshing = true;
						m_state.worker->post(RefreshRegistry{ m_state.registry_roots });
					}
					else
					{
						m_state.status_message = "Failed to save: " + output_path;
					}
				}
			}
			else if (do_delete)
			{
				if (!m_blueprintEditorSourcePath.empty())
				{
					std::error_code ec;
					if (std::filesystem::remove(m_blueprintEditorSourcePath, ec))
					{
						m_state.status_message = "Deleted: " + m_blueprintEditorSourcePath;

						// Trigger refresh
						m_state.is_refreshing = true;
						m_state.worker->post(RefreshRegistry{ m_state.registry_roots });
					}
					else
					{
						m_state.status_message = "Failed to delete: " + m_blueprintEditorSourcePath;
					}
				}
			}

			// Reset state
			m_showBlueprintEditor = false;
			m_blueprintEditorMode = BlueprintEditorMode::None;
			m_blueprintEditorSelectedProject = -1;
			m_blueprintEditorName[0] = '\0';
			m_blueprintEditorXml.clear();
			m_blueprintEditorSourcePath.clear();
			m_blueprintEditorSyntaxResult.clear();
		}
	}

	// Uninstall confirmation dialog
	void Instinctiv::render_uninstall_confirm_dialog()
	{
		if (!m_showUninstallConfirm || !m_uninstallTarget)
			return;

		ConstrainDialogToWindow(ImVec2(550, 400));

		bool open = true;
		bool do_uninstall = false;

		if (ImGui::Begin("Confirm Uninstall", &open, ImGuiWindowFlags_NoCollapse))
		{
			// Warning header
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
			ImGui::Text("Are you sure you want to uninstall '%s'?", m_uninstallTarget->project_name().c_str());
			ImGui::PopStyleColor();

			ImGui::Spacing();
			ImGui::Text("The following items will be removed:");
			ImGui::Spacing();

			// Calculate available height for the list (leave room for dry-run text, separator, and buttons)
			float reservedHeight = ImGui::GetFrameHeightWithSpacing() * 2 + 8;  // buttons + small padding
			if (m_state.dry_run)
				reservedHeight += ImGui::GetTextLineHeightWithSpacing();  // dry-run text

			float listHeight = ImGui::GetContentRegionAvail().y - reservedHeight;
			if (listHeight < 100.0f)
				listHeight = 100.0f;

			ImGui::BeginChild("UninstallItems", ImVec2(-1, listHeight), true);  // true = border
			for (const auto& desc : m_uninstallDescriptions)
			{
				ImGui::BulletText("%s", desc.c_str());
			}
			ImGui::EndChild();

			// Dry run hint
			if (m_state.dry_run)
			{
				ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "(Dry-run mode is enabled)");
			}

			ImGui::Separator();

			// Buttons
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
			if (ImGui::Button("Yes, Uninstall", ImVec2(120, 0)))
			{
				do_uninstall = true;
				open = false;
			}
			ImGui::PopStyleColor(2);

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80, 0)))
			{
				open = false;
			}
		}
		ImGui::End();

		if (!open)
		{
			if (do_uninstall && m_uninstallTarget)
			{
				// Proceed with actual uninstall
				spdlog::info("User confirmed uninstall: {}", m_uninstallTarget->project_name());

				// Setup progress dialog
				m_state.progress_operation = m_state.dry_run ? "Dry-run Uninstall" : "Uninstall";
				m_state.progress_phase = "Starting";
				m_state.progress_detail = "";
				m_state.progress_percent = -1;
				m_state.progress_log.clear();
				m_state.progress_log.push_back("Uninstalling: " + m_uninstallTarget->project_name());
				m_state.show_progress_dialog = true;

				if (m_state.active_blueprint)
					PNQ_RELEASE(m_state.active_blueprint);
				m_state.active_blueprint = m_uninstallTarget;
				PNQ_ADDREF(m_state.active_blueprint);

				m_state.worker->post(StartClean{ m_state.m_snapshot_registry, m_uninstallTarget, m_state.dry_run });
			}

			// Reset state
			m_showUninstallConfirm = false;
			m_uninstallTarget = nullptr;
			m_uninstallDescriptions.clear();
		}
	}

	// Browse for folder dialog
	std::string Instinctiv::browse_for_folder(HWND hwnd, const char* title)
	{
		std::string result;

		// Use modern IFileDialog
		IFileDialog* pfd = nullptr;
		HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
		if (SUCCEEDED(hr))
		{
			DWORD options;
			pfd->GetOptions(&options);
			pfd->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

			if (title)
			{
				int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
				std::wstring wtitle(len, 0);
				MultiByteToWideChar(CP_UTF8, 0, title, -1, &wtitle[0], len);
				pfd->SetTitle(wtitle.c_str());
			}

			hr = pfd->Show(hwnd);
			if (SUCCEEDED(hr))
			{
				IShellItem* psi = nullptr;
				hr = pfd->GetResult(&psi);
				if (SUCCEEDED(hr))
				{
					PWSTR path = nullptr;
					hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
					if (SUCCEEDED(hr) && path)
					{
						int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
						result.resize(size - 1);
						WideCharToMultiByte(CP_UTF8, 0, path, -1, &result[0], size, nullptr, nullptr);
						CoTaskMemFree(path);
					}
					psi->Release();
				}
			}
			pfd->Release();
		}

		return result;
	}


	// Show save file dialog, returns selected path or empty string if cancelled
	std::string Instinctiv::show_save_dialog(const char* filter, const char* default_name, const char* default_ext)
	{
		char filename[MAX_PATH] = "";
		if (default_name)
			strncpy_s(filename, default_name, _TRUNCATE);

		OPENFILENAMEA ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = m_hWnd;
		ofn.lpstrFilter = filter;
		ofn.lpstrFile = filename;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrDefExt = default_ext;
		ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

		if (GetSaveFileNameA(&ofn))
			return filename;
		return "";
	}

}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	instinctiv::Instinctiv app;
	if (app.initialize(hInstance))
	{
		app.run();
	}
	return 0;
}

// DirectX 11 helper functions

// Helper: format file size
static std::string FormatFileSize(uint64_t bytes)
{
	if (bytes < 1024)
		return std::format("{} B", bytes);
	else if (bytes < 1024 * 1024)
		return std::format("{:.1f} KB", bytes / 1024.0);
	else if (bytes < 1024 * 1024 * 1024)
		return std::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
	else
		return std::format("{:.2f} GB", bytes / (1024.0 * 1024.0 * 1024.0));
}

// Helper: format timestamp
static std::string FormatTimestamp(std::chrono::system_clock::time_point tp)
{
	auto time_t = std::chrono::system_clock::to_time_t(tp);
	std::tm tm;
	localtime_s(&tm, &time_t);
	char buf[64];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
	return buf;
}
