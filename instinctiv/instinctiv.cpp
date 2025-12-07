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

// DirectX 11 globals
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static UINT                     g_ResizeWidth = 0;
static UINT                     g_ResizeHeight = 0;
static HWND                     g_hWnd = nullptr;

// Config globals
static pnq::config::TomlBackend* g_pConfigBackend = nullptr;
static std::filesystem::path g_appDataPath;
static std::filesystem::path g_configPath;
static std::string g_imguiIniPath;

// Drag-and-drop
static std::string g_droppedFile;  // File dropped onto window (processed in main loop)

// Forward declarations
static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static void InitializeConfig();
static void InitializeLogging();
static void RebuildFontAtlas(float fontSize);

static void RenderProgressDialog();
static void RenderFirstRunDialog();
static void ProcessWorkerMessages();
static std::string BrowseForFolder(HWND hwnd, const char* title);
static std::string FormatFileSize(uint64_t bytes);
static std::string FormatTimestamp(std::chrono::system_clock::time_point tp);
static std::string ShowSaveDialog(HWND hwnd, const char* filter, const char* default_name, const char* default_ext);
static void StartBackupFromBlueprint(insti::ProjectBlueprint* bp_entry);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	// Initialize config and logging first
	InitializeConfig();
	InitializeLogging();

	spdlog::info("instinctiv starting up");

	// Load window settings
	auto& windowSettings = instinctiv::config::theSettings.window;
	int width = windowSettings.width.get();
	int height = windowSettings.height.get();
	int posX = windowSettings.positionX.get();
	int posY = windowSettings.positionY.get();
	bool maximized = windowSettings.maximized.get();

	// Create application window
	WNDCLASSEXW wc = {
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

	HWND hwnd = CreateWindowExW(
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
	g_hWnd = hwnd;

	// Enable drag-and-drop
	DragAcceptFiles(hwnd, TRUE);

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		UnregisterClassW(wc.lpszClassName, hInstance);
		return 1;
	}

	ShowWindow(hwnd, maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
	UpdateWindow(hwnd);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// Set ImGui ini file path to AppData folder
	if (!g_imguiIniPath.empty())
		io.IniFilename = g_imguiIniPath.c_str();

	// Setup Dear ImGui style
	auto& appSettings = instinctiv::config::theSettings.application;
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
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

	// Load font with size from settings
	int32_t fontSizeScaled = appSettings.fontSizeScaled.get();
	float fontSize = fontSizeScaled / 100.0f;
	RebuildFontAtlas(fontSize);

	// Initialize application state
	auto& app = instinctiv::AppState::instance();
	app.initialize();

	// Main loop
	bool done = false;
	while (!done)
	{
		// Poll and handle messages
		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
			if (msg.message == WM_QUIT)
				done = true;
		}
		if (done)
			break;

		// Handle window resize
		if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
		{
			CleanupRenderTarget();
			g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
			g_ResizeWidth = g_ResizeHeight = 0;
			CreateRenderTarget();
		}

		// Start the Dear ImGui frame
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// Keyboard shortcuts
		if (!ImGui::GetIO().WantTextInput)
		{
			// Ctrl+R - Refresh
			if (ImGui::IsKeyPressed(ImGuiKey_R) && ImGui::GetIO().KeyCtrl && !app.is_refreshing)
			{
				app.is_refreshing = true;
				app.status_message = "Scanning for snapshots...";
				app.worker->post(instinctiv::RefreshRegistry{ app.registry_roots });
			}

			// Escape - Close dialogs
			if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				if (app.show_progress_dialog && !app.worker->is_busy())
					app.show_progress_dialog = false;
				else if (app.show_first_run_dialog)
					app.show_first_run_dialog = false;
			}

			// F5 - Refresh (alternative)
			if (ImGui::IsKeyPressed(ImGuiKey_F5) && !app.is_refreshing)
			{
				app.is_refreshing = true;
				app.status_message = "Scanning for snapshots...";
				app.worker->post(instinctiv::RefreshRegistry{ app.registry_roots });
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
				for (auto* entry : app.owned_snapshots)
				{
					if (entry->path == g_droppedFile)
					{
						app.selected_snapshot = entry;
						app.status_message = "Selected: " + dropped.filename().string();
						break;
					}
				}
				// If not found in registry, show message
				if (app.selected_snapshot == nullptr || app.selected_snapshot->path != g_droppedFile)
				{
					app.status_message = "Snapshot not in registry: " + dropped.filename().string();
				}
				*/
			}
			else if (pnq::string::equals_nocase(ext, ".xml"))
			{
				app.status_message = "Dropped blueprint: " + dropped.filename().string();
			}
			else
			{
				app.status_message = "Unsupported file type: " + ext;
			}

			g_droppedFile.clear();
		}

		// Menu bar
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Exit", "Alt+F4"))
					done = true;
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("View"))
			{
				if (ImGui::MenuItem("Refresh", "Ctrl+R", false, !app.is_refreshing))
				{
					app.is_refreshing = true;
					app.status_message = "Scanning for snapshots...";
					app.worker->post(instinctiv::RefreshRegistry{ app.registry_roots });
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
		ProcessWorkerMessages();

		// Main window content
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

		const auto projects = app.snapshot_registry->discover_project_blueprints();
		const auto instances = app.snapshot_registry->discover_instance_blueprints();

		const char* preview = (app.selected_blueprint_index >= 0 && app.selected_blueprint_index < (int)projects.size())
			? projects[app.selected_blueprint_index]->name().c_str()
			: "(All Projects)";
		if (ImGui::BeginCombo("##ProjectBlueprint", preview))
		{
			// "All" option only if multiple blueprints
			if (projects.size() > 1)
			{
				bool is_selected = (app.selected_blueprint_index < 0);
				if (ImGui::Selectable("(All Project Blueprints)", is_selected))
				{
					app.selected_blueprint_index = -1;
					instinctiv::config::theSettings.application.lastBlueprint.set("");
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}

			for (int i = 0; i < (int)projects.size(); ++i)
			{
				bool is_selected = (app.selected_blueprint_index == i);
				if (ImGui::Selectable(projects[i]->name().c_str(), is_selected))
				{
					app.selected_blueprint_index = i;
					instinctiv::config::theSettings.application.lastBlueprint.set(projects[i]->name());
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
			app.filter_text = filter_buf;
			app.filter_dirty = true;
		}
		ImGui::SameLine();

		// Refresh button with spinner when busy
		ImGui::BeginDisabled(app.is_refreshing);
		if (ImGui::Button(app.is_refreshing ? "Refreshing..." : "Refresh"))
		{
			app.is_refreshing = true;
			app.status_message = "Scanning for snapshots...";
			app.worker->post(instinctiv::RefreshRegistry{ app.registry_roots });
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		// Backup button - requires a blueprint selected
		bool has_blueprint = (app.selected_blueprint_index >= 0 && app.selected_blueprint_index < (int)projects.size());
		bool is_busy = app.worker->is_busy();

		ImGui::BeginDisabled(!has_blueprint || is_busy);
		if (ImGui::Button("Backup"))
		{
			StartBackupFromBlueprint(projects[app.selected_blueprint_index]);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !has_blueprint)
			ImGui::SetTooltip("Select a blueprint first");

		ImGui::SameLine();

		// Clean button - requires a blueprint selected
		ImGui::BeginDisabled(!has_blueprint || is_busy);
		if (ImGui::Button("Clean"))
		{
			auto* blueprint = projects[app.selected_blueprint_index];
			app.progress_operation = app.dry_run ? "Dry-run" : "Clean";
			app.progress_phase = "Starting";
			app.progress_detail = "";
			app.progress_percent = -1;
			app.progress_log.clear();
			app.show_progress_dialog = true;

			if (app.active_blueprint)
				app.active_blueprint->release(REFCOUNT_DEBUG_ARGS);
			app.active_blueprint = blueprint;

			app.worker->post(instinctiv::StartClean{ blueprint, blueprint->name(), app.dry_run });
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !has_blueprint)
			ImGui::SetTooltip("Select a blueprint first");

		ImGui::SameLine();

		// Dry-run checkbox
		ImGui::Checkbox("Dry-run", &app.dry_run);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Simulate operations without making changes");

		// Status message
		if (!app.status_message.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", app.status_message.c_str());
		}

		ImGui::Separator();

		// Two-column layout: Snapshots (2/3) | Details (1/3)
		float total_width = ImGui::GetContentRegionAvail().x;
		float snapshot_width = total_width * 2.0f / 3.0f;

		// Left panel: Snapshot table (2/3 width)
		ImGui::BeginChild("SnapshotList", ImVec2(snapshot_width, 0), true);

		// Get selected blueprint name for filtering (spaces -> underscores to match filename convention)
		std::string blueprint_filter;
		if (app.selected_blueprint_index >= 0 && app.selected_blueprint_index < (int)projects.size())
		{
			blueprint_filter = projects[app.selected_blueprint_index]->name();
			std::replace(blueprint_filter.begin(), blueprint_filter.end(), ' ', '_');
		}

		const auto search_text = pnq::string::lowercase(app.filter_text);
		auto filtered = app.snapshot_registry->discover_instances(search_text);

		// Sort by timestamp (newest first) <- TODO: should use UI sorting instead
		std::sort(filtered.begin(), filtered.end(),
			[](insti::InstanceBlueprint* a, insti::InstanceBlueprint* b) {
				return a->instance().timestamp > b->instance().timestamp;
			});

		if (filtered.empty())
		{
			ImGui::TextDisabled("No snapshots found");
		}
		else
		{
			ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("SnapshotTable", 4, table_flags))
			{
				ImGui::TableSetupColumn("Variant", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthStretch, 1.2f);
				ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				for (auto* entry : filtered)
				{
					ImGui::TableNextRow();

					// Check installation status for row highlighting
					auto status = instinctiv::InstallStatus::NotInstalled; // TBD: do something like the old "app.get_install_status(entry);"
					bool is_selected = (app.selected_snapshot == entry);

					// Set row background color for installed snapshot
					if (status == instinctiv::InstallStatus::Installed)
					{
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(100, 200, 100, 60));
					}
					else if (status == instinctiv::InstallStatus::DifferentVersion)
					{
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(220, 180, 50, 40));
					}

					// Variant
					ImGui::TableNextColumn();
					ImGuiSelectableFlags sel_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
					if (ImGui::Selectable(entry->name().c_str(), is_selected, sel_flags))
					{
						app.selected_snapshot = entry;
					}

					// Version
					ImGui::TableNextColumn();
					ImGui::Text("%s", "???"); // entry->version.c_str());

					// Date
					ImGui::TableNextColumn();
					ImGui::Text("%s", entry->instance().timestamp_string().c_str());

					// Size
					ImGui::TableNextColumn();
					ImGui::Text("%s", FormatFileSize(1860).c_str());
				}

				ImGui::EndTable();
			}
		}
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::End();

		// Progress dialog
		RenderProgressDialog();

		// First-run setup dialog
		RenderFirstRunDialog();

		// Rendering
		ImGui::Render();
		const float clear_color[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
		g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		g_pSwapChain->Present(1, 0); // VSync
	}

	// Save window state before shutdown
	{
		WINDOWPLACEMENT wp{};
		wp.length = sizeof(WINDOWPLACEMENT);
		if (GetWindowPlacement(hwnd, &wp))
		{
			auto& ws = instinctiv::config::theSettings.window;
			ws.maximized.set(wp.showCmd == SW_SHOWMAXIMIZED);
			RECT& rc = wp.rcNormalPosition;
			ws.positionX.set(rc.left);
			ws.positionY.set(rc.top);
			ws.width.set(rc.right - rc.left);
			ws.height.set(rc.bottom - rc.top);
		}
	}

	// Save configuration
	instinctiv::config::theSettings.save(*g_pConfigBackend);
	spdlog::info("Configuration saved to: {}", g_configPath.string());

	// Cleanup
	app.shutdown();

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	DestroyWindow(hwnd);
	UnregisterClassW(wc.lpszClassName, hInstance);

	delete g_pConfigBackend;
	g_pConfigBackend = nullptr;

	spdlog::info("instinctiv shutting down");
	return 0;
}

// DirectX 11 helper functions

static bool CreateDeviceD3D(HWND hWnd)
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
		&sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}

static void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static void CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
	pBackBuffer->Release();
}

