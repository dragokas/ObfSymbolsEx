// PdbSymbolExtractor.cpp - Implementation of PDB symbol extraction class
#include "PdbSymbolExtractor.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cwctype>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <windows.h>
#include <comdef.h>
#include <atlbase.h>
#include <dia2.h>

namespace fs = std::filesystem;

// Maps a raw CV_call_e code (from IDiaSymbol::get_callingConvention()) to its
// "__xxxcall" spelling. Shared by ExtractCallingConvention() (the top-level
// CALLING_CONVENTION field) and BuildFunctionPointerTypeName() (which needs
// the same mapping for a function-pointer-typed parameter/return value's own
// calling convention, e.g. "void (__stdcall*)(...)"). CV_call_e is a small,
// fully known enum, so every defined value is mapped explicitly rather than
// falling back to "?" -- an unrecognized (future SDK) value still prints its
// raw numeric code instead of hiding it.
static std::wstring CallingConventionCodeToString(DWORD callingConvention) {
    switch (callingConvention) {
        case CV_CALL_NEAR_C:      return L"__cdecl";
        case CV_CALL_NEAR_PASCAL: return L"__pascal";
        case CV_CALL_NEAR_FAST:   return L"__fastcall";
        case CV_CALL_NEAR_STD:    return L"__stdcall";
        case CV_CALL_NEAR_SYS:    return L"__syscall";
        case CV_CALL_THISCALL:    return L"__thiscall";
        case CV_CALL_CLRCALL:     return L"__clrcall";
        case CV_CALL_NEAR_VECTOR: return L"__vectorcall";
        case CV_CALL_SWIFT:       return L"__swiftcall";
        case CV_CALL_GENERIC:     return L"__generic";
        case CV_CALL_INLINE:      return L"__inline"; // always-inlined; no real convention
        case CV_CALL_FAR_C:       return L"__cdecl_far";
        case CV_CALL_FAR_PASCAL:  return L"__pascal_far";
        case CV_CALL_FAR_FAST:    return L"__fastcall_far";
        case CV_CALL_FAR_STD:     return L"__stdcall_far";
        case CV_CALL_FAR_SYS:     return L"__syscall_far";
        case CV_CALL_MIPSCALL:    return L"__mipscall";
        case CV_CALL_ALPHACALL:   return L"__alphacall";
        case CV_CALL_PPCCALL:     return L"__ppccall";
        case CV_CALL_SHCALL:      return L"__shcall";
        case CV_CALL_ARMCALL:     return L"__armcall";
        case CV_CALL_AM33CALL:    return L"__am33call";
        case CV_CALL_TRICALL:     return L"__tricall";
        case CV_CALL_SH5CALL:     return L"__sh5call";
        case CV_CALL_M32RCALL:    return L"__m32rcall";
        default: {
            // Unrecognized value (e.g. a newer CV_call_e member than this
            // build's DIA headers define) -- report the raw code rather
            // than silently mapping it to "?".
            wchar_t buf[24];
            swprintf_s(buf, 24, L"CV_CALL_0x%02X", callingConvention);
            return buf;
        }
    }
}

enum class SpecialMethodKind {
    None,
    Destructor,                // "ClassName::~ClassName"
    ScalarDeletingDestructor,  // "ClassName::`scalar deleting destructor'"
    VectorDeletingDestructor,  // "ClassName::`vector deleting destructor'"
};

// IDiaSymbol::get_name() for a SymTagFunction/SymTagThunk does NOT return the
// raw decorated name (e.g. "??1ClassName@@..." / "??_GClassName@@..."). DIA
// already undecorates it to a friendly, class-qualified form such as
// "ClassName::~ClassName" or "ClassName::`scalar deleting destructor'". Both
// KIND classification and vtable-slot-inheritance matching need to recognize
// these actual friendly-name patterns, so the detection lives in one place.
static SpecialMethodKind ClassifySpecialMethod(const std::wstring& qualifiedName) {
    if (qualifiedName.find(L"`vector deleting destructor'") != std::wstring::npos) {
        return SpecialMethodKind::VectorDeletingDestructor;
    }
    if (qualifiedName.find(L"`scalar deleting destructor'") != std::wstring::npos) {
        return SpecialMethodKind::ScalarDeletingDestructor;
    }
    size_t pos = qualifiedName.rfind(L"::");
    std::wstring unqualified = (pos == std::wstring::npos) ? qualifiedName : qualifiedName.substr(pos + 2);
    if (!unqualified.empty() && unqualified[0] == L'~') {
        return SpecialMethodKind::Destructor;
    }
    return SpecialMethodKind::None;
}

static std::wstring ClassifyFunctionKind(const std::wstring& name) {
    switch (ClassifySpecialMethod(name)) {
        case SpecialMethodKind::VectorDeletingDestructor: return L"VECTOR_DELETING_DESTRUCTOR";
        case SpecialMethodKind::ScalarDeletingDestructor: return L"DELETING_DESTRUCTOR";
        case SpecialMethodKind::Destructor: return L"DESTRUCTOR";
        default: return L"FUNCTION";
    }
}

// A method's DIA-reported name is class-qualified (e.g. "CASW_Marine::Suicide").
// For matching an override against the ancestor that introduces the same
// virtual function, only the unqualified name plus parameter signature is
// relevant -- the qualifying class necessarily differs between override and
// introducer.
//
// Destructors (and the compiler-generated "deleting destructor" wrapper that
// actually occupies the vtable slot in the MSVC ABI) are the one case where
// this naive "strip the qualifier" approach fails: their unqualified name
// always repeats their OWN class ("~CASW_Marine" vs "~CASW_Inhabitable_NPC"),
// so it never matches across a hierarchy the way a normal method name does.
// Normalize every destructor-like method of the same kind to one fixed key
// so an override can still be traced back to whichever ancestor introduces
// that destructor kind.
static std::wstring UnqualifiedMethodKey(const std::wstring& qualifiedName, const std::wstring& signature) {
    switch (ClassifySpecialMethod(qualifiedName)) {
        case SpecialMethodKind::VectorDeletingDestructor:
            return L"`vector deleting destructor'" + signature;
        case SpecialMethodKind::ScalarDeletingDestructor:
            return L"`scalar deleting destructor'" + signature;
        case SpecialMethodKind::Destructor:
            return L"~" + signature;
        default:
            break;
    }

    size_t pos = qualifiedName.rfind(L"::");
    std::wstring unqualified = (pos == std::wstring::npos) ? qualifiedName : qualifiedName.substr(pos + 2);
    return unqualified + signature;
}

// A much cruder relative of UnqualifiedMethodKey(), used only to judge
// whether a SOURCE_FILE resolved via IDiaSession::findLinesByRVA is safe to
// trust when its RVA is shared by more than one symbol -- see the
// SOURCE_FILE trust pass below for why. Deliberately simpler than
// UnqualifiedMethodKey(): it does NOT fold every destructor-like method
// into one shared key (there "~CFoo" and "~CBar" must compare *equal* to
// find a common ancestor across a hierarchy; here they must compare
// *unequal*, because they really are two different destructors with two
// different definitions, and an RVA collision between them would make
// SOURCE_FILE correct for at most one of them). It also does not fold in
// the parameter signature, so two overloads of the same name (which could
// differ only by a pointer-typed template argument, e.g.
// CUtlVector<CFoo*,...>::InsertBefore vs CUtlVector<CBar*,...>::InsertBefore)
// still compare equal -- these are still genuinely the same template
// definition, just instantiated for a different type, so trusting a shared
// SOURCE_FILE for them is correct, not a guess.
static std::wstring UnqualifiedNameForSourceLocationTrust(const std::wstring& qualifiedName) {
    size_t pos = qualifiedName.rfind(L"::");
    std::wstring unqualified = (pos == std::wstring::npos) ? qualifiedName : qualifiedName.substr(pos + 2);

    // Strip this symbol's own top-level template arguments, if any (e.g. a
    // free function "TemplateFunction<int>" vs "TemplateFunction<double>"),
    // so distinct instantiations of the same template still compare equal.
    // A class-template method's arguments (e.g. "CUtlMemory<CFoo,int>")
    // already sit before the "::" stripped above and never reach here.
    size_t angleBracket = unqualified.find(L'<');
    if (angleBracket != std::wstring::npos) {
        unqualified = unqualified.substr(0, angleBracket);
    }

    return unqualified;
}

