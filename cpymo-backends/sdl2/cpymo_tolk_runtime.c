#include "cpymo_tolk_runtime.h"

#ifdef _WIN32

#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif

#include <windows.h>

#define CPYMO_TOLK_MANIFEST_ID 100
#define CPYMO_TOLK_FILE_ID_BASE 1000
#define CPYMO_TOLK_MAX_FILES 64
#define CPYMO_TOLK_NAME_MAX 128

typedef void (*cpymo_tolk_load_fn)(void);
typedef void (*cpymo_tolk_unload_fn)(void);
typedef void (*cpymo_tolk_output_fn)(const wchar_t *, int);
typedef void (*cpymo_tolk_silence_fn)(void);

static HMODULE cpymo_tolk_module;
static cpymo_tolk_load_fn cpymo_tolk_load_ptr;
static cpymo_tolk_unload_fn cpymo_tolk_unload_ptr;
static cpymo_tolk_output_fn cpymo_tolk_output_ptr;
static cpymo_tolk_silence_fn cpymo_tolk_silence_ptr;
static int cpymo_tolk_tried;

/* %LOCALAPPDATA%\CPyMO\runtime */
static int cpymo_tolk_runtime_dir(wchar_t *out, size_t cap)
{
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", out, (DWORD)cap);
    size_t used;

    if (len == 0 || len >= (DWORD)cap) return 0;

    used = wcslen(out);
    if (used + 20 >= cap) return 0;
    wcscpy(out + used, L"\\CPyMO\\runtime");
    return 1;
}

static void cpymo_tolk_mkdirs(wchar_t *path)
{
    wchar_t *p;

    for (p = path + 3; *p != L'\0'; ++p) {
        if (*p != L'\\') continue;
        *p = L'\0';
        CreateDirectoryW(path, NULL);
        *p = L'\\';
    }
    CreateDirectoryW(path, NULL);
}

static int cpymo_tolk_write_file(
    const wchar_t *path, const void *data, DWORD size)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    HANDLE file;
    DWORD written = 0;
    BOOL ok;

    /* 已经释放过同大小的文件就不再重写（14 个文件每次启动都写太慢）。 */
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &info)) {
        if (info.nFileSizeHigh == 0 && info.nFileSizeLow == size) return 1;
    }

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;

    ok = WriteFile(file, data, size, &written, NULL);
    CloseHandle(file);
    return ok && written == size;
}

static int cpymo_tolk_extract_one(
    HMODULE self, int id, const wchar_t *dir, const char *name_utf8)
{
    wchar_t name[CPYMO_TOLK_NAME_MAX];
    wchar_t path[MAX_PATH];
    HRSRC res;
    HGLOBAL loaded;
    const void *data;
    DWORD size;

    if (MultiByteToWideChar(CP_UTF8, 0, name_utf8, -1, name,
            CPYMO_TOLK_NAME_MAX) <= 0)
        return 0;

    if (swprintf(path, MAX_PATH, L"%s\\%s", dir, name) < 0) return 0;

    res = FindResourceW(self, MAKEINTRESOURCEW(CPYMO_TOLK_FILE_ID_BASE + id),
        RT_RCDATA);
    if (res == NULL) return 0;

    size = SizeofResource(self, res);
    loaded = LoadResource(self, res);
    if (loaded == NULL || size == 0) return 0;
    data = LockResource(loaded);
    if (data == NULL) return 0;

    return cpymo_tolk_write_file(path, data, size);
}

static void cpymo_tolk_extract_all(const wchar_t *dir)
{
    HMODULE self = GetModuleHandleW(NULL);
    HRSRC res;
    HGLOBAL loaded;
    const char *manifest;
    DWORD size;
    DWORD i = 0;
    int id = 0;

    res = FindResourceW(self, MAKEINTRESOURCEW(CPYMO_TOLK_MANIFEST_ID), RT_RCDATA);
    if (res == NULL) return;
    size = SizeofResource(self, res);
    loaded = LoadResource(self, res);
    if (loaded == NULL || size == 0) return;
    manifest = (const char *)LockResource(loaded);
    if (manifest == NULL) return;

    while (i < size && id < CPYMO_TOLK_MAX_FILES) {
        DWORD start = i;
        char name[CPYMO_TOLK_NAME_MAX];

        while (i < size && manifest[i] != '\n' && manifest[i] != '\r') ++i;

        if (i > start) {
            DWORD len = i - start;
            if (len >= sizeof(name)) len = sizeof(name) - 1;
            memcpy(name, manifest + start, len);
            name[len] = '\0';
            cpymo_tolk_extract_one(self, id, dir, name);
            ++id;
        }

        while (i < size && (manifest[i] == '\n' || manifest[i] == '\r')) ++i;
    }
}

static void cpymo_tolk_init(void)
{
    wchar_t dir[MAX_PATH];
    wchar_t dll[MAX_PATH];

    if (cpymo_tolk_tried) return;
    cpymo_tolk_tried = 1;

    if (cpymo_tolk_runtime_dir(dir, MAX_PATH)) {
        cpymo_tolk_mkdirs(dir);
        cpymo_tolk_extract_all(dir);

        if (swprintf(dll, MAX_PATH, L"%s\\Tolk.dll", dir) > 0) {
            cpymo_tolk_module = LoadLibraryExW(
                dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (cpymo_tolk_module != NULL) {
                /* Tolk 内部还要动态加载各家读屏客户端 DLL，把它所在目录
                 * 加进搜索路径，否则那些 DLL 找不到。 */
                SetDllDirectoryW(dir);
            }
        }
    }

    /* 开发时 exe 旁边就有 dll 的情况。 */
    if (cpymo_tolk_module == NULL)
        cpymo_tolk_module = LoadLibraryW(L"Tolk.dll");
    if (cpymo_tolk_module == NULL) return;

    cpymo_tolk_load_ptr = (cpymo_tolk_load_fn)
        GetProcAddress(cpymo_tolk_module, "Tolk_Load");
    cpymo_tolk_unload_ptr = (cpymo_tolk_unload_fn)
        GetProcAddress(cpymo_tolk_module, "Tolk_Unload");
    cpymo_tolk_output_ptr = (cpymo_tolk_output_fn)
        GetProcAddress(cpymo_tolk_module, "Tolk_Output");
    cpymo_tolk_silence_ptr = (cpymo_tolk_silence_fn)
        GetProcAddress(cpymo_tolk_module, "Tolk_Silence");

    if (cpymo_tolk_load_ptr != NULL) cpymo_tolk_load_ptr();
}

void cpymo_tolk_load(void)
{
    cpymo_tolk_init();
}

void cpymo_tolk_unload(void)
{
    cpymo_tolk_init();
    if (cpymo_tolk_unload_ptr != NULL) cpymo_tolk_unload_ptr();
}

void cpymo_tolk_output(const wchar_t *text)
{
    cpymo_tolk_init();
    if (cpymo_tolk_output_ptr != NULL && text != NULL)
        cpymo_tolk_output_ptr(text, 1);
}

void cpymo_tolk_silence(void)
{
    cpymo_tolk_init();
    if (cpymo_tolk_silence_ptr != NULL) cpymo_tolk_silence_ptr();
}

#else

void cpymo_tolk_load(void) { }
void cpymo_tolk_unload(void) { }
void cpymo_tolk_output(const wchar_t *text) { (void)text; }
void cpymo_tolk_silence(void) { }

#endif
