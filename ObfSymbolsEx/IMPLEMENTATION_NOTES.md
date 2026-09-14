# ObfSymbolsEx Implementation Notes

## Dynamic DLL Loading Implementation

### Overview

ObfSymbolsEx has been implemented to dynamically load the DIA SDK DLL (`msdia140.dll`) without requiring COM registration. This makes the application portable and easy to distribute.

### Key Implementation Details

#### 1. DLL Location Strategy

The `FindMsdiaDll()` function searches for `msdia140.dll` in the following order:

1. **Same directory as executable** (checked first for portability)
2. **Visual Studio 2022 Professional / Enterprise / Community** installations
   (both `Program Files` and `Program Files (x86)`), plus the VS Installer's
   own `Feedback\amd64` copy
3. **`%PATH%`** — via `SearchPathW`, as a final fallback

There is no embedded-resource fallback: an earlier prototype embedded the DLL
as an `RCDATA` resource and extracted it at runtime, but that approach was
intentionally removed. See [EMBEDDED_DLL_FEATURE.md](EMBEDDED_DLL_FEATURE.md)
for why, and for the current strategy in full.

```cpp
// Check if DLL exists in the same directory as the executable
wchar_t exePath[MAX_PATH];
GetModuleFileNameW(NULL, exePath, MAX_PATH);
fs::path exeDir = fs::path(exePath).parent_path();
fs::path localDll = exeDir / L"msdia140.dll";

if (fs::exists(localDll)) {
    return localDll.wstring();
}

// ... fixed Visual Studio install paths checked here ...

// Finally, fall back to %PATH%
wchar_t buffer[MAX_PATH];
DWORD len = SearchPathW(NULL, L"msdia140.dll", NULL, MAX_PATH, buffer, NULL);
if (len > 0) {
    return std::wstring(buffer);
}
```

#### 2. No-Registration COM Object Creation

The `NoRegCoCreate()` function creates COM objects without registry lookup:

```cpp
HRESULT NoRegCoCreate(const std::wstring& dllPath, REFCLSID rclsid, REFIID riid, void** ppv) {
    // 1. Load the DLL
    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    
    // 2. Get DllGetClassObject function
    DllGetClassObjectFunc pDllGetClassObject = 
        (DllGetClassObjectFunc)GetProcAddress(hDll, "DllGetClassObject");
    
    // 3. Get the class factory
    CComPtr<IClassFactory> pClassFactory;
    HRESULT hr = pDllGetClassObject(rclsid, IID_IClassFactory, (void**)&pClassFactory);
    
    // 4. Create the instance
    hr = pClassFactory->CreateInstance(NULL, riid, ppv);
    
    return hr;
}
```

#### 3. Symbol Visibility Detection

Functions are classified as PUBLIC or PRIVATE based on:

- **get_isStatic()**: Static functions are marked PRIVATE
- **get_access()**: Functions with CV_public access are PUBLIC
- **Default**: Non-static functions are typically PUBLIC

```cpp
// Check access level - default to private
func.isPublic = false;
DWORD access;
if (pSymbol->get_access(&access) == S_OK) {
    func.isPublic = (access == CV_public);
}

// Also consider symbols at top-level scope or with external linkage as public
BOOL isStatic = FALSE;
if (pSymbol->get_isStatic(&isStatic) == S_OK && !isStatic) {
    func.isPublic = true;
}
```

### Build Process Integration

The build script (`build.ps1`) automatically copies `msdia140.dll` after successful compilation:

1. Builds the project
2. Searches for `msdia140.dll` in Visual Studio installation
3. Copies it to the output directory (`x64\Release\` or `x64\Debug\`)

This ensures the executable is immediately ready for standalone distribution.

### Distribution Package

For deployment, distribute these two files together:
- `ObfSymbolsEx.exe` (main executable)
- `msdia140.dll` (DIA SDK runtime)

No installation, registration, or Visual Studio is required on the target machine.

### Advantages of This Approach

✅ **No Registration**: Eliminates the need for `regsvr32` or admin rights
✅ **Portable**: Can run from any directory
✅ **Side-by-Side**: Multiple versions can coexist
✅ **No Dependencies**: Doesn't require Visual Studio on target machines
✅ **Version Control**: Specific DLL version travels with the executable
✅ **Simplified Deployment**: Just copy two files

### Technical Considerations

#### DLL Lifetime Management

The DLL is intentionally not unloaded after creating the COM object:

```cpp
// Note: We intentionally don't call FreeLibrary here because the DLL 
// needs to stay loaded. The COM object references will keep it alive
return S_OK;
```

This is correct behavior because:
- The IDiaDataSource and related objects contain function pointers into the DLL
- Unloading the DLL would invalidate these pointers
- The OS will unload the DLL when the process exits

#### Platform Considerations

The implementation targets x64 architecture and searches for:
- `msdia140.dll` in `amd64` subdirectories
- Visual Studio 2022 installations (v143 toolset)

For x86 builds, the search paths would need to be adjusted to look in the appropriate 32-bit DLL locations.

### Testing

The included `TestApp` project provides comprehensive validation:
- 435 function symbols extracted from TestApp.pdb
- Various C++ constructs: classes, structs, templates, overloads, namespaces
- Both PUBLIC and PRIVATE functions correctly identified
- Method overloads properly distinguished with name mangling

### Future Enhancements

Potential improvements:
1. Support for additional symbol types (variables, types, etc.)
2. Filtering options (by name pattern, visibility, address range)
3. Different output formats (JSON, CSV, XML)
4. Symbol demangling for more readable output
5. Parallel PDB processing for multiple files
6. Integration with other debugging tools

## Conclusion

The dynamic DLL loading approach makes ObfSymbolsEx a truly portable and easy-to-use tool for extracting function symbols from PDB files, without the complexity and administrative requirements of COM registration.