// MSVC only records a vtable slot offset (the CodeView LF_ONEMETHOD
// "vbaseoff" field) for the method that INTRODUCES a virtual function; an
// overriding method farther down the hierarchy carries no such field, so
// IDiaSymbol::get_virtualBaseOffset() answers 0 for it -- indistinguishable
// from a genuine slot 0 without further work. This walks the class's own
// base-class chain, exactly as DIA reports it via SymTagBaseClass, looking
// for the nearest ancestor that introduces a virtual method with the same
// unqualified name and parameter signature. This mirrors how the compiler
// itself locates the slot being overridden -- it is not guessed from source
// order, SDK headers, or any data outside this PDB.
//
// Slot plus vtable-shape identity for a virtual method that INTRODUCES its
// slot, whether found as a compiled FunctionSymbol or as a declaration-only
// entry in `introducingSlotsByClass`. The shape/slot-count travel with the
// index because a class implementing more than one interface (multiple
// inheritance) is only able to report ONE vtable shape at its own
// class/method level (see the "secondary multiple-inheritance bases" note
// below) -- the introducing interface's OWN shape, captured here, is the
// only reliable source for the others.
struct IntroducingSlotInfo {
    LONG vtableIndex = -1;
    DWORD vtableShapeId = 0;
    DWORD vtableSlotCount = 0;
};

// The introducing declaration itself may have no compiled body anywhere in
// the PDB (a pure-virtual declared only on an abstract interface base class,
// never given an out-of-line definition), so it can never appear in
// `symbols` -- there is no RVA to build a FunctionSymbol from. Such
// declarations are looked up separately, in `introducingSlotsByClass`
// (captured directly from DIA's TYPE-level method metadata; see
// ExtractSymbolsFromPdb). Returns true and fills `outSlot` if an introducing
// ancestor is found anywhere in this PDB, false otherwise.
static bool FindIntroducingAncestorVTableSlot(
    DWORD classId,
    const std::wstring& methodKey,
    const std::unordered_map<DWORD, ClassHierarchyInfo>& classHierarchy,
    const std::unordered_map<DWORD, std::vector<size_t>>& methodsByClassId,
    const std::vector<FunctionSymbol>& symbols,
    const std::unordered_map<DWORD, std::unordered_map<std::wstring, IntroducingSlotInfo>>& introducingSlotsByClass,
    std::unordered_set<DWORD>& visitedClasses,
    IntroducingSlotInfo& outSlot) {
    if (classId == 0 || !visitedClasses.insert(classId).second) {
        return false;
    }

    auto methodsIt = methodsByClassId.find(classId);
    if (methodsIt != methodsByClassId.end()) {
        for (size_t idx : methodsIt->second) {
            const FunctionSymbol& candidate = symbols[idx];
            if (candidate.isIntroducingVirtual && candidate.vtableIndex >= 0 &&
                UnqualifiedMethodKey(candidate.name, candidate.signature) == methodKey) {
                outSlot.vtableIndex = candidate.vtableIndex;
                outSlot.vtableShapeId = candidate.vtableShapeId;
                outSlot.vtableSlotCount = candidate.vtableSlotCount;
                return true;
            }
        }
    }

    auto declIt = introducingSlotsByClass.find(classId);
    if (declIt != introducingSlotsByClass.end()) {
        auto keyIt = declIt->second.find(methodKey);
        if (keyIt != declIt->second.end()) {
            outSlot = keyIt->second;
            return true;
        }
    }

    auto classIt = classHierarchy.find(classId);
    if (classIt != classHierarchy.end()) {
        for (DWORD baseClassId : classIt->second.baseClassIds) {
            if (FindIntroducingAncestorVTableSlot(
                    baseClassId, methodKey, classHierarchy, methodsByClassId, symbols,
                    introducingSlotsByClass, visitedClasses, outSlot)) {
                return true;
            }
        }
    }

    return false;
}

PdbSymbolExtractor::PdbSymbolExtractor() 
    : _initialized(false)
    , _lastError(L"")
    , _pDiaDataSource(nullptr) {
    
    // Initialize COM
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        _lastError = L"Failed to initialize COM";
        return;
    }

    // Find the DIA SDK DLL
    std::wcout << L"Locating msdia140.dll..." << std::endl;
    std::wstring dllPath = FindMsdiaDll();
    
    if (dllPath.empty()) {
        _lastError = L"Failed to locate msdia140.dll. Please ensure Visual Studio 2022 with DIA SDK is installed, or place msdia140.dll in the same directory as this executable.";
        CoUninitialize();
        return;
    }
    
    std::wcout << L"Found DIA SDK at: " << dllPath << std::endl;

    // Create DIA data source without registration
    hr = NoRegCoCreate(dllPath, CLSID_DiaSource, __uuidof(IDiaDataSource), (void**)&_pDiaDataSource);

    if (FAILED(hr) || !_pDiaDataSource) {
        _lastError = L"Failed to create DIA data source from: " + dllPath;
        CoUninitialize();
        return;
    }

    // Successfully initialized
    _initialized = true;
}

PdbSymbolExtractor::~PdbSymbolExtractor() {
    // Release the DIA data source
    if (_pDiaDataSource) {
        _pDiaDataSource->Release();
        _pDiaDataSource = nullptr;
    }

    // Uninitialize COM
    CoUninitialize();
}

// Find msdia140.dll in Visual Studio installation or extract from resources
std::wstring PdbSymbolExtractor::FindMsdiaDll() {
    // Get executable directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path localDll = exeDir / L"msdia140.dll";
    
    // Check if DLL exists in the same directory as the executable
    if (fs::exists(localDll)) {
        std::wcout << L"Found DIA SDK at: " << localDll.wstring() << std::endl;
        return localDll.wstring();
    }

    // Try common Visual Studio 2022 locations as fallback
    std::wcout << L"Searching Visual Studio installation directories..." << std::endl;
    std::vector<std::wstring> searchPaths = {
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\DIA SDK\\bin\\amd64\\msdia140.dll",
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\DIA SDK\\bin\\amd64\\msdia140.dll",
        L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\DIA SDK\\bin\\amd64\\msdia140.dll",
        L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Professional\\DIA SDK\\bin\\amd64\\msdia140.dll",
        L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Enterprise\\DIA SDK\\bin\\amd64\\msdia140.dll",
        L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Community\\DIA SDK\\bin\\amd64\\msdia140.dll",
		L"C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\Feedback\\amd64\\msdia140.dll",
    };
	
    // Search in Visual Studio installation directories
    for (const auto& path : searchPaths) {
        if (fs::exists(path)) {
            std::wcout << L"Found DIA SDK at: " << path << std::endl;
            return path;
        }
    }
	
	// Search on %PATH%
	wchar_t buffer[MAX_PATH];

	DWORD len = SearchPathW(
		NULL,
		L"msdia140.dll",
		NULL,
		MAX_PATH,
		buffer,
		NULL
	);
	
	if (len > 0)
	{
		return std::wstring(buffer);
	}

    return L"";
}

// Create DIA data source without registration using DLL loading
HRESULT PdbSymbolExtractor::NoRegCoCreate(const std::wstring& dllPath, REFCLSID rclsid, REFIID riid, void** ppv) {
    if (dllPath.empty()) {
        return E_FAIL;
    }

    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    if (!hDll) {
        _lastError = L"Failed to load DLL: " + dllPath + L" Error code: " + std::to_wstring(::GetLastError());
        return E_FAIL;
    }

    typedef HRESULT(__stdcall* DllGetClassObjectFunc)(REFCLSID, REFIID, LPVOID*);
    DllGetClassObjectFunc pDllGetClassObject = (DllGetClassObjectFunc)GetProcAddress(hDll, "DllGetClassObject");
    
    if (!pDllGetClassObject) {
        _lastError = L"Failed to get DllGetClassObject from DLL";
        FreeLibrary(hDll);
        return E_FAIL;
    }

    CComPtr<IClassFactory> pClassFactory;
    HRESULT hr = pDllGetClassObject(rclsid, IID_IClassFactory, (void**)&pClassFactory);
    if (FAILED(hr)) {
        _lastError = L"Failed to get class factory";
        FreeLibrary(hDll);
        return hr;
    }

    hr = pClassFactory->CreateInstance(NULL, riid, ppv);
    if (FAILED(hr)) {
        _lastError = L"Failed to create instance";
        FreeLibrary(hDll);
        return hr;
    }

    // Note: We intentionally don't call FreeLibrary here because the DLL needs to stay loaded
    // The COM object references will keep it alive
    return S_OK;
}

// Generate obfuscated name using FNV-1a hash algorithm
// Combines function name and RVA to ensure uniqueness
std::wstring PdbSymbolExtractor::GenerateObfuscatedName(const std::wstring& realName, DWORD rva) {
    // FNV-1a hash parameters (32-bit)
    const uint32_t FNV_PRIME = 0x01000193;
    const uint32_t FNV_OFFSET_BASIS = 0x811C9DC5;
    
    uint32_t hash = FNV_OFFSET_BASIS;
    
    // Hash the real function name
    for (wchar_t c : realName) {
        // Process both bytes of the wide character for better distribution
        hash ^= static_cast<uint32_t>(c & 0xFF);
        hash *= FNV_PRIME;
        hash ^= static_cast<uint32_t>((c >> 8) & 0xFF);
        hash *= FNV_PRIME;
    }
    
    // Mix in the RVA to ensure uniqueness (handles edge cases like identical names)
    // Use XOR and shift to mix bits thoroughly
    hash ^= rva;
    hash *= FNV_PRIME;
    hash ^= (rva >> 16);
    hash *= FNV_PRIME;
    
    // Format as obf_XXXXXXXX (8 uppercase hex digits)
    wchar_t obfName[32];
    swprintf_s(obfName, 32, L"obf_%08X", hash);
    
    return obfName;
}

