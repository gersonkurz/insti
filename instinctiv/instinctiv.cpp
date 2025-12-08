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

		// Start the Dear ImGui frame
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// Keyboard shortcuts
		if (!ImGui::GetIO().WantTextInput)
		{
			// Ctrl+R - Refresh
			if (ImGui::IsKeyPressed(ImGuiKey_R) && ImGui::GetIO().KeyCtrl && !m_state.is_refreshing)
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

			// F5 - Refresh (alternative)
			if (ImGui::IsKeyPressed(ImGuiKey_F5) && !m_state.is_refreshing)
			{
				m_state.is_refreshing = true;
				m_state.status_message = "Scanning for snapshots...";
				m_state.worker->post(RefreshRegistry{ m_state.registry_roots });
			}
		}

		// Process dropped file
		if (!g_droppedFile.empty())
		{
			std::filesystem::path dropped{ g_droppedFile };
			std::string ext = dropped.extension().string();

			if (pnq::string::equals_nocase(ext, ".zip"))
			{
				/*
				// Find matching snapshot entry and select it
				for (auto* entry : m_state.owned_snapshots)
				{
					if (entry->path == g_droppedFile)
					{
						m_state.selected_snapshot = entry;
						m_state.status_message = "Selected: " + dropped.filename().string();
						break;
					}
				}
				// If not found in registry, show message
				if (m_state.selected_snapshot == nullptr || m_state.selected_snapshot->path != g_droppedFile)
				{
					m_state.status_message = "Snapshot not in registry: " + dropped.filename().string();
				}
				*/
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

		// Menu bar
		if (ImGui::BeginMainMenuBar())
		{
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
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Tools"))
			{
				ImGui::MenuItem("Settings...");
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Help"))
			{
				ImGui::MenuItem("About...");
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		// Process worker thread messages
		process_worker_messages();

		// Main window content
		ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
		ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - ImGui::GetFrameHeight()));
		ImGui::Begin("##MainContent", nullptr,
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoBringToFrontOnFocus);

		ImGui::Text("insti %s", insti::version());
		ImGui::Separator();

		// Blueprint selector combobox
		ImGui::SetNextItemWidth(200.0f);

		pnq::RefCountedVector<insti::Instance*> no_instances;
		pnq::RefCountedVector<insti::Project*> no_projects;
		pnq::RefCountedVector<insti::Instance*>* instances{ &no_instances };
		pnq::RefCountedVector<insti::Project*>* projects{ &no_projects };

		// helper: index of current selection in combobox. it's a bit tricky, I know
		int selected_project_index = -1;
		std::string name_of_selected_project{ "" };

		if (m_state.m_snapshot_registry)
		{
			projects = &(m_state.m_snapshot_registry->m_projects);
			instances = &(m_state.m_snapshot_registry->m_instances);

			if (projects->size() == 1)
			{
				selected_project_index = 0;

				// must select the one-and-only if not selected already
				const auto lastBlueprint = config::theSettings.application.lastBlueprint.get();
				name_of_selected_project = projects->at(0)->project_name();
				if (!pnq::string::equals(lastBlueprint, name_of_selected_project))
				{
					config::theSettings.application.lastBlueprint.set(name_of_selected_project);
					config::theSettings.save(*m_pConfigBackend);
				}
			}
			else if (projects->size() > 1)
			{
				// check if selection still possible
				const auto lastBlueprint = config::theSettings.application.lastBlueprint.get();
				bool found = false;
				int current_index = 0;
				for (const auto& project : *projects)
				{
					const auto nameOfThisProject = project->project_name();
					if (pnq::string::equals(lastBlueprint, nameOfThisProject))
					{
						selected_project_index = current_index;
						name_of_selected_project = nameOfThisProject;
						found = true;
						break;
					}
					++current_index;
				}
				if (!found)
				{
					name_of_selected_project = projects->at(0)->project_name();
					assert(!pnq::string::equals(lastBlueprint, name_of_selected_project));
					config::theSettings.application.lastBlueprint.set(name_of_selected_project);
					config::theSettings.save(*m_pConfigBackend);
				}
			}
			else
			{
				// do not touch the configuration just yet, probably missing an update. keep selection at -1
			}
		}
		const char* preview = "";

		if (ImGui::BeginCombo("##Project", name_of_selected_project.c_str()))
		{
			for (int i = 0; i < (int)projects->size(); ++i)
			{
				bool is_selected = (selected_project_index == i);
				if (ImGui::Selectable((*projects)[i]->project_name().c_str(), is_selected))
				{
					/*if (selected_blueprint_index != i)
					{
						selected_blueprint_index = i;
						config::theSettings.application.lastBlueprint.set((*projects)[i]->name());
						config::theSettings.save(*m_pConfigBackend);
					}
					*/
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
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

		// Refresh button with spinner when busy
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

		// Backup button - requires a blueprint selected
		bool has_blueprint = (selected_project_index >= 0 && selected_project_index < (int)projects->size());
		bool is_busy = m_state.worker->is_busy();

		ImGui::BeginDisabled(!has_blueprint || is_busy);
		if (ImGui::Button("Backup"))
		{
			start_backup_from_project((*projects)[selected_project_index]);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !has_blueprint)
			ImGui::SetTooltip("Select a blueprint first");

		ImGui::SameLine();

		// Clean button - requires a blueprint selected
		ImGui::BeginDisabled(!has_blueprint || is_busy);
		if (ImGui::Button("Clean"))
		{
			auto* blueprint = (*projects)[selected_project_index];
			m_state.progress_operation = m_state.dry_run ? "Dry-run" : "Clean";
			m_state.progress_phase = "Starting";
			m_state.progress_detail = "";
			m_state.progress_percent = -1;
			m_state.progress_log.clear();
			m_state.show_progress_dialog = true;

			if (m_state.active_blueprint)
				m_state.active_blueprint->release(REFCOUNT_DEBUG_ARGS);
			m_state.active_blueprint = blueprint;

			m_state.worker->post(StartClean{ m_state.m_snapshot_registry, blueprint, m_state.dry_run });
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !has_blueprint)
			ImGui::SetTooltip("Select a blueprint first");

		ImGui::SameLine();
		if (ImGui::Button("Restore"))
		{
			start_backup_from_project((*projects)[selected_project_index]);
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

		ImGui::Separator();

		float total_width = ImGui::GetContentRegionAvail().x;
		float snapshot_width = total_width;

		// Left panel: Snapshot table (2/3 width)
		ImGui::BeginChild("SnapshotList", ImVec2(snapshot_width, 0), true);

		// Get selected blueprint name for filtering (spaces -> underscores to match filename convention)
		std::string blueprint_filter;
		if (selected_project_index >= 0 && selected_project_index < (int)projects->size())
		{
			blueprint_filter = (*projects)[selected_project_index]->project_name();
			std::replace(blueprint_filter.begin(), blueprint_filter.end(), ' ', '_');
		}

		const auto search_text = pnq::string::lowercase(m_state.filter_text);
		//config::theSettings.application.lastBlueprint.set(((*projects)[i]->name());
		pnq::RefCountedVector<insti::Instance*> filtered;
		for (auto* bp : *instances)
		{
			if (bp->matches(search_text))
			{
				filtered.push_back(bp);
			}
		}

		// Sort by timestamp (newest first) <- TODO: should use UI sorting instead
		std::sort(filtered.begin(), filtered.end(),
			[](insti::Instance* a, insti::Instance* b) {
				return a->m_timestamp > b->m_timestamp;
			});

		if (filtered.empty())
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

				int entry_index = 1000;
				for (auto* entry : filtered)
				{
					ImGui::PushID(entry_index++);
					ImGui::TableNextRow();

					// Check installation status for row highlighting
					auto status = entry->m_install_status;
					bool is_selected = (m_state.selected_snapshot == entry);

					// Set row background color for installed snapshot
					if (status == insti::InstallStatus::Installed)
					{
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(100, 200, 100, 60));
					}
					else if (status == insti::InstallStatus::DifferentVersion)
					{
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(220, 180, 50, 40));
					}

					// Variant
					ImGui::TableNextColumn();
					ImGuiSelectableFlags sel_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
					if (ImGui::Selectable(entry->project_name().c_str(), is_selected, sel_flags))
					{
						m_state.selected_snapshot = entry;
					}

					ImGui::TableNextColumn();
					ImGui::Text("%s", entry->m_description.c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%s", entry->timestamp_string().c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%s", entry->installdir().c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%s", entry->project_version().c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%s", entry->m_machine.c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%s", entry->m_user.c_str());
					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::End();

		// Progress dialog
		render_progress_dialog();

		// First-run setup dialog
		render_first_run_dialog();

		// Rendering
		ImGui::Render();
		const float clear_color[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
		m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
		m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clear_color);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		m_pSwapChain->Present(1, 0); // VSync
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

		m_hWnd = CreateWindowExW(
			0,
			wc.lpszClassName,
			L"insti",
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
			ImGui::StyleColorsLight();
		else
			ImGui::StyleColorsDark();

		ImGuiStyle& style = ImGui::GetStyle();
		style.FrameRounding = 4.0f;
		style.WindowRounding = 6.0f;
		style.ScrollbarRounding = 4.0f;
		style.GrabRounding = 4.0f;

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

	// Rebuild font atlas with custom font
	void Instinctiv::rebuild_font_atlas(float fontSize)
	{
		ImGuiIO& io = ImGui::GetIO();

		// Clear existing fonts
		io.Fonts->Clear();

		// Try to load Segoe UI from Windows fonts
		wchar_t windowsDir[MAX_PATH];
		if (GetWindowsDirectoryW(windowsDir, MAX_PATH) == 0)
		{
			io.Fonts->AddFontDefault();
		}
		else
		{
			std::wstring fontsDir = std::wstring(windowsDir) + L"\\Fonts\\";
			const wchar_t* fontFiles[] = {
				L"segoeui.ttf",  // Segoe UI (modern, clean)
				L"arial.ttf"     // Arial (fallback)
			};

			bool fontLoaded = false;
			for (const wchar_t* fontFile : fontFiles)
			{
				std::wstring fontPath = fontsDir + fontFile;
				// Convert to narrow string for ImGui
				int size = WideCharToMultiByte(CP_UTF8, 0, fontPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
				std::string narrowPath(size, 0);
				WideCharToMultiByte(CP_UTF8, 0, fontPath.c_str(), -1, &narrowPath[0], size, nullptr, nullptr);
				narrowPath.resize(size - 1); // Remove null terminator

				if (io.Fonts->AddFontFromFileTTF(narrowPath.c_str(), fontSize))
				{
					spdlog::info("Loaded font: {} at size {}", narrowPath, fontSize);
					fontLoaded = true;
					break;
				}
			}

			if (!fontLoaded)
			{
				spdlog::warn("Could not load custom font, using default");
				io.Fonts->AddFontDefault();
			}
		}

		// Rebuild font texture
		io.Fonts->Build();

		// Set the new font as default
		if (io.Fonts->Fonts.Size > 0)
			io.FontDefault = io.Fonts->Fonts[0];

		// Notify ImGui backends to update their font texture
		if (m_pd3dDevice && m_pd3dDeviceContext)
		{
			ImGui_ImplDX11_InvalidateDeviceObjects();
			ImGui_ImplDX11_CreateDeviceObjects();
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
			m_state.active_blueprint->release(REFCOUNT_DEBUG_ARGS);
		m_state.active_blueprint = blueprint;

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
