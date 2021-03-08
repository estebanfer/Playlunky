#include "file_io.h"

#include "detour_entry.h"
#include "detour_helper.h"
#include "sigscan.h"
#include "log.h"
#include "mod/virtual_filesystem.h"

#include <d3d11.h>
#include <Windows.h>

#include <imgui.h>
#include <util/imgui/imgui_impl_dx11.h>
#include <util/imgui/imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct DetourCreateWindowEx {
	inline static auto Trampoline = &CreateWindowEx;
	static HWND Detour(
		DWORD     dwExStyle,
		LPCSTR    lpClassName,
		LPCSTR    lpWindowName,
		DWORD     dwStyle,
		int       X,
		int       Y,
		int       nWidth,
		int       nHeight,
		HWND      hWndParent,
		HMENU     hMenu,
		HINSTANCE hInstance,
		LPVOID    lpParam
	) {
		if (lpClassName == std::string_view{ "WindowClass1" }) {
			s_Window = Trampoline(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
			return s_Window;
		}
		return Trampoline(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
	}
	inline static HWND s_Window{ nullptr };
};

struct DetourSwapChainPresent {
	inline static SigScan::Function<HRESULT(__stdcall*) (IDXGISwapChain*, UINT, UINT)> Trampoline{
		.Signature = "\x48\x89\x5c\x24\x10\x48\x89\x74\x24\x20\x55\x57\x41\x56\x48\x8d\x6c\x24\x90\x48\x81\xec\x70\x01\x00\x00"_sig,
		.Module = "dxgi.dll"
	};
	static HRESULT Detour(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
	{
		if (s_SwapChain == nullptr) {
			if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&s_Device)))
			{
				s_SwapChain = pSwapChain;
				s_Device->GetImmediateContext(&s_Context);

#ifndef NDEBUG
				DXGI_SWAP_CHAIN_DESC sd;
				s_SwapChain->GetDesc(&sd);
				assert(DetourCreateWindowEx::s_Window == sd.OutputWindow);
#endif

				CreateRenderTargetView();
				
				auto init_imgui = []() {
					ImGui::CreateContext();
					ImGuiIO& io = ImGui::GetIO();
					io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
					io.MouseDrawCursor = true;
					ImGui_ImplWin32_Init(DetourCreateWindowEx::s_Window);
					ImGui_ImplDX11_Init(s_Device, s_Context);
				};
				init_imgui();
			}
		}
		else {
			assert(s_SwapChain == pSwapChain);

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			// TODO: Hook any other UI

			ImGui::SetNextWindowSize({ -1, 30 });
			ImGui::SetNextWindowPos({ -1, 30 });
			ImGui::Begin(
				"Overlay",
				nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBringToFrontOnFocus |
				ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, .3f), "Playlunky " PLAYLUNKY_VERSION);
			ImGui::SetWindowPos({ 0, ImGui::GetIO().DisplaySize.y - 30 }, ImGuiCond_Always);
			ImGui::End();

			//ImGui::ShowMetricsWindow();
			//ImGui::Begin("Styles");
			//ImGui::ShowStyleEditor();
			//ImGui::End();

			ImGui::Render();

			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}
		return Trampoline(pSwapChain, SyncInterval, Flags);
	}
	
	static void CreateRenderTargetView() {
		if (s_SwapChain != nullptr) {
			ID3D11Texture2D* back_buffer;
			if (SUCCEEDED(s_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&back_buffer)) && back_buffer != nullptr) {
				s_Device->CreateRenderTargetView(back_buffer, nullptr, &s_MainRenderTargetView);
				s_Context->OMSetRenderTargets(1, &s_MainRenderTargetView, NULL);
				back_buffer->Release();
			}
		}
	}
	static void ReleaseRenderTargetView() {
		if (s_MainRenderTargetView != nullptr)
		{
			s_MainRenderTargetView->Release();
			s_MainRenderTargetView = nullptr;
		}
	}

	inline static IDXGISwapChain* s_SwapChain{ nullptr };
	inline static ID3D11Device* s_Device { nullptr };
	inline static ID3D11DeviceContext* s_Context { nullptr };
	inline static ID3D11RenderTargetView* s_MainRenderTargetView { nullptr };
};

struct DetourSwapChainResizeBuffers {
	inline static SigScan::Function<HRESULT(__stdcall*) (IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT)> Trampoline{
		.Signature = "\x48\x8b\xc4\x55\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8d\x68\xb1\x48\x81\xec\xc0\x00\x00\x00\x48\xc7\x45\x1f\xfe\xff\xff\xff"_sig,
		.Module = "dxgi.dll"
	};
	static HRESULT Detour(
		IDXGISwapChain* pSwapChain,
		UINT        BufferCount,
		UINT        Width,
		UINT        Height,
		DXGI_FORMAT NewFormat,
		UINT        SwapChainFlags
	) {
		DetourSwapChainPresent::ReleaseRenderTargetView();
		const HRESULT result = Trampoline(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
		DetourSwapChainPresent::CreateRenderTargetView();
		return result;
	}
};

struct DetourWindowProc {
	inline static SigScan::Function<WNDPROC> Trampoline{
		.Signature = "\x40\x55\x56\x57\x41\x54\x41\x57\x48\x8d\x6c\x24\xc9\x48\x81\xec\xb0\x00\x00\x00\x48\x8b\x05"_sig
	};
	static LRESULT Detour(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
		ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
		return Trampoline(window, message, wParam, lParam);
	}

	inline static bool s_MouseDownOnNonClientArea{ false };
};

std::vector<DetourEntry> GetImguiDetours() {
	return {
		DetourHelper<DetourCreateWindowEx>::GetDetourEntry("CreateWindowEx"),
		DetourHelper<DetourSwapChainPresent>::GetDetourEntry("IDXGISwapChain::Present"),
		DetourHelper<DetourSwapChainResizeBuffers>::GetDetourEntry("IDXGISwapChain::ResizeBuffers"),
		DetourHelper<DetourWindowProc>::GetDetourEntry("WindowProc")
	};
}