// Get type name from a DIA type symbol
std::wstring PdbSymbolExtractor::GetTypeName(IDiaSymbol* pType) {
    if (!pType) {
        return L"?";
    }

    DWORD symTag = 0;
    pType->get_symTag(&symTag);

    // DIA represents both "T*" and "T&" as a SymTagPointerType wrapping the
    // pointee type -- get_reference() is what distinguishes a reference from
    // a real pointer. This symbol has no get_name() of its own (it's not
    // itself a named type), so without unwrapping it here, every
    // pointer/reference-to-class parameter or return type -- most commonly
    // a "const Foo&" return, e.g. "virtual const QAngle& ASWEyeAngles()" --
    // fell straight through to the "?" fallback below.
    if (symTag == SymTagPointerType) {
        CComPtr<IDiaSymbol> pPointee;
        if (pType->get_type(&pPointee) != S_OK || !pPointee) {
            return L"?";
        }

        // A pointer/reference to a function (SymTagFunctionType) -- an
        // ordinary function pointer, or a pointer-to-member-function when
        // the pointer symbol itself carries a classParent (e.g. Source
        // engine's "typedef void (CBaseEntity::*BASEPTR)(void)" pattern) --
        // already spells out its own "(*)"/"(ClassName::*)" as part of its
        // C-style type name, so it is built as a whole and returned as-is;
        // it must NOT also get the generic " *"/" &" suffix added below.
        DWORD pointeeSymTag = 0;
        pPointee->get_symTag(&pointeeSymTag);
        if (pointeeSymTag == SymTagFunctionType) {
            return BuildFunctionPointerTypeName(pType, pPointee);
        }

        std::wstring inner = GetTypeName(pPointee);
        if (inner == L"?") {
            return L"?";
        }

        BOOL pointeeIsConst = FALSE;
        pPointee->get_constType(&pointeeIsConst);

        BOOL isRef = FALSE;
        pType->get_reference(&isRef);

        std::wstring result;
        if (pointeeIsConst) {
            result += L"const ";
        }
        result += inner;
        result += isRef ? L" &" : L" *";
        return result;
    }

    // Fixed-size array (e.g. "float[3]"): DIA gives the element type via
    // get_type() and the element count via get_count(), same shape as the
    // pointer case above -- no get_name() of its own either.
    if (symTag == SymTagArrayType) {
        CComPtr<IDiaSymbol> pElementType;
        if (pType->get_type(&pElementType) != S_OK || !pElementType) {
            return L"?";
        }

        std::wstring elementName = GetTypeName(pElementType);
        if (elementName == L"?") {
            return L"?";
        }

        DWORD count = 0;
        if (pType->get_count(&count) == S_OK) {
            return elementName + L"[" + std::to_wstring(count) + L"]";
        }
        return elementName + L"[]";
    }

    // Try to get type name directly
    BSTR bstrTypeName;
    if (pType->get_name(&bstrTypeName) == S_OK && bstrTypeName && wcslen(bstrTypeName) > 0) {
        std::wstring typeName = bstrTypeName;
        SysFreeString(bstrTypeName);
        return typeName;
    }

    // For basic types or unnamed types, try getting basic type info
    DWORD baseType = 0;
    ULONGLONG length = 0;
    
    if (pType->get_baseType(&baseType) == S_OK) {
        pType->get_length(&length);
        
        // Map basic types to names
        switch (baseType) {
            // btNoType marks the "..." ellipsis parameter of a variadic
            // function (e.g. CUtlBuffer::Scanf(const char*, ...)) -- DIA
            // gives it a real SymTagFunctionArgType/SymTagBaseType pair
            // like any other parameter, just with no actual type behind it.
            case btNoType: return L"...";
            case btVoid: return L"void";
            case btChar: return L"char";
            case btWChar: return L"wchar_t";
            case btBool: return L"bool";
            case btChar8:  return L"char8_t";
            case btChar16: return L"char16_t";
            case btChar32: return L"char32_t";
            // COM/OLE basic types -- surface in CRT/OS-boundary signatures
            // (e.g. VARIANT-based APIs) even though this is a native C++
            // symbol extractor, since DIA reports them the same way as any
            // other btXxx base type.
            case btBSTR:     return L"BSTR";
            case btHresult:  return L"HRESULT";
            case btVariant:  return L"VARIANT";
            case btCurrency: return L"CURRENCY";
            case btDate:     return L"DATE";

            case btInt:
            case btLong:
                if (length == 1) return L"char";
                else if (length == 2) return L"short";
                else if (length == 4) return L"int";
                else if (length == 8) return L"__int64";
                else return L"int" + std::to_wstring(length * 8);
                
            case btUInt:
            case btULong:
                if (length == 1) return L"unsigned char";
                else if (length == 2) return L"unsigned short";
                else if (length == 4) return L"unsigned int";
                else if (length == 8) return L"unsigned __int64";
                else return L"uint" + std::to_wstring(length * 8);
                
            case btFloat:
                if (length == 4) return L"float";
                else if (length == 8) return L"double";
                else return L"float" + std::to_wstring(length * 8);
                
            default:
                return L"?";
        }
    }

    return L"?";
}

// Builds the C-style spelling of a function-pointer or pointer-to-member-
// function type, e.g. "void* (__cdecl*)(const char *, int *)" for a plain
// function pointer (Source engine's CreateInterfaceFn pattern), or
// "void (CBaseEntity::*)(void)" for a pointer-to-member-function (Source
// engine's "typedef void (CBaseEntity::*BASEPTR)(void)" pattern). DIA
// represents both as a SymTagPointerType (pPointerType) wrapping a
// SymTagFunctionType (pFunctionType) with no get_name() of its own at
// either level; GetTypeName() detects this shape and delegates here instead
// of applying its generic " *"/" &" pointer suffix, since the "(*)"/
// "(ClassName::*)" here already conveys "pointer to" as part of the
// C-style spelling itself.
std::wstring PdbSymbolExtractor::BuildFunctionPointerTypeName(IDiaSymbol* pPointerType, IDiaSymbol* pFunctionType) {
    std::wstring returnTypeName = L"?";
    CComPtr<IDiaSymbol> pReturnType;
    if (pFunctionType->get_type(&pReturnType) == S_OK && pReturnType) {
        returnTypeName = GetTypeName(pReturnType);
    }

    std::wstring callingConvention;
    DWORD callingConventionCode = 0;
    if (pFunctionType->get_callingConvention(&callingConventionCode) == S_OK) {
        callingConvention = CallingConventionCodeToString(callingConventionCode) + L" ";
    }

    // A pointer-to-member-function carries the owning class as its
    // classParent (checked on the pointer symbol, not the function type);
    // an ordinary function pointer has none.
    std::wstring classPrefix;
    CComPtr<IDiaSymbol> pClassParent;
    if (pPointerType->get_classParent(&pClassParent) == S_OK && pClassParent) {
        BSTR bstrClassName = nullptr;
        if (pClassParent->get_name(&bstrClassName) == S_OK && bstrClassName) {
            classPrefix = std::wstring(bstrClassName) + L"::";
            SysFreeString(bstrClassName);
        }
    }

    // Parameter list -- the same SymTagFunctionArgType walk
    // ExtractFunctionSignature() does, just against this nested function
    // type instead of a top-level symbol's own function type.
    std::wstring params;
    CComPtr<IDiaEnumSymbols> pEnumArgs;
    if (pFunctionType->findChildren(SymTagFunctionArgType, NULL, nsNone, &pEnumArgs) == S_OK && pEnumArgs) {
        CComPtr<IDiaSymbol> pArg;
        ULONG celt = 0;
        bool first = true;
        while (SUCCEEDED(pEnumArgs->Next(1, &pArg, &celt)) && celt == 1) {
            if (!first) {
                params += L", ";
            }
            first = false;

            CComPtr<IDiaSymbol> pArgType;
            if (pArg->get_type(&pArgType) == S_OK && pArgType) {
                params += GetTypeName(pArgType);
            } else {
                params += L"?";
            }
            pArg.Release();
        }
    }
    if (params.empty()) {
        params = L"void";
    }

    return returnTypeName + L" (" + callingConvention + classPrefix + L"*)(" + params + L")";
}

