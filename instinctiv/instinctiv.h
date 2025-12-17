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
#include <dwmapi.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <tchar.h>
#include <windowsx.h>  // For GET_X_LPARAM, GET_Y_LPARAM

#pragma comment(lib, "dwmapi.lib")

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
		void render_title_bar();
		void render_menu_bar();
		void render_toolbar();
		void render_snapshot_table();
		void render_progress_dialog();
		void render_font_dialog();
		void render_backup_dialog();
		void render_settings_dialog();
		void render_blueprint_editor();
		void render_uninstall_confirm_dialog();

		// Title bar helpers
		bool is_window_maximized() const;

		// Helpers
		void handle_keyboard_shortcuts();
		void handle_dropped_file();
		void rebuild_font_atlas(float fontSize);
		void apply_style();
		std::string show_save_dialog(const char* filter, const char* default_name, const char* default_ext);
		std::string browse_for_folder(HWND hwnd, const char* title);

		// DirectX
		bool create_device_d3d(HWND hWnd);
		void cleanup_device_d3d();
		void create_render_target();
		void cleanup_render_target();

		// Worker
		void process_worker_messages();

		// Operations - project-based (from toolbar buttons)
		void start_backup_from_project(insti::Project* project);
		void start_clean_from_project(insti::Project* project);
		void start_verify_from_project(insti::Project* project);

		// Operations - instance-based (from right-click context menu)
		void start_restore_from_instance(insti::Instance* instance);
		void start_backup_from_instance(insti::Instance* instance);  // Refresh snapshot
		void start_clean_from_instance(insti::Instance* instance);
		void start_verify_from_instance(insti::Instance* instance);

		// Hook execution
		void start_hook_execution(insti::IHook* hook);

		// Lifecycle operations
		void start_startup(insti::Project* blueprint);
		void start_shutdown(insti::Project* blueprint);

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

		// Font size change (deferred to avoid mid-frame rebuild)
		float m_pendingFontSize{ 0.0f };

		// Font selection dialog
		bool m_showFontDialog{ false };
		std::vector<std::string> m_availableFonts;
		int m_selectedFontIndex{ -1 };
		std::string m_originalFontName;  // To restore on Cancel

		// Backup options dialog
		bool m_showBackupDialog{ false };
		insti::Project* m_backupProject{ nullptr };  // Project to backup (not owned, just reference)
		char m_backupDescription[256]{};
		std::string m_backupFilename;  // Auto-generated filename (read-only display)
		int m_backupSelectedRoot{ 0 };  // Index of selected root for saving

		// Settings dialog
		bool m_showSettingsDialog{ false };
		std::vector<std::string> m_settingsRoots;  // Editable copy of registry roots
		int m_settingsSelectedRoot{ -1 };  // Selected root in list

		// Blueprint editor dialog
		bool m_showBlueprintEditor{ false };
		enum class BlueprintEditorMode { None, Add, Edit, Remove };
		BlueprintEditorMode m_blueprintEditorMode{ BlueprintEditorMode::None };
		int m_blueprintEditorSelectedProject{ -1 };  // Selected project in combobox
		char m_blueprintEditorName[256]{};  // Project name
		std::string m_blueprintEditorXml;  // XML content
		std::string m_blueprintEditorSourcePath;  // Original file path (empty for new)
		std::string m_blueprintEditorSyntaxResult;  // Syntax check result message

		// Uninstall confirmation dialog
		bool m_showUninstallConfirm{ false };
		insti::Project* m_uninstallTarget{ nullptr };  // Project/Instance to uninstall
		std::vector<std::string> m_uninstallDescriptions;  // What will be removed

		// Custom title bar
		DWORD m_accentColor{ RGB(0, 120, 212) };  // Windows accent color
		bool m_windowFocused{ true };
	};
}
