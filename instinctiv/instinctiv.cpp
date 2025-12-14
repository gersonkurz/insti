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
		config::theSettings.save(*m_pConfigBackend);
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
		ID3D11Texture2D* pBackBuffer;
		m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
		m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
		pBackBuffer->Release();
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

		// UI rendering
		render_menu_bar();

		// Handle Ctrl+Mousewheel for font size changes
		ImGuiIO& io = ImGui::GetIO();
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
				config::theSettings.save(*m_pConfigBackend);
				// Defer font rebuild until next frame
				m_pendingFontSize = newSize;
			}
		}

		// Main window content
		ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
		ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - ImGui::GetFrameHeight()));
		ImGui::Begin("##MainContent", nullptr,
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoBringToFrontOnFocus);

		render_toolbar();
		ImGui::Separator();
		render_snapshot_table();

		ImGui::End();

		// Modal dialogs
		render_progress_dialog();
		render_first_run_dialog();
		render_font_dialog();

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
			else if (m_state.show_first_run_dialog)
				m_state.show_first_run_dialog = false;
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
		if (!ImGui::BeginMainMenuBar())
			return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Exit", "Alt+F4"))
				m_done = true;
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
				config::theSettings.save(*m_pConfigBackend);
				ImGui::StyleColorsDark();
				apply_style();
			}

			if (ImGui::MenuItem("Light Theme", nullptr, currentTheme == "Light"))
			{
				appSettings.theme.set("Light");
				config::theSettings.save(*m_pConfigBackend);
				apply_light_theme();
				apply_style();
			}

			if (ImGui::MenuItem("Tomorrow Night Blue", nullptr, currentTheme == "Tomorrow Night Blue"))
			{
				appSettings.theme.set("Tomorrow Night Blue");
				config::theSettings.save(*m_pConfigBackend);
				apply_tomorrow_night_blue();
				apply_style();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Select Font..."))
			{
				m_showFontDialog = true;
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

		ImGui::EndMainMenuBar();
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
				config::theSettings.save(*m_pConfigBackend);
			}
		}

		// Project selector combobox
		ImGui::SetNextItemWidth(200.0f);
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
						config::theSettings.save(*m_pConfigBackend);
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
		bool has_instance = (m_state.selected_instance != nullptr);
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

		// Restore button - requires an instance selected in the table
		ImGui::BeginDisabled(!has_instance || is_busy);
		if (ImGui::Button("Restore"))
		{
			start_restore_from_instance(m_state.selected_instance);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !has_instance)
			ImGui::SetTooltip("Select a snapshot from the table first");

		ImGui::SameLine();

		// Clean button - requires a project selected
		ImGui::BeginDisabled(!has_project || is_busy);
		if (ImGui::Button("Clean"))
		{
			m_state.progress_operation = m_state.dry_run ? "Dry-run Clean" : "Clean";
			m_state.progress_phase = "Starting";
			m_state.progress_detail = "";
			m_state.progress_percent = -1;
			m_state.progress_log.clear();
			m_state.show_progress_dialog = true;

			if (m_state.active_blueprint)
				PNQ_RELEASE(m_state.active_blueprint);
			m_state.active_blueprint = m_current_project;
			PNQ_ADDREF(m_state.active_blueprint);

			m_state.worker->post(StartClean{ m_state.m_snapshot_registry, m_current_project, m_state.dry_run });
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !has_project)
			ImGui::SetTooltip("Select a project first");

		// Standalone hook buttons from current project
		if (has_project)
		{
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
		ImGui::BeginChild("SnapshotList", ImVec2(table_width, 0), true);

		if (m_filtered_instances.empty())
		{
			ImGui::TextDisabled("No snapshots found");
		}
		else
		{
			ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("SnapshotTable", 7, table_flags))
			{
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("Install Directory", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("Machine", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthFixed, 1.0f);
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
					ImGuiSelectableFlags sel_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
					if (ImGui::Selectable(entry->project_name().c_str(), is_selected, sel_flags))
						m_state.selected_instance = entry;

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

	void Instinctiv::render_first_run_dialog()
	{
		if (!m_state.show_first_run_dialog)
			return;

		ImGui::SetNextWindowSize(ImVec2(450, 250), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

		if (ImGui::Begin("Welcome to insti", nullptr, flags))
		{
			ImGui::TextWrapped("No blueprints or snapshots were found in the configured registry folders.");
			ImGui::Spacing();
			ImGui::TextWrapped("To get started, add a folder containing blueprint files (.xml) or snapshots (.zip).");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Show current registry roots
			ImGui::Text("Current registry folders:");
			if (m_state.registry_roots.empty())
			{
				ImGui::TextDisabled("  (none configured)");
			}
			else
			{
				for (const auto& root : m_state.registry_roots)
				{
					ImGui::BulletText("%s", root.c_str());
				}
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Add folder button
			if (ImGui::Button("Add Folder...", ImVec2(120, 0)))
			{
				std::string folder = browse_for_folder(m_hWnd, "Select Registry Folder");
				if (!folder.empty())
				{
					// Add to registry roots
					m_state.registry_roots.push_back({ folder, true });

					// Update settings
					auto& registrySettings = config::theSettings.registry;
					std::string roots_str = registrySettings.roots.get();
					if (!roots_str.empty())
						roots_str += ",";
					roots_str += folder;
					registrySettings.roots.set(roots_str);

					// Trigger refresh
					m_state.is_refreshing = true;
					m_state.status_message = "Scanning for snapshots...";

					// on first thought, this is no good, because it'll overwrite an existing SnapshotRegistry.
					// on second thought, it IS good, because it solves precisely the problem that we shouldn't modify
					// the vectors of an existing object while it is being processed in a background thread
					m_state.worker->post(RefreshRegistry{ m_state.registry_roots });

					// Close dialog (will reopen if still empty after refresh)
					m_state.show_first_run_dialog = false;
					m_state.first_refresh_done = false;  // Allow re-check after refresh
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Continue Anyway", ImVec2(120, 0)))
			{
				m_state.show_first_run_dialog = false;
			}
		}
		ImGui::End();
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

		m_hWnd = CreateWindowExW(
			0,
			wc.lpszClassName,
			pnq::win32::wstr_param{ strWindowTitle },
			WS_OVERLAPPEDWINDOW,
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
			config::theSettings.load(*m_pConfigBackend);
		}

		// Ensure default registry root exists
		auto& registrySettings = config::theSettings.registry;
		const auto roots = registrySettings.roots.get();
		if (!roots.empty())
		{
			// Parse and create first root if needed
			std::istringstream iss{ roots };
			std::string first_root;
			std::getline(iss, first_root, ',');
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

					// Check for empty registry on first refresh
					if (!m_state.first_refresh_done)
					{
						m_state.first_refresh_done = true;
						if (instances.empty() && projects.empty())
						{
							m_state.show_first_run_dialog = true;
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


					// Notify new registry of operation completion (invalidates installation cache)
					if (m.success && m_state.m_snapshot_registry)
					{
						if (m_state.progress_operation == "Restore")
							m_state.m_snapshot_registry->notify_restore_complete("");
						else if (m_state.progress_operation == "Clean")
							m_state.m_snapshot_registry->notify_clean_complete();
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
				// Other message types handled in future milestones
				}, *msg);
		}
	}

	// Initialize logging
	void Instinctiv::initialize_logging()
	{
		auto& loggingSettings = config::theSettings.logging;

		// Determine log file path
		auto logFilePath = loggingSettings.logFilePath.get();
		if (logFilePath.empty())
		{
			logFilePath = (m_appDataPath / "insti.log").string();
			loggingSettings.logFilePath.set(logFilePath);
		}

		// Setup spdlog with file sink
		try
		{
			auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, true);
			auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

			std::vector<spdlog::sink_ptr> sinks{ console_sink, file_sink };
			auto logger = std::make_shared<spdlog::logger>("insti", sinks.begin(), sinks.end());

			// Set log level from config
			const auto logLevel = loggingSettings.logLevel.get();
			logger->set_level(spdlog::level::from_str(logLevel));

			// Flush on every log message (important for debugging crashes/hangs)
			logger->flush_on(spdlog::level::trace);

			spdlog::set_default_logger(logger);
			spdlog::info("Logging initialized - file: {}, level: {}", logFilePath, logLevel);
		}
		catch (const spdlog::spdlog_ex& ex)
		{
			// Fall back to console only
			spdlog::error("Failed to create file logger: {}", ex.what());
		}
	}


	// Start backup operation from a blueprint entry
	void Instinctiv::start_backup_from_project(insti::Project* blueprint)
	{
		if (!blueprint)
			return;

		spdlog::info("start_backup_from_project: {}", blueprint->source_path());

		spdlog::info("Blueprint loaded: {} v{}", blueprint->project_name(), blueprint->project_version());

		// TBD: we should really show a dialog here instead of doing auto-start

		// Get output directory from settings (first registry root)
		auto& registrySettings = config::theSettings.registry;
		std::string roots_str = registrySettings.roots.get();
		std::string output_dir;

		// Use defaultOutputDir if set, otherwise first registry root
		std::string default_output = registrySettings.defaultOutputDir.get();
		if (!default_output.empty())
		{
			output_dir = default_output;
		}
		else
		{
			// Parse first root
			std::istringstream iss(roots_str);
			std::getline(iss, output_dir, ',');
			// Trim whitespace
			size_t start = output_dir.find_first_not_of(" \t");
			size_t end = output_dir.find_last_not_of(" \t");
			if (start != std::string::npos && end != std::string::npos)
				output_dir = output_dir.substr(start, end - start + 1);
		}

		if (output_dir.empty())
		{
			spdlog::error("No registry root configured for output");
			blueprint->release(REFCOUNT_DEBUG_ARGS);
			m_state.status_message = "No output directory configured";
			return;
		}

		// Generate filename following the naming pattern: ${project}-${timestamp}
		auto now = std::chrono::system_clock::now();
		auto time_t_now = std::chrono::system_clock::to_time_t(now);
		std::tm tm_now;
		localtime_s(&tm_now, &time_t_now);

		char timestamp[32];
		std::strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &tm_now);

		// Use blueprint name as project, "default" as variant, blueprint version as version
		std::string project = blueprint->project_name();

		// Sanitize names (replace spaces with underscores)
		std::replace(project.begin(), project.end(), ' ', '_');

		std::string filename = std::format("{}-{}.zip", project, timestamp);
		std::filesystem::path output_path = std::filesystem::path(output_dir) / filename;

		spdlog::info("Output path: {}", output_path.string());

		// Store blueprint for the operation (will be released when complete)
		if (m_state.active_blueprint)
			PNQ_RELEASE(m_state.active_blueprint);
		m_state.active_blueprint = blueprint;
		PNQ_ADDREF(m_state.active_blueprint);

		// Setup progress dialog
		m_state.show_progress_dialog = true;
		m_state.progress_operation = "Backup";
		m_state.progress_phase = "Starting...";
		m_state.progress_detail.clear();
		m_state.progress_percent = -1;
		m_state.progress_log.clear();
		m_state.progress_log.push_back("Starting backup: " + blueprint->project_name());
		m_state.progress_log.push_back("Output: " + output_path.string());

		// Start backup on worker thread
		m_state.worker->post(StartBackup{ m_state.m_snapshot_registry, blueprint, output_path.string() });
	}

	// Progress dialog during operations
	void Instinctiv::render_progress_dialog()
	{
		if (!m_state.show_progress_dialog)
			return;

		ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

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

			// Log area
			ImGui::Text("Log:");
			ImGui::BeginChild("ProgressLog", ImVec2(0, -30), true);
			for (const auto& line : m_state.progress_log)
			{
				ImGui::TextWrapped("%s", line.c_str());
			}
			// Auto-scroll to bottom
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
			ImGui::EndChild();

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

		ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

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
				config::theSettings.save(*m_pConfigBackend);
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