// Extract function signature (parameters only, no return type)
std::wstring PdbSymbolExtractor::ExtractFunctionSignature(IDiaSymbol* pSymbol) {
    if (!pSymbol) {
        return L"()";
    }

    // Get function type
    CComPtr<IDiaSymbol> pFunctionType;
    if (pSymbol->get_type(&pFunctionType) != S_OK || !pFunctionType) {
        return L"()";
    }

    // Enumerate function arguments
    CComPtr<IDiaEnumSymbols> pEnumArgs;
    if (FAILED(pFunctionType->findChildren(SymTagFunctionArgType, NULL, nsNone, &pEnumArgs))) {
        return L"()";
    }

    LONG argCount = 0;
    pEnumArgs->get_Count(&argCount);
    
    if (argCount == 0) {
        return L"()";
    }

    // Build signature string
    std::wstring signature = L"(";
    CComPtr<IDiaSymbol> pArg;
    ULONG argCelt = 0;
    bool first = true;
    
    while (SUCCEEDED(pEnumArgs->Next(1, &pArg, &argCelt)) && argCelt == 1) {
        if (!first) {
            signature += L", ";
        }
        first = false;
        
        // Get the argument type
        CComPtr<IDiaSymbol> pArgType;
        if (pArg->get_type(&pArgType) == S_OK && pArgType) {
            signature += GetTypeName(pArgType);
        } else {
            signature += L"?";
        }
        
        pArg.Release();
    }
    
    signature += L")";
    return signature;
}

// Extract the function's return type name (e.g. "void", "int",
// "MathOperations"). For a SymTagFunction, get_type() gives the function's
// *type* symbol (a SymTagFunctionType); that type symbol's own get_type()
// gives its return type -- the same DIA relationship ExtractFunctionSignature
// already follows to reach SymTagFunctionArgType children, just one property
// deeper. Resolution goes through GetTypeName(), so the same basic-type
// mapping and "?" fallback for unresolved complex types apply here too.
std::wstring PdbSymbolExtractor::ExtractReturnType(IDiaSymbol* pSymbol) {
    if (!pSymbol) {
        return L"?";
    }

    CComPtr<IDiaSymbol> pFunctionType;
    if (pSymbol->get_type(&pFunctionType) != S_OK || !pFunctionType) {
        return L"?";
    }

    CComPtr<IDiaSymbol> pReturnType;
    if (pFunctionType->get_type(&pReturnType) != S_OK || !pReturnType) {
        return L"?";
    }

    return GetTypeName(pReturnType);
}

// Extract the function's calling convention (e.g. "__thiscall", "__cdecl").
// Reached via the same function-type symbol ExtractReturnType() and
// ExtractFunctionSignature() already fetch via get_type() -- one more
// direct DIA property on that same symbol
// (IDiaSymbol::get_callingConvention(), a CV_call_e value from cvconst.h),
// not a separate lookup.
std::wstring PdbSymbolExtractor::ExtractCallingConvention(IDiaSymbol* pSymbol) {
    if (!pSymbol) {
        return L"?";
    }

    CComPtr<IDiaSymbol> pFunctionType;
    if (pSymbol->get_type(&pFunctionType) != S_OK || !pFunctionType) {
        return L"?";
    }

    DWORD callingConvention = 0;
    if (pFunctionType->get_callingConvention(&callingConvention) != S_OK) {
        return L"?";
    }

    return CallingConventionCodeToString(callingConvention);
}

// Extract the source file + line number of the first line record covering
// this RVA range, formatted as "File.cpp:123". Unlike the other Extract*
// helpers, this one needs the IDiaSession (not just the symbol) because
// line-number lookup is a session-level query (IDiaSession::findLinesByRVA),
// not a property of the symbol itself. Returns "?" when DIA has no line
// information here -- e.g. a symbol with no compiled body (length 0), a
// symbol from a module compiled without debug line info, or a thunk.
std::wstring PdbSymbolExtractor::ExtractSourceLocation(IDiaSession* pSession, DWORD rva, DWORD length) {
    if (!pSession || length == 0) {
        return L"?";
    }

    CComPtr<IDiaEnumLineNumbers> pLines;
    if (pSession->findLinesByRVA(rva, length, &pLines) != S_OK || !pLines) {
        return L"?";
    }

    // The first line record in RVA order is usually the function's opening
    // line (often the opening brace, after prologue setup) -- close enough
    // to "where this function is defined" for locating it in source, even
    // though it is not guaranteed to be the exact declaration line.
    //
    // This can legitimately return no records at all: an RVA can be
    // identical-code-folded together with a function from a compiland whose
    // own debug line info was never retained, and no line record survives
    // for that address at all. When a line record IS found but the RVA is
    // shared by several differently-named symbols, ExtractSymbolsFromPdb()'s
    // SOURCE_FILE trust pass may still discard it afterward -- a resolved
    // answer here is not yet a guarantee it is correct for this particular
    // symbol name.
    CComPtr<IDiaLineNumber> pLine;
    ULONG celt = 0;
    if (pLines->Next(1, &pLine, &celt) != S_OK || celt != 1 || !pLine) {
        return L"?";
    }

    DWORD lineNumber = 0;
    if (pLine->get_lineNumber(&lineNumber) != S_OK) {
        return L"?";
    }

    CComPtr<IDiaSourceFile> pSourceFile;
    if (pLine->get_sourceFile(&pSourceFile) != S_OK || !pSourceFile) {
        return L"?";
    }

    BSTR bstrFileName = nullptr;
    if (pSourceFile->get_fileName(&bstrFileName) != S_OK || !bstrFileName) {
        return L"?";
    }
    std::wstring fullPath(bstrFileName);
    SysFreeString(bstrFileName);

    // PDB source paths are typically full build-machine paths (e.g.
    // "d:\buildagent\work\...\main.cpp"); only the filename itself is
    // useful for locating the file in a checked-out source tree.
    size_t lastSlash = fullPath.find_last_of(L"\\/");
    std::wstring fileName = (lastSlash == std::wstring::npos) ? fullPath : fullPath.substr(lastSlash + 1);

    return fileName + L":" + std::to_wstring(lineNumber);
}