static void CleanupRenderTarget()
{
	if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Initialize configuration
static void InitializeConfig()
{
	// Get AppData path: %LOCALAPPDATA%\insti
	g_appDataPath = pnq::path::get_known_folder(FOLDERID_LocalAppData) / "insti";
	std::filesystem::create_directories(g_appDataPath);

	g_configPath = g_appDataPath / "insti.toml";
	g_imguiIniPath = (g_appDataPath / "imgui.ini").string();

	// Load configuration
	g_pConfigBackend = new pnq::config::TomlBackend{ g_configPath.string() };
	instinctiv::config::theSettings.load(*g_pConfigBackend);

	// Ensure default registry root exists
	auto& registrySettings = instinctiv::config::theSettings.registry;
	std::string roots = registrySettings.roots.get();
	if (!roots.empty())
	{
		// Parse and create first root if needed
		std::istringstream iss(roots);
		std::string first_root;
		std::getline(iss, first_root, ',');
		if (!first_root.empty())
		{
			std::filesystem::create_directories(first_root);
		}
	}
}

// Initialize logging
static void InitializeLogging()
{
	auto& loggingSettings = instinctiv::config::theSettings.logging;

	// Determine log file path
	std::string logFilePath = loggingSettings.logFilePath.get();
	if (logFilePath.empty())
	{
		logFilePath = (g_appDataPath / "insti.log").string();
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
		std::string logLevel = loggingSettings.logLevel.get();
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

// Rebuild font atlas with custom font
static void RebuildFontAtlas(float fontSize)
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
	if (g_pd3dDevice && g_pd3dDeviceContext)
	{
		ImGui_ImplDX11_InvalidateDeviceObjects();
		ImGui_ImplDX11_CreateDeviceObjects();
	}
}

// Process messages from worker thread
static void ProcessWorkerMessages()
{
	auto& app = instinctiv::AppState::instance();

	while (auto msg = app.worker->poll())
	{
		std::visit([&](auto&& m) {
			using T = std::decay_t<decltype(m)>;

			if constexpr (std::is_same_v<T, instinctiv::RegistryRefreshComplete>)
			{
				// Build trees from discovered entries
				app.is_refreshing = false;
				app.status_message = std::format("Found {} instance{}, {} project{}",
					m.instance_blueprints.size(), m.instance_blueprints.size() == 1 ? "" : "s",
					m.project_blueprints.size(), m.project_blueprints.size() == 1 ? "" : "s");

				// Check for empty registry on first refresh
				if (!app.first_refresh_done)
				{
					app.first_refresh_done = true;
					if (m.project_blueprints.empty() && m.project_blueprints.empty())
					{
						app.show_first_run_dialog = true;
					}
				}
			}
			else if constexpr (std::is_same_v<T, instinctiv::Progress>)
			{
				app.progress_phase = m.phase;
				app.progress_detail = m.detail;
				app.progress_percent = m.percent;
			}
			else if constexpr (std::is_same_v<T, instinctiv::LogEntry>)
			{
				std::string prefix;
				switch (m.level)
				{
				case instinctiv::LogEntry::Level::Warning: prefix = "[WARN] "; break;
				case instinctiv::LogEntry::Level::Error: prefix = "[ERROR] "; break;
				default: break;
				}
				app.progress_log.push_back(prefix + m.message);
			}
			else if constexpr (std::is_same_v<T, instinctiv::OperationComplete>)
			{
				app.progress_phase = m.success ? "Complete" : "Failed";
				app.progress_percent = m.success ? 100 : -1;
				app.progress_log.push_back(m.message);


				// Notify new registry of operation completion (invalidates installation cache)
				if (m.success && app.snapshot_registry)
				{
					if (app.progress_operation == "Restore")
						app.snapshot_registry->notify_restore_complete("");
					else if (app.progress_operation == "Clean")
						app.snapshot_registry->notify_clean_complete();
					else if (app.progress_operation == "Backup" && !m.snapshot_path.empty())
						app.snapshot_registry->notify_backup_complete(m.snapshot_path);
				}

				// Refresh registry if backup succeeded (new snapshot may have been created)
				if (m.success && app.progress_operation == "Backup")
				{
					app.worker->post(instinctiv::RefreshRegistry{ app.registry_roots });
				}
			}
			else if constexpr (std::is_same_v<T, instinctiv::ErrorDecision>)
			{
				// For now, log the error and auto-skip
				// TODO M4.9: Show error decision dialog
				spdlog::warn("Error during operation: {} - {}", m.message, m.context);
				app.progress_log.push_back("[ERROR] " + m.message + ": " + m.context);

				// Send SkipAll decision to continue
				app.worker->post(instinctiv::DecisionResponse{ insti::IActionCallback::Decision::SkipAll });
			}
			else if constexpr (std::is_same_v<T, instinctiv::FileConflict>)
			{
				// For now, log and auto-continue (overwrite)
				// TODO M4.9: Show file conflict dialog
				spdlog::info("File conflict: {} ({})", m.path, m.action);
				app.progress_log.push_back("[CONFLICT] " + m.path + " (" + m.action + ")");

				// Send Continue decision to overwrite
				app.worker->post(instinctiv::DecisionResponse{ insti::IActionCallback::Decision::Continue });
			}
			// Other message types handled in future milestones
			}, *msg);
	}
}

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

// Show save file dialog, returns selected path or empty string if cancelled
static std::string ShowSaveDialog(HWND hwnd, const char* filter, const char* default_name, const char* default_ext)
{
	char filename[MAX_PATH] = "";
	if (default_name)
		strncpy_s(filename, default_name, _TRUNCATE);

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = default_ext;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

	if (GetSaveFileNameA(&ofn))
		return filename;
	return "";
}

// Start backup operation from a blueprint entry
static void StartBackupFromBlueprint(insti::ProjectBlueprint* blueprint)
{
	auto& app = instinctiv::AppState::instance();

	spdlog::info("StartBackupFromBlueprint: {}", blueprint->source_path());

	spdlog::info("Blueprint loaded: {} v{}", blueprint->name(), blueprint->version());

	// Get output directory from settings (first registry root)
	auto& registrySettings = instinctiv::config::theSettings.registry;
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
		app.status_message = "No output directory configured";
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
	std::string project = blueprint->name();

	// Sanitize names (replace spaces with underscores)
	std::replace(project.begin(), project.end(), ' ', '_');

	std::string filename = std::format("{}-{}.zip", project, timestamp);
	std::filesystem::path output_path = std::filesystem::path(output_dir) / filename;

	spdlog::info("Output path: {}", output_path.string());

	// Store blueprint for the operation (will be released when complete)
	if (app.active_blueprint)
		app.active_blueprint->release(REFCOUNT_DEBUG_ARGS);
	app.active_blueprint = blueprint;

	// Setup progress dialog
	app.show_progress_dialog = true;
	app.progress_operation = "Backup";
	app.progress_phase = "Starting...";
	app.progress_detail.clear();
	app.progress_percent = -1;
	app.progress_log.clear();
	app.progress_log.push_back("Starting backup: " + blueprint->name());
	app.progress_log.push_back("Output: " + output_path.string());

	// Start backup on worker thread
	app.worker->post(instinctiv::StartBackup{ blueprint, output_path.string() });
}

// Progress dialog during operations
static void RenderProgressDialog()
{
	auto& app = instinctiv::AppState::instance();

	if (!app.show_progress_dialog)
		return;

	ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = true;
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

	std::string title = app.progress_operation + " Progress";
	if (ImGui::Begin(title.c_str(), &open, flags))
	{
		// Current phase
		ImGui::Text("Phase: %s", app.progress_phase.c_str());

		// Current item
		if (!app.progress_detail.empty())
		{
			ImGui::TextWrapped("Current: %s", app.progress_detail.c_str());
		}

		// Progress bar
		ImGui::Spacing();
		if (app.progress_percent >= 0)
		{
			ImGui::ProgressBar(app.progress_percent / 100.0f, ImVec2(-1, 0));
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
		for (const auto& line : app.progress_log)
		{
			ImGui::TextWrapped("%s", line.c_str());
		}
		// Auto-scroll to bottom
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();

		// Cancel button
		if (app.worker->is_busy())
		{
			if (ImGui::Button("Cancel", ImVec2(80, 0)))
			{
				app.worker->cancel();
				app.progress_log.push_back("Cancelling...");
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
		app.show_progress_dialog = false;
		// Cleanup active blueprint
		if (app.active_blueprint)
		{
			app.active_blueprint->release(REFCOUNT_DEBUG_ARGS);
			app.active_blueprint = nullptr;
		}
	}
}

// Browse for folder dialog
static std::string BrowseForFolder(HWND hwnd, const char* title)
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

// First-run setup dialog when registry is empty
static void RenderFirstRunDialog()
{
	auto& app = instinctiv::AppState::instance();

	if (!app.show_first_run_dialog)
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
		if (app.registry_roots.empty())
		{
			ImGui::TextDisabled("  (none configured)");
		}
		else
		{
			for (const auto& root : app.registry_roots)
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
			std::string folder = BrowseForFolder(g_hWnd, "Select Registry Folder");
			if (!folder.empty())
			{
				// Add to registry roots
				app.registry_roots.push_back({ folder, true });

				// Update settings
				auto& registrySettings = instinctiv::config::theSettings.registry;
				std::string roots_str = registrySettings.roots.get();
				if (!roots_str.empty())
					roots_str += ",";
				roots_str += folder;
				registrySettings.roots.set(roots_str);

				// Update snapshot_registry with new roots
				app.snapshot_registry = std::make_unique<insti::SnapshotRegistry>(app.registry_roots);

				// Trigger refresh
				app.is_refreshing = true;
				app.status_message = "Scanning for snapshots...";
				app.worker->post(instinctiv::RefreshRegistry{ app.registry_roots });

				// Close dialog (will reopen if still empty after refresh)
				app.show_first_run_dialog = false;
				app.first_refresh_done = false;  // Allow re-check after refresh
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Continue Anyway", ImVec2(120, 0)))
		{
			app.show_first_run_dialog = false;
		}
	}
	ImGui::End();
}

// Win32 message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		g_ResizeWidth = LOWORD(lParam);
		g_ResizeHeight = HIWORD(lParam);
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
