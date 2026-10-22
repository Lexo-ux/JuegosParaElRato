// SPDX-License-Identifier: GPL-3.0-or-later
// Integration test for the runtime resolver (not shipped).
// Builds an executable that lays out Unreal-like globals (object array + name pool) at
// addresses no SDK knows, loads Grounded2PanelES.dll into itself and prints the panel log.
// Run under Windows or Wine:  PruebaMotor.exe <ruta de Grounded2PanelES.dll>
#include <windows.h>
#include <shlobj.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace {

struct FakeName { int32_t index; uint32_t number; };
struct FakeObject {
    void* vtable;
    uint32_t flags;
    int32_t index;
    void* klass;
    FakeName name;
    void* outer;
    uint8_t padding[0x100];
};
struct FakeItem { FakeObject* object; uint8_t pad[0x10]; };
struct FakeObjectArray { FakeItem* objects; int32_t max; int32_t num; };
// FChunkedFixedUObjectArray (Unreal 5 default): chunks of 64K items.
struct FakeChunkedArray { FakeItem** chunks; FakeItem* preAllocated; int32_t max; int32_t num; int32_t maxChunks; int32_t numChunks; };
struct FakeNamePool { uint64_t lock; uint32_t currentBlock; uint32_t cursor; uint8_t* blocks[8192]; };

// Code bytes that look like UObject::ProcessEvent's flag checks:
//   test dword ptr [rcx+0B0h], 400h ; test dword ptr [rcx+0B0h], 400000h ; ret
__attribute__((section(".text"))) const unsigned char kFakeProcessEvent[] = {
    0xF7, 0x81, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
    0xF7, 0x81, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
    0xC3};

void Dummy() {}
void* vtable[0x200];

// Padding so the globals do not sit at the start of the section.
volatile uint8_t padding[0x4000] = {1};
FakeObjectArray objectArray = {nullptr, 0, 0};
volatile uint8_t padding3[0x800] = {1};
FakeChunkedArray chunkedArray = {nullptr, nullptr, 0, 0, 0, 0};
volatile uint8_t padding2[0x1000] = {1};
FakeNamePool namePool = {1, 0, 0, {}};

uint32_t cursor = 0;
uint32_t AddName(const char* text) {
    uint8_t* block = namePool.blocks[0];
    const uint32_t index = cursor / 2;
    const uint16_t length = static_cast<uint16_t>(std::strlen(text));
    const uint16_t header = static_cast<uint16_t>((length << 6) | (7 << 1));
    std::memcpy(block + cursor, &header, 2);
    std::memcpy(block + cursor + 2, text, length);
    cursor = (cursor + 2 + length + 1) & ~1u;
    namePool.cursor = cursor;
    return index;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    // The panel reads the game version from this string in .rdata.
    static const wchar_t* version = L"5.6.1-3100008+++Augusta+release-0.6.0.0";
    std::wcout << L"Versión simulada: " << version << std::endl;

    namePool.blocks[0] = static_cast<uint8_t*>(_aligned_malloc(1 << 17, 64));
    std::memset(namePool.blocks[0], 0, 1 << 17);
    AddName("None");
    AddName("ByteProperty");
    const uint32_t names[] = {AddName("Object"), AddName("Class"), AddName("Function"), AddName("Package")};

    for (auto& entry : vtable) entry = reinterpret_cast<void*>(&Dummy);
    vtable[0x4C] = const_cast<unsigned char*>(kFakeProcessEvent);

    bool chunked = false;
    const wchar_t* dll = L"Grounded2PanelES.dll";
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--bloques") chunked = true;
        else dll = argv[i];
    }
    std::printf("Formato de la tabla de objetos: %s\n", chunked ? "por bloques" : "plana");

    const int count = 70000;   // more than one chunk
    objectArray.objects = static_cast<FakeItem*>(_aligned_malloc(sizeof(FakeItem) * 0x20000, 64));
    std::memset(objectArray.objects, 0, sizeof(FakeItem) * 0x20000);
    FakeObject* classObject = new FakeObject{};
    for (int i = 0; i < count; ++i) {
        auto* object = new FakeObject{};
        object->vtable = vtable;
        object->index = i;
        object->klass = classObject;
        object->name = {static_cast<int32_t>(names[i % 4]), 0};
        objectArray.objects[i].object = object;
    }
    if (chunked) {
        chunkedArray.chunks = new FakeItem*[2]{objectArray.objects, objectArray.objects + 0x10000};
        chunkedArray.max = 0x20000;
        chunkedArray.num = count;
        chunkedArray.maxChunks = 3;
        chunkedArray.numChunks = 2;
        // Only the panel reads it: keep the compiler from dropping the stores.
        asm volatile("" : : "r"(&chunkedArray) : "memory");
        std::printf("Tabla por bloques en %p\n", static_cast<void*>(&chunkedArray));
    } else {
        objectArray.max = 0x20000;
        objectArray.num = count;
    }
    std::printf("Objetos simulados en %p, tabla de nombres en %p\n", static_cast<void*>(&objectArray), static_cast<void*>(&namePool.blocks[0]));

    HMODULE module = LoadLibraryW(dll);
    std::printf("LoadLibrary: %s\n", module ? "correcto" : "fallo");
    if (!module) return 2;
    Sleep(20000);

    wchar_t* base = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base);
    std::wstring log = std::wstring(base) + L"\\Grounded2PanelES\\registro.txt";
    CoTaskMemFree(base);
    std::ifstream file(log.c_str());
    std::string line;
    bool foundObjects = false, foundNames = false, foundProcessEvent = false, foundVersion = false;
    while (std::getline(file, line)) {
        std::cout << line << "\n";
        foundObjects |= line.find("GObjects localizado") != std::string::npos;
        foundNames |= line.find("Tabla de nombres localizada") != std::string::npos;
        foundProcessEvent |= line.find("ProcessEvent verificado") != std::string::npos;
        foundVersion |= line.find("0.6.0.0") != std::string::npos;
    }
    std::printf("\nRESULTADO objetos=%d nombres=%d processEvent=%d version=%d\n", foundObjects, foundNames, foundProcessEvent, foundVersion);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), foundObjects && foundNames && foundProcessEvent && foundVersion ? 0 : 1);
    return 0;
}