// Extract symbols from PDB file using the initialized DIA data source
HRESULT PdbSymbolExtractor::ExtractSymbolsFromPdb(const std::wstring& pdbPath,
                                                   std::vector<FunctionSymbol>& symbols,
                                                   std::vector<VTableSymbol>& vtables,
                                                   std::unordered_map<DWORD, ClassHierarchyInfo>& classHierarchy,
                                                   DWORD& outTargetPointerSize) {
    if (!_pDiaDataSource) {
        _lastError = L"DIA data source not initialized";
        return E_FAIL;
    }

    symbols.clear();
    vtables.clear();
    classHierarchy.clear();
    outTargetPointerSize = static_cast<DWORD>(sizeof(void*));

    // Load debug data. A .exe/.dll path goes through loadDataForExe(), which
    // has DIA locate and load the matching PDB itself (same directory as the
    // binary, embedded GUID/age against a symbol server or local cache, ...)
    // -- letting the caller point at the binary directly instead of having
    // to know its PDB's exact path. Anything else (a .pdb path, or no
    // recognized extension) goes through loadDataFromPdb() as before.
    std::wstring extension = fs::path(pdbPath).extension().wstring();
    for (wchar_t& c : extension) {
        c = static_cast<wchar_t>(towlower(c));
    }
    bool isBinaryPath = (extension == L".exe" || extension == L".dll");

    HRESULT hr = isBinaryPath
        ? _pDiaDataSource->loadDataForExe(pdbPath.c_str(), NULL, NULL)
        : _pDiaDataSource->loadDataFromPdb(pdbPath.c_str());
    if (FAILED(hr)) {
        _lastError = (isBinaryPath ? L"Failed to load debug data for executable: " : L"Failed to load PDB file: ") + pdbPath;
        return hr;
    }

    // Open a session
    CComPtr<IDiaSession> pSession;
    hr = _pDiaDataSource->openSession(&pSession);
    if (FAILED(hr)) {
        _lastError = L"Failed to open DIA session";
        return hr;
    }

    // Get the global scope
    CComPtr<IDiaSymbol> pGlobal;
    hr = pSession->get_globalScope(&pGlobal);
    if (FAILED(hr)) {
        _lastError = L"Failed to get global scope";
        return hr;
    }

    // Determine the target pointer size from the PDB, not from the architecture
    // of the ObfSymbolsEx process. This matters when a 64-bit build of ObfSymbolsEx
    // analyzes a 32-bit game PDB (such as Source/Alien Swarm builds).
    DWORD machineType = 0;
    DWORD targetPointerSize = static_cast<DWORD>(sizeof(void*));
    if (pGlobal->get_machineType(&machineType) == S_OK) {
        switch (machineType) {
            case IMAGE_FILE_MACHINE_I386:
                targetPointerSize = 4;
                break;
            case IMAGE_FILE_MACHINE_AMD64:
            case IMAGE_FILE_MACHINE_ARM64:
                targetPointerSize = 8;
                break;
            default:
                std::wcout << L"Warning: Unknown PDB machine type 0x"
                           << std::hex << machineType << std::dec
                           << L"; using host pointer size ("
                           << targetPointerSize << L")" << std::endl;
                break;
        }
    } else {
        std::wcout << L"Warning: DIA did not provide PDB machine type; using host pointer size ("
                   << targetPointerSize << L")" << std::endl;
    }

    std::wcout << L"PDB target machine: 0x" << std::hex << machineType
               << std::dec << L", pointer size: " << targetPointerSize << std::endl;

    outTargetPointerSize = targetPointerSize;

    // Enumerate all function symbols
    CComPtr<IDiaEnumSymbols> pEnumSymbols;
    hr = pGlobal->findChildren(SymTagFunction, NULL, nsNone, &pEnumSymbols);
    if (FAILED(hr)) {
        _lastError = L"Failed to enumerate symbols";
        return hr;
    }

    LONG count = 0;
    pEnumSymbols->get_Count(&count);
    std::wcout << L"Found " << count << L" function symbols" << std::endl;

    CComPtr<IDiaSymbol> pSymbol;
    ULONG celt = 0;

    while (SUCCEEDED(pEnumSymbols->Next(1, &pSymbol, &celt)) && celt == 1) {
        FunctionSymbol func{};
        func.rva = 0;
        func.length = 0;
        func.isPublic = false;
        func.isVirtual = false;
        func.isThunk = false;
        func.thunkOrdinal = 0;
        func.thunkTargetRva = 0;
        func.symbolKind = L"FUNCTION";
        func.isIntroducingVirtual = false;
        func.vtableOffset = -1;
        func.vtableIndex = -1;
        func.vtableShapeId = 0;
        func.vtableSlotCount = 0;
        func.thisAdjust = 0;
        func.className.clear();
        func.classParentId = 0;

        // Get function name
        BSTR bstrName;
        if (pSymbol->get_name(&bstrName) == S_OK) {
            func.name = bstrName;
            SysFreeString(bstrName);
        }

        func.symbolKind = ClassifyFunctionKind(func.name);

        // Get function signature (parameters only, no return type)
        func.signature = ExtractFunctionSignature(pSymbol);
        func.returnType = ExtractReturnType(pSymbol);
        func.callingConvention = ExtractCallingConvention(pSymbol);

        // Get relative virtual address
        DWORD rva;
        if (pSymbol->get_relativeVirtualAddress(&rva) == S_OK) {
            func.rva = rva;
            
            // Generate obfuscated name using hash of function name + RVA
            func.obfuscatedName = GenerateObfuscatedName(func.name, rva);
        }

        // Get function length
        ULONGLONG length;
        if (pSymbol->get_length(&length) == S_OK) {
            func.length = length;
        }

        func.sourceLocation = ExtractSourceLocation(pSession, func.rva, static_cast<DWORD>(func.length));

        // Determine whether DIA identifies this function as virtual and, if so,
        // obtain its byte offset within the virtual function table.
        //
        // IDiaSymbol::get_virtualBaseOffset() returns the VFTable offset in bytes.
        // For a normal MSVC vftable this is directly convertible to a slot index:
        //     vtableIndex = vtableOffset / targetPointerSize
        // IMPORTANT: use the pointer size encoded by the PDB target machine, not
        // sizeof(void*) of this executable.
        BOOL isVirtual = FALSE;
        if (pSymbol->get_virtual(&isVirtual) == S_OK && isVirtual) {
            func.isVirtual = true;

            // get_intro() distinguishes a virtual function that introduces a
            // slot in its class from an inherited/overridden virtual function.
            BOOL isIntro = FALSE;
            if (pSymbol->get_intro(&isIntro) == S_OK) {
                func.isIntroducingVirtual = (isIntro != FALSE);
            }

            DWORD vtableOffset = 0;
            if (pSymbol->get_virtualBaseOffset(&vtableOffset) == S_OK) {
                func.vtableOffset = static_cast<LONG>(vtableOffset);
                if (targetPointerSize != 0 && (vtableOffset % targetPointerSize) == 0) {
                    func.vtableIndex = static_cast<LONG>(vtableOffset / targetPointerSize);
                } else {
                    std::wcout << L"Warning: non-aligned VFTable offset 0x"
                               << std::hex << vtableOffset << std::dec
                               << L" for " << func.name << std::endl;
                }
            }

            DWORD shapeId = 0;
            if (pSymbol->get_virtualTableShapeId(&shapeId) == S_OK) {
                func.vtableShapeId = shapeId;
            }

            LONG thisAdjust = 0;
            if (pSymbol->get_thisAdjust(&thisAdjust) == S_OK) {
                func.thisAdjust = thisAdjust;
            }
        }

        // DIA can associate a method with its enclosing class. This is useful
        // for multiple inheritance, where the same slot index can occur in
        // more than one vtable belonging to the same most-derived type.
        CComPtr<IDiaSymbol> pClassParent;
        if (pSymbol->get_classParent(&pClassParent) == S_OK && pClassParent) {
            pClassParent->get_symIndexId(&func.classParentId);

            BSTR bstrClassName = nullptr;
            if (pClassParent->get_name(&bstrClassName) == S_OK && bstrClassName) {
                func.className = bstrClassName;
                SysFreeString(bstrClassName);
            }
        }

        // Determine if function is public or private
        // Check access level - default to private
        DWORD access;
        if (pSymbol->get_access(&access) == S_OK) {
            func.isPublic = (access == CV_public);
        }
        
        // Also consider symbols at top-level scope or with external linkage as public
        BOOL isStatic = FALSE;
        if (pSymbol->get_isStatic(&isStatic) == S_OK && !isStatic) {
            // Non-static functions are typically public
            func.isPublic = true;
        }

        symbols.push_back(func);
        pSymbol.Release();
    }

    // DIA represents compiler-generated adjustor/vtable thunks as SymTagThunk,
    // not necessarily as SymTagFunction. Enumerate them explicitly so a VTable
    // slot can point to the thunk that is actually stored in the table.
    CComPtr<IDiaEnumSymbols> pEnumThunks;
    if (pGlobal->findChildren(SymTagThunk, NULL, nsNone, &pEnumThunks) == S_OK && pEnumThunks) {
        CComPtr<IDiaSymbol> pThunk;
        ULONG thunkCelt = 0;
        while (SUCCEEDED(pEnumThunks->Next(1, &pThunk, &thunkCelt)) && thunkCelt == 1) {
            FunctionSymbol thunk{};
            thunk.rva = 0;
            thunk.length = 0;
            thunk.isPublic = false;
            thunk.isVirtual = false;
            thunk.isThunk = true;
            thunk.thunkOrdinal = 0;
            thunk.thunkTargetRva = 0;
            thunk.symbolKind = L"THUNK";
            thunk.isIntroducingVirtual = false;
            thunk.vtableOffset = -1;
            thunk.vtableIndex = -1;
            thunk.vtableShapeId = 0;
            thunk.vtableSlotCount = 0;
            thunk.thisAdjust = 0;
            thunk.classParentId = 0;

            BSTR bstrName = nullptr;
            if (pThunk->get_name(&bstrName) == S_OK && bstrName) {
                thunk.name = bstrName;
                SysFreeString(bstrName);
            }

            // A thunk's name matches the function it thunks to (per CodeView),
            // and DIA does expose a callable type for most thunks; without
            // this, thunk.signature was left as an empty string, so a thunk's
            // real name printed with no "()" at all and could never match an
            // introducing method's "name+signature" key during vtable-slot
            // resolution below.
            thunk.signature = ExtractFunctionSignature(pThunk);
            thunk.returnType = ExtractReturnType(pThunk);
            thunk.callingConvention = ExtractCallingConvention(pThunk);

            DWORD rva = 0;
            if (pThunk->get_relativeVirtualAddress(&rva) == S_OK) {
                thunk.rva = rva;
                thunk.obfuscatedName = GenerateObfuscatedName(thunk.name, rva);
            }

            ULONGLONG length = 0;
            if (pThunk->get_length(&length) == S_OK) {
                thunk.length = length;
            }

            thunk.sourceLocation = ExtractSourceLocation(pSession, thunk.rva, static_cast<DWORD>(thunk.length));

            DWORD ordinal = 0;
            if (pThunk->get_thunkOrdinal(&ordinal) == S_OK) {
                thunk.thunkOrdinal = ordinal;
            }

            DWORD targetRva = 0;
            if (pThunk->get_targetRelativeVirtualAddress(&targetRva) == S_OK) {
                thunk.thunkTargetRva = targetRva;
            }

            BOOL isVirtual = FALSE;
            if (pThunk->get_virtual(&isVirtual) == S_OK && isVirtual) {
                thunk.isVirtual = true;

                BOOL isIntro = FALSE;
                if (pThunk->get_intro(&isIntro) == S_OK) {
                    thunk.isIntroducingVirtual = (isIntro != FALSE);
                }

                DWORD vtableOffset = 0;
                if (pThunk->get_virtualBaseOffset(&vtableOffset) == S_OK) {
                    thunk.vtableOffset = static_cast<LONG>(vtableOffset);
                    if (targetPointerSize != 0 && (vtableOffset % targetPointerSize) == 0) {
                        thunk.vtableIndex = static_cast<LONG>(vtableOffset / targetPointerSize);
                    }
                }

                DWORD shapeId = 0;
                if (pThunk->get_virtualTableShapeId(&shapeId) == S_OK) {
                    thunk.vtableShapeId = shapeId;
                }

                LONG thisAdjust = 0;
                if (pThunk->get_thisAdjust(&thisAdjust) == S_OK) {
                    thunk.thisAdjust = thisAdjust;
                }
            }

            CComPtr<IDiaSymbol> pClassParent;
            if (pThunk->get_classParent(&pClassParent) == S_OK && pClassParent) {
                pClassParent->get_symIndexId(&thunk.classParentId);
                BSTR bstrClassName = nullptr;
                if (pClassParent->get_name(&bstrClassName) == S_OK && bstrClassName) {
                    thunk.className = bstrClassName;
                    SysFreeString(bstrClassName);
                }
            }

            symbols.push_back(std::move(thunk));
            pThunk.Release();
        }
    }

    // Build a map from DIA VTableShape ID to the number of entries in that
    // virtual table shape. This lets the output describe the complete size of
    // the relevant vtable, not just the slot of an individual method.
    std::unordered_map<DWORD, DWORD> vtableShapeCounts;
    CComPtr<IDiaEnumSymbols> pEnumVtableShapes;
    if (pGlobal->findChildren(SymTagVTableShape, NULL, nsNone, &pEnumVtableShapes) == S_OK && pEnumVtableShapes) {
        CComPtr<IDiaSymbol> pShape;
        ULONG shapeCelt = 0;
        while (SUCCEEDED(pEnumVtableShapes->Next(1, &pShape, &shapeCelt)) && shapeCelt == 1) {
            DWORD shapeId = 0;
            DWORD shapeCount = 0;
            if (pShape->get_symIndexId(&shapeId) == S_OK &&
                pShape->get_count(&shapeCount) == S_OK) {
                vtableShapeCounts[shapeId] = shapeCount;
            }
            pShape.Release();
        }
    }

    for (auto& func : symbols) {
        if (func.vtableShapeId != 0) {
            auto it = vtableShapeCounts.find(func.vtableShapeId);
            if (it != vtableShapeCounts.end()) {
                func.vtableSlotCount = it->second;
            }
        }
    }

    // Introducing virtual methods that have NO compiled body anywhere in
    // this PDB -- e.g. a pure-virtual declaration on an abstract interface
    // base class that is never given an out-of-line definition -- never
    // become a SymTagFunction with an RVA, so they can never appear in
    // `symbols`. Their vtable-slot metadata (get_intro/get_virtualBaseOffset)
    // is nonetheless present on the TYPE-level method declaration DIA
    // returns from a UDT's own findChildren(SymTagFunction, ...), so it is
    // captured here, purely to let an overriding method farther down the
    // hierarchy still resolve to the correct slot (see
    // FindIntroducingAncestorVTableSlot() above). classId -> (unqualified
    // name+signature key -> slot/shape info). The shape/slot-count are the
    // introducing class's OWN class-level values (`udtShapeId`/
    // `udtShapeCount` at the point this introducing class's UDT is visited)
    // -- confirmed by instrumentation to be the only reliable source of a
    // secondary interface's shape when the concrete class implementing it
    // also implements another interface (see the multiple-inheritance note
    // near FindIntroducingAncestorVTableSlot()).
    std::unordered_map<DWORD, std::unordered_map<std::wstring, IntroducingSlotInfo>> introducingSlotsByClass;

    // Build a direct RVA index so the UDT enrichment pass stays O(N + M)
    // instead of scanning the complete symbol vector for every class method.
    //
    // Keyed by (RVA, fully qualified name) rather than RVA alone: the linker
    // commonly folds distinct functions with byte-identical compiled bodies
    // into one physical RVA (identical code folding / COMDAT folding) --
    // trivial one-line getters, empty overrides, "return -1;" stubs, and so
    // on are extremely common in a large C++ codebase and fold constantly.
    // A single RVA can end up shared by dozens of otherwise-unrelated
    // methods from different classes. Keying by RVA alone would merge a
    // UDT method's virtual metadata into whichever unrelated `symbols[]`
    // entry happened to claim that RVA first, corrupting that entry's
    // isVirtual/VTABLE_* fields with data that belongs to a different
    // method entirely (and never enriching the true owner of that entry).
    // The qualified name disambiguates: two genuinely different symbols
    // essentially never share both the same RVA and the same qualified
    // name.
    std::unordered_map<DWORD, std::unordered_map<std::wstring, size_t>> symbolByRvaAndName;
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (symbols[i].rva != 0) {
            symbolByRvaAndName[symbols[i].rva].emplace(symbols[i].name, i);
        }
    }

    // SOURCE_FILE trust pass. IDiaSession::findLinesByRVA (see
    // ExtractSourceLocation()) answers "what line info exists at this RVA",
    // not "what line info belongs to this specific symbol name" -- when ICF
    // folds several byte-identical functions into one physical RVA (the
    // exact phenomenon `symbolByRvaAndName` above exists to survive), DIA
    // can still return exactly one line record for that address, correct
    // for whichever single one of the folded functions the linker happened
    // to keep debug info attached to. For same-named template-method
    // instantiations (every CUtlMemory<T,int>::Grow(int), sharing one
    // genuine definition site in a header) that single answer is equally
    // correct for all of them and is left alone. But when the RVA is shared
    // by symbols with genuinely different unqualified names -- confirmed in
    // server.pdb, e.g. CBaseSpriteProjectile::Precache() and
    // CBaseSpriteProjectile::HandleThink() both compiling to an identical
    // trivial body and both reporting the unrelated "dt_send.cpp:290" --
    // the one file/line DIA reports can be correct for at most one of them
    // and is simply wrong, silently, for the rest: worse than "?", because
    // it looks resolved. Reusing `symbolByRvaAndName` costs nothing extra to
    // build; this only decides, per RVA, whether its resolved SOURCE_FILE
    // (if any) is safe to keep.
    for (const auto& rvaEntry : symbolByRvaAndName) {
        const auto& namesAtRva = rvaEntry.second;
        if (namesAtRva.size() < 2) {
            continue;
        }

        bool allSameUnqualifiedName = true;
        std::wstring firstUnqualified;
        bool first = true;
        for (const auto& nameEntry : namesAtRva) {
            std::wstring unqualified = UnqualifiedNameForSourceLocationTrust(nameEntry.first);
            if (first) {
                firstUnqualified = unqualified;
                first = false;
            } else if (unqualified != firstUnqualified) {
                allSameUnqualifiedName = false;
                break;
            }
        }

        if (!allSameUnqualifiedName) {
            for (const auto& nameEntry : namesAtRva) {
                symbols[nameEntry.second].sourceLocation = L"?";
            }
        }
    }

    // Second pass: enumerate functions as children of every UDT as well.
    // This catches method/type relationships that are not fully represented
    // by the global SymTagFunction enumeration and gives us a reliable class
    // name for virtual methods. We merge by RVA, so output remains one record
    // per function. The same pass records each class's own name and its
    // direct base classes (DIA's own declaration order) into `classHierarchy`
    // -- returned to the caller, and also used below to resolve the true
    // vtable slot of an overriding virtual method, since MSVC does not
    // record that slot on the override itself. See
    // FindIntroducingAncestorVTableIndex() above.
    CComPtr<IDiaEnumSymbols> pEnumUdts;
    if (pGlobal->findChildren(SymTagUDT, NULL, nsNone, &pEnumUdts) == S_OK && pEnumUdts) {
        CComPtr<IDiaSymbol> pUdt;
        ULONG udtCelt = 0;
        while (SUCCEEDED(pEnumUdts->Next(1, &pUdt, &udtCelt)) && udtCelt == 1) {
            DWORD udtClassId = 0;
            pUdt->get_symIndexId(&udtClassId);

            if (udtClassId != 0) {
                ClassHierarchyInfo& classInfo = classHierarchy[udtClassId];

                if (classInfo.className.empty()) {
                    BSTR bstrThisUdtName = nullptr;
                    if (pUdt->get_name(&bstrThisUdtName) == S_OK && bstrThisUdtName) {
                        classInfo.className = bstrThisUdtName;
                        SysFreeString(bstrThisUdtName);
                    }
                }

                // Record this class's direct base classes so an overriding
                // virtual method can later be traced back to whichever
                // ancestor actually introduces its vtable slot, and so the
                // inheritance report can walk the same graph.
                CComPtr<IDiaEnumSymbols> pEnumBases;
                if (pUdt->findChildren(SymTagBaseClass, NULL, nsNone, &pEnumBases) == S_OK && pEnumBases) {
                    CComPtr<IDiaSymbol> pBase;
                    ULONG baseCelt = 0;
                    while (SUCCEEDED(pEnumBases->Next(1, &pBase, &baseCelt)) && baseCelt == 1) {
                        CComPtr<IDiaSymbol> pBaseType;
                        if (pBase->get_type(&pBaseType) == S_OK && pBaseType) {
                            DWORD baseClassId = 0;
                            if (pBaseType->get_symIndexId(&baseClassId) == S_OK && baseClassId != 0) {
                                classInfo.baseClassIds.push_back(baseClassId);

                                // Seed the base's own name too, in case this
                                // is the only place in the PDB that names it
                                // (e.g. it has no virtual methods of its own).
                                ClassHierarchyInfo& baseInfo = classHierarchy[baseClassId];
                                if (baseInfo.className.empty()) {
                                    BSTR bstrBaseName = nullptr;
                                    if (pBaseType->get_name(&bstrBaseName) == S_OK && bstrBaseName) {
                                        baseInfo.className = bstrBaseName;
                                        SysFreeString(bstrBaseName);
                                    }
                                }
                            }
                        }
                        pBase.Release();
                    }
                }
            }

            DWORD udtShapeId = 0;
            DWORD udtShapeCount = 0;
            if (pUdt->get_virtualTableShapeId(&udtShapeId) == S_OK && udtShapeId != 0) {
                // Prefer the already enumerated global VTableShape map. Some
                // PDBs expose a valid shape ID on the UDT while
                // get_virtualTableShape() itself is unavailable.
                auto udtShapeIt = vtableShapeCounts.find(udtShapeId);
                if (udtShapeIt != vtableShapeCounts.end()) {
                    udtShapeCount = udtShapeIt->second;
                }

                if (udtShapeCount == 0) {
                    CComPtr<IDiaSymbol> pUdtShape;
                    if (pUdt->get_virtualTableShape(&pUdtShape) == S_OK && pUdtShape) {
                        pUdtShape->get_count(&udtShapeCount);
                    }
                }
            }

            CComPtr<IDiaEnumSymbols> pEnumMethods;
            if (pUdt->findChildren(SymTagFunction, NULL, nsNone, &pEnumMethods) == S_OK && pEnumMethods) {
                CComPtr<IDiaSymbol> pMethod;
                ULONG methodCelt = 0;
                while (SUCCEEDED(pEnumMethods->Next(1, &pMethod, &methodCelt)) && methodCelt == 1) {
                    // The method's own fully qualified name is needed both to
                    // key `introducingSlotsByClass` below and -- critically
                    // -- to disambiguate which `symbols[]` entry actually
                    // corresponds to this method when merging by RVA (see
                    // `symbolByRvaAndName` above): an RVA can be shared by
                    // several unrelated methods due to identical code
                    // folding, and the name is what tells them apart.
                    std::wstring methodName;
                    {
                        BSTR bstrMethodName = nullptr;
                        if (pMethod->get_name(&bstrMethodName) == S_OK && bstrMethodName) {
                            methodName = bstrMethodName;
                            SysFreeString(bstrMethodName);
                        }
                    }

                    // A UDT's own findChildren(SymTagFunction, ...) returns
                    // TYPE-level method declarations, independent of whether
                    // that method was ever compiled to an out-of-line body
                    // in this module. A pure-virtual declaration on an
                    // abstract interface base class commonly has no RVA at
                    // all, yet its get_intro()/get_virtualBaseOffset() are
                    // still populated from the type record -- capture that
                    // for override resolution even when there is no RVA to
                    // merge into an existing `symbols` entry.
                    BOOL methodIsVirtual = FALSE;
                    BOOL methodIsIntro = FALSE;
                    DWORD methodVtableOffset = 0;
                    bool haveMethodVtableOffset = false;
                    if (pMethod->get_virtual(&methodIsVirtual) == S_OK && methodIsVirtual) {
                        pMethod->get_intro(&methodIsIntro);
                        haveMethodVtableOffset =
                            (pMethod->get_virtualBaseOffset(&methodVtableOffset) == S_OK);

                        if (methodIsIntro && haveMethodVtableOffset &&
                            targetPointerSize != 0 && (methodVtableOffset % targetPointerSize) == 0 &&
                            !methodName.empty()) {
                            std::wstring methodSignature = ExtractFunctionSignature(pMethod);
                            std::wstring key = UnqualifiedMethodKey(methodName, methodSignature);

                            IntroducingSlotInfo slotInfo;
                            slotInfo.vtableIndex = static_cast<LONG>(methodVtableOffset / targetPointerSize);

                            // The per-method shape query is frequently
                            // unavailable (confirmed by instrumentation to
                            // return S_FALSE, never S_OK, for every method of
                            // at least one real-world class); fall back to
                            // this introducing class's own class-level shape,
                            // which -- for a class introducing only one
                            // interface's worth of virtuals, as an abstract
                            // interface base class does -- correctly
                            // identifies that interface's vtable.
                            DWORD introShapeId = 0;
                            if (pMethod->get_virtualTableShapeId(&introShapeId) == S_OK && introShapeId != 0) {
                                slotInfo.vtableShapeId = introShapeId;
                                auto shapeIt = vtableShapeCounts.find(introShapeId);
                                slotInfo.vtableSlotCount =
                                    (shapeIt != vtableShapeCounts.end()) ? shapeIt->second : 0;
                            } else {
                                slotInfo.vtableShapeId = udtShapeId;
                                slotInfo.vtableSlotCount = udtShapeCount;
                            }

                            introducingSlotsByClass[udtClassId][key] = slotInfo;
                        }
                    }

                    // IDiaSymbol::get_name() on a UDT's own method child
                    // returns the UNQUALIFIED name (e.g. "Suicide"), while
                    // the same logical method's global SymTagFunction entry
                    // (what `symbolByRvaAndName` is keyed from) is reported
                    // class-qualified (e.g. "CASW_Marine::Suicide"). This is
                    // a consistent, well-defined difference in DIA's naming
                    // convention between enumeration contexts -- confirmed
                    // by instrumenting this lookup and observing thousands
                    // of (globalName == "Class::method", udtChildName ==
                    // "method") pairs and no counterexamples. Reconstruct
                    // the same qualified form using this class's own name
                    // (already recorded into `classHierarchy` above, in this
                    // same UDT's iteration) so the lookup key matches
                    // exactly what `symbolByRvaAndName` holds.
                    std::wstring qualifiedMethodName = methodName;
                    if (!methodName.empty()) {
                        auto classNameIt = classHierarchy.find(udtClassId);
                        if (classNameIt != classHierarchy.end() && !classNameIt->second.className.empty()) {
                            qualifiedMethodName = classNameIt->second.className + L"::" + methodName;
                        }
                    }

                    DWORD methodRva = 0;
                    size_t matchedSymbolIndex = static_cast<size_t>(-1);
                    if (pMethod->get_relativeVirtualAddress(&methodRva) == S_OK && methodRva != 0) {
                        auto rvaIt = symbolByRvaAndName.find(methodRva);
                        if (rvaIt != symbolByRvaAndName.end()) {
                            auto nameIt = rvaIt->second.find(qualifiedMethodName);
                            if (nameIt != rvaIt->second.end()) {
                                matchedSymbolIndex = nameIt->second;
                            } else if (rvaIt->second.size() == 1) {
                                // Defensive fallback only: a single,
                                // unambiguous symbol already claims this RVA,
                                // so it is this method even though the
                                // reconstructed qualified name didn't match
                                // it exactly (e.g. DIA could not provide a
                                // class name here). There is no other
                                // candidate it could be.
                                matchedSymbolIndex = rvaIt->second.begin()->second;
                            }
                        }
                    }

                    if (matchedSymbolIndex != static_cast<size_t>(-1)) {
                        FunctionSymbol& func = symbols[matchedSymbolIndex];

                        BSTR bstrUdtName = nullptr;
                        pUdt->get_symIndexId(&func.classParentId);

                        if (pUdt->get_name(&bstrUdtName) == S_OK && bstrUdtName) {
                            if (func.className.empty()) {
                                func.className = bstrUdtName;
                            }
                            SysFreeString(bstrUdtName);
                        }

                        if (methodIsVirtual) {
                            func.isVirtual = true;
                            func.isIntroducingVirtual = (methodIsIntro != FALSE);

                            if (haveMethodVtableOffset) {
                                func.vtableOffset = static_cast<LONG>(methodVtableOffset);
                                if (targetPointerSize != 0 &&
                                    (methodVtableOffset % targetPointerSize) == 0) {
                                    func.vtableIndex = static_cast<LONG>(
                                        methodVtableOffset / targetPointerSize);
                                }
                            }

                            DWORD shapeId = 0;
                            if (pMethod->get_virtualTableShapeId(&shapeId) == S_OK) {
                                func.vtableShapeId = shapeId;
                                auto shapeIt = vtableShapeCounts.find(shapeId);
                                if (shapeIt != vtableShapeCounts.end()) {
                                    func.vtableSlotCount = shapeIt->second;
                                }
                            } else if (udtShapeId != 0) {
                                // Some method symbols do not expose the shape
                                // directly. Fall back to the enclosing UDT's
                                // virtual table shape.
                                func.vtableShapeId = udtShapeId;
                                func.vtableSlotCount = udtShapeCount;
                            }

                            LONG thisAdjust = 0;
                            if (pMethod->get_thisAdjust(&thisAdjust) == S_OK) {
                                func.thisAdjust = thisAdjust;
                            }
                        }
                    }
                    pMethod.Release();
                }
            }
            pUdt.Release();
        }
    }

    // Enumerate concrete VTable symbols from each UDT. DIA exposes a VTable
    // as a class child of its owning UDT; they are not children of the global
    // scope. This is important for concrete table identity and multiple
    // inheritance, where one UDT can own more than one VTable.
    CComPtr<IDiaEnumSymbols> pEnumVtableUdts;
    if (pGlobal->findChildren(SymTagUDT, NULL, nsNone, &pEnumVtableUdts) == S_OK && pEnumVtableUdts) {
        CComPtr<IDiaSymbol> pUdt;
        ULONG udtCelt = 0;
        while (SUCCEEDED(pEnumVtableUdts->Next(1, &pUdt, &udtCelt)) && udtCelt == 1) {
            CComPtr<IDiaEnumSymbols> pEnumVtables;
            if (pUdt->findChildren(SymTagVTable, NULL, nsNone, &pEnumVtables) == S_OK && pEnumVtables) {
                CComPtr<IDiaSymbol> pVtable;
                ULONG vtableCelt = 0;

                while (SUCCEEDED(pEnumVtables->Next(1, &pVtable, &vtableCelt)) && vtableCelt == 1) {
                    VTableSymbol table{};
                    table.pointerSize = targetPointerSize;

                    pVtable->get_symIndexId(&table.symbolId);
                    pVtable->get_relativeVirtualAddress(&table.rva);
                    pVtable->get_classParentId(&table.classParentId);

                    // The UDT is the authoritative owner because this VTable
                    // was found among that UDT's class children.
                    if (table.classParentId == 0)
                        pUdt->get_symIndexId(&table.classParentId);

                    BSTR bstrClassName = nullptr;
                    if (pUdt->get_name(&bstrClassName) == S_OK && bstrClassName) {
                        table.className = bstrClassName;
                        SysFreeString(bstrClassName);
                    }

                    CComPtr<IDiaSymbol> pClassParent;
                    if (table.className.empty() &&
                        pVtable->get_classParent(&pClassParent) == S_OK && pClassParent) {
                        if (pClassParent->get_name(&bstrClassName) == S_OK && bstrClassName) {
                            table.className = bstrClassName;
                            SysFreeString(bstrClassName);
                        }
                    }

                    CComPtr<IDiaSymbol> pShape;
                    if (pVtable->get_type(&pShape) == S_OK && pShape) {
                        pShape->get_symIndexId(&table.shapeId);
                        pShape->get_count(&table.slotCount);
                    }

                    // Avoid accidental duplicates if DIA exposes the same
                    // VTable through more than one enumeration path.
                    bool duplicate = false;
                    for (const auto& existing : vtables) {
                        if (existing.symbolId != 0 &&
                            existing.symbolId == table.symbolId) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate)
                        vtables.push_back(std::move(table));

                    pVtable.Release();
                }
            }
            pUdt.Release();
        }
    }

    std::wcout << L"Found " << vtables.size() << L" VTable symbols" << std::endl;

    // Third pass: resolve the real vtable slot of overriding virtual methods.
    //
    // CodeView only stores a vtable slot offset (LF_ONEMETHOD's "vbaseoff")
    // for the method that INTRODUCES a virtual function. A method that
    // merely overrides an inherited virtual carries no such field in the
    // PDB, so IDiaSymbol::get_virtualBaseOffset() answers 0 for it in both
    // passes above -- not a genuine slot 0, just absent data that DIA
    // reports as if it were zero. Left uncorrected, every override in the
    // PDB is misreported at VTABLE_OFFSET=0x0 / VTABLE_INDEX=0.
    //
    // The real slot is recovered by walking the class's own base-class
    // chain (as DIA reports it via SymTagBaseClass) for the nearest ancestor
    // that introduces a virtual method with the same unqualified name and
    // parameter signature -- exactly how the compiler itself found the slot
    // being overridden. This is derived entirely from this PDB's own DIA
    // data; nothing is inferred from source order, SDK headers, or the
    // reference game source under ASWRD_Game_Source_Code/.
    std::unordered_map<DWORD, std::vector<size_t>> methodsByClassId;
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (symbols[i].isVirtual && symbols[i].classParentId != 0) {
            methodsByClassId[symbols[i].classParentId].push_back(i);
        }
    }

    size_t resolvedOverrideCount = 0;
    size_t correctedShapeCount = 0;
    for (size_t i = 0; i < symbols.size(); ++i) {
        FunctionSymbol& func = symbols[i];
        if (!func.isVirtual || func.isIntroducingVirtual || func.classParentId == 0) {
            continue;
        }

        const std::wstring methodKey = UnqualifiedMethodKey(func.name, func.signature);
        std::unordered_set<DWORD> visitedClasses;
        IntroducingSlotInfo introSlot;
        bool found = FindIntroducingAncestorVTableSlot(
            func.classParentId, methodKey, classHierarchy, methodsByClassId, symbols,
            introducingSlotsByClass, visitedClasses, introSlot);

        if (found) {
            func.vtableIndex = introSlot.vtableIndex;
            func.vtableOffset = static_cast<LONG>(static_cast<ULONGLONG>(introSlot.vtableIndex) * targetPointerSize);
            ++resolvedOverrideCount;

            // A class implementing more than one interface (multiple
            // inheritance) can only have ONE of its several vtable shapes
            // reported at its own class/method level (confirmed by
            // instrumentation: DIA's per-method and per-class shape queries
            // both consistently report just one shape for such a class,
            // e.g. IBotController's for a class also implementing
            // IPlayerInfo). Every override defaults to that one class-level
            // shape during the earlier UDT-merge pass, so an override whose
            // resolved index doesn't fit in it (or which never got a shape
            // at all) is a class-level shape that provably describes the
            // wrong vtable for this particular slot. The introducing
            // interface's OWN shape -- captured in `introSlot` above --
            // stands in for it: for a class that implements an interface
            // without extending it further, the concrete secondary vtable
            // and the interface's own vtable are the same shape.
            const bool shapeMissing = (func.vtableShapeId == 0 || func.vtableSlotCount == 0);
            const bool indexOutOfShape = (func.vtableSlotCount != 0 &&
                                           func.vtableIndex >= static_cast<LONG>(func.vtableSlotCount));
            if ((shapeMissing || indexOutOfShape) && introSlot.vtableShapeId != 0) {
                func.vtableShapeId = introSlot.vtableShapeId;
                func.vtableSlotCount = introSlot.vtableSlotCount;
                ++correctedShapeCount;
            }
        } else {
            // DIA's own value for a non-introducing method is not
            // meaningful (see above), and no ancestor could be resolved
            // from this PDB -- report unavailable rather than the
            // misleading offset/index DIA returned.
            func.vtableOffset = -1;
            func.vtableIndex = -1;
        }
    }

    std::wcout << L"Resolved " << resolvedOverrideCount
               << L" overriding virtual method(s) to their inherited VTable slot"
               << L" (" << correctedShapeCount
               << L" also needed VTABLE_SHAPE/VTABLE_SLOTS corrected to the introducing interface's own)"
               << std::endl;

    return S_OK;
}

// Main extraction method
bool PdbSymbolExtractor::ExtractSymbols(const std::wstring& pdbPath,
                                        std::vector<FunctionSymbol>& symbols,
                                        std::vector<VTableSymbol>& vtables,
                                        std::unordered_map<DWORD, ClassHierarchyInfo>& classHierarchy,
                                        DWORD& targetPointerSize) {
    symbols.clear();
    vtables.clear();
    classHierarchy.clear();
    targetPointerSize = static_cast<DWORD>(sizeof(void*));
    _lastError.clear();

    // Check if the extractor was initialized successfully
    if (!_initialized) {
        if (_lastError.empty()) {
            _lastError = L"PdbSymbolExtractor was not initialized successfully";
        }
        return false;
    }

    HRESULT hr = ExtractSymbolsFromPdb(pdbPath, symbols, vtables, classHierarchy, targetPointerSize);
    if (FAILED(hr)) {
        if (_lastError.empty()) {
            _lastError = L"Failed to extract symbols (HRESULT: 0x" + std::to_wstring(hr) + L")";
        }
        return false;
    }

    std::wcout << L"Extracted " << symbols.size() << L" function symbols" << std::endl;
    return true;
}

