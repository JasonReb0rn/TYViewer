#include "folder_picker.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace Util
{
	std::string pickFolderDialog(GLFWwindow* window, const std::string& title)
	{
		HWND owner = nullptr;
		if (window)
		{
			owner = glfwGetWin32Window(window);
		}

		HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		bool didInit = SUCCEEDED(hr);
		// If COM was already initialized differently, we can still try.
		if (hr == RPC_E_CHANGED_MODE)
		{
			didInit = false;
		}

		IFileDialog* pDialog = nullptr;
		hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDialog));
		if (FAILED(hr) || pDialog == nullptr)
		{
			if (didInit) CoUninitialize();
			return "";
		}

		DWORD options = 0;
		pDialog->GetOptions(&options);
		pDialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

		if (!title.empty())
		{
			pDialog->SetTitle(std::wstring(title.begin(), title.end()).c_str());
		}

		hr = pDialog->Show(owner);
		if (FAILED(hr))
		{
			pDialog->Release();
			if (didInit) CoUninitialize();
			return "";
		}

		IShellItem* pItem = nullptr;
		hr = pDialog->GetResult(&pItem);
		if (FAILED(hr) || pItem == nullptr)
		{
			pDialog->Release();
			if (didInit) CoUninitialize();
			return "";
		}

		PWSTR pszFilePath = nullptr;
		hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

		std::string result;
		if (SUCCEEDED(hr) && pszFilePath)
		{
			// Convert UTF-16 → UTF-8 (basic).
			int len = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, nullptr, 0, nullptr, nullptr);
			if (len > 0)
			{
				result.resize((size_t)len - 1);
				// Write into a mutable buffer (C++17 guarantees string::data() is mutable, but keep this compatible).
				WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, &result[0], len, nullptr, nullptr);
			}
			CoTaskMemFree(pszFilePath);
		}

		pItem->Release();
		pDialog->Release();
		if (didInit) CoUninitialize();

		return result;
	}
}

#else

namespace Util
{
	std::string pickFolderDialog(GLFWwindow*, const std::string&)
	{
		return "";
	}
}

#endif

