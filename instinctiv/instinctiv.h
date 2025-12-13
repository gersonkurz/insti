#pragma once

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

namespace instinctiv
{
	// the application class
	class Instinctiv final
	{
	public:
		Instinctiv();
		~Instinctiv();

		bool initialize(HINSTANCE hInstance);

		void run();

		PNQ_DECLARE_NON_COPYABLE(Instinctiv);
		LRESULT wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	private:
		void initialize_config();
		void initialize_logging();

		// Rendering
		void render();
		void render_menu_bar();
		void render_toolbar();
		void render_snapshot_table();
		void render_first_run_dialog();
		void render_progress_dialog();

		// Helpers
		void handle_keyboard_shortcuts();
		void handle_dropped_file();
		void rebuild_font_atlas(float fontSize);
		std::string show_save_dialog(const char* filter, const char* default_name, const char* default_ext);
		std::string browse_for_folder(HWND hwnd, const char* title);

		// DirectX
		bool create_device_d3d(HWND hWnd);
		void cleanup_device_d3d();
		void create_render_target();
		void cleanup_render_target();

		// Worker
		void process_worker_messages();

		// Operations
		void start_backup_from_project(insti::Project* project);
		void start_restore_from_instance(insti::Instance* instance);
		void start_hook_execution(insti::IHook* hook);

	private:
		// Config and paths
		std::filesystem::path m_appDataPath;
		std::filesystem::path m_configPath;
		pnq::config::TomlBackend* m_pConfigBackend{ nullptr };
		std::string m_imguiIniPath;

		// Application state
		AppState m_state;

		// Frame-local state (computed each frame, shared across render methods)
		insti::Project* m_current_project{ nullptr };  // Currently selected project in combobox
		pnq::RefCountedVector<insti::Instance*> m_filtered_instances;  // Filtered instance list

		// DirectX state
		ID3D11Device* m_pd3dDevice{ nullptr };
		ID3D11DeviceContext* m_pd3dDeviceContext{ nullptr };
		IDXGISwapChain* m_pSwapChain{ nullptr };
		ID3D11RenderTargetView* m_mainRenderTargetView{ nullptr };
		UINT m_ResizeWidth{ 0 };
		UINT m_ResizeHeight{ 0 };

		// Window state
		HWND m_hWnd{ nullptr };
		WNDCLASSEXW m_wc{};
		HINSTANCE m_hInstance{};
		bool m_done{ false };
	};
}